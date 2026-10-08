// Sound of the 3DS game loop (M2.3): an AudioBridge on the platform device (the ndsp device of
// engine/src/audio/ndsp/, or the Null device when ndspInit fails), driven like the desktop
// game_stack / game_loop: precache of the level's sounds and startLevel at level start, the
// world's sound queue drained after every step, paused with the world and on APT suspend/sleep,
// stopped at exit. Music (M2.4), when run.ini music is not 0: the level's module pre-rendered
// as <game dir>/music/<name>.ogg (tremor, AS3D_WITH_TREMOR) when the file exists, else the
// module through libopenmpt (AS3D_WITH_OPENMPT), else none; the decoder runs on the feeder
// thread (core 1).
#pragma once

#include <cstdint>
#include <string>

#include "audio_bridge.h"

namespace as3d_game {
class GameSession;
}

namespace ctrapp {

class CtrAudio {
public:
    // run.ini music=0|1 and music_taps, the pre-rendered files' directory; call before start() /
    // attach(). Off in a build without a music decoder.
    void setMusic(bool on, int taps, const std::string& prerenderedDir) {
        music_ = on && (AS3D_WITH_OPENMPT || AS3D_WITH_TREMOR);
        taps_ = taps;
        prerenderedDir_ = prerenderedDir;
    }
    bool music() const { return music_; }
    // Opens the device and starts the session's level (precache, startLevel). False when audio
    // could not start at all (nothing is played then); the Null fallback counts as started.
    // `precache` false (run.ini precache=0): no level-start precache, sounds load on first use.
    bool start(as3d_game::GameSession& session, bool precache = true);
    // The front end (M2.1): GameStack owns the bridge and GameFlow starts the levels (the view
    // precaches the sounds); this only follows APT suspend/sleep, drains the world's queue and
    // logs the summary. stop() leaves the bridge to its owner.
    void attach(as3d_game::AudioBridge& bridge);
    // A new level (GameSession::kLevelStarted).
    void levelStarted(as3d_game::GameSession& session);
    // After each simulation step: plays and clears the world's sound requests.
    void afterStep(as3d::World& world);
    // APT suspend / sleep (true) and restore / wake up (false).
    void setSuspended(bool suspended);
    // Once per displayed frame: follows the world's pause (START, the bottom-screen button).
    void followPause(bool worldPaused);
    // The PERF_SUMMARY audio lines (audio, audio_feed, music).
    void logSummary();
    void stop();

    bool active() const { return started_; }

private:
    void apply();
    void precache(as3d_game::GameSession& session);
    as3d_game::AudioBridge& bridge() { return external_ ? *external_ : bridge_; }
    const as3d_game::AudioBridge& bridge() const { return external_ ? *external_ : bridge_; }

    as3d_game::AudioBridge bridge_;
    as3d_game::AudioBridge* external_ = nullptr; // attach()
    bool started_ = false;
    bool suspended_ = false, worldPaused_ = false;
    int precached_ = 0;
    bool precache_ = true;
    bool music_ = AS3D_WITH_OPENMPT || AS3D_WITH_TREMOR;
    int taps_ = 0; // run.ini music_taps
    std::string prerenderedDir_;
};

} // namespace ctrapp
