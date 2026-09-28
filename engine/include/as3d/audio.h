// Sound effects and tracker music. See docs/audio.md for the design writeup,
// the sound inventory and the music decoder choice.
//
// Thread model: `Audio`'s public methods (other than render()) are called
// from the main/game thread. The mixer itself runs on the audio callback
// thread (Sdl backend) or synchronously inside render() (Null backend,
// called by the game loop or by tests). Commands issued from the main
// thread are queued into a small fixed-capacity, mutex-guarded queue that
// the audio thread drains at the start of every callback; see
// src/audio/command_queue.h for why a plain mutex was chosen over a
// lock-free queue. No allocation happens on the audio thread: sounds are
// decoded up front on the main thread in loadSound(), and only immutable
// data (plus plain values) cross the queue.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "as3d/core.h"
#include "as3d/vfs.h"

namespace as3d {

using u64 = std::uint64_t;

// 0 is never a valid id.
using SoundId = u32;
using VoiceId = u32;
constexpr SoundId kInvalidSoundId = 0;
constexpr VoiceId kInvalidVoiceId = 0;

enum class AudioBackend {
    Null,  // No real device; pull mixed audio manually with Audio::render().
           // Deterministic, used by headless tests and offline rendering.
    Sdl,   // Opens an SDL audio device and mixes from its callback.
};

struct PlayParams {
    float volume = 1.0f;  // linear gain, 0 = silent. May exceed 1; the
                           // mixer's limiter prevents hard clipping.
    float pan = 0.0f;     // -1 (full left) .. 0 (centre) .. +1 (full right),
                           // constant-power law.
    float pitch = 1.0f;   // playback speed / resample ratio, > 0.
    bool loop = false;
};

struct AudioStats {
    int activeVoices = 0;        // SFX voices currently playing
    int maxVoices = 0;           // mixer voice pool size (32)
    u64 totalVoicesStarted = 0;  // lifetime count of play() calls that started a voice
    u64 voicesStolen = 0;        // lifetime count of voice-stealing events
    u64 underruns = 0;           // audio thread found no new command room / device starved
    bool musicPlaying = false;
};

// Opaque base for the two backends; games do not interact with this
// directly beyond choosing an AudioBackend in Audio::init().
class AudioDevice {
public:
    virtual ~AudioDevice() = default;
};

// Mixer, sound cache and music player. One instance per running game.
class Audio {
public:
    Audio();
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    // `preferredRate` is a hint (44100 or 48000); the actual device rate
    // (Sdl backend) may differ and is reported by deviceRate(). Returns
    // false on failure (logs the reason); safe to call init() again after
    // a failed attempt.
    bool init(Vfs& vfs, AudioBackend backend = AudioBackend::Sdl, int preferredRate = 44100);
    void shutdown();

    // Loads and decodes a WAV from `gamePath` (normalised and cached, so
    // repeated calls with equivalent paths return the same id). Returns
    // kInvalidSoundId on failure (missing file, unsupported/malformed WAV);
    // never throws, never crashes on malformed input.
    SoundId loadSound(const char* gamePath);

    // Starts a voice. Returns kInvalidVoiceId if `id` is invalid; otherwise
    // always succeeds (steals the quietest/oldest voice if the 32-voice
    // pool is full).
    VoiceId play(SoundId id, const PlayParams& params = PlayParams{});
    void stop(VoiceId voice);
    void setVoiceParams(VoiceId voice, const PlayParams& params);
    void stopAll();

    // Loads and starts a tracker module (.mo3/.it/.xm/.s3m/.mod, see
    // docs/audio.md) from the VFS. Replaces any currently playing music.
    // Returns false on failure (logs the reason).
    bool playMusic(const char* gamePath, bool loop = true);
    void stopMusic();
    bool isMusicPlaying() const;
    std::string musicTitle() const;

    void setSfxVolume(float linear);
    void setMusicVolume(float linear);
    void pause(bool paused);

    // Null backend only: renders `frames` stereo frames (2*frames floats,
    // interleaved L,R) by draining pending commands and mixing, exactly as
    // the Sdl backend's callback would. No-op (logs a warning once) if the
    // Sdl backend is active.
    void render(float* interleavedStereo, int frames);

    int deviceRate() const;
    AudioStats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace as3d
