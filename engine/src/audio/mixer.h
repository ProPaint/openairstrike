// Fixed-voice-count software mixer: linear-interpolation resampling,
// constant-power pan, looping, voice stealing, and a soft output limiter.
//
// Concurrency: a single mutex guards the whole voice table (and the music
// decoder slot). This is deliberately not a lock-free design -- see the
// comment on Mixer below and engine/include/as3d/audio.h's header comment
// for why a plain mutex is the right trade-off here.
#pragma once

#include <mutex>

#include "as3d/audio.h"
#include "music_decoder.h"
#include "wav.h"

namespace as3d::audio {

using as3d::u64;

constexpr int kMaxVoices = 32;

struct Voice {
    const Sample* sample = nullptr;
    double pos = 0.0;   // fractional frame index into sample->pcm
    double step = 1.0;  // resample increment per output frame
    float volume = 1.0f;
    float gainL = 1.0f;
    float gainR = 1.0f;
    bool loop = false;
    bool active = false;
    u32 generation = 1;  // bumped every time the slot is reused; encoded into VoiceId
    u64 startSeq = 0;    // for "oldest wins" voice-stealing tie-breaks
};

// Mixer state is protected by a single mutex rather than a lock-free
// command queue: every public entry point (from the game thread) does O(1)
// or O(kMaxVoices) work under the lock, and render() (from the audio
// thread) never allocates, blocks on I/O, or calls anything that can throw
// while holding it. With only 32 voices the whole critical section is a
// handful of microseconds, so contention is a non-issue at audio-engine
// scale, and a mutex is far easier to get right than a correct multi-writer
// lock-free structure. See docs/audio.md "Thread safety".
class Mixer {
public:
    Mixer() = default;
    ~Mixer();
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;

    void setDeviceRate(int rate);

    VoiceId startVoice(const Sample* sample, const PlayParams& params);
    void stopVoice(VoiceId id);
    void setVoiceParams(VoiceId id, const PlayParams& params);
    void stopAll();

    // Takes ownership of `decoder` (may be null to stop music). `decoder`
    // must already have had setLoop() applied.
    void setMusic(MusicDecoder* decoder);
    void clearMusic();
    bool hasMusic() const;
    // Under the lock: jumps the music to `order`, or to order 0 when the module is shorter.
    bool jumpMusicToOrder(int order);
    int musicOrder() const;

    void setSfxVolume(float v);
    void setMusicVolume(float v);
    void setPaused(bool paused);

    // Renders `frames` interleaved stereo frames into `out` (2*frames
    // floats already owned by the caller). Safe to call from any single
    // thread as long as it's always the same thread (the "audio thread").
    void render(float* out, int frames);

    int activeVoiceCount() const;
    u64 voicesStolenCount() const;
    u64 totalVoicesStartedCount() const;

private:
    int allocateSlot();  // caller holds mutex_
    void applyPan(Voice& v, const PlayParams& params) const;

    mutable std::mutex mutex_;
    Voice voices_[kMaxVoices]{};
    int deviceRate_ = 44100;
    float sfxVolume_ = 1.0f;
    float musicVolume_ = 1.0f;
    bool paused_ = false;
    u64 nextSeq_ = 1;
    u64 stolen_ = 0;
    u64 started_ = 0;
    MusicDecoder* music_ = nullptr;  // owned
};

}  // namespace as3d::audio
