#include "game_view.h"

#include <algorithm>

namespace as3d_game {

using namespace as3d;

bool GameView::init(GameSession& session, std::string* error) {
    if (!renderer_.init(session.vfs(), session.db(), error)) return false;
    // The HUD is optional: without its textures the game still runs (a warning is logged).
    std::string hudError;
    hudReady_ = r2d_.init(&hudError) && assets_.load(session.vfs(), &hudError);
    if (!hudReady_) AS3D_WARN("HUD unavailable: %s", hudError.c_str());
    return beginLevel(session, error);
}

bool GameView::beginLevel(GameSession& session, std::string* error) {
    return renderer_.beginLevel(session.world(), error);
}

void GameView::step(const GameSession& session) {
    const World& w = session.world();
    if (!w.paused()) renderer_.step(w, w.config().dt);
}

ui::HudState hudStateOf(const GameSession& session) {
    const World& w = session.world();
    ui::HudState hs;
    hs.playerCount = std::min(std::max(w.numPlayers(), 1), 2);
    for (int p = 0; p < hs.playerCount; ++p) {
        const PlayerRecord& pr = w.player(p);
        ui::HudPlayer& hp = hs.players[p];
        int pi = w.playerEntityIndex(p);
        hp.health = pi >= 0 ? std::max(w.entity(pi).f(F_HEALTH), 0.0f) : 0.0f;
        hp.lives = pr.lives > 0.0f ? static_cast<int>(std::min(pr.lives, 99.0f)) : 0;
        hp.score = session.displayScore(p);
        hp.weapon = (pr.weapon >= 0.0f && pr.weapon < 64.0f) ? static_cast<int>(pr.weapon) : 0;
        hp.missileSelected = pr.currentMissile;
        for (int t = 0; t < ui::kMissileTypes; ++t) hp.missiles[t] = pr.missiles[t] > 0 ? pr.missiles[t] : -1;
        for (int k = 0; k < ui::kPowerupKinds; ++k) hp.powerups[k] = std::max(pr.powerups[k], 0);
    }
    return hs;
}

void GameView::draw(const GameSession& session, int width, int height) {
    const World& w = session.world();
    renderer_.render(w, width, height);
    // Pass 13: the 2D layer. No HUD on intermission levels or while it is hidden (level end,
    // game over); the hint box is drawn over everything.
    if (hudReady_) {
        r2d_.begin(width, height);
        if (!w.intermission() && !w.levelComplete() && !w.gameOver()) ui::drawHud(r2d_, assets_, hudStateOf(session));
        if (w.hintShowing()) ui::drawHint(r2d_, assets_, w.hintText());
        r2d_.flush();
    }
    // Pass 14: brightness.
    renderer_.drawBrightness(width, height, brightness);
}

} // namespace as3d_game
