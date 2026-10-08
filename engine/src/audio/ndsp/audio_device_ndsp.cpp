// The ndsp output device of the 3DS (audio_device_ndsp.h for the design).
#include "audio_device_ndsp.h"

#include <3ds.h>

#include <cstdio>
#include <cstring>
#include <mutex>

#include "as3d/core.h"
#include "audio_device.h"
#include "mixer.h"

namespace as3d::audio {
namespace {

constexpr int kChannel = 0;
constexpr int kBuffers = 4;
constexpr int kFrames = 1024; // per buffer
constexpr int kRate = 32728;  // NDSP_SAMPLE_RATE = SYSCLOCK_SOC / 512 = 32728.498 Hz
constexpr size_t kStackBytes = 32 * 1024;
constexpr int kAppCpuLimit = 30; // percent of core 1 granted to the application

double ticksToUs(u64 t) { return static_cast<double>(t) * 1000000.0 / SYSCLOCK_ARM11; }

// The counters of the open device (one at a time: ndsp has one owner).
std::mutex g_statsMutex;
ndsp::NdspStats g_stats;
u64 g_openTick = 0;
u64 g_renderTicks = 0;
char g_description[160] = "ndsp: not open";

class NdspAudioDevice final : public IAudioDevice {
public:
    ~NdspAudioDevice() override {
        close();
        ndspExit(); // makePlatformAudioDevice's ndspInit
    }

    bool open(int /*preferredRate*/, Mixer* mixer) override {
        close();
        mixer_ = mixer;
        mixer_->setDeviceRate(kRate);

        const size_t bytes = sizeof(int16_t) * 2 * kFrames * kBuffers;
        pcm_ = static_cast<int16_t*>(linearAlloc(bytes));
        if (!pcm_) {
            AS3D_ERROR("audio: ndsp: linearAlloc(%u) failed", static_cast<unsigned>(bytes));
            return false;
        }
        std::memset(pcm_, 0, bytes);

        ndspSetOutputMode(NDSP_OUTPUT_STEREO);
        ndspChnReset(kChannel);
        ndspChnSetInterp(kChannel, NDSP_INTERP_NONE); // the channel plays at the DSP's own rate
        ndspChnSetRate(kChannel, static_cast<float>(NDSP_SAMPLE_RATE));
        ndspChnSetFormat(kChannel, NDSP_FORMAT_STEREO_PCM16);
        float mix[12] = {};
        mix[0] = mix[1] = 1.0f; // front left / right
        ndspChnSetMix(kChannel, mix);

        {
            std::lock_guard<std::mutex> lock(g_statsMutex);
            g_stats = ndsp::NdspStats();
            g_stats.open = true;
            g_openTick = svcGetSystemTick();
            g_renderTicks = 0;
        }

        // Prime the queue: four buffers ahead (silence unless something already plays).
        std::memset(buf_, 0, sizeof buf_);
        for (int i = 0; i < kBuffers; ++i) {
            buf_[i].data_pcm16 = pcm_ + 2 * kFrames * i;
            buf_[i].nsamples = kFrames;
            render(buf_[i], false);
        }
        next_ = 0;
        paused_ = false;

        LightEvent_Init(&wake_, RESET_ONESHOT);
        running_ = true;
        ndspSetCallback(&NdspAudioDevice::frameCallback, this);

        static bool cpuLimitSet = false;
        if (!cpuLimitSet) {
            const Result r = APT_SetAppCpuTimeLimit(kAppCpuLimit);
            if (R_FAILED(r)) AS3D_WARN("audio: ndsp: APT_SetAppCpuTimeLimit(%d) failed: 0x%08lX", kAppCpuLimit, r);
            cpuLimitSet = true;
        }
        s32 prio = 0x30;
        svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
        prio = prio > 0x18 ? prio - 1 : 0x18; // one step above the opener (lower is higher)
        int core = 1;
        thread_ = threadCreate(&NdspAudioDevice::feederEntry, this, kStackBytes, prio, core, false);
        if (!thread_) {
            core = -2; // the application's default core
            thread_ = threadCreate(&NdspAudioDevice::feederEntry, this, kStackBytes, prio, core, false);
        }
        if (!thread_) {
            AS3D_ERROR("audio: ndsp: cannot create the feeder thread");
            running_ = false;
            ndspSetCallback(nullptr, nullptr);
            ndspChnWaveBufClear(kChannel);
            linearFree(pcm_);
            pcm_ = nullptr;
            std::lock_guard<std::mutex> lock(g_statsMutex);
            g_stats.open = false;
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(g_statsMutex);
            g_stats.feederOnCore1 = core == 1;
            std::snprintf(g_description, sizeof g_description,
                          "ndsp: channel %d stereo pcm16 %d Hz, %d x %d frames (%.0f ms queued), feeder core %s prio 0x%lX",
                          kChannel, kRate, kBuffers, kFrames, 1000.0 * kBuffers * kFrames / kRate,
                          core == 1 ? "1" : "0 (core 1 refused)", static_cast<unsigned long>(prio));
        }
        AS3D_INFO("audio: %s", g_description);
        return true;
    }

    void close() override {
        if (thread_) {
            running_ = false;
            LightEvent_Signal(&wake_);
            threadJoin(thread_, U64_MAX);
            threadFree(thread_);
            thread_ = nullptr;
        }
        if (pcm_) {
            ndspSetCallback(nullptr, nullptr);
            ndspChnWaveBufClear(kChannel);
            ndspChnReset(kChannel);
            // ndspChnWaveBufClear only unlinks: let the DSP stop reading before the memory goes.
            svcSleepThread(20 * 1000 * 1000);
            linearFree(pcm_);
            pcm_ = nullptr;
            std::lock_guard<std::mutex> lock(g_statsMutex);
            g_stats.open = false;
            std::snprintf(g_description, sizeof g_description, "ndsp: not open");
        }
        mixer_ = nullptr;
    }

    int rate() const override { return kRate; }

    void setPaused(bool paused) override {
        if (!pcm_ || paused == paused_) return;
        paused_ = paused;
        ndspChnSetPaused(kChannel, paused);
    }

private:
    static void frameCallback(void* self) { LightEvent_Signal(&static_cast<NdspAudioDevice*>(self)->wake_); }

    static void feederEntry(void* self) { static_cast<NdspAudioDevice*>(self)->feed(); }

    static u8 status(const ndspWaveBuf& b) { return *static_cast<const volatile u8*>(&b.status); }

    void feed() {
        while (running_) {
            LightEvent_Wait(&wake_);
            if (!running_) break;
            if (status(buf_[next_]) != NDSP_WBUF_DONE) continue;
            int queued = 0;
            for (const ndspWaveBuf& b : buf_) {
                const u8 s = status(b);
                if (s == NDSP_WBUF_QUEUED || s == NDSP_WBUF_PLAYING) ++queued;
            }
            const bool dry = queued == 0 && !paused_;
            bool first = true;
            while (running_ && status(buf_[next_]) == NDSP_WBUF_DONE) {
                render(buf_[next_], first && dry);
                first = false;
                next_ = (next_ + 1) % kBuffers;
            }
        }
    }

    // Renders `b` from the mixer (under the mixer's lock) and queues it.
    void render(ndspWaveBuf& b, bool underrun) {
        const u64 t0 = svcGetSystemTick();
        mixer_->renderS16(b.data_pcm16, kFrames);
        const u64 dt = svcGetSystemTick() - t0;
        int peak = 0;
        for (int i = 0; i < 2 * kFrames; ++i) {
            int v = b.data_pcm16[i];
            if (v < 0) v = -v;
            if (v > peak) peak = v;
        }
        DSP_FlushDataCache(b.data_pcm16, sizeof(int16_t) * 2 * kFrames);
        ndspChnWaveBufAdd(kChannel, &b);

        std::lock_guard<std::mutex> lock(g_statsMutex);
        if (thread_) ++g_stats.fills; // the four priming buffers are not counted
        if (underrun) ++g_stats.underruns;
        g_renderTicks += dt;
        const uint32_t us = static_cast<uint32_t>(ticksToUs(dt));
        if (us > g_stats.maxFillUs) g_stats.maxFillUs = us;
        if (peak > g_stats.peak) g_stats.peak = peak;
    }

    Mixer* mixer_ = nullptr;
    int16_t* pcm_ = nullptr; // kBuffers x kFrames stereo frames, linear memory
    ndspWaveBuf buf_[kBuffers];
    int next_ = 0; // the buffer the DSP finishes next
    LightEvent wake_;
    Thread thread_ = nullptr;
    volatile bool running_ = false;
    volatile bool paused_ = false;
};

} // namespace

namespace ndsp {

NdspStats stats(bool resetPeak) {
    std::lock_guard<std::mutex> lock(g_statsMutex);
    NdspStats s = g_stats;
    if (s.open) {
        s.renderUs = static_cast<uint64_t>(ticksToUs(g_renderTicks));
        s.openUs = static_cast<uint64_t>(ticksToUs(svcGetSystemTick() - g_openTick));
        s.droppedFrames = ndspGetDroppedFrames();
    }
    if (resetPeak) g_stats.peak = 0;
    return s;
}

const char* description() { return g_description; }

} // namespace ndsp

// No SDL on the 3DS: AudioBackend::Sdl has no device here (Audio::init fails, the caller falls
// back to the Null device).
std::unique_ptr<IAudioDevice> makeSdlAudioDevice() { return nullptr; }

std::unique_ptr<IAudioDevice> makePlatformAudioDevice() {
    const Result r = ndspInit();
    if (R_FAILED(r)) {
        static bool logged = false;
        if (!logged) {
            AS3D_WARN("audio: ndspInit failed (0x%08lX): no DSP firmware (sdmc:/3ds/dspfirm.cdc)? no sound", r);
            logged = true;
        }
        return nullptr;
    }
    return std::make_unique<NdspAudioDevice>();
}

} // namespace as3d::audio
