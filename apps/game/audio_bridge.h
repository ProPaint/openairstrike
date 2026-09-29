// Plays the world's sound requests (World::soundEvents(), filled by StartSound,
// StartLoopingSound and StopLoopingSound) through the engine mixer, and the level music
// (docs/spec/engine-behaviour.md 12).
//
// 3D sound is off, as in the shipped config.ini (Sound3D=0): "without 3D sound, sounds play
// centred at full volume". Volumes start at the shipped SfxVolume / MusicVolume (0.5); behind
// the front end they follow its settings (setVolumes). The front end's menu sounds, the
// level-name typewriter and the game-over music jump go through playUi / gameOverMusic.
#pragma once

#include <string>
#include <vector>

#include "as3d/audio.h"
#include "as3d/world.h"

namespace as3d_game {

class AudioBridge {
public:
    struct Stats {
        unsigned long long played = 0;
        unsigned long long loopsStarted = 0;
        unsigned long long loopsStopped = 0;
        unsigned long long missing = 0; // requests whose sample could not be loaded
    };

    // `nullDevice`: no output device, the mixer is pulled by pump() (headless, tests).
    // Falls back to the null device when the SDL device cannot be opened.
    bool init(as3d::Vfs& vfs, bool nullDevice);
    void shutdown();

    // Stops every sound and starts the level's music module (empty: no music).
    void startLevel(const std::string& musicPath);
    // Processes and clears the world's sound queue; stops loops whose entity is gone.
    void drain(as3d::World& world);
    void setPaused(bool paused);
    // A 2D sound outside the world's queue (menu sounds, sounds\\type.wav).
    void playUi(const std::string& sample);
    void setVolumes(float sfx, float music);
    // The current module jumps to pattern order 35 (frontend.md 3.10).
    void gameOverMusic();
    // Null device only: mixes `frames` stereo frames, as the device callback would.
    void pump(int frames);

    bool active() const { return ok_; }
    bool nullDevice() const { return null_; }
    const Stats& stats() const { return stats_; }
    as3d::Audio& audio() { return audio_; }
    int activeLoops() const { return static_cast<int>(loops_.size()); }

private:
    struct Loop {
        as3d::EntityHandle entity;
        std::string sample;
        as3d::VoiceId voice = as3d::kInvalidVoiceId;
    };
    void stopLoopsOf(const as3d::EntityHandle& h, const std::string* sample);

    as3d::Audio audio_;
    bool ok_ = false;
    bool null_ = true;
    bool paused_ = false;
    std::vector<Loop> loops_;
    std::vector<float> scratch_;
    Stats stats_;
};

} // namespace as3d_game
