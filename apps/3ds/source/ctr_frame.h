// The dual-screen frame composer of the 3DS game loop (M1.8): the FrameHook (ctr_loop.h) of
// mode=bot and mode=play. It owns the PICA WorldRenderer (engine/src/render/pica/
// world_renderer_pica.cpp), the 2D layer (Renderer2D with its PICA flush, UiAssets) and the
// loading screen.
//
// Level start (begin()): pica::initBackend, renderer init, beginLevel (the terrain and its
// textures: the first VRAM tenants), UiAssets::load (font and HUD atlases), then the precache:
// World::precacheScripts, WorldRenderer::precache (particle textures, then the closure's meshes
// and skins), UiAssets::precachePlay; a standalone loading frame (progress bar on both screens)
// between the steps; then AS3D_PRECACHE in the log and LoadStats::reset, so that every later
// load counts as lazy (AS3D_LAZY_LOADS at the end).
//
// Per displayed frame:
//   prepare(info)   the HUD state; the stereo parameters from the slider (stereoFor); one
//                   WorldRenderer::prepare(world, Viewport{400x240, maxEyeOffset = the eye offset,
//                   minFocalDistance = the focal distance}); the top 2D quad list (pause banner,
//                   perf line) built once for both eyes
//   drawTop(eye)    eye 0 opens the backend frame (pica::frameBegin); drawOn(top[eye]);
//                   WorldRenderer::draw(EyeView) (it clears to the level's clear colour: the fog
//                   colour, black without fog); the top quads flushed at screen depth (zero
//                   parallax: the same pixels in both eyes); the brightness pass (DST_COLOR,
//                   SRC_COLOR) unless brightness is the neutral 0.5
//   drawBottom()    drawOn(bottom); clear black; the HUD (HudTarget::Bottom320x240), the hint box,
//                   the touch controls; pica::frameEnd
//   stats()         perf.csv's renderer columns
//   afterFrame()    run.ini dump_at=N: the first frame after N simulation steps writes
//                   top_left.ppm, top_right.ppm (when two eyes were drawn) and bottom.ppm;
//                   dump_every=N and dump_screens=1 (the front end): <prefix>... PPMs
//
// The same composer is the front end's view (makeFlowView below, M2.1).
#pragma once

#include <citro3d.h>

#include <memory>
#include <string>

#include "ctr_loop.h"
#include "flow_view.h"
#include "run_config.h"

namespace as3d_game {
class GameFlow;
}

namespace ctrapp {

// Stereo (world units). The eye offset at slider 1: each eye moves this far along the camera's
// right axis (the eyes are 2 kMaxEyeOffset apart). The focal (zero-parallax) distance is the
// camera-to-player depth, at least kMinFocalDistance, so the player's helicopter sits on the
// screen plane and the ground below and ahead goes into the screen.
//
// With f = 1/tan(fovY/2) and A = f/aspect (1.04 at the game's 60 degrees on 400x240), a point at
// depth d has the on-screen disparity 400 A e (1/D - 1/d) pixels (e = eye offset, D = focal
// distance): the far limit, d -> infinity, is 400 A e / D. Mission 1's camera follows the
// helicopter at a depth of about 180, so kMaxEyeOffset 4 gives at most 9 px of far disparity at
// slider 1 (about 2.3 % of the width), the ground under the helicopter 0, nearer things out of
// the screen. Tune on hardware with run.ini eye_offset= and min_focal=.
constexpr float kMaxEyeOffset = 4.0f;
constexpr float kMinFocalDistance = 40.0f;

struct StereoParams {
    int eyes = 1;            // 1: mono (left eye only, EyeView{})
    float eyeOffset = 0.0f;  // world units, the right eye's; the left eye takes -eyeOffset
    float focal = 0.0f;      // zero-parallax depth
};
// slider in [0, 1] (0 or below the dead zone: mono), the camera-to-player depth.
StereoParams stereoFor(float slider, float playerDepth, float maxEyeOffset, float minFocal);

// A render target as an upright binary PPM (run.ini dumps; the game selector's too).
bool dumpRenderTarget(C3D_RenderTarget* t, int width, const std::string& path);

class GameFrame final : public FrameHook {
public:
    explicit GameFrame(const RunConfig& run);
    ~GameFrame() override;

    // The front end (M2.1, runCtrMenuLoop): the FlowView GameStack builds (game_stack.h
    // StackConfig::makeView). Its init loads the backend, the 2D layer (the menus need it before
    // any level) and the renderer; its beginLevel replaces the renderer by a fresh one for each
    // level (the last level's textures leave VRAM), then terrain and precache behind the loading
    // screen; its drawFrame (GameFlow::draw, called from prepare()) takes the flow's layers and
    // builds the bottom screen's 2D list: HUD, touch buttons during play, the front end's
    // screens at the 320x240 mapping (scale 0.4). The top screen shows the world (attract level
    // or mission; stereo as in play) with PAUSED under the in-game menu and the game's title
    // where the desktop's main menu has its 3D banner, or the title on black without a world.
    // The returned view must not outlive this object.
    std::unique_ptr<as3d_game::FlowView> makeFlowView(as3d::ctr::CtrContext& ctx);
    void setFlow(as3d_game::GameFlow* flow);
    // GameFlow::loadingHook: a standalone frame of the original's loading screen on both screens
    // (with a visible progress bar), at most every 100 ms unless progress is 1.
    void loadingScreen(float progress, bool intermission);

    bool begin(as3d::ctr::CtrContext& ctx, as3d_game::GameSession& session) override;
    void prepare(const FrameInfo& info) override;
    void drawTop(as3d::ctr::CtrContext& ctx, int eye, const FrameInfo& info) override;
    void drawBottom(as3d::ctr::CtrContext& ctx, const FrameInfo& info) override;
    void stats(FrameStats& out) override;
    void afterFrame(as3d::ctr::CtrContext& ctx, const FrameInfo& info) override;
    void end() override;

    struct Impl; // ctr_frame.cpp (its FlowView adapter reaches it)

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace ctrapp
