#include "game_stack.h"

#include "as3d/load_stats.h"

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
        if (view) {
            view->precacheSounds = [this](const as3d::PrecacheSet& s) { return audio.precacheSounds(s); };
            // GameOptions::precache: view->init precached the first level before the audio
            // existed; its sounds now, before play starts.
            if (const as3d::PrecacheSet* s = session->world().precacheSet()) {
                audio.precacheSounds(*s);
                as3d::LoadStats::reset();
            }
        }
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
