#include "game_view.h"

#include <algorithm>

namespace as3d_game {

using namespace as3d;

bool GameView::init(GameSession& session, std::string* error) {
    if (!renderer_.init(session.vfs(), session.db(), error)) return false;
    return beginLevel(session, error);
}

bool GameView::beginLevel(GameSession& session, std::string* error) {
    return renderer_.beginLevel(session.world(), error);
}

void GameView::step(const GameSession& session) {
    const World& w = session.world();
    if (!w.paused()) renderer_.step(w, w.config().dt);
}

void GameView::draw(const GameSession& session, int width, int height) {
    renderer_.render(session.world(), width, height);
    buildStatusOverlay(session, rects_);
    renderer_.drawOverlay(rects_.data(), rects_.size(), width, height);
}

void buildStatusOverlay(const GameSession& session, std::vector<OverlayRect>& out) {
    out.clear();
    const World& w = session.world();
    if (w.intermission()) return;
    // Health bar where the HUD has it (engine-behaviour.md 11.2): frame at (10, 10)
    // 180x21, fill from x = 12, full at 400 health.
    for (int p = 0; p < w.numPlayers(); ++p) {
        float y = 10.0f + 26.0f * static_cast<float>(p);
        out.push_back({10.0f, y, 180.0f, 21.0f, Vec4{0.1f, 0.1f, 0.1f, 0.6f}});
        int pi = w.playerEntityIndex(p);
        float health = pi >= 0 ? w.entity(pi).f(F_HEALTH) : 0.0f;
        float k = std::min(std::max(health / 400.0f, 0.0f), 1.0f);
        Vec4 col = k > 0.5f ? Vec4{0.2f, 0.85f, 0.2f, 0.9f} : k > 0.25f ? Vec4{0.95f, 0.75f, 0.1f, 0.9f}
                                                                       : Vec4{0.95f, 0.15f, 0.1f, 0.9f};
        if (k > 0.0f) out.push_back({12.0f, y + 3.0f, 172.0f * k, 15.0f, col});
    }
    // Lives: min(p_lives, 5) icons at x = 15 + 32 i, y = 555.
    float lives = w.player(0).lives;
    int n = lives > 0.0f ? std::min(static_cast<int>(lives), 5) : 0;
    for (int i = 0; i < n; ++i) out.push_back({15.0f + 32.0f * static_cast<float>(i), 555.0f, 24.0f, 24.0f, Vec4{0.9f, 0.6f, 0.1f, 0.85f}});
    // Tutorial hint box placeholder (11.3): centred 360x160; the text goes to stdout.
    if (w.hintShowing()) out.push_back({220.0f, 220.0f, 360.0f, 160.0f, Vec4{0.05f, 0.08f, 0.2f, 0.75f}});
}

} // namespace as3d_game
