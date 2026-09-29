// Rendering side of the game executable: the world renderer plus the minimal status
// overlay that stands in for the HUD until the real one exists (coloured bars only: player
// health, lives, a hint-box placeholder). Needs a current GLES 3.0 context.
#pragma once

#include <string>
#include <vector>

#include "as3d/world_render.h"
#include "game_session.h"

namespace as3d_game {

class GameView {
public:
    bool init(GameSession& session, std::string* error);
    // Call after every level load (GameSession::kLevelStarted and after init).
    bool beginLevel(GameSession& session, std::string* error);
    // One fixed step of the render-side simulation (particles); not while paused.
    void step(const GameSession& session);
    // Draws the world and the status overlay into the bound framebuffer.
    void draw(const GameSession& session, int width, int height);

    as3d::WorldRenderer& renderer() { return renderer_; }

private:
    as3d::WorldRenderer renderer_;
    std::vector<as3d::OverlayRect> rects_;
};

// The status overlay rectangles for the current state (virtual 800x600 screen).
void buildStatusOverlay(const GameSession& session, std::vector<as3d::OverlayRect>& out);

} // namespace as3d_game
