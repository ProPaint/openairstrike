// Rendering side of the game executable: the world renderer, the HUD and tutorial hint box
// (engine/src/ui), the front end's screens with the main menu's 3D banner, and the
// brightness overlay, in the frame order of docs/spec/render-pipeline.md 1.1 (3D passes, 2D
// layer, brightness) and frontend.md 4.1. Needs a current GLES 3.0 context.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "as3d/defs.h"
#include "as3d/profile.h"
#include "as3d/vfs.h"
#include "as3d/ui.h"
#include "as3d/world_render.h"
#include "game_session.h"

namespace as3d::ui {
class Frontend;
struct ModelView;
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

// The first game's main-menu banner (objects\banner.obj: the flaming title mesh, additive, unlit),
// with its own mesh renderer and resources, drawn in a viewport of the bound framebuffer. The
// game view draws it in the main menu's top 200 virtual pixels; the game selector's card draws
// it into its marquee (docs/spec/issues/164).
class BannerMesh {
public:
    BannerMesh();
    ~BannerMesh();
    BannerMesh(const BannerMesh&) = delete;
    BannerMesh& operator=(const BannerMesh&) = delete;
    // Loads the mesh and its textures through `vfs`, which must outlive this object.
    bool init(as3d::Vfs& vfs, const as3d::DefDatabase& db, std::string* error);
    bool valid() const { return impl_ != nullptr; }
    // The viewport (x, yTop, w, h) is in pixels of a framebuffer of fbWidth x fbHeight, y from its
    // top edge; with `clip` (x, yTop, w, h, the same way) the mesh is scissored to it. `mt` is the
    // menu time. `loopFit` (the web page's rendered marquee): the yaw swings at the pitch's period,
    // so that 2 pi seconds loop exactly.
    void draw(float mt, int fbWidth, int fbHeight, int x, int yTop, int w, int h, const int* clip = nullptr,
              bool loopFit = false);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// A rectangle of the bound framebuffer in pixels, GL convention: (x, y) is the lower-left corner,
// y counts from the bottom (the convention of glViewport and of as3d::Viewport).
struct ViewRect {
    int x = 0, y = 0, width = 0, height = 0;
};

// Which layout the HUD takes (as3d/ui.h, tables in as3d/hud_layout.h): Main is the 800x600
// virtual screen scaled into the rect (the desktop, Android and web layout); Bottom320x240 is
// the 3DS bottom screen's HUD, laid out for the 0.4 scale of a 320x240 rect with the lower
// 36 px kept free for the touch buttons.
using HudTarget = as3d::ui::HudTarget;

// The desktop preview of the 3DS's two screens (as3d_game --dual-screen): a 400x480 frame with
// the top screen (400x240, the world, no HUD) above the bottom one (320x240, centred: HUD, hint
// box, menus and touch buttons). In a larger frame the pair is scaled uniformly (the smaller of
// width / 400 and height / 480) and centred. Rectangles in GL convention like ViewRect.
constexpr int kDualTopWidth = 400, kDualBottomWidth = 320, kDualScreenHeight = 240;
constexpr int kDualFrameWidth = 400, kDualFrameHeight = 480;

struct DualScreenRects {
    ViewRect top, bottom;
    float scale = 1.0f; // frame pixels per 3DS pixel
};
DualScreenRects dualScreenRects(int frameWidth, int frameHeight);

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
    // into the bound framebuffer: the game without the front end (`--level N`). The order of
    // the layer calls below: renderWorld (worldRect, prepareWorld, drawWorld), drawHud,
    // drawFront (the hint box), drawBrightness, clearBars; each over the frame rectangle
    // (0, 0, width, height).
    void draw(const GameSession& session, int width, int height);
    // The frame behind the front end: world, HUD, front end (with the banner between its two
    // halves), brightness.
    void drawFrame(const GameSession& session, int width, int height, const FrameLayers& layers);
    // The layers, for a host that draws them into separate targets. Each sets its own viewport
    // (and scissor) and lays its 2D content out with computeMapping(rect.width, rect.height), so
    // that a 320x240 rect gets the 0.4 scale layout.
    //
    // The rectangle the world takes inside `frame`: the whole frame, or the centred 4:3 field
    // with screenMode == kScreen4x3.
    ViewRect worldRect(const ViewRect& frame) const;
    // Once per frame: reads the world for the draws that follow. `rect` is the world rectangle
    // itself (worldRect(frame)), not the frame. maxEyeOffset / minFocalDistance are the largest
    // |eyeOffset| and smallest non-zero focal distance of the EyeViews drawWorld will be called
    // with (0, 0: mono).
    void prepareWorld(const GameSession& session, const ViewRect& rect, float maxEyeOffset = 0.0f,
                      float minFocalDistance = 0.0f);
    // Passes 0 to 12 for one eye into `rect` (the size of the prepared one), any number of times
    // after one prepareWorld.
    void drawWorld(const GameSession& session, const ViewRect& rect, const as3d::EyeView& eye = as3d::EyeView{});
    // The HUD of the session (nothing on intermission levels or while it is hidden), or of an
    // explicit state, laid out in `rect`.
    void drawHud(const GameSession& session, const ViewRect& rect, HudTarget target = HudTarget::Main);
    void drawHud(const as3d::ui::HudState& state, const ViewRect& rect, HudTarget target = HudTarget::Main);
    // The rest of the 2D layer: the world's hint box (with `hintBox`) and the front end's
    // menus, intro pages, main-menu banner and model views (frontend may be null).
    void drawFront(const GameSession& session, const ViewRect& rect, as3d::ui::Frontend* frontend, bool hintBox,
                   HudTarget target = HudTarget::Main);
    // Pass 14 over `rect`, with the given brightness or the member's.
    void drawBrightness(const ViewRect& rect, float brightness);
    void drawBrightness(const ViewRect& rect);
    // screenMode == kScreen4x3: blacks out what is outside the field of `frame`.
    void clearBars(const ViewRect& frame);
    // The main menu's 3D banner (frontend.md 3.3 step 5) in the top 200 virtual pixels.
    void drawBanner(float mt, int width, int height);
    void drawBanner(float mt, const ViewRect& rect);
    // The sequels' model view (the helicopter selection's preview, as2/frontend.md 3.18): the
    // object with its attachments in a viewport of the frame drawFrame is drawing (or of the
    // last one drawn). Called through GameHost::drawModel.
    void drawModel(GameSession& session, const as3d::ui::ModelView& view);

    as3d::WorldRenderer& renderer() { return renderer_; }
    bool hudAvailable() const { return hudReady_; }
    const as3d::ui::UiAssets& assets() const { return assets_; }
    as3d::ui::Renderer2D& overlay() { return r2d_; }
    float brightness = kDefaultBrightness;
    // Settings::screenMode (as3d/profile.h): with kScreen4x3 the world is drawn only in the
    // centred 4:3 area and everything outside it ends black (after the 2D layer and the
    // brightness pass). Drawing only: the simulation never sees it.
    int screenMode = as3d::kScreenWide;
    // as3d_game --stereo-debug E[,D]: the world drawn twice side by side from one prepare, the
    // left eye (offset -E) in the left half and the right eye (+E) in the right half, with
    // focal distance D (0: parallel eyes). 0 = off (the normal view).
    float stereoDebugEyeOffset = 0.0f;
    float stereoDebugFocal = 0.0f;
    // as3d_game --dual-screen: draw() and drawFrame() compose the frame of dualScreenRects:
    // world and brightness in the top rectangle, HUD, hint box and front end in the bottom one
    // (HudTarget::Bottom320x240, no brightness pass: the screen is mostly black). The touch
    // buttons are the host's.
    bool dualScreen = false;
    // With GameOptions::precache: loads the sounds of the level's precache set (the audio side,
    // AudioBridge::precacheSounds); unset = no sound precache. Returns how many it looked up.
    std::function<int(const as3d::PrecacheSet&)> precacheSounds;
    // The report of the last level-start precache (GameOptions::precache).
    const as3d::PrecacheReport& lastPrecache() const { return lastPrecache_; }

private:
    struct Preview;
    void renderWorld(const GameSession& session, const ViewRect& frame);
    void drawDual(const GameSession& session, const ViewRect& frame, const FrameLayers& layers, bool sessionHud,
                  bool hintBox);
    as3d::WorldRenderer renderer_;
    as3d::ui::Renderer2D r2d_;
    as3d::ui::UiAssets assets_;
    BannerMesh banner_;
    std::unique_ptr<Preview> preview_;
    bool hudReady_ = false;
    ViewRect frameRect_{0, 0, 800, 600};
    // GameOptions::precache: the level precached last, whose lazy loads are logged at its end.
    void precacheLevel(GameSession& session);
    void logLazyLoads();
    as3d::PrecacheReport lastPrecache_;
    int precachedMission_ = -1;
};

// The HUD state for the current frame, from the world: health, lives, score, weapon,
// missiles, power-ups, and the level name with the level clock for the typewriter.
as3d::ui::HudState hudStateOf(const GameSession& session);

} // namespace as3d_game
