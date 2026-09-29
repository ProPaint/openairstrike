#include "game_stack.h"

namespace as3d_game {

bool GameStack::build(const StackConfig& config, std::string* error, const std::function<void(float)>& progress) {
    teardown();
    config_ = config;
    auto fail = [&]() {
        teardown();
        return false;
    };
    session.reset(new GameSession());
    if (!session->init(config.game, error)) return fail();
    if (progress) progress(0.3f);
    if (config.view) {
        view.reset(new GameView());
        std::string err;
        if (!view->init(*session, &err, session->hasLevel())) {
            if (error) *error = "renderer: " + err;
            return fail();
        }
    }
    if (!config.noAudio) {
        audio.init(session->vfs(), config.nullAudio);
        audio.startLevel(session->musicPath());
    }
    if (config.frontend) {
        flow.reset(new GameFlow(*session, audio));
        std::string err;
        if (!flow->init(config.flow, &err)) {
            if (error) *error = "front end: " + err;
            return fail();
        }
        flow->setView(view.get());
    }
    return true;
}

void GameStack::teardown() {
    if (flow) {
        flow->saveNow();
        flow.reset();
    }
    audio.shutdown();
    view.reset();
    session.reset();
}

} // namespace as3d_game
