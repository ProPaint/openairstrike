// Rendering side of the game executable: the world renderer, the HUD and tutorial hint box
// (engine/src/ui) and the brightness overlay, in the frame order of
// docs/spec/render-pipeline.md 1.1 (3D passes, 2D layer, brightness). Needs a current
// GLES 3.0 context.
#pragma once

#include <string>

#include "as3d/ui.h"
#include "as3d/world_render.h"
#include "game_session.h"

namespace as3d_game {

// config.ini Brightness default (render-pipeline.md 1.5).
constexpr float kDefaultBrightness = 0.6f;

class GameView {
public:
    bool init(GameSession& session, std::string* error);
    // Call after every level load (GameSession::kLevelStarted and after init).
    bool beginLevel(GameSession& session, std::string* error);
    // One fixed step of the render-side simulation (particles); not while paused.
    void step(const GameSession& session);
    // Draws the world, the HUD and the brightness overlay into the bound framebuffer.
    void draw(const GameSession& session, int width, int height);

    as3d::WorldRenderer& renderer() { return renderer_; }
    bool hudAvailable() const { return hudReady_; }
    float brightness = kDefaultBrightness;

private:
    as3d::WorldRenderer renderer_;
    as3d::ui::Renderer2D r2d_;
    as3d::ui::UiAssets assets_;
    bool hudReady_ = false;
};

// The HUD state for the current frame, from the world (only the fields the HUD spec
// settles: health, lives, score, weapon, missiles, power-ups).
as3d::ui::HudState hudStateOf(const GameSession& session);

} // namespace as3d_game
