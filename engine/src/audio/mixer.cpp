#include "mixer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace as3d::audio {
namespace {

constexpr int kSlotBits = 6;  // room for 64 slots; kMaxVoices (32) fits with headroom
constexpr u32 kSlotMask = (1u << kSlotBits) - 1;

VoiceId encodeVoiceId(int slot, u32 generation) {
    return (generation << kSlotBits) | static_cast<u32>(slot);
}
int decodeSlot(VoiceId id) { return static_cast<int>(id & kSlotMask); }
u32 decodeGeneration(VoiceId id) { return id >> kSlotBits; }

constexpr double kPi = 3.14159265358979323846;

}  // namespace

Mixer::~Mixer() { delete music_; }

void Mixer::setDeviceRate(int rate) {
    std::lock_guard<std::mutex> lock(mutex_);
    deviceRate_ = rate > 0 ? rate : 44100;
}

void Mixer::applyPan(Voice& v, const PlayParams& params) const {
    float pan = std::clamp(params.pan, -1.0f, 1.0f);
    double angle = (static_cast<double>(pan) + 1.0) * (kPi / 4.0);  // 0 .. pi/2
    v.gainL = static_cast<float>(std::cos(angle));
    v.gainR = static_cast<float>(std::sin(angle));
    v.volume = std::max(0.0f, params.volume);
}

int Mixer::allocateSlot() {
    for (int i = 0; i < kMaxVoices; ++i) {
        if (!voices_[i].active) return i;
    }
    // Steal the quietest voice; ties broken by the oldest (smallest startSeq).
    int victim = 0;
    for (int i = 1; i < kMaxVoices; ++i) {
        if (voices_[i].volume < voices_[victim].volume ||
            (voices_[i].volume == voices_[victim].volume && voices_[i].startSeq < voices_[victim].startSeq)) {
            victim = i;
        }
    }
    ++stolen_;
    return victim;
}

VoiceId Mixer::startVoice(const Sample* sample, const PlayParams& params) {
    if (!sample || sample->frameCount() == 0 || sample->rate <= 0) return kInvalidVoiceId;
    std::lock_guard<std::mutex> lock(mutex_);
    int slot = allocateSlot();
    Voice& v = voices_[slot];
    v.sample = sample;
    v.pos = 0.0;
    double pitch = params.pitch > 0.0f ? params.pitch : 1.0f;
    v.step = (static_cast<double>(sample->rate) / static_cast<double>(deviceRate_)) * pitch;
    v.loop = params.loop;
    v.active = true;
    v.generation = v.generation == 0 ? 1 : v.generation + 1;
    v.startSeq = nextSeq_++;
    applyPan(v, params);
    ++started_;
    return encodeVoiceId(slot, v.generation);
}

void Mixer::stopVoice(VoiceId id) {
    if (id == kInvalidVoiceId) return;
    std::lock_guard<std::mutex> lock(mutex_);
    int slot = decodeSlot(id);
    if (slot < 0 || slot >= kMaxVoices) return;
    Voice& v = voices_[slot];
    if (v.active && v.generation == decodeGeneration(id)) v.active = false;
}

void Mixer::setVoiceParams(VoiceId id, const PlayParams& params) {
    if (id == kInvalidVoiceId) return;
    std::lock_guard<std::mutex> lock(mutex_);
    int slot = decodeSlot(id);
    if (slot < 0 || slot >= kMaxVoices) return;
    Voice& v = voices_[slot];
    if (!v.active || v.generation != decodeGeneration(id)) return;
    double pitch = params.pitch > 0.0f ? params.pitch : 1.0f;
    v.step = (static_cast<double>(v.sample->rate) / static_cast<double>(deviceRate_)) * pitch;
    v.loop = params.loop;
    applyPan(v, params);
}

void Mixer::stopAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& v : voices_) v.active = false;
}

void Mixer::setMusic(MusicDecoder* decoder) {
    std::lock_guard<std::mutex> lock(mutex_);
    delete music_;
    music_ = decoder;
}

void Mixer::clearMusic() {
    std::lock_guard<std::mutex> lock(mutex_);
    delete music_;
    music_ = nullptr;
}

bool Mixer::jumpMusicToOrder(int order) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!music_) return false;
    if (order < 0 || order >= music_->numOrders()) order = 0;
    return music_->setOrder(order);
}

int Mixer::musicOrder() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return music_ ? music_->currentOrder() : -1;
}

bool Mixer::hasMusic() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return music_ != nullptr;
}

void Mixer::setSfxVolume(float v) {
    std::lock_guard<std::mutex> lock(mutex_);
    sfxVolume_ = std::max(0.0f, v);
}

void Mixer::setMusicVolume(float v) {
    std::lock_guard<std::mutex> lock(mutex_);
    musicVolume_ = std::max(0.0f, v);
}

void Mixer::setPaused(bool paused) {
    std::lock_guard<std::mutex> lock(mutex_);
    paused_ = paused;
}

void Mixer::render(float* out, int frames) {
    if (frames <= 0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    std::memset(out, 0, sizeof(float) * 2 * static_cast<size_t>(frames));
    if (paused_) return;

    for (auto& v : voices_) {
        if (!v.active) continue;
        const Sample* s = v.sample;
        const size_t frameCount = s->frameCount();
        const int channels = s->channels;
        for (int i = 0; i < frames && v.active; ++i) {
            size_t i0 = static_cast<size_t>(v.pos);
            if (i0 >= frameCount) {
                v.active = false;
                break;
            }
            double frac = v.pos - static_cast<double>(i0);
            size_t i1 = i0 + 1;
            if (i1 >= frameCount) i1 = v.loop ? 0 : i0;

            float l, r;
            if (channels >= 2) {
                float l0 = s->pcm[i0 * channels + 0] / 32768.0f;
                float l1 = s->pcm[i1 * channels + 0] / 32768.0f;
                float r0 = s->pcm[i0 * channels + 1] / 32768.0f;
                float r1 = s->pcm[i1 * channels + 1] / 32768.0f;
                l = static_cast<float>(l0 + (l1 - l0) * frac);
                r = static_cast<float>(r0 + (r1 - r0) * frac);
            } else {
                float m0 = s->pcm[i0] / 32768.0f;
                float m1 = s->pcm[i1] / 32768.0f;
                l = r = static_cast<float>(m0 + (m1 - m0) * frac);
            }
            float gain = v.volume * sfxVolume_;
            out[i * 2 + 0] += l * v.gainL * gain;
            out[i * 2 + 1] += r * v.gainR * gain;

            v.pos += v.step;
            if (v.pos >= static_cast<double>(frameCount)) {
                if (v.loop) {
                    while (v.pos >= static_cast<double>(frameCount)) v.pos -= static_cast<double>(frameCount);
                } else {
                    v.active = false;
                }
            }
        }
    }

    if (music_) {
        constexpr int kChunk = 256;
        float scratch[kChunk * 2];
        int remaining = frames;
        int offset = 0;
        while (remaining > 0) {
            int n = std::min(remaining, kChunk);
            music_->read(deviceRate_, scratch, n);
            for (int i = 0; i < n * 2; ++i) out[offset * 2 + i] += scratch[i] * musicVolume_;
            offset += n;
            remaining -= n;
        }
    }

    const size_t total = static_cast<size_t>(frames) * 2;
    for (size_t i = 0; i < total; ++i) out[i] = std::tanh(out[i]);
}

int Mixer::activeVoiceCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    int n = 0;
    for (auto& v : voices_) n += v.active ? 1 : 0;
    return n;
}

u64 Mixer::voicesStolenCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stolen_;
}

u64 Mixer::totalVoicesStartedCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return started_;
}

}  // namespace as3d::audio
