// Rendering side of the game executable: the world renderer, the HUD and tutorial hint box
// (engine/src/ui), the front end's screens with the main menu's 3D banner, and the
// brightness overlay, in the frame order of docs/spec/render-pipeline.md 1.1 (3D passes, 2D
// layer, brightness) and frontend.md 4.1. Needs a current GLES 3.0 context.
#pragma once

#include <memory>
#include <string>

#include "as3d/profile.h"
#include "as3d/ui.h"
#include "as3d/world_render.h"
#include "game_session.h"

namespace as3d::ui {
class Frontend;
}

namespace as3d_game {

// config.ini Brightness default (render-pipeline.md 1.5).
constexpr float kDefaultBrightness = 0.6f;

// What one frame shows besides the world, behind the front end (game_flow.h).
struct FrameLayers {
    bool world = true;                     // the 3D level (false: black, e.g. the intro pages)
    bool hud = false;
    as3d::ui::HudState hudState;
    as3d::ui::Frontend* frontend = nullptr; // menus, intro pages, banner
    float brightness = kDefaultBrightness;
};

class GameView {
public:
    GameView();
    ~GameView();
    // With `level`, also beginLevel (the session must have a level loaded).
    bool init(GameSession& session, std::string* error, bool level = true);
    // Call after every level load (GameSession::kLevelStarted and after init).
    bool beginLevel(GameSession& session, std::string* error);
    // One fixed step of the render-side simulation (particles); not while paused.
    void step(const GameSession& session);
    // Draws the world, the HUD (and a hint box the world holds) and the brightness overlay
    // into the bound framebuffer: the game without the front end (`--level N`).
    void draw(const GameSession& session, int width, int height);
    // The frame behind the front end: world, HUD, front end (with the banner between its two
    // halves), brightness.
    void drawFrame(const GameSession& session, int width, int height, const FrameLayers& layers);
    // The main menu's 3D banner (frontend.md 3.3 step 5) in the top 200 virtual pixels.
    void drawBanner(float mt, int width, int height);

    as3d::WorldRenderer& renderer() { return renderer_; }
    bool hudAvailable() const { return hudReady_; }
    const as3d::ui::UiAssets& assets() const { return assets_; }
    as3d::ui::Renderer2D& overlay() { return r2d_; }
    float brightness = kDefaultBrightness;
    // Settings::screenMode (as3d/profile.h): with kScreen4x3 the world is drawn only in the
    // centred 4:3 area and everything outside it ends black (after the 2D layer and the
    // brightness pass). Drawing only: the simulation never sees it.
    int screenMode = as3d::kScreenWide;

private:
    struct Banner;
    void renderWorld(const as3d::World& world, int width, int height);
    void clearBars(int width, int height);
    as3d::WorldRenderer renderer_;
    as3d::ui::Renderer2D r2d_;
    as3d::ui::UiAssets assets_;
    std::unique_ptr<Banner> banner_;
    bool hudReady_ = false;
};

// The HUD state for the current frame, from the world: health, lives, score, weapon,
// missiles, power-ups, and the level name with the level clock for the typewriter.
as3d::ui::HudState hudStateOf(const GameSession& session);

} // namespace as3d_game
