// One running game as a unit: the session (data, world), its renderer, its audio and its front
// end, built together and torn down together, so that the window can leave a game for the game
// selector and start another without restarting the process (docs/spec/issues/163). The
// windowed loop (game_loop.cpp) runs its game through one; apps/tests/launcher_test.cpp builds
// and tears down the two games in turn with the same code and counts the GL objects.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "audio_bridge.h"
#include "game_flow.h"
#include "game_session.h"
#include "game_view.h"

namespace as3d_game {

struct StackConfig {
    GameOptions game;
    bool frontend = false;    // a GameFlow over the session (the menus)
    FlowConfig flow;
    bool view = true;         // a renderer: needs a current GLES 3.0 context
    bool noAudio = false;     // no mixer at all
    bool nullAudio = false;   // the mixer on the null device (headless, tests)
};

class GameStack {
public:
    GameStack() = default;
    ~GameStack() { teardown(); }
    GameStack(const GameStack&) = delete;
    GameStack& operator=(const GameStack&) = delete;

    // Session, renderer (with `view`; the level's too when the session starts one), audio with
    // the level's music, front end (not booted: the owner sets its hooks first). `progress`
    // runs between the steps (0.3 after the data is loaded). On failure everything built so far
    // is torn down again.
    bool build(const StackConfig& config, std::string* error, const std::function<void(float)>& progress = {});
    // The profile is saved, then the front end, the audio device, the renderer (every GL object
    // of the game) and the session (VFS, definitions, world) go, in that order. Safe to repeat.
    void teardown();
    bool active() const { return session != nullptr; }
    const StackConfig& config() const { return config_; }

    std::unique_ptr<GameSession> session;
    std::unique_ptr<GameView> view;
    AudioBridge audio;
    std::unique_ptr<GameFlow> flow;

private:
    StackConfig config_;
};

} // namespace as3d_game
