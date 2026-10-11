#include "ctr_perf.h"

#include <3ds.h>
#include <malloc.h>

#include <algorithm>
#include <atomic>
#include <mutex>

#include "as3d/core.h"
#include "as3d/fs_util.h"
#include "ctr_mem.h"
#include "run_config.h"

namespace ctrapp {
namespace {

// The log file and perf.csv, and their pending text. The engine's log sink (any thread) and
// Perf::record append to the buffers under g_logMutex; the SD writes happen on the writer thread
// (lowest priority, so it runs while the main thread waits for the vblank or the GPU), never on
// the main thread during play: a second's flush there cost a frame (the console's mono runs had
// a double frame every 53-59 frames). Perf::flush (suspend, exit) writes on the calling thread.
std::mutex g_logMutex;  // the buffers
std::mutex g_fileMutex; // the files (the writer, Perf::flush, close)
std::FILE* g_logFile = nullptr;
std::FILE* g_csvFile = nullptr;
std::string g_logBuf, g_csvBuf;
bool g_ioFlag = false; // a write on the main thread happened (Perf::flush)
std::atomic<uint64_t> g_writerWroteAt{0}; // svcGetSystemTick at the end of the writer thread's last write

// queueFileWrite: the files to write, under g_logMutex (taken by drainToFiles).
struct FileJob {
    std::string path;
    std::vector<uint8_t> bytes;
};
std::vector<FileJob> g_fileJobs;
std::vector<std::string> g_runFiles; // noteRunFile, under g_logMutex

struct Writer {
    Thread thread = nullptr;
    LightEvent wake;
    volatile bool quit = false;
};
Writer g_writer;

// Takes the pending text and writes it (fwrite + fflush, no fsync).
void drainToFiles() {
    std::string log, csv;
    std::vector<FileJob> jobs;
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        log.swap(g_logBuf);
        csv.swap(g_csvBuf);
        jobs.swap(g_fileJobs);
    }
    // The queued files first (their AS3D_FILE_WRITE line goes out with the next drain).
    for (const FileJob& j : jobs) {
        const uint64_t t0 = svcGetSystemTick();
        std::string why;
        const bool ok = as3d::writeFileAtomic(j.path, j.bytes.data(), j.bytes.size(), &why);
        const double ms = static_cast<double>(svcGetSystemTick() - t0) * 1000.0 / SYSCLOCK_ARM11;
        if (ok) AS3D_INFO("AS3D_FILE_WRITE path=%s bytes=%zu ms=%.0f (writer thread)", j.path.c_str(), j.bytes.size(), ms);
        else AS3D_WARN("AS3D_FILE_WRITE path=%s failed: %s", j.path.c_str(), why.c_str());
    }
    if (log.empty() && csv.empty()) return;
    std::lock_guard<std::mutex> files(g_fileMutex);
    if (g_logFile && !log.empty()) {
        std::fwrite(log.data(), 1, log.size(), g_logFile);
        std::fflush(g_logFile);
    }
    if (g_csvFile && !csv.empty()) {
        std::fwrite(csv.data(), 1, csv.size(), g_csvFile);
        std::fflush(g_csvFile);
    }
    g_writerWroteAt = svcGetSystemTick();
}

void writerEntry(void*) {
    while (!g_writer.quit) {
        LightEvent_WaitTimeout(&g_writer.wake, 2000000000LL); // signalled about once a second
        drainToFiles();
    }
}

void startWriter() {
    if (g_writer.thread) return;
    LightEvent_Init(&g_writer.wake, RESET_ONESHOT);
    g_writer.quit = false;
    g_writer.thread = threadCreate(writerEntry, nullptr, 16 * 1024, 0x3F, -2, false);
    if (!g_writer.thread) AS3D_WARN("perf: no writer thread, the log is written on the main thread");
}

void stopWriter() {
    if (!g_writer.thread) return;
    g_writer.quit = true;
    LightEvent_Signal(&g_writer.wake);
    threadJoin(g_writer.thread, U64_MAX);
    threadFree(g_writer.thread);
    g_writer.thread = nullptr;
}

void wakeWriter() {
    if (g_writer.thread) LightEvent_Signal(&g_writer.wake);
    else drainToFiles(); // no thread: the old behaviour
}

void logSink(as3d::LogLevel level, const char* line) {
    bool wake = false;
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        g_logBuf += line;
        wake = level >= as3d::LogLevel::Error || g_logBuf.size() > 65536;
    }
    if (wake) wakeWriter();
}

double percentile(std::vector<float>& v, double p) {
    if (v.empty()) return 0.0;
    size_t k = static_cast<size_t>(p * (v.size() - 1));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[k];
}

constexpr size_t kMaxSamples = 36000; // ten minutes at 60 Hz; the statistics beyond keep avg and max only

} // namespace

bool queueFileWrite(const std::string& path, std::vector<uint8_t>&& bytes) {
    if (!g_writer.thread) return false;
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        g_fileJobs.push_back(FileJob{path, std::move(bytes)});
    }
    LightEvent_Signal(&g_writer.wake);
    return true;
}

void noteRunFile(const std::string& name) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    for (const std::string& n : g_runFiles)
        if (n == name) return;
    g_runFiles.push_back(name);
}

std::vector<std::string> runFiles() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    return g_runFiles;
}

size_t pendingFileWrites() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    return g_fileJobs.size();
}

bool perfWriterWroteSince(uint64_t tick) {
    const uint64_t at = g_writerWroteAt;
    return at != 0 && at + 4481136u >= tick; // a write ending up to one vblank before the frame counts
}

bool takePerfIoFlag() {
    const bool f = g_ioFlag;
    g_ioFlag = false;
    return f;
}

uint64_t nowTicks() { return svcGetSystemTick(); }
double ticksToMs(uint64_t ticks) { return static_cast<double>(ticks) * 1000.0 / SYSCLOCK_ARM11; }

Perf::~Perf() { close(); }

void Perf::open(bool csv) {
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        // The first open of the process truncates; later ones (the game selector, then the front
        // end, then the selector again) append.
        static bool opened = false;
        std::lock_guard<std::mutex> files(g_fileMutex);
        if (!g_logFile) g_logFile = std::fopen((std::string(kDataDir) + "/log.txt").c_str(), opened ? "a" : "w");
        opened = opened || g_logFile;
    }
    startWriter();
    as3d::setLogSink(logSink);
    as3d::setLogLevel(as3d::LogLevel::Info);
    if (csv) {
        // The first perf.csv of the launch is truncated and gets the header; later ones (a
        // soak's stages) append.
        static bool csvOpened = false;
        bool header = false;
        {
            std::lock_guard<std::mutex> files(g_fileMutex);
            if (!g_csvFile) {
                g_csvFile = std::fopen((std::string(kDataDir) + "/perf.csv").c_str(), csvOpened ? "a" : "w");
                header = g_csvFile && !csvOpened;
                csvOpened = csvOpened || g_csvFile;
            }
            csv_ = g_csvFile;
        }
        if (csv_) noteRunFile("perf.csv");
        if (csv_ && header) {
            std::lock_guard<std::mutex> lock(g_logMutex);
            g_csvBuf += "frame,step_ms,prepare_ms,cpu_ms,gpu_ms,eyes,verts,draws,tex_vram_kb,tex_linear_kb,"
                        "linear_free_kb,lazy_loads,arena_kb,sim_steps,submit_ms,wait_ms,end_ms,after_ms,sync_ms,render_hz,stage,"
                        "draws_r,verts_r,gpu_kick_ms\n";
        } else if (!csv_) {
            AS3D_WARN("cannot open perf.csv");
        }
    }
    steps_.reserve(4096);
    periods_.reserve(4096);
    lastFlush_ = nowTicks();
}

void Perf::record(const FrameSample& s) {
    const size_t linearFree = linearSpaceFree();
    linearFreeMin_ = std::min(linearFreeMin_, linearFree);
    // The heap's high-water mark. mallinfo walks every chunk of the heap on the main thread: done
    // every 32 frames (twice: here and in heapSample) it was the frame after it that missed its
    // vblank, 59% and 78% of the mono misses of the third console soak (frame mod 32 == 1: a 57-65%
    // miss rate there against 0.6-1.3% elsewhere; docs/3ds-port.md "Third console soak"). Now one
    // walk every kHeapSampleFrames, and the next frame's miss is excused as after an SD write.
    if (s.frame % kHeapSampleFrames == 0) {
        struct mallinfo mi = mallinfo();
        heapHigh_ = std::max(heapHigh_, static_cast<size_t>(mi.uordblks));
        heapArenaHigh_ = std::max(heapArenaHigh_, static_cast<size_t>(mi.arena));
        heapNote(static_cast<size_t>(mi.uordblks));
        g_ioFlag = true;
    }
    stepSum_ += s.stepMs;
    stepMax_ = std::max<double>(stepMax_, s.stepMs);
    if (steps_.size() < kMaxSamples) steps_.push_back(s.stepMs);
    if (s.periodMs > 0.0f) {
        periodSum_ += s.periodMs;
        periodMax_ = std::max<double>(periodMax_, s.periodMs);
        ++periodCount_;
        if (s.periodMs > kOverMs) ++over_;
        if (periods_.size() < kMaxSamples) periods_.push_back(s.periodMs);
    }
    if (s.periodMs > 0.0f) {
        const float split[8] = {s.stats.prepareMs, s.waitMs, s.submitMs, s.endMs, s.afterMs, s.cpuMs, s.gpuMs, s.syncMs};
        for (int i = 0; i < 8; ++i) splitSum_[i] += split[i];
        ++splitCount_;
        if (gpus_.size() < kMaxSamples) gpus_.push_back(s.gpuMs);
    }
    afterMax_ = std::max<double>(afterMax_, s.afterMs);
    if (s.periodMs > 0.0f) {
        ++stepHist_[std::min(std::max(s.simSteps, 0), 3)];
        if (s.renderHz == 30) ++framesAt30_;
    }
    lazyMax_ = std::max(lazyMax_, s.stats.lazyLoads);
    arenaMax_ = std::max(arenaMax_, s.stats.arenaKb);
    // gpu_ms is the previous frame's tail after its C3D_FrameEnd; with its kick-to-end time it is
    // that frame's GPU time from the first command list.
    const float gpuKickMs =
        prevEnd_ > prevGpuStart_ && prevGpuStart_ ? static_cast<float>(ticksToMs(prevEnd_ - prevGpuStart_)) + s.gpuMs : s.gpuMs;
    prevGpuStart_ = s.stats.gpuStartTick ? s.stats.gpuStartTick : s.endTick;
    prevEnd_ = s.endTick;
    if (csv_) {
        char row[256];
        const int n = std::snprintf(row, sizeof row, "%u,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%u,%u,%u,%u,%u,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%.3f\n",
                                    s.frame, s.stepMs, s.stats.prepareMs, s.cpuMs, s.gpuMs, s.stats.eyes, s.stats.verts,
                                    s.stats.draws, s.stats.texVramKb, s.stats.texLinearKb,
                                    static_cast<unsigned>(linearFree / 1024), s.stats.lazyLoads, s.stats.arenaKb,
                                    s.simSteps, s.submitMs, s.waitMs, s.endMs, s.afterMs, s.syncMs, s.renderHz, stage_,
                                    s.stats.drawsRight, s.stats.vertsRight, gpuKickMs);
        if (n > 0) {
            std::lock_guard<std::mutex> lock(g_logMutex);
            g_csvBuf.append(row, static_cast<size_t>(std::min(n, static_cast<int>(sizeof row) - 1)));
        }
    }
    service();
}

void Perf::service() {
    const uint64_t now = nowTicks();
    if (now - lastFlush_ < SYSCLOCK_ARM11) return;
    lastFlush_ = now;
    wakeWriter(); // the writer thread writes; nothing on this thread
}

void Perf::flush() {
    lastFlush_ = nowTicks();
    g_ioFlag = true;
    drainToFiles();
}

void Perf::gpuStats(double* avg, double* p95) {
    *avg = splitCount_ ? splitSum_[6] / static_cast<double>(splitCount_) : 0.0;
    *p95 = percentile(gpus_, 0.95);
}

void Perf::writeSummary(unsigned frames, unsigned steps, unsigned dropped) {
    const double n = frames ? static_cast<double>(frames) : 1.0;
    const double stepP95 = percentile(steps_, 0.95);
    const double periodP95 = percentile(periods_, 0.95);
    const double periodAvg = periodCount_ ? periodSum_ / periodCount_ : 0.0;
    struct mallinfo mi = mallinfo();
    heapHigh_ = std::max(heapHigh_, static_cast<size_t>(mi.uordblks));
    heapArenaHigh_ = std::max(heapArenaHigh_, static_cast<size_t>(mi.arena));
    AS3D_INFO("PERF_SUMMARY frames=%u steps=%u dropped=%u", frames, steps, dropped);
    AS3D_INFO("PERF_SUMMARY step_ms avg=%.3f p95=%.3f max=%.3f", stepSum_ / n, stepP95, stepMax_);
    AS3D_INFO("PERF_SUMMARY frame_ms avg=%.3f p95=%.3f max=%.3f", periodAvg, periodP95, periodMax_);
    if (splitCount_) {
        const double k = static_cast<double>(splitCount_);
        AS3D_INFO("PERF_SUMMARY split_ms prepare=%.3f wait=%.3f submit=%.3f sync=%.3f end=%.3f after=%.3f after_max=%.1f "
                  "cpu=%.3f gpu=%.3f gpu_p95=%.3f",
                  splitSum_[0] / k, splitSum_[1] / k, splitSum_[2] / k, splitSum_[7] / k, splitSum_[3] / k, splitSum_[4] / k,
                  afterMax_, splitSum_[5] / k, splitSum_[6] / k, percentile(gpus_, 0.95));
    }
    AS3D_INFO("PERF_SUMMARY steps_per_frame 0=%u 1=%u 2=%u 3plus=%u frames_at_30hz=%u", stepHist_[0], stepHist_[1],
              stepHist_[2], stepHist_[3], framesAt30_);
    AS3D_INFO("PERF_SUMMARY over_ms=%.1f over=%u of=%u", kOverMs, over_, periodCount_);
    AS3D_INFO("PERF_SUMMARY load_ms=%.0f", loadMs_);
    AS3D_INFO("PERF_SUMMARY heap_kb high=%zu arena=%zu", heapHigh_ / 1024, heapArenaHigh_ / 1024);
    AS3D_INFO("PERF_SUMMARY linear_free_kb now=%zu min=%zu", static_cast<size_t>(linearSpaceFree()) / 1024,
              (linearFreeMin_ == SIZE_MAX ? 0 : linearFreeMin_) / 1024);
    AS3D_INFO("PERF_SUMMARY lazy_loads max=%u", lazyMax_);
    AS3D_INFO("PERF_SUMMARY arena_kb max=%u", arenaMax_);
    flush();
}

void Perf::close() {
    as3d::setLogSink(nullptr);
    stopWriter();
    drainToFiles();
    std::lock_guard<std::mutex> files(g_fileMutex);
    if (g_csvFile) {
        std::fclose(g_csvFile);
        g_csvFile = nullptr;
    }
    csv_ = nullptr;
    if (g_logFile) {
        std::fclose(g_logFile);
        g_logFile = nullptr;
    }
}

} // namespace ctrapp
