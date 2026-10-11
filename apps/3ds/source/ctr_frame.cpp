// The frame composer (ctr_frame.h).
#include "ctr_frame.h"

#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "as3d/console_hud.h"
#include "as3d/hud_layout.h"
#include "as3d/console_menu.h"
#include "as3d/console_pad.h"
#include "as3d/core.h"
#include "as3d/frontend.h"
#include "as3d/load_stats.h"
#include "as3d/precache.h"
#include "as3d/render_state.h"
#include "as3d/scene.h"
#include "as3d/terrain_build.h"
#include "as3d/texconv.h"
#include "as3d/touch_controls.h"
#include "as3d/ui.h"
#include "as3d/world.h"
#include "as3d/world_render.h"
#include "ctr_banner.h"
#include "ctr_context.h"
#include "ctr_input.h"
#include "ctr_mem.h"
#include "ctr_perf.h"
#include "ctr_wash.h"
#include "key_glyphs.h"
#include "eye_replay_pica.h"
#include "flat_fill.h"
#include "game_flow.h"
#include "game_session.h"
#include "gpu_mesh_pica.h"
#include "gpu_passes_pica.h"
#include "mesh_renderer_pica.h"
#include "model_view.h"
#include "pass_profile.h"
#include "shadow_pica.h"
#include "stereo_comfort.h"
#include "tev.h"
#include "pica_frame.h"
#include "quality_pica.h"
#include "state_check_pica.h"
#include "terrain_pica.h"
#include "texture_budget.h"
#include "ui_pica.h"
#include "water_renderer_pica.h"
#include "world_pica.h"

namespace ctrapp {

using namespace as3d;
using as3d::ctr::CtrContext;

StereoParams stereoFor(float slider, float playerDepth, float maxEyeOffset, float minFocal) {
    StereoParams p;
    if (!(slider > 0.02f)) return p; // the context's dead zone (ctr_context.h eyeCount)
    p.eyes = 2;
    p.eyeOffset = std::min(slider, 1.0f) * maxEyeOffset;
    p.focal = std::max(playerDepth, minFocal);
    return p;
}

namespace {

constexpr int kTopW = as3d::ctr::kTopWidth, kTopH = as3d::ctr::kTopHeight;
constexpr int kBottomW = as3d::ctr::kBottomWidth, kBottomH = as3d::ctr::kBottomHeight;
constexpr float kAspect = static_cast<float>(kTopW) / static_cast<float>(kTopH);

// A render target as an upright binary PPM. The colour buffer is 240 x width (the screen is
// rotated); the display transfer gives it as the LCD sees it: memory row x is screen column x,
// from the bottom of the screen up (bytes A B G R).
} // namespace

bool dumpRenderTarget(C3D_RenderTarget* t, int width, const std::string& path) {
    const bool rgb565 = t->frameBuf.colorFmt == GPU_RB_RGB565; // run.ini top_format=rgb565
    const size_t bytes = static_cast<size_t>(240) * static_cast<size_t>(width) * 4;
    auto* lin = static_cast<u8*>(linearAlloc(bytes));
    if (!lin) return false;
    const u32 fmt = rgb565 ? GX_TRANSFER_FMT_RGB565 : GX_TRANSFER_FMT_RGBA8;
    C3D_SyncDisplayTransfer(static_cast<u32*>(t->frameBuf.colorBuf), GX_BUFFER_DIM(240, width),
                            reinterpret_cast<u32*>(lin), GX_BUFFER_DIM(240, width),
                            GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
                                GX_TRANSFER_IN_FORMAT(fmt) | GX_TRANSFER_OUT_FORMAT(fmt) |
                                GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    GSPGPU_InvalidateDataCache(lin, bytes);
    if (rgb565) { // expanded in place to the RGBA8 layout below (A B G R), from the end
        const size_t n = static_cast<size_t>(240) * static_cast<size_t>(width);
        for (size_t i = n; i-- > 0;) {
            const unsigned v = static_cast<unsigned>(lin[i * 2] | lin[i * 2 + 1] << 8);
            const unsigned r = v >> 11, g = (v >> 5) & 63, b = v & 31;
            u8* o = lin + i * 4;
            o[0] = 255;
            o[1] = static_cast<u8>(b << 3 | b >> 2);
            o[2] = static_cast<u8>(g << 2 | g >> 4);
            o[3] = static_cast<u8>(r << 3 | r >> 2);
        }
    }
    bool ok = false;
    if (std::FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fprintf(f, "P6\n%d 240\n255\n", width);
        std::vector<u8> row(static_cast<size_t>(width) * 3);
        for (int y = 0; y < 240; ++y) {
            for (int x = 0; x < width; ++x) {
                const u8* px = lin + (static_cast<size_t>(x) * 240 + static_cast<size_t>(239 - y)) * 4;
                row[static_cast<size_t>(x) * 3 + 0] = px[3];
                row[static_cast<size_t>(x) * 3 + 1] = px[2];
                row[static_cast<size_t>(x) * 3 + 2] = px[1];
            }
            std::fwrite(row.data(), 1, row.size(), f);
        }
        ok = std::fclose(f) == 0;
    }
    linearFree(lin);
    return ok;
}

// The depth buffer of a render target as an upright PPM (run.ini state_check's L mark): D24S8 read
// as 32-bit words by the display transfer, the bytes 2, 1, 0 of each word (the 24-bit depth, high
// byte first: the console's AS3D_WASH_SELFTEST of 2026-10-11 matched these bytes, not 3, 2, 1, whose
// first is the stencil) as R, G, B: the cleared far depth is black, the depth grows towards the
// near plane.
bool dumpRenderTargetDepth(C3D_RenderTarget* t, int width, const std::string& path) {
    if (!t->frameBuf.depthBuf || t->frameBuf.depthFmt != GPU_RB_DEPTH24_STENCIL8) return false;
    const size_t bytes = static_cast<size_t>(240) * static_cast<size_t>(width) * 4;
    auto* lin = static_cast<u8*>(linearAlloc(bytes));
    if (!lin) return false;
    C3D_SyncDisplayTransfer(static_cast<u32*>(t->frameBuf.depthBuf), GX_BUFFER_DIM(240, width),
                            reinterpret_cast<u32*>(lin), GX_BUFFER_DIM(240, width),
                            GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
                                GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                                GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    GSPGPU_InvalidateDataCache(lin, bytes);
    bool ok = false;
    if (std::FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fprintf(f, "P6\n%d 240\n255\n", width);
        std::vector<u8> row(static_cast<size_t>(width) * 3);
        for (int y = 0; y < 240; ++y) {
            for (int x = 0; x < width; ++x) {
                const u8* px = lin + (static_cast<size_t>(x) * 240 + static_cast<size_t>(239 - y)) * 4;
                row[static_cast<size_t>(x) * 3 + 0] = px[2];
                row[static_cast<size_t>(x) * 3 + 1] = px[1];
                row[static_cast<size_t>(x) * 3 + 2] = px[0];
            }
            std::fwrite(row.data(), 1, row.size(), f);
        }
        ok = std::fclose(f) == 0;
    }
    linearFree(lin);
    return ok;
}

namespace {

bool dumpTarget(C3D_RenderTarget* t, int width, const std::string& path) { return dumpRenderTarget(t, width, path); }

double kb(size_t b) { return static_cast<double>(b) / 1024.0; }

// The sequels' model view on the top screen (docs/3ds-port.md M5.2): the desktop draws it on its
// bottom screen inside the view's virtual rectangle, mapped at 0.4 px per virtual pixel; the top
// screen has nothing else to show while the helicopter selection is open, so the rectangle is
// drawn there at twice that, 0.8 (460 x 270 -> 368 x 216 px), centred, over the selection's dot
// grid at the same mapping. The desktop's dual-screen frame at --size 800x960 has the same
// mapping on its bottom screen (the comparison of the report).
constexpr float kModelPxPerVirtual = 0.8f;

// A 2D pass (or the brightness pass) in the pass profile (pass_profile.h): its CPU ticks, the
// 2D runs and vertices it drew.
class UiMeter {
public:
    explicit UiMeter(pica::Pass p)
        : p_(p), t0_(svcGetSystemTick()), d0_(ui::ui2dPicaStats().runs), v0_(ui::ui2dPicaStats().vertices) {}
    ~UiMeter() {
        const ui::Ui2dPicaStats& s = ui::ui2dPicaStats();
        pica::passProfile().add(p_, svcGetSystemTick() - t0_, s.runs - d0_ + extraDraws, s.vertices - v0_ + extraVerts);
        pica::gpuPassMark(p_); // run.ini gpu_passes: the pass's GPU time in a timed frame
    }
    int extraDraws = 0, extraVerts = 0; // draws that are not 2D runs (the model view, the brightness quad)

private:
    pica::Pass p_;
    u64 t0_;
    int d0_, v0_;
};

constexpr unsigned kPassReportFrames = 300;

// A frame whose submit (drawTop + drawBottom) takes longer than this logs AS3D_SLOW_SUBMIT with
// its own per-pass split (the owner's drop 5 play session: 18-24 ms submits every 9-11 frames
// with fire held, no load at those frames). At most kSlowSubmitLogs lines per level.
constexpr double kSlowSubmitMs = 15.0;

// Everything drawTouchControls reads (input/touch_controls.cpp): the layout's circles and alpha,
// the held buttons, the drag target, the HUD fields of the faces, the rules and assets. Equal to
// the last frame's: the list is not built again (round 3, R3.2), its flush replays the last one.
struct TouchKey {
    float circles[kTouchButtonCount * 3];
    float layoutAlpha, alpha, targetX, targetY;
    int32_t held, dragging, targetValid;
    int32_t weapon, missileSelected, powerupSelected;
    int32_t upgrades[ui::kWeaponSlots], missiles[ui::kMissileTypes], powerups[ui::kPowerupSlots];
    const void* rules;
    const void* assets;
};
TouchKey touchKeyOf(const TouchMapper& t, const ui::TouchControlsState& ts) {
    TouchKey k;
    std::memset(&k, 0, sizeof k);
    const TouchLayout& L = t.layout();
    for (int i = 0; i < kTouchButtonCount; ++i) {
        k.circles[i * 3] = L.circles[i].x;
        k.circles[i * 3 + 1] = L.circles[i].y;
        k.circles[i * 3 + 2] = L.circles[i].r;
        if (t.buttonHeld(static_cast<TouchButton>(i))) k.held |= 1 << i;
    }
    k.layoutAlpha = L.alpha;
    k.alpha = ts.alpha;
    k.dragging = t.dragging() ? 1 : 0;
    k.targetValid = t.targetValid() ? 1 : 0;
    k.targetX = t.targetX();
    k.targetY = t.targetY();
    if (const ui::HudPlayer* p = ts.player) {
        k.weapon = p->weapon;
        k.missileSelected = p->missileSelected;
        k.powerupSelected = p->powerupSelected;
        for (int i = 0; i < ui::kWeaponSlots; ++i) k.upgrades[i] = p->upgrades[i];
        for (int i = 0; i < ui::kMissileTypes; ++i) k.missiles[i] = p->missiles[i];
        for (int i = 0; i < ui::kPowerupSlots; ++i) k.powerups[i] = p->powerups[i];
    } else {
        k.weapon = -1;
    }
    k.rules = ts.rules;
    k.assets = ts.assets;
    return k;
}
constexpr int kSlowSubmitLogs = 60;

// The AS3D_GPU_LEAN counters (over the frames since the last line).
struct LeanSums {
    unsigned frames = 0;
    unsigned bottomSkipped = 0, bottomDrawn = 0;
    unsigned eyeSkipped[2] = {0, 0};
    unsigned brightnessSkipped = 0;
    unsigned brightnessFolded = 0; // R4.6: eye draws with the brightness folded (no quad)
};

} // namespace

struct GameFrame::Impl {
    size_t lodCoarseSeen = 0, lodVisibleSeen = 0; // terrain LOD totals at the last pass report
    size_t lodSwitchSeen = 0, lodSwitchFramesSeen = 0;
    RunConfig run;
    CtrContext* ctx = nullptr;
    as3d_game::GameSession* session = nullptr;
    bool backend = false;
    std::unique_ptr<WorldRenderer> renderer;
    std::unique_ptr<ui::Renderer2D> r2d;       // the top screen's 2D list (built once, flushed per eye)
    std::unique_ptr<ui::Renderer2D> r2dTopOver; // the top screen's list over the model view (its static fill)
    std::unique_ptr<ui::Renderer2D> r2dBottom; // the bottom screen's HUD (the front end: built in prepare)
    // The bottom screen's touch band and the front end's menus, each a list of its own: the band
    // rarely changes, so its converted vertices stay cached (renderer2d_pica.cpp) while the HUD's
    // digits and bars change under it.
    std::unique_ptr<ui::Renderer2D> r2dTouch;
    std::unique_ptr<ui::Renderer2D> r2dMenu;
    bool touchQuads = false, menuQuads = false; // the front end's frame filled them
    bool touchPrebuild = false; // r2dTouch holds the band's first list, for the next forced loading frame
    TouchKey touchKey{};        // what r2dTouch's list was built from in play (touchKeyValid)
    bool touchKeyValid = false;
    // R4.1 (docs/3ds-port.md "Stereo GPU budget (round 4, R4.1)"): bottom_hz=30: the bottom screen was drawn last frame; right_eye_rate: the eyes this frame skips
    // (their targets keep the last picture), the stereo frames since both eyes were drawn.
    bool bottomDrawnLast = false;
    bool eyeSkip[2] = {false, false};
    unsigned stereoFrames = 0;
    LeanSums lean;
    unsigned kicksSeen = 0; // pica::gpuKicks() at the last AS3D_GPU_LEAN line
    uint64_t kickTick = 0; // the first gpu_kick of the frame (the pacer's GPU time)
    // The slow-submit log: the pass totals and the late-texture count when the frame began.
    uint64_t submitTick = 0;
    uint64_t passTicks0[pica::kPassCount] = {};
    int lateQueued0 = 0;
    int slowLogged = 0;
    // The front end's three bottom lists this frame (HUD, touch band, menus): flush ms, and
    // whether the flush reused its cached vertices (1) or rebuilt them (0); -1 = not flushed.
    double listMs[3] = {0, 0, 0};
    pica::MeshDrawStats meshSum; // the mesh passes since the last AS3D_MESH line
    double culledSum = 0.0;      // model records culled in prepare over the same frames
    int listHit[3] = {-1, -1, -1};
    int listVerts[3] = {0, 0, 0};
    struct ListSums { // AS3D_UI_LISTS: the three lists over the frames since the last line
        unsigned frames = 0;
        unsigned flushes[3] = {0, 0, 0};
        double ms[3] = {0, 0, 0};
        long units[3] = {0, 0, 0}, built[3] = {0, 0, 0}, draws[3] = {0, 0, 0};
        long verts[3] = {0, 0, 0}; // R4.7: the AS3D_HUD line's vertices per list
    } listSum;
    std::unique_ptr<ui::UiAssets> assets;
    bool hudReady = false;
    // R4.5: the first game's menus in the console layouts (run.ini ui_menus, docs/3ds-ui-design.md
    // "AirStrike 3D menus on the console"), with the owner's key pictures in their footers.
    ui::ConsoleMenuView menuView;
    Texture2D keysTexture;
    ui::KeyGlyphAtlas keyAtlas;
    // R4.7 (docs/3ds-ui-design.md "In play on the console (round 4, R4.7)"): the first game's HUD
    // tiles are the touch controls (TouchLayoutOptions::hudTiles): no circle band then.
    static bool tilesOn(const TouchMapper* t) { return t && t->layout().tileCount > 0; }
    const ui::KeyGlyphAtlas* keyGlyphs() const { return keysTexture.valid() ? &keyAtlas : nullptr; }
    // The pad reminder and the pressed tile over the HUD list (bottom-screen pixels).
    void hudExtras(ui::Renderer2D& r, const TouchMapper* t) {
        ui::RectF pressed;
        const int k = t ? t->pressedTile() : -1;
        if (k >= 0) {
            const TouchTile& tile = t->layout().tiles[k];
            pressed = {tile.x, tile.y, tile.w, tile.h};
        }
        ui::drawConsoleHudExtras(r, *assets, keyGlyphs(), k >= 0 ? &pressed : nullptr);
    }
    bool precached = false;
    int mission = 0;
    // The front end (M2.1).
    as3d_game::GameFlow* flow = nullptr;
    const std::function<int(const PrecacheSet&)>* precacheSounds = nullptr; // the FlowView's
    bool levelBegun = false;  // the renderer holds a level
    bool haveLayers = false;  // drawFrame ran in this prepare
    as3d_game::FrameLayers layers;
    const FrameInfo* info = nullptr; // during prepare
    bool worldDrawn = true;
    // The wash detector (ctr_wash.h, run.ini wash_check): the frame's central view, a mark due.
    ctrapp::WashDetector wash;
    pica::EyeReplayStats replaySeen; // the eye replay's totals at the last AS3D_EYE_REPLAY line
    // R3.3: the world's draws/vertices after the left eye (the right eye's share is the rest), and
    // the terrain's per-eye chunk sums at the last AS3D_STEREO60 line.
    int worldDrawsLeft = 0, worldVertsLeft = 0, worldDrawsRight = 0, worldVertsRight = 0;
    pica::TerrainEyeChunks eyeChunksSeen;
    bool washMarkPending = false;
    const Terrain* washTopOf = nullptr; // the terrain whose highest point washTop holds
    float washTop = 0.0f;
    WorldView washView;
    // The sequels' model view (FlowView::drawModel, from GameFlow::draw in prepare): the object's
    // parts (meshes and skins in a cache of their own, released with the renderer at every level
    // start), the pass prepared in prepare and drawn on each eye of the top screen.
    struct ModelPreview {
        std::unique_ptr<ResourceCache> cache;
        std::string object;
        std::vector<as3d_game::ModelViewPart> parts;
    };
    std::unique_ptr<ModelPreview> preview;
    bool modelActive = false;
    ui::ModelView modelView;
    pica::MeshPass modelPass;
    // The first game's main-menu banner on the top screen (ctr_banner.h): its mesh and material
    // from a cache of its own, loaded at the attract level's start (with the precache) and
    // released with the renderer at every level start, as the model view's.
    std::unique_ptr<ConsoleBanner> banner;
    bool bannerActive = false; // this frame
    bool bannerTried = false; // loadBanner ran since the last level start
    int modelX = 0, modelY = 0, modelW = 0, modelH = 0; // the viewport, top screen pixels from the top left
    float modelFocal = 0.0f; // the model view's screen plane: its nearest vertex (0: unknown)
    float modelFar = 0.0f;   // and its farthest vertex
    float topBrightness = 0.6f;
    float cmdUsageMax = 0.0f; // the largest C3D_GetCmdBufUsage seen after a top eye's last draw (256 KB list; full drops draws)
    // The frame.
    StereoParams stereo;
    float playerDepth = 0.0f;
    // Stereo comfort (docs/3ds-port.md "Stereo comfort and frame polish (round 2, P5)"): the slider's
    // eye offset, the comfort clamp's target and smoothed ratio, the depths it read this frame.
    pica::ComfortFilter comfort;
    pica::FocalFilter focalFilter;
    unsigned lastStep = 0;
    bool haveLastStep = false;
    float fullEyeOffset = 0.0f, comfortTarget = 1.0f;
    float nearDepth = 0.0f, farDepth = 0.0f, nearPx = 0.0f, farPx = 0.0f;
    struct StereoSums { // the AS3D_STEREO line, over the frames since the last one
        unsigned frames = 0, clamped = 0, nearLimited = 0, farLimited = 0;
        double offsetSum = 0.0, nearPxSum = 0.0, farPxSum = 0.0, ratioSum = 0.0;
        float offsetMin = 0.0f, offsetMax = 0.0f, nearPxMax = 0.0f, farPxMax = 0.0f, nearDepthMin = 0.0f;
        unsigned probes0 = 0, scans0 = 0;
        long long steps0 = 0;
        unsigned long long probeTicks0 = 0, modelTicks0 = 0;
    } stereoSums;
    float prepareMs = 0.0f;
    ui::HudState hud;
    bool showHud = false;
    bool topQuads = false;
    size_t arenaUsed = 0;
    bool dumped = false;
    uint64_t lastLoadingFrame = 0;
    // R4.6: AS3D_TERRAIN_LIGHTS counts from these (terrainPicaStats at the last line).
    struct LightSeen {
        size_t chunks = 0, reuses = 0, evals = 0;
        uint64_t ticks = 0;
    } lightSeen;
    // R4.6: the last precacheLevel's parts (ms), for AS3D_LEVEL_RENDERER.
    struct PrecacheParts {
        double scripts = 0, world = 0, tex = 0, mip = 0, swizzle = 0, upload = 0, mesh = 0, shadows = 0, frames = 0,
               read = 0, decode = 0, ui = 0, sounds = 0, total = 0;
        int textures = 0;
    } lastPrecache;
    uint64_t loadingTicks = 0; // the load profile (AS3D_LOAD): loading screen frames
    int loadingFrames = 0;
    // dump_screens: the screen of the last frame, the pending dump.
    std::string screen;
    int screenSeq = 0;
    unsigned screenDumpFrame = 0;
    unsigned nextDumpStep = 0; // dump_from: the next simulation step a dump_every dump is due
    std::string screenDumpName;

    float maxEyeOffset() const { return run.eyeOffset > 0.0f ? run.eyeOffset : kMaxEyeOffset; }
    float minFocal() const { return run.minFocal > 0.0f ? run.minFocal : kMinFocalDistance; }
    pica::ComfortLimits comfortLimits() const {
        pica::ComfortLimits l;
        l.maxNearPx = run.maxParallaxPx;
        l.maxFarPx = run.maxFarParallaxPx;
        return l;
    }
    void logStereoConfig() const {
        AS3D_INFO("AS3D_STEREO_CONFIG eye_offset=%.2f (at slider 1) min_focal=%.1f max_parallax_px=%.1f "
                  "max_far_parallax_px=%.1f stereo_comfort=%d (the near / far parallax limits at slider 1)",
                  static_cast<double>(maxEyeOffset()), static_cast<double>(minFocal()), static_cast<double>(run.maxParallaxPx),
                  static_cast<double>(run.maxFarParallaxPx), run.stereoComfort ? 1 : 0);
    }

    // The comfort clamp (stereo_comfort.h), after the world's prepare measured the scene's depths:
    // the eye offset becomes the largest fraction of the slider's that keeps the nearest content
    // within max_parallax_px in front of the screen and the farthest terrain within
    // max_far_parallax_px behind it (both x slider), approached a little per frame.
    void clampStereo(const WorldView& wv, float dt) {
        fullEyeOffset = stereo.eyeOffset;
        const pica::SceneDepth& sd = pica::sceneDepth();
        nearDepth = farDepth = 0.0f;
        if (sd.valid) {
            if (sd.terrainHit) nearDepth = sd.terrainNear;
            if (sd.modelNear > 0.0f && (nearDepth == 0.0f || sd.modelNear < nearDepth)) nearDepth = sd.modelNear;
            if (sd.terrainHit) farDepth = sd.terrainFar;
        }
        const float a = pica::stereoA(wv.fovY, kAspect), w = static_cast<float>(kTopW);
        comfortTarget = run.stereoComfort
                            ? pica::comfortRatio(comfortLimits(), nearDepth, farDepth, stereo.focal, a, w, fullEyeOffset)
                            : 1.0f;
        const float ratio = comfort.update(comfortTarget, dt);
        stereo.eyeOffset = fullEyeOffset * ratio;
        nearPx = std::max(0.0f, -pica::parallaxPxPerUnit(nearDepth, stereo.focal, a, w) * stereo.eyeOffset);
        farPx = std::max(0.0f, pica::parallaxPxPerUnit(farDepth, stereo.focal, a, w) * stereo.eyeOffset);
        // The AS3D_STEREO sums.
        StereoSums& s = stereoSums;
        const float e = stereo.eyeOffset;
        if (s.frames == 0) {
            s.offsetMin = s.offsetMax = e;
            s.nearDepthMin = nearDepth;
        }
        ++s.frames;
        s.offsetSum += e;
        s.ratioSum += ratio;
        s.offsetMin = std::min(s.offsetMin, e);
        s.offsetMax = std::max(s.offsetMax, e);
        if (ratio < 0.999f) ++s.clamped;
        const float kn = -pica::parallaxPxPerUnit(nearDepth, stereo.focal, a, w) * fullEyeOffset;
        const float kf = pica::parallaxPxPerUnit(farDepth, stereo.focal, a, w) * fullEyeOffset;
        const float slider = fullEyeOffset / maxEyeOffset();
        if (kn > run.maxParallaxPx * slider) ++s.nearLimited;
        if (kf > run.maxFarParallaxPx * slider) ++s.farLimited;
        s.nearPxSum += nearPx;
        s.farPxSum += farPx;
        s.nearPxMax = std::max(s.nearPxMax, nearPx);
        s.farPxMax = std::max(s.farPxMax, farPx);
        if (nearDepth > 0.0f && (s.nearDepthMin == 0.0f || nearDepth < s.nearDepthMin)) s.nearDepthMin = nearDepth;
    }

    // The touch band of the level's first frame, built at the end of the level start (the last
    // loading frame converts it into the list's cache units): a mapper set up as the input's
    // (nothing held yet) and the HUD state of the loaded session. A unit that differs on the first
    // frame (a count) is rebuilt alone; the tessellated discs are the cost and they match.
    void prebuildTouch(const as3d_game::GameSession& ses) {
        touchPrebuild = false;
        if (!hudReady || !run.touch || !ses.hasLevel() || ses.world().intermission() ||
            !pica::passEnabled(pica::Pass::Touch))
            return;
        as3d::TouchMapper t;
        CtrInput::setUpTouch(t, assets->game);
        if (tilesOn(&t)) return; // R4.7: the HUD's tiles, no band to build
        const ui::HudState h = as3d_game::hudStateOf(ses);
        ui::TouchControlsState ts;
        ts.assets = assets.get();
        ts.player = &h.players[0];
        ts.rules = &ses.rules();
        r2dTouch->begin(kBottomW, kBottomH);
        ui::drawTouchControls(*r2dTouch, t, ts);
        touchPrebuild = true;
        touchKeyValid = false;
    }

    // A level start: the filters take the first frame's values at once, the sums start again.
    void resetStereo() {
        comfort.reset();
        focalFilter.reset();
        haveLastStep = false;
        stereoSums = StereoSums();
        nearDepth = farDepth = 0.0f;
    }

    void logStereoSums() {
        StereoSums& s = stereoSums;
        const pica::SceneDepth& sd = pica::sceneDepth();
        if (sd.probes < s.probes0 || sd.modelScans < s.scans0) s = StereoSums(); // a level start reset the counters
        if (s.frames > 0) {
            const double n = static_cast<double>(s.frames);
            const double k = static_cast<double>(SYSCLOCK_ARM11) / 1000.0;
            const unsigned probes = sd.probes - s.probes0, scans = sd.modelScans - s.scans0;
            AS3D_INFO("AS3D_STEREO frames=%u eye_offset min=%.2f avg=%.2f max=%.2f (slider x eye_offset=%.2f) comfort_avg=%.3f "
                      "clamped=%.1f%% near_limited=%.1f%% far_limited=%.1f%% near_px avg=%.1f max=%.1f far_px avg=%.1f "
                      "max=%.1f near_depth_min=%.1f focal=%.1f probe_ms=%.3f (%u probes, %.0f steps each) model_scan_ms=%.3f",
                      s.frames, static_cast<double>(s.offsetMin), s.offsetSum / n, static_cast<double>(s.offsetMax),
                      static_cast<double>(fullEyeOffset), s.ratioSum / n, 100.0 * s.clamped / n, 100.0 * s.nearLimited / n,
                      100.0 * s.farLimited / n, s.nearPxSum / n, static_cast<double>(s.nearPxMax), s.farPxSum / n,
                      static_cast<double>(s.farPxMax), static_cast<double>(s.nearDepthMin), static_cast<double>(stereo.focal),
                      probes ? static_cast<double>(sd.probeTicks - s.probeTicks0) / k / probes : 0.0, probes,
                      probes ? static_cast<double>(sd.probeSteps - s.steps0) / probes : 0.0,
                      scans ? static_cast<double>(sd.modelTicks - s.modelTicks0) / k / scans : 0.0);
        }
        s = StereoSums();
        s.probes0 = sd.probes;
        s.scans0 = sd.modelScans;
        s.steps0 = sd.probeSteps;
        s.probeTicks0 = sd.probeTicks;
        s.modelTicks0 = sd.modelTicks;
    }

    // A standalone frame of the loading screen on both screens (and both eyes). The direct
    // modes: a progress bar, "LOADING" once the font is there. The front end: the original's
    // loading screen (ui::drawLoadingScreen, built before the frame opens: its picture may load
    // on first use) with the bar along the bottom edge. At most every 100 ms (each frame waits
    // for a vblank, which also means the GPU is done with every earlier frame).
    void loadingFrame(float progress, bool force = false, bool intermission = false) {
        const uint64_t now = svcGetSystemTick();
        if (!force && lastLoadingFrame && now - lastLoadingFrame < SYSCLOCK_ARM11 / 10) return;
        lastLoadingFrame = now;
        struct Count { // the load profile: the time the loading screen costs
            Impl& im;
            u64 t0;
            ~Count() {
                im.loadingTicks += svcGetSystemTick() - t0;
                ++im.loadingFrames;
            }
        } count{*this, now};
        const float p = std::clamp(progress, 0.0f, 1.0f);
        const bool original = flow && hudReady;
        // R4.8: the console's loading screen (the picture 1:1 or 1/2 on top with the name and the
        // bar, the controls card on the bottom); ui_menus=0: the desktop's on both screens at 0.4.
        const bool consoleLoading = original && run.uiMenus;
        if (consoleLoading) {
            r2d->begin(kTopW, kTopH);
            r2dBottom->begin(kBottomW, kBottomH);
            ui::drawConsoleLoading(*r2d, *r2dBottom, *assets, p, intermission, keyGlyphs());
        } else if (original) {
            r2d->begin(kTopW, kTopH);
            ui::drawLoadingScreen(*r2d, *assets, p, intermission);
            r2dBottom->begin(kBottomW, kBottomH);
            ui::drawLoadingScreen(*r2dBottom, *assets, p, intermission);
        }
        if (!ctx->beginFrame()) return;
        pica::frameBegin();
        auto screen = [&](C3D_RenderTarget* t, int w, int h, bool depth, ui::Renderer2D* list) {
            pica::drawOn(pica::FrameTarget{t, w, h, depth});
            clear(Vec4{original ? 0.0f : 0.02f, original ? 0.0f : 0.03f, original ? 0.0f : 0.06f, 1.0f}, depth, 1.0f);
            if (original) {
                list->flush();
                if (intermission || consoleLoading) return; // the attract level: black, as the desktop; the console's bar is in the list
                const float x0 = 8.0f, x1 = static_cast<float>(w) - 8.0f, y0 = static_cast<float>(h) - 8.0f, y1 = y0 + 3.0f;
                pica::fillRect(x0, y0, x1, y1, 48, 0, 0);
                pica::fillRect(x0, y0, x0 + (x1 - x0) * p, y1, 230, 120, 20);
                return;
            }
            const float x0 = static_cast<float>(w) * 0.15f, x1 = static_cast<float>(w) * 0.85f;
            const float y0 = static_cast<float>(h) * 0.6f, y1 = y0 + 8.0f;
            pica::fillRect(x0 - 2, y0 - 2, x1 + 2, y1 + 2, 90, 90, 100);
            pica::fillRect(x0, y0, x1, y1, 20, 20, 30);
            // A ramp from dark red to the HUD orange as the bar fills.
            const uint8_t g = static_cast<uint8_t>(60 + 100 * p);
            pica::fillRect(x0, y0, x0 + (x1 - x0) * p, y1, 230, g, 20);
            if (hudReady) {
                r2d->begin(w, h);
                ui::TextStyle st;
                st.scale = 2.0f;
                st.color = ui::orange();
                ui::drawText(*r2d, assets->uiFont(), 300, 220, "LOADING", st);
                r2d->flush();
            }
        };
        for (int eye = 0; eye < 2; ++eye) screen(ctx->topTarget(eye), kTopW, kTopH, true, r2d.get());
        if (touchPrebuild && force) {
            // The touch band's first list (prebuildTouch): converted into its cache units here,
            // drawn on the bottom target and cleared over by the loading screen below, so the
            // mission's first frame finds it cached (P1/P4: 15-26 ms inside that frame before).
            touchPrebuild = false;
            pica::drawOn(pica::FrameTarget{ctx->bottomTarget(), kBottomW, kBottomH, false});
            const u64 t0 = svcGetSystemTick();
            const int v0 = ui::ui2dPicaStats().vertices;
            r2dTouch->flush();
            AS3D_INFO("AS3D_TOUCH_PREBUILD ms=%.1f vertices=%d (in the loading screen)",
                      static_cast<double>(svcGetSystemTick() - t0) * 1000.0 / SYSCLOCK_ARM11,
                      ui::ui2dPicaStats().vertices - v0);
        }
        screen(ctx->bottomTarget(), kBottomW, kBottomH, false, r2dBottom.get());
        pica::frameEnd();
        ctx->endFrame();
        if (run.dumpLoading && original && force && !intermission) { // run.ini dump_loading (R4.4)
            static int loadingDumps = 0;
            char b[64];
            std::snprintf(b, sizeof b, "%sload%02d", run.dumpPrefix.c_str(), loadingDumps++);
            const std::string d = std::string(kDataDir) + "/" + b;
            const bool t = dumpTarget(ctx->topTarget(0), kTopW, d + "_top.ppm"), bt = dumpTarget(ctx->bottomTarget(), kBottomW, d + "_bottom.ppm");
            AS3D_INFO("AS3D_DUMP %s loading=%.2f top=%d bottom=%d", b, static_cast<double>(p), t ? 1 : 0, bt ? 1 : 0);
        }
    }

    // Two empty frames: C3D_FrameBegin waits until the GPU is done with every texture and buffer
    // the last frames read.
    void emptyFrames() {
        for (int k = 0; k < 2; ++k) {
            if (!ctx->beginFrame()) break;
            pica::frameBegin();
            pica::frameEnd();
            ctx->endFrame();
        }
    }

    // The eye's view and the water plane for the wash detector (ctr_wash.h): only the first game's
    // plane is judged (the sequels' wave grid has no single height).
    bool washInputs(int eye, const FrameInfo& info, ctrapp::WashView* v, ctrapp::WashScene* s, const Terrain** terrainOut) {
        if (!info.session || !info.session->hasLevel()) return false;
        const pica::WaterPicaStats& w = pica::waterPicaStats();
        if (!w.active || w.grid || !pica::passEnabled(pica::Pass::Water) || !pica::passEnabled(pica::Pass::Terrain)) return false;
        const World& world = info.session->world();
        const Terrain* terrain = world.terrain();
        if (!terrain) return false;
        if (eye == 0 || eyeSkip[0]) washView = worldViewOf(world, kAspect); // (right_eye_rate: the left eye skipped)
        EyeView ev;
        if (stereo.eyes == 2) {
            ev.eyeOffset = eye == 0 ? -stereo.eyeOffset : stereo.eyeOffset;
            ev.focalDistance = stereo.focal;
        }
        v->view = eyeViewMatrix(washView, ev);
        v->clip = eyeProjection(washView, ev, kAspect) * v->view;
        v->nearPlane = washView.nearPlane;
        v->farPlane = washView.farPlane;
        if (washTopOf != terrain) {
            float top = -1e30f;
            for (const Vec3& p : terrain->positions()) top = std::max(top, p.z);
            washTop = top;
            washTopOf = terrain;
        }
        s->planeZ = w.planeZ;
        s->mapW = static_cast<float>(terrain->width()) * kHmapCellSize;
        s->mapH = static_cast<float>(terrain->height()) * kHmapCellSize;
        s->topZ = washTop;
        s->height = [terrain](float x, float y) { return terrain->heightAt(x, y); };
        *terrainOut = terrain;
        return true;
    }
    void queueWashCopy(CtrContext& c, int eye, const FrameInfo& info) {
        ctrapp::WashView v;
        ctrapp::WashScene s;
        const Terrain* terrain = nullptr;
        if (!washInputs(eye, info, &v, &s, &terrain)) return;
        wash.queue(c.topTarget(eye), eye, v, s, terrain, info.frame);
    }

    // R4.1 (docs/3ds-port.md "Stereo GPU budget (round 4, R4.1)"): the effective settings.
    int bottomHz() const { return run.bottomHz > 0 ? run.bottomHz : (run.gpuLean ? 30 : 60); }
    bool brightnessSkip() const { return run.brightnessSkip >= 0 ? run.brightnessSkip != 0 : true; }

    // run.ini top_format (R4.6, docs/3ds-port.md "GPU cuts and the level start (round 4, R4.6)"): the
    // top eyes' colour buffers swapped between RGBA8 and RGB565 at a stage start, as depth16 swaps
    // the depth buffers, so a soak stage can A/B it (the context makes them once, at launch, from
    // the run.ini's top_format). Outside the frames, the GPU idle: each eye's colour buffer freed and
    // made again in the format wanted, its display transfer told the formats, the top screen's
    // framebuffers set to RGB565 when both eyes are 565 (the transfer converts otherwise).
    void applyTopFormat(bool want565) {
        if (!ctx || pica::inFrame()) return; // the GPU must be idle (waitGpuIdle does not wait inside a frame)
        const GPU_COLORBUF want = want565 ? GPU_RB_RGB565 : GPU_RB_RGBA8;
        bool waited = false;
        int changed = 0, failed = 0;
        for (int eye = 0; eye < 2; ++eye) {
            C3D_RenderTarget* t = ctx->topTarget(eye);
            if (!t || !t->frameBuf.colorBuf || t->frameBuf.colorFmt == want) continue;
            if (!waited) {
                pica::waitGpuIdle();
                waited = true;
            }
            const GPU_COLORBUF had = static_cast<GPU_COLORBUF>(t->frameBuf.colorFmt);
            vramFree(t->frameBuf.colorBuf);
            GPU_COLORBUF fmt = want;
            void* buf = vramAlloc(C3D_CalcColorBufSize(t->frameBuf.width, t->frameBuf.height, want));
            if (!buf) { // the old format back (its block was just freed)
                ++failed;
                fmt = had;
                buf = vramAlloc(C3D_CalcColorBufSize(t->frameBuf.width, t->frameBuf.height, had));
            }
            if (!buf) { // never expected: no colour buffer at all would draw nowhere
                AS3D_ERROR("AS3D_TOP_FORMAT eye %d: no VRAM for a colour buffer", eye);
                continue;
            }
            C3D_FrameBufColor(&t->frameBuf, buf, fmt); // citro3d frees it with the target (ownsColor)
            if (fmt == want) ++changed;
        }
        if (!waited) return;
        bool all565 = true;
        for (int eye = 0; eye < 2; ++eye) {
            C3D_RenderTarget* t = ctx->topTarget(eye);
            if (t && t->frameBuf.colorFmt != GPU_RB_RGB565) all565 = false;
        }
        gfxSetScreenFormat(GFX_TOP, all565 ? GSP_RGB565_OES : GSP_BGR8_OES);
        for (int eye = 0; eye < 2; ++eye) {
            C3D_RenderTarget* t = ctx->topTarget(eye);
            if (!t) continue;
            const bool in565 = t->frameBuf.colorFmt == GPU_RB_RGB565;
            const u32 flags = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
                              GX_TRANSFER_IN_FORMAT(in565 ? GX_TRANSFER_FMT_RGB565 : GX_TRANSFER_FMT_RGBA8) |
                              GX_TRANSFER_OUT_FORMAT(all565 ? GX_TRANSFER_FMT_RGB565 : GX_TRANSFER_FMT_RGB8) |
                              GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
            C3D_RenderTargetSetOutput(t, GFX_TOP, eye == 0 ? GFX_LEFT : GFX_RIGHT, flags);
        }
        as3d::ctr::setTopColor565(all565);
        AS3D_INFO("AS3D_TOP_FORMAT rgb565=%d changed=%d failed=%d", all565 ? 1 : 0, changed, failed);
    }

    // run.ini depth16=1 (docs/3ds-port.md "Stereo GPU budget (round 4, R4.1)"): the top eyes' depth
    // buffers as D16 instead of D24S8 (the stencil is never used): half the depth bytes the clears
    // fill and every depth test reads and writes, and 2 x 188 KB of VRAM more for the textures
    // (the texture budget grows by as much). The depth precision of the W-buffer mapping drops from
    // 2^-24 to 2^-16 of the depth range. The wash detector and the depth dumps read D24S8 only: off.
    // Swapped outside the frames, both ways (a soak stage without the key gets D24S8 back).
    void applyDepthFormat(bool d16) {
        if (!ctx) return;
        const GPU_DEPTHBUF want = d16 ? GPU_RB_DEPTH16 : GPU_RB_DEPTH24_STENCIL8;
        bool waited = false;
        int changed = 0, failed = 0;
        for (int eye = 0; eye < 2; ++eye) {
            C3D_RenderTarget* t = ctx->topTarget(eye);
            if (!t || !t->frameBuf.depthBuf || t->frameBuf.depthFmt == want) continue;
            if (!waited) {
                pica::waitGpuIdle();
                waited = true;
            }
            void* old = t->frameBuf.depthBuf;
            const GPU_DEPTHBUF had = static_cast<GPU_DEPTHBUF>(t->frameBuf.depthFmt);
            vramFree(old);
            const size_t bytes = C3D_CalcDepthBufSize(t->frameBuf.width, t->frameBuf.height, want);
            void* buf = vramAlloc(bytes);
            GPU_DEPTHBUF fmt = want;
            if (!buf) { // the old format back (its block was just freed)
                ++failed;
                fmt = had;
                buf = vramAlloc(C3D_CalcDepthBufSize(t->frameBuf.width, t->frameBuf.height, had));
            }
            C3D_FrameBufDepth(&t->frameBuf, buf, fmt); // citro3d frees it with the target (ownsDepth)
            if (fmt == want) ++changed;
        }
        bool allD16 = d16;
        for (int eye = 0; eye < 2; ++eye) {
            C3D_RenderTarget* t = ctx->topTarget(eye);
            if (t && t->frameBuf.depthFmt != GPU_RB_DEPTH16) allD16 = false;
        }
        const size_t saved = allD16 ? 2u * (240u * 400u * 4u - 240u * 400u * 2u) : 0u;
        pica::textureBudget().setVramLimit(pica::TextureBudget::kDefaultVramLimit + saved);
        if (d16 || changed)
            AS3D_INFO("AS3D_DEPTH16 on=%d changed=%d failed=%d texture_vram_limit_kb=%u", allD16 ? 1 : 0, changed, failed,
                      static_cast<unsigned>(pica::textureBudget().vramLimit() / 1024));
    }

    bool initBackend() {
        if (!pica::initBackend()) {
            AS3D_ERROR("pica::initBackend failed");
            return false;
        }
        backend = true;
        if (void* warm = vramAlloc(0x100)) vramFree(warm);
        // The hardware tuning switches (run.ini passes=, hud=, touch=, depthmap=).
        std::string unknown;
        uint32_t mask = run.passes.empty() ? pica::kAllPasses : pica::parsePassList(run.passes, &unknown);
        if (!run.hud) mask &= ~(1u << static_cast<int>(pica::Pass::Hud));
        if (!run.touch) mask &= ~(1u << static_cast<int>(pica::Pass::Touch));
        pica::setPassMask(mask);
        pica::setGpuKick(run.gpuKick);
        pica::setClearMode(static_cast<pica::ClearMode>(run.clearMode));
        pica::setArenaPoison(run.arenaPoison); // docs/3ds-port.md "Frame synchronisation"
        pica::setStateCheck(run.stateCheck);
        pica::setProbeEvery(run.effectiveProbeEvery()); // docs/3ds-port.md "Soak mode and the log push"
        pica::setProbeInject(run.probeInject);
        // The wash detector and the terrain's priming draw (docs/3ds-port.md "Wash detector").
        applyDepthFormat(run.depth16); // R4.1 depth16=
        applyTopFormat(run.top565);    // R4.6 top_format= per stage
        wash.setEnabled(run.effectiveWashCheck() && !run.depth16);
        wash.logEvals = run.washLog;
        pica::setTerrainPrime(run.terrainPrime);
        pica::setTerrainForceState(run.terrainForceState);
        pica::setEyeReplayEnabled(run.eyeReplay);
        replaySeen = pica::eyeReplayStats(); // this composer's AS3D_EYE_REPLAY lines count from here (a soak stage)
        eyeChunksSeen = pica::terrainEyeChunks();
        pica::setTerrainWashInject(run.washInject);
        // R4.1: the per-pass GPU timing and the mid-frame kick after the terrain.
        pica::setGpuPassEvery(run.gpuPasses == 1 ? 10 : run.gpuPasses);
        pica::setGpuKickAfterTerrain(run.gpuKickAt == 1);
        AS3D_INFO("AS3D_GPU_LEAN gpu_lean=%d gpu_kick_at=%s bottom_hz=%d terrain_far_tev=%d brightness_skip=%d "
                  "right_eye_rate=%s gpu_passes=%d depth16=%d",
                  run.gpuLean ? 1 : 0, pica::gpuKickAfterTerrain() ? "terrain" : "eye", bottomHz(),
                  pica::renderQuality().terrainFarTev ? 1 : 0, brightnessSkip() ? 1 : 0,
                  run.rightEyeRate == 1 ? "half" : run.rightEyeRate == 2 ? "alternate" : "full", pica::gpuPassEvery(),
                  run.depth16 ? 1 : 0);
        // R4.6: the GPU cuts (docs/3ds-port.md "GPU cuts and the level start (round 4, R4.6)").
        pica::setWaterCover(run.waterCover);
        pica::setShadowFastHit(run.shadowFastHit);
        setTexconvReference(!run.texFast);
        setBaseTextureReference(!run.baseInt);
        pica::setTerrainLightCache(run.terrainLightCache);
        AS3D_INFO("AS3D_R46 brightness_mode=%s water_cover=%d shadow_fast_hit=%d tex_fast=%d base_int=%d "
                  "terrain_light_cache=%d terrain_base_cache=%d",
                  run.brightnessMode == 1 ? "quad" : "fold", run.waterCover ? 1 : 0, run.shadowFastHit ? 1 : 0,
                  run.texFast ? 1 : 0, run.baseInt ? 1 : 0, run.terrainLightCache ? 1 : 0, run.terrainBaseCache ? 1 : 0);
        AS3D_INFO("AS3D_WASH_CHECK on=%d inject=%d selftest=%d terrain_prime=%d terrain_force_state=%d eye_replay=%d",
                  wash.enabled() ? 1 : 0, run.washInject, run.washSelftest ? 1 : 0, run.terrainPrime ? 1 : 0,
                  run.terrainForceState ? 1 : 0, run.eyeReplay ? 1 : 0);
        std::string off;
        for (int i = 0; i < pica::kPassCount; ++i)
            if (!pica::passBit(mask, static_cast<pica::Pass>(i))) off += std::string(off.empty() ? "" : ",") + pica::passName(static_cast<pica::Pass>(i));
        AS3D_INFO("AS3D_TUNING passes_off=%s depthmap=%c mesh_sort=%d mesh_merge=%d%s%s", off.empty() ? "none" : off.c_str(),
                  run.depthMapZ ? 'z' : 'w', pica::meshStateSort() ? 1 : 0, pica::meshMerge() ? 1 : 0, unknown.empty() ? "" : " unknown_passes=", unknown.c_str());
        return true;
    }

    bool makeRenderer(as3d_game::GameSession& ses) {
        renderer.reset();
        renderer.reset(new WorldRenderer());
        std::string err;
        if (!renderer->init(ses.vfs(), ses.db(), &err)) {
            AS3D_ERROR("WorldRenderer::init: %s", err.c_str());
            return false;
        }
        return true;
    }

    // The 2D layer: the font and the HUD atlases (optional, as on the desktop).
    void loadUi(as3d_game::GameSession& ses) {
        r2d.reset(new ui::Renderer2D());
        r2dBottom.reset(new ui::Renderer2D());
        r2dTopOver.reset(new ui::Renderer2D());
        r2dTouch.reset(new ui::Renderer2D());
        touchKeyValid = false;
        // The touch band's circles (docs/3ds-port.md "Stereo at 60"): 48 segments made it 16k
        // vertices a frame, more than the terrain; at the band's radii 24 keep the edge within
        // a quarter pixel of the circle (run.ini touch_segments=).
        r2dTouch->setTessellateShapes(true, run.touchSegments);
        r2dMenu.reset(new ui::Renderer2D());
        assets.reset(new ui::UiAssets());
        std::string hudError;
        const GameId game = ses.game() ? ses.game()->id : GameId::AirStrike3D;
        assets->hudTarget = ui::HudTarget::Bottom320x240;
        hudReady = r2d->init(&hudError) && r2dBottom->init(&hudError) && r2dTopOver->init(&hudError) &&
                   r2dTouch->init(&hudError) && r2dMenu->init(&hudError) && assets->load(ses.vfs(), &hudError, game);
        if (!hudReady) AS3D_WARN("HUD unavailable: %s", hudError.c_str());
        // The key pictures: the console menus' footers and (R4.7) the in-play pad reminder.
        if (!keysTexture.valid()) loadKeyGlyphs(keysTexture, keyAtlas);
        menuView.setKeyGlyphs(keysTexture.valid() ? &keyAtlas : nullptr);
    }

    // The level-start precache (run.ini precache=1), then every later load counts as lazy.
    // R4.6: the terrain's base texture cache (terrain_pica.h) of this level start, next to the
    // silhouettes' file; set before the renderer's beginLevel, written after it (writer thread).
    void setBaseCache(as3d_game::GameSession& ses) {
        if (!run.terrainBaseCache || !ses.hasLevel()) {
            pica::setTerrainBaseCacheFile(std::string());
            return;
        }
        pica::setTerrainBaseCacheFile(std::string(kDataDir) + "/cache/" + (ses.game() ? ses.game()->key : "as3d") + "_m" +
                                      std::to_string(ses.mission()) + (ses.world().intermission() ? "_attract" : "") +
                                      ".terrain");
    }
    void saveBaseCache() {
        const int saved = pica::saveTerrainBaseCache([](const std::string& path, std::vector<u8>&& bytes) {
            return queueFileWrite(path, std::move(bytes));
        });
        const pica::TerrainBaseCacheStats& s = pica::terrainBaseCacheStats();
        if (s.used)
            AS3D_INFO("AS3D_TERRAIN_BASE_CACHE hit=%d blocks=%d file_kb=%.0f load_ms=%.1f saved=%d", s.hit ? 1 : 0, s.blocks,
                      static_cast<double>(s.fileBytes) / 1024.0, s.loadMs, saved);
    }

    void precacheLevel(as3d_game::GameSession& ses, float from, float to) {
        if (!run.precache || !ses.hasLevel()) return;
        World& world = ses.world();
        const as3d::ctr::StreamReadStats rd0 = as3d::ctr::streamReadStats();
        const u64 t0 = svcGetSystemTick();
        PrecacheReport r = world.precacheScripts();
        const u64 t1 = svcGetSystemTick();
        const as3d::ctr::StreamReadStats rd1 = as3d::ctr::streamReadStats();
        loadingFrame(from, true);
        const pica::LoadProfile lp0 = pica::loadProfile();
        const double shadowMs0 = pica::shadowPicaStats().generateMs;
        const u64 frames0 = loadingTicks;
        // The silhouette cache of this level (shadow_pica.h): cached ones are not rasterised.
        if (run.shadowCache) {
            pica::setShadowCacheFile(std::string(kDataDir) + "/cache/" + (ses.game() ? ses.game()->key : "as3d") + "_m" +
                                     std::to_string(ses.mission()) + (world.intermission() ? "_attract" : "") + ".shadows");
        } else {
            pica::setShadowCacheFile(std::string());
        }
        const u64 t2 = svcGetSystemTick();
        r += renderer->precache(world, ses.db(), [this, from, to](float q) { loadingFrame(from + (to - from) * q); });
        // The file goes to the writer thread (ctr_perf.h): the first start's write and fsync
        // took 2.1 s of the loading screen on the console.
        const int saved = pica::saveShadowCache([](const std::string& path, std::vector<u8>&& bytes) {
            return queueFileWrite(path, std::move(bytes));
        });
        const u64 t3 = svcGetSystemTick();
        if (run.shadowCache) {
            const pica::ShadowCacheStats& cs = pica::shadowCacheStats();
            AS3D_INFO("AS3D_SHADOW_CACHE loaded=%d (%.0f KB, %.0f ms) hits=%d misses=%d saved=%d (%.0f ms) key_ms=%.1f "
                      "key_models=%d key_reuses=%d place_ms=%.1f fast_hit=%d",
                      cs.loaded, static_cast<double>(cs.fileBytes) / 1024.0, cs.loadMs, cs.hits, cs.misses, saved,
                      cs.saveMs, cs.keyMs, cs.keyModels, cs.keyReuses, cs.placeMs, run.shadowFastHit ? 1 : 0);
        }
        const as3d::ctr::StreamReadStats rd2 = as3d::ctr::streamReadStats();
        const LoadCounts before = LoadStats::lazyLoads();
        if (hudReady) {
            assets->precachePlay();
            if (flow) assets->precacheMenus(); // the front end's and the in-game menus' pictures
        }
        const u64 t4 = svcGetSystemTick();
        // The front end: the level's sounds and the menus' (GameStack wires the view to its AudioBridge).
        if (precacheSounds && *precacheSounds && world.precacheSet()) {
            PrecacheSet set = *world.precacheSet();
            for (const std::string& s : ui::UiAssets::menuSounds()) set.sounds.push_back(s);
            (*precacheSounds)(set);
        }
        const u64 t5 = svcGetSystemTick();
        {
            const pica::LoadProfile& lp = pica::loadProfile();
            const double k = static_cast<double>(SYSCLOCK_ARM11) / 1000.0;
            const double texMs = static_cast<double>(lp.texTicks - lp0.texTicks) / k;
            const double meshMs = static_cast<double>(lp.meshTicks - lp0.meshTicks) / k;
            const double shadowMs = pica::shadowPicaStats().generateMs - shadowMs0;
            const double framesMs = static_cast<double>(loadingTicks - frames0) / k;
            const double worldMs = static_cast<double>(t3 - t2) / k;
            AS3D_INFO("AS3D_LOAD phase=precache_scripts ms=%.0f read_ms=%.0f (%lu file reads, %.0f KB)",
                      static_cast<double>(t1 - t0) / k, static_cast<double>(rd1.ticks - rd0.ticks) / k,
                      static_cast<unsigned long>(rd1.reads - rd0.reads), static_cast<double>(rd1.bytes - rd0.bytes) / 1024.0);
            const double readMs = static_cast<double>(rd2.ticks - rd1.ticks) / k;
            AS3D_INFO("AS3D_LOAD phase=precache_world ms=%.0f textures=%d tex_ms=%.0f (mip %.0f swizzle %.0f upload %.0f, "
                      "%d levels, %.0f KB) meshes=%d mesh_ms=%.0f shadows_ms=%.0f loading_frames_ms=%.0f "
                      "read_ms=%.0f (%lu file reads, %.0f KB) decode_parse_ms=%.0f",
                      worldMs, lp.texCount - lp0.texCount, texMs, static_cast<double>(lp.texMipTicks - lp0.texMipTicks) / k,
                      static_cast<double>(lp.texSwizzleTicks - lp0.texSwizzleTicks) / k,
                      static_cast<double>(lp.texUploadTicks - lp0.texUploadTicks) / k, lp.texLevels - lp0.texLevels,
                      static_cast<double>(lp.texBytes - lp0.texBytes) / 1024.0, lp.meshCount - lp0.meshCount, meshMs,
                      shadowMs, framesMs, readMs, static_cast<unsigned long>(rd2.reads - rd1.reads),
                      static_cast<double>(rd2.bytes - rd1.bytes) / 1024.0, worldMs - texMs - meshMs - shadowMs - framesMs - readMs);
            PrecacheParts& pp = lastPrecache;
            pp.scripts = static_cast<double>(t1 - t0) / k;
            pp.world = worldMs;
            pp.tex = texMs;
            pp.mip = static_cast<double>(lp.texMipTicks - lp0.texMipTicks) / k;
            pp.swizzle = static_cast<double>(lp.texSwizzleTicks - lp0.texSwizzleTicks) / k;
            pp.upload = static_cast<double>(lp.texUploadTicks - lp0.texUploadTicks) / k;
            pp.textures = lp.texCount - lp0.texCount;
            pp.mesh = meshMs;
            pp.shadows = shadowMs;
            pp.frames = framesMs;
            pp.read = readMs;
            pp.decode = worldMs - texMs - meshMs - shadowMs - framesMs - readMs;
            pp.ui = static_cast<double>(t4 - t3) / k;
            pp.sounds = static_cast<double>(t5 - t4) / k;
            pp.total = static_cast<double>(t5 - t0) / k;
            AS3D_INFO("AS3D_LOAD phase=precache_ui ms=%.0f", static_cast<double>(t4 - t3) / k);
            if (t5 > t4) AS3D_INFO("AS3D_LOAD phase=precache_sounds ms=%.0f", static_cast<double>(t5 - t4) / k);
        }
        const LoadCounts d = LoadStats::lazyLoads() - before;
        r.textures += static_cast<int>(d.of(LoadKind::Texture));
        r.sounds += static_cast<int>(d.of(LoadKind::Sound));
        r.bytes += d.bytes;
        AS3D_INFO("AS3D_PRECACHE mission=%d %s", mission, r.str().c_str());
        LoadStats::reset();
        precached = true;
    }

    void logLevelStart(uint64_t t0, uint64_t t1, uint64_t t2, uint64_t t3) const {
        auto ms = [](uint64_t a, uint64_t b) { return static_cast<double>(b - a) * 1000.0 / SYSCLOCK_ARM11; };
        const pica::TextureStats ts = pica::textureStats();
        AS3D_INFO("level start: beginLevel %.0f ms, UI %.0f ms, precache %.0f ms; textures VRAM %.1f KB (%d), linear "
                  "%.1f KB (%d), VRAM fallbacks %d, failures %d; linear free %.1f KB",
                  ms(t0, t1), ms(t1, t2), ms(t2, t3), kb(ts.vramBytes), ts.vramCount, kb(ts.linearBytes), ts.linearCount,
                  ts.vramFallbacks, ts.failures, kb(linearSpaceFree()));
        logMem("start", gameKey(), mission);
        const pica::LoadProfile& lp = pica::loadProfile();
        const double k = static_cast<double>(SYSCLOCK_ARM11) / 1000.0;
        AS3D_INFO("AS3D_LOAD phase=level_start ms=%.0f begin_level=%.0f ui=%.0f precache=%.0f loading_frames=%d "
                  "loading_frames_ms=%.0f; since boot: textures=%d tex_ms=%.0f (mip %.0f swizzle %.0f upload %.0f) "
                  "meshes=%d mesh_ms=%.0f",
                  ms(t0, t3), ms(t0, t1), ms(t1, t2), ms(t2, t3), loadingFrames, static_cast<double>(loadingTicks) / k,
                  lp.texCount, static_cast<double>(lp.texTicks) / k, static_cast<double>(lp.texMipTicks) / k,
                  static_cast<double>(lp.texSwizzleTicks) / k, static_cast<double>(lp.texUploadTicks) / k, lp.meshCount,
                  static_cast<double>(lp.meshTicks) / k);
    }

    const char* gameKey() const { return session && session->game() ? session->game()->key : ""; }

    void logLazyLoads() {
        if (precached) {
            AS3D_INFO("AS3D_LAZY_LOADS mission=%d %s", mission, LoadStats::lazyLoads().str().c_str());
            logMem("end", gameKey(), mission); // the level's renderer is still alive
        }
        precached = false;
    }

    // The front end's level start (FlowView::beginLevel).
    bool flowBeginLevel(as3d_game::GameSession& ses, std::string* error) {
        session = &ses;
        const bool intermission = ses.hasLevel() && ses.world().intermission();
        loadingFrame(0.2f, true, intermission); // also: the GPU is done with the last level's frames
        logLazyLoads();
        slowLogged = 0;
        LoadStats::PrecacheScope scope; // the level start's loads are not lazy ones (log_lazy)
        mission = ses.mission();
        const uint64_t t0 = svcGetSystemTick();
        // A fresh renderer and 2D layer per level: the renderer's caches (meshes, skins, particle
        // textures) and the menu pictures loaded since the last level start (about 2 MB, the
        // first VRAM tenants after the HUD) go, so the new level fills VRAM from its terrain on;
        // the menus load their pictures again on first use (outside the frame). Two empty frames
        // first: the GPU is then done with everything the loading frame above read.
        if (levelBegun) {
            emptyFrames();
            preview.reset();
            banner.reset();
            bannerTried = false;
            if (!makeRenderer(ses)) {
                if (error) *error = "renderer";
                return false;
            }
            loadUi(ses);
            loadingFrame(0.25f, true, intermission);
        }
        levelBegun = false;
        wash.reset();
        // The read plan of this level start (ctr_loop.h ReadPlan): the renderer's level start,
        // the precache and the sounds.
        ReadPlan plan;
        plan.begin(ses.vfs(),
                   std::string(ses.game() ? ses.game()->key : "as3d") + "_m" + std::to_string(mission) +
                       (intermission ? "_attract" : "_fe"),
                   run.prefetch && run.precache);
        std::string err;
        setBaseCache(ses);
        if (!renderer->beginLevel(ses.world(), &err)) {
            AS3D_ERROR("WorldRenderer::beginLevel: %s", err.c_str());
            if (error) *error = err;
            plan.end();
            return false;
        }
        saveBaseCache();
        levelBegun = true;
        if (intermission) loadBanner(ses);
        const uint64_t t1 = svcGetSystemTick();
        loadingFrame(0.3f, true, intermission);
        precacheLevel(ses, 0.35f, 0.95f);
        plan.end();
        const uint64_t t3 = svcGetSystemTick();
        logLevelStart(t0, t1, t1, t3);
        resetStereo();
        prebuildTouch(ses);
        loadingFrame(1.0f, true, intermission);
        return true;
    }

    // The bottom screen's list of the front end's frame (FlowView::drawFrame).
    void flowLayers(const as3d_game::GameSession& ses, const as3d_game::FrameLayers& l) {
        layers = l;
        haveLayers = true;
        if (!hudReady) return;
        ui::Renderer2D& r = *r2dBottom;
        assets->hudTarget = ui::HudTarget::Bottom320x240;
        r.begin(kBottomW, kBottomH);
        const bool tiles = info && tilesOn(info->touch);
        if (l.hud && pica::passEnabled(pica::Pass::Hud)) {
            ui::drawHud(r, *assets, l.hudState);
            if (tiles) hudExtras(r, info->playing ? info->touch : nullptr);
        }
        touchQuads = false;
        if (info && info->playing && info->touch && !tiles && l.hud && pica::passEnabled(pica::Pass::Touch)) {
            ui::TouchControlsState ts;
            ts.assets = assets.get();
            ts.player = &l.hudState.players[0];
            ts.rules = &ses.rules();
            r2dTouch->begin(kBottomW, kBottomH);
            ui::drawTouchControls(*r2dTouch, *info->touch, ts);
            touchQuads = true;
            touchKeyValid = false;
        }
        // frontend.md 4.1: the menus after the HUD. The main menu's 3D banner (between the two
        // halves on the desktop) is the title on the top screen here; the sequels' model view
        // (between the halves as well) is prepared for the top screen (setModel).
        menuQuads = false;
        if (ui::Frontend* fe = l.frontend; fe && run.uiMenus && ui::ConsoleMenuView::handles(*fe)) {
            // R4.5: the console layout of the menu (its hit rectangles written first).
            ui::Renderer2D& m = *r2dMenu;
            m.begin(kBottomW, kBottomH);
            menuView.applyHits(*fe);
            menuView.drawBottom(m, *assets, *fe);
            // R4.8: the sequels' helicopter selection keeps its model view on the top screen.
            if (fe->modelViewVisible()) fe->drawModelViews();
            menuQuads = true;
        } else if (ui::Frontend* fe = l.frontend) {
            ui::Renderer2D& m = *r2dMenu;
            m.begin(kBottomW, kBottomH);
            fe->drawUnder(m, *assets);
            // Cache units (Renderer2D::cacheBreak, docs/3ds-port.md "Stereo comfort and frame
            // polish"): the screen's background apart from its items (MenuSystem::drawItems breaks
            // after each item), so a pulsing focus or a ticking counter rebuilds its own unit only.
            m.cacheBreak();
            if (fe->modelViewVisible()) fe->drawModelViews();
            fe->drawOver(m, *assets);
            menuQuads = true;
        }
    }

    // The banner object of the game (the first game has one; the sequels none: their title stays
    // in text). Called at an attract level's start, and from prepare if a menu shows the banner
    // without one (precache=0).
    void loadBanner(as3d_game::GameSession& ses) {
        banner.reset();
        bannerTried = true;
        std::unique_ptr<ConsoleBanner> b(new ConsoleBanner());
        if (b->load(ses.vfs(), ses.db())) banner = std::move(b);
    }

    // prepare: the banner's pass at the menu clock, when the main menu shows it.
    void prepareBanner(as3d_game::GameSession& ses, ui::Frontend* fe, const FrameInfo& fi) {
        bannerActive = false;
        if (!fe || !fe->bannerVisible() || modelActive || !pica::passEnabled(pica::Pass::Model)) return;
        if (!banner && !bannerTried) loadBanner(ses);
        if (!banner) return;
        banner->prepare(fe->menus().menuTime(), fi.slider, fi.eyes, run.stereoComfort, maxEyeOffset(), minFocal());
        bannerActive = true;
    }

    // FlowView::drawModel (prepare, from GameFlow::draw): the parts of the view's object (loaded
    // when it changes), the pass with the view's entity matrix, the viewport on the top screen.
    void setModel(as3d_game::GameSession& ses, const ui::ModelView& v) {
        if (!preview) {
            preview.reset(new ModelPreview());
            preview->cache.reset(new ResourceCache(ses.vfs()));
        }
        ModelPreview& p = *preview;
        if (p.object != v.object) {
            const uint64_t t0 = svcGetSystemTick();
            p.object = v.object;
            p.parts = as3d_game::modelViewParts(ses.db(), ses.vfs(), *p.cache, v.object);
            AS3D_INFO("AS3D_MODELVIEW object=%s parts=%d load_ms=%.1f", v.object.c_str(), static_cast<int>(p.parts.size()),
                      static_cast<double>(svcGetSystemTick() - t0) * 1000.0 / SYSCLOCK_ARM11);
        }
        modelView = v;
        modelActive = !p.parts.empty();
        if (!modelActive) return;
        // The desktop's rectangle (GameView::drawModel: computeMapping, rounded) at kModelPxPerVirtual,
        // centred on the top screen.
        modelW = static_cast<int>(std::lround(v.viewport.w * kModelPxPerVirtual));
        modelH = static_cast<int>(std::lround(v.viewport.h * kModelPxPerVirtual));
        modelX = (kTopW - modelW) / 2;
        modelY = (kTopH - modelH) / 2;
        // GameView::drawModel: MeshRenderer::begin(camera at the origin, SceneLighting()), the
        // model matrix of the view's angles and origin times each part's.
        const Mat4 model = as3d_game::entityMatrix(v.angles, v.origin);
        modelPass.begin(SceneLighting(), nullptr, 0);
        modelPass.setEnvironment(Mat4::identity(), 0.0f, false);
        for (const as3d_game::ModelViewPart& part : p.parts) modelPass.submit(*part.gpu, part.material, model * part.local, part.colour);
        modelPass.finish();
        // Stereo (docs/3ds-port.md "Stereo comfort and frame polish"): the screen plane at the nearest
        // vertex of its parts, so the model sits behind the panel's static fill (2D, at the screen
        // plane) drawn over it; 0 = the view's own distance (prepare).
        modelFocal = modelFar = 0.0f;
        const float aspect = modelH > 0 ? static_cast<float>(modelW) / static_cast<float>(modelH) : 1.0f;
        for (const as3d_game::ModelViewPart& part : p.parts) {
            if (!part.gpu || !part.gpu->valid) continue;
            float n = 0.0f, f = 0.0f;
            if (!pica::meshDepthRange(model * part.local, part.gpu->model.positions, v.fovY, aspect, v.nearPlane, v.farPlane,
                                      n, f))
                continue;
            if (modelFocal == 0.0f || n < modelFocal) modelFocal = n;
            modelFar = std::max(modelFar, f);
        }
    }

    // One eye of the model view: the viewport and its depth, the GL MeshRenderer's depth mapping
    // and fog (SceneLighting's linear 650..900: none at the model's distance), the eye's projection.
    void drawModelEye(int eye) {
        const int glY = kTopH - modelY - modelH;
        setViewport(modelX, glY, modelW, modelH);
        setScissor(true, modelX, glY, modelW, modelH);
        clearDepth();
        WorldView wv;
        wv.view = Mat4::identity();
        wv.fovY = modelView.fovY;
        wv.nearPlane = modelView.nearPlane;
        wv.farPlane = modelView.farPlane;
        EyeView ev;
        if (stereo.eyes == 2) {
            ev.eyeOffset = eye == 0 ? -stereo.eyeOffset : stereo.eyeOffset;
            ev.focalDistance = stereo.focal;
        }
        const SceneLighting light;
        pica::setDepthMapping(FogDepthMap::WBuffer, wv.nearPlane);
        pica::setLevelFogColor(light.fogColor);
        if (light.fogStart < wv.farPlane) pica::setFog(wv.nearPlane, wv.farPlane, light.fogStart, light.fogEnd);
        else pica::disableFog();
        const float aspect = static_cast<float>(modelW) / static_cast<float>(modelH);
        modelPass.draw(pica::picaEyeProjection(wv, ev, aspect) * wv.view);
        pica::disableFog();
        setScissor(false);
        setViewport(0, 0, kTopW, kTopH);
    }

    // The top screen's 2D list (prepare): PAUSED, the title, the model view's backdrop, the perf line.
    void buildTopQuads(const FrameInfo& fi) {
        topQuads = false;
        if (!hudReady) return;
        const ui::Frontend* fe = flow ? layers.frontend : nullptr;
        bool paused = fi.paused;
        bool title = false;
        if (flow) {
            paused = fe && fe->state() == ui::FrontendState::Playing && fe->menuOpen() && fe->topScreen() == ui::Screen::InGame;
            title = fe && !modelActive && !bannerActive &&
                    (fe->bannerVisible() || (!worldDrawn && fe->state() != ui::FrontendState::Intro));
        }
        // R4.5: the console menus' reading panel (titles, Information, scores, results).
        const bool menuTop = flow && fe && run.uiMenus && ui::ConsoleMenuView::handles(*fe);
        if (menuTop) title = false; // the main menu shows its banner; the panels their own titles
        // R4.7: the top screen in play (the level name, the cheat message, PAUSED; the first
        // game's console HUD: its bottom layout draws neither text).
        ui::ConsolePlayTop play;
        const bool consoleHud = assets->hudLayout().levelNameY < 0;
        if (consoleHud) {
            const ui::HudState* hs = flow ? (haveLayers && layers.hud ? &layers.hudState : nullptr) : (showHud ? &hud : nullptr);
            if (hs) {
                play.levelName = hs->levelName;
                play.levelTime = hs->levelTime;
                play.message = hs->message;
                play.messageAge = hs->messageAge;
            }
            play.paused = paused;
        }
        const bool playTop = consoleHud && ui::consolePlayTopVisible(play);
        if (!paused && !title && !modelActive && !run.perf && !menuTop && !playTop) return;
        r2d->begin(kTopW, kTopH);
        if (menuTop) menuView.drawTop(*r2d, *assets, *fe);
        if (modelActive) {
            // The selection's dot grid, mapped as the model's rectangle: virtual v at pixel
            // model corner + kModelPxPerVirtual (v - viewport corner).
            const ui::Mapping& m = r2d->mapping();
            const float s = kModelPxPerVirtual / m.scaleX;
            const float ox = (static_cast<float>(modelX) - kModelPxPerVirtual * modelView.viewport.x - m.offsetX) / m.scaleX;
            const float oy = (static_cast<float>(modelY) - kModelPxPerVirtual * modelView.viewport.y - m.offsetY) / m.scaleY;
            ui::drawModelViewBackdrop(*r2d, *assets, ox, oy, s, true);
            // Over the model, as the desktop's panel: its static fill (the same noise, a fresh offset).
            r2dTopOver->begin(kTopW, kTopH);
            ui::drawModelViewStatic(*r2dTopOver, *assets, ox, oy, s);
        }
        ui::TextStyle st;
        if (title && session && session->game()) {
            st.scale = 3.0f;
            st.color = ui::orange();
            st.align = ui::Align::Center;
            ui::drawTextShadowed(*r2d, assets->uiFont(), 400, worldDrawn ? 60 : 250, session->game()->title, st);
        }
        if (playTop) {
            ui::drawConsolePlayTop(*r2d, *assets, play);
        } else if (paused) {
            st.scale = 3.0f;
            st.color = ui::orange();
            st.align = ui::Align::Left;
            ui::drawText(*r2d, assets->uiFont(), 290, 260, "PAUSED", st);
        }
        if (run.perf) {
            char line[96];
            std::snprintf(line, sizeof line, "cpu %.1f gpu %.1f prep %.1f", static_cast<double>(fi.lastCpuMs),
                          static_cast<double>(fi.lastGpuMs), static_cast<double>(prepareMs));
            st.scale = 1.0f;
            st.color = ui::Color{1, 1, 1, 0.8f};
            st.align = ui::Align::Left;
            ui::drawText(*r2d, assets->uiFont(), 12, 572, line, st);
        }
        topQuads = true;
    }

    void dumpPair(const std::string& base, const FrameInfo& fi) {
        const std::string dir = std::string(kDataDir) + "/";
        const bool t = dumpTarget(ctx->topTarget(0), kTopW, dir + base + "_top.ppm");
        // The right eye too when two eyes were drawn this frame (the world, the model view, the
        // banner, or the 2D layer alone: the 2D-at-the-screen-plane check compares them).
        const bool r = fi.eyes == 2 && dumpTarget(ctx->topTarget(1), kTopW, dir + base + "_right.ppm");
        const bool b = dumpTarget(ctx->bottomTarget(), kBottomW, dir + base + "_bottom.ppm");
        AS3D_INFO("AS3D_DUMP %s frame=%u step=%u screen=%s top=%d right=%d bottom=%d", base.c_str(), fi.frame, fi.step,
                  fi.screen, t ? 1 : 0, r ? 1 : 0, b ? 1 : 0);
        if (worldDrawn && stereo.eyes == 2)
            AS3D_INFO("AS3D_DUMP %s stereo eye_offset_slider=%.3f eye_offset=%.3f comfort=%.3f focal=%.1f near_depth=%.1f "
                      "far_depth=%.1f near_px=%.1f far_px=%.1f",
                      base.c_str(), static_cast<double>(fullEyeOffset), static_cast<double>(stereo.eyeOffset),
                      static_cast<double>(comfort.ratio), static_cast<double>(stereo.focal), static_cast<double>(nearDepth),
                      static_cast<double>(farDepth), static_cast<double>(nearPx), static_cast<double>(farPx));
        if (bannerActive) {
            const pica::MeshDrawStats& bs = banner->lastStats();
            AS3D_INFO("AS3D_BANNER_DUMP %s mt=%.2f eyes=%d eye_offset=%.2f focal=%.1f draws=%d vertices=%d triangles=%d", base.c_str(),
                      static_cast<double>(banner->time()), banner->stereo().eyes, static_cast<double>(banner->stereo().eyeOffset),
                      static_cast<double>(banner->stereo().focal), bs.draws, bs.vertices, bs.triangles);
        }
        if (modelActive) {
            const pica::MeshDrawStats& ms = modelPass.lastStats();
            AS3D_INFO("AS3D_MODELVIEW_DUMP %s object=%s angles=(%.2f, %.2f, %.2f) viewport=(%d, %d, %d, %d) eyes=%d "
                      "eye_offset=%.2f focal=%.1f draws=%d vertices=%d triangles=%d env_draws=%d",
                      base.c_str(), modelView.object.c_str(), static_cast<double>(modelView.angles[0]),
                      static_cast<double>(modelView.angles[1]), static_cast<double>(modelView.angles[2]), modelX, modelY,
                      modelW, modelH, stereo.eyes, static_cast<double>(stereo.eyeOffset), static_cast<double>(stereo.focal),
                      ms.draws, ms.vertices, ms.triangles, ms.envDraws);
        }
    }
};

namespace {

// GameStack's view in the menu mode: forwards to the composer.
class CtrFlowView final : public as3d_game::FlowView {
public:
    explicit CtrFlowView(GameFrame::Impl& im) : im_(im) { im_.precacheSounds = &precacheSounds; }
    ~CtrFlowView() override { im_.precacheSounds = nullptr; }

    bool init(as3d_game::GameSession& ses, std::string* error, bool level) override {
        im_.session = &ses;
        if (!im_.initBackend()) {
            if (error) *error = "pica::initBackend failed";
            return false;
        }
        im_.loadingFrame(0.0f, true);
        im_.loadUi(ses);
        if (!im_.makeRenderer(ses)) {
            if (error) *error = "WorldRenderer::init failed";
            return false;
        }
        AS3D_INFO("front end view: 2D layer %s, renderer ready; stereo eye offset %.2f at slider 1",
                  im_.hudReady ? "loaded" : "unavailable", static_cast<double>(im_.maxEyeOffset()));
        im_.logStereoConfig();
        return level ? beginLevel(ses, error) : true;
    }
    bool beginLevel(as3d_game::GameSession& ses, std::string* error) override { return im_.flowBeginLevel(ses, error); }
    void drawFrame(const as3d_game::GameSession& ses, int, int, const as3d_game::FrameLayers& layers) override {
        im_.flowLayers(ses, layers);
    }
    void drawModel(as3d_game::GameSession& ses, const ui::ModelView& v) override { im_.setModel(ses, v); }

private:
    GameFrame::Impl& im_;
};

} // namespace

GameFrame::GameFrame(const RunConfig& run) : impl_(new Impl) { impl_->run = run; }
GameFrame::~GameFrame() = default;

std::unique_ptr<as3d_game::FlowView> GameFrame::makeFlowView(CtrContext& ctx) {
    impl_->ctx = &ctx;
    return std::unique_ptr<as3d_game::FlowView>(new CtrFlowView(*impl_));
}

void GameFrame::setFlow(as3d_game::GameFlow* flow) { impl_->flow = flow; }

as3d::ui::ConsoleMenuView* GameFrame::menuView() { return impl_->run.uiMenus ? &impl_->menuView : nullptr; }

void GameFrame::loadingScreen(float progress, bool intermission) {
    if (!impl_->ctx || !impl_->backend) return;
    impl_->loadingFrame(progress, progress >= 1.0f, intermission);
}

bool GameFrame::begin(CtrContext& ctx, as3d_game::GameSession& session) {
    Impl& im = *impl_;
    im.ctx = &ctx;
    im.session = &session;
    im.mission = session.mission();
    LoadStats::PrecacheScope scope; // the level start's loads are not lazy ones (log_lazy)
    const uint64_t tEntry = svcGetSystemTick();
    const uint64_t frames0 = im.loadingTicks;
    const int frameCount0 = im.loadingFrames;
    if (!im.initBackend()) return false;
    im.loadingFrame(0.0f, true);
    const uint64_t t0 = svcGetSystemTick();

    World& world = session.world();
    std::string err;
    if (!im.makeRenderer(session)) return false;
    const uint64_t tMade = svcGetSystemTick();
    // The terrain first: its textures are the first VRAM tenants.
    im.setBaseCache(session);
    if (!im.renderer->beginLevel(world, &err)) {
        AS3D_ERROR("WorldRenderer::beginLevel: %s", err.c_str());
        return false;
    }
    im.saveBaseCache();
    im.levelBegun = true;
    im.wash.reset();
    const uint64_t t1 = svcGetSystemTick();
    im.loadingFrame(0.3f, true);

    im.loadUi(session);
    im.loadingFrame(0.4f, true);
    const uint64_t t2 = svcGetSystemTick();

    im.precacheLevel(session, 0.5f, 0.95f);
    const uint64_t t3 = svcGetSystemTick();
    im.logLevelStart(t0, t1, t2, t3);
    AS3D_INFO("stereo: eye offset %.2f at slider 1, focal distance >= %.1f (the camera-to-player depth)",
              static_cast<double>(im.maxEyeOffset()), static_cast<double>(im.minFocal()));
    im.logStereoConfig();
    im.resetStereo();
    const uint64_t t4 = svcGetSystemTick();
    im.prebuildTouch(session);
    const uint64_t t5 = svcGetSystemTick();
    im.loadingFrame(1.0f, true);
    {
        // R4.6 (docs/3ds-port.md "GPU cuts and the level start (round 4, R4.6)"): the renderer's
        // part of AS3D_LEVEL_START (from this call's entry to its end) by name. The loading frames
        // are counted once (frames=) and taken out of the phases they fall in.
        const uint64_t tEnd = svcGetSystemTick();
        const double k = static_cast<double>(SYSCLOCK_ARM11) / 1000.0;
        auto ms = [k](uint64_t a, uint64_t b) { return static_cast<double>(b - a) / k; };
        const pica::TerrainPicaStats& ts = pica::terrainPicaStats();
        const pica::ShadowCacheStats& cs = pica::shadowCacheStats();
        const Impl::PrecacheParts& pp = im.lastPrecache;
        const double framesMs = static_cast<double>(im.loadingTicks - frames0) / k;
        const double total = ms(tEntry, tEnd);
        const double beginLevel = ms(tMade, t1);
        const double precache = ms(t2, t3);
        const double named = ms(tEntry, tMade) + beginLevel + ms(t1, t2) + precache + ms(t3, t4) + ms(t4, t5) +
                             ms(t5, tEnd);
        AS3D_INFO("AS3D_LEVEL_RENDERER ms=%.0f | init_backend+make_renderer=%.0f | begin_level=%.0f (terrain=%.0f: "
                  "generate_base=%.0f textures_create=%.0f geometry=%.0f read_decode_upload=%.0f; water+scene=%.0f) | "
                  "ui=%.0f | precache=%.0f (scripts=%.0f; world=%.0f: textures=%.0f n=%d mip=%.0f swizzle=%.0f upload=%.0f "
                  "meshes=%.0f shadows=%.0f [cache_load=%.0f key=%.0f place=%.0f (pages %d: %.0f) hits=%d misses=%d] read=%.0f "
                  "decode_parse=%.0f; ui=%.0f sounds=%.0f) | logs+stereo=%.0f | prebuild_touch=%.0f | last_frame=%.0f | "
                  "loading_frames=%d (%.0f ms, inside the phases) | unnamed=%.0f",
                  total, ms(tEntry, tMade), beginLevel, ts.loadMs, ts.loadGenerateMs, ts.loadCreateMs,
                  ts.loadGeometryMs, ts.loadRestMs, beginLevel - ts.loadMs, ms(t1, t2), precache, pp.scripts, pp.world,
                  pp.tex, pp.textures, pp.mip, pp.swizzle, pp.upload, pp.mesh, pp.shadows, cs.loadMs, cs.keyMs, cs.placeMs,
                  pica::shadowPicaStats().atlasPages, pica::shadowPicaStats().atlasPageMs, cs.hits, cs.misses, pp.read, pp.decode, pp.ui, pp.sounds, ms(t3, t4), ms(t4, t5), ms(t5, tEnd),
                  im.loadingFrames - frameCount0, framesMs, total - named);
    }
    return true;
}

void GameFrame::prepare(const FrameInfo& info) {
    Impl& im = *impl_;
    const uint64_t t0 = svcGetSystemTick();
    as3d_game::GameSession& ses = *info.session;
    im.session = &ses;
    im.info = &info;
    if (im.flow) {
        // The front end's frame: GameFlow::draw calls the view's drawFrame (flowLayers), which
        // builds the bottom screen's list here, outside the frame (menu pictures load on first
        // use).
        im.haveLayers = false;
        im.modelActive = false;
        im.flow->draw(kBottomW, kBottomH);
        // The model view takes the top screen (on black, as the desktop's bottom screen under it).
        im.worldDrawn = im.haveLayers && im.layers.world && ses.hasLevel() && im.levelBegun && !im.modelActive;
        im.topBrightness = im.haveLayers ? im.layers.brightness : as3d_game::kDefaultBrightness;
        im.showHud = false; // in the list already
    } else {
        im.worldDrawn = true;
        im.topBrightness = im.run.brightness;
        const World& world = ses.world();
        im.showHud = ses.hasLevel() && !(world.intermission() || world.levelComplete() || world.gameOver());
        if (im.hudReady) im.hud = as3d_game::hudStateOf(ses);
    }
    im.info = nullptr;
    pica::passProfile().add(pica::Pass::PrepFlow, svcGetSystemTick() - t0, 0, 0);

    im.stereo = StereoParams();
    // The seconds since the last frame (the simulation steps), for the stereo filters.
    const float dt = im.haveLastStep && info.step >= im.lastStep ? static_cast<float>(info.step - im.lastStep) / 60.0f : 0.0f;
    im.lastStep = info.step;
    im.haveLastStep = true;
    if (im.worldDrawn) {
        const World& world = ses.world();
        // Stereo: zero parallax at the player's depth.
        const WorldView wv = worldViewOf(world, kAspect);
        im.playerDepth = im.minFocal();
        bool anchored = false;
        const int pi = world.playerEntityIndex(0);
        if (pi >= 0) {
            const Vec3 p = world.entity(pi).v3(F_BASE_ORIGIN);
            const Vec4 v = wv.view * Vec4{p.x, p.y, p.z, 1.0f};
            if (-v.z > 0.0f) {
                im.playerDepth = -v.z;
                anchored = true;
            }
        }
        float minFocal = im.minFocal();
        // No player (the attract levels behind the menus): the screen plane where the last frame's
        // depths let the most depth through within the comfort limits (stereo_comfort.h
        // balancedFocal), instead of min_focal, which put the whole scene far behind the screen
        // (41 px of far parallax at slider 1 before the clamp).
        if (!anchored && im.run.stereoComfort && info.eyes == 2 && im.nearDepth > 0.0f) {
            const float s = std::min(std::max(info.slider, 0.0f), 1.0f);
            pica::ComfortLimits lim = im.comfortLimits();
            lim.maxNearPx *= s;
            lim.maxFarPx *= s;
            const float d = pica::balancedFocal(lim, im.nearDepth, im.farDepth, pica::stereoA(wv.fovY, kAspect),
                                                static_cast<float>(kTopW), s * im.maxEyeOffset());
            if (d > 0.0f) {
                im.playerDepth = std::max(d, wv.nearPlane);
                minFocal = wv.nearPlane;
            }
        }
        im.stereo = stereoFor(info.eyes == 2 ? info.slider : 0.0f, im.playerDepth, im.maxEyeOffset(), minFocal);
        // The screen plane eases after the player's depth (a respawn, a camera jump), the depth of
        // the whole scene with it.
        if (im.stereo.eyes == 2) {
            if (im.run.stereoComfort) im.stereo.focal = std::max(im.focalFilter.update(im.stereo.focal, dt), minFocal);
        } else {
            im.focalFilter.reset();
            im.comfort.reset();
        }

        Viewport vp;
        vp.width = kTopW;
        vp.height = kTopH;
        if (im.stereo.eyes == 2) {
            vp.maxEyeOffset = im.stereo.eyeOffset;
            vp.minFocalDistance = im.stereo.focal;
        }
        pica::setFrameEyes(im.stereo.eyes); // the stereo-only quality knobs (quality_pica.h)
        im.renderer->prepare(world, vp);
        // The comfort clamp only lowers the eye offset: the cull volume prepared for the slider's
        // offset holds both eyes.
        if (im.stereo.eyes == 2) im.clampStereo(wv, dt);
    } else {
        im.focalFilter.reset();
        im.comfort.reset();
    }

    // The model view: zero parallax at the model's distance.
    if (im.modelActive) {
        if (im.run.stereoComfort && im.modelFocal > 0.0f) {
            // The model's front on the screen plane, its back within max_far_parallax_px.
            im.stereo = stereoFor(info.eyes == 2 ? info.slider : 0.0f, im.modelFocal, im.maxEyeOffset(), 1.0f);
            const float s = std::min(std::max(info.slider, 0.0f), 1.0f);
            const float aspect = im.modelH > 0 ? static_cast<float>(im.modelW) / static_cast<float>(im.modelH) : 1.0f;
            im.stereo.eyeOffset =
                pica::limitEyeOffsetForDepth(im.stereo.eyeOffset, im.modelFocal, im.modelFar, pica::stereoA(im.modelView.fovY, aspect),
                                             static_cast<float>(im.modelW), im.run.maxFarParallaxPx * s);
        } else
            im.stereo = stereoFor(info.eyes == 2 ? info.slider : 0.0f, -im.modelView.origin[2], im.maxEyeOffset(), im.minFocal());
    }

    // The first game's banner over the attract level (the main menu).
    im.bannerActive = false;
    if (im.flow) im.prepareBanner(ses, im.haveLayers ? im.layers.frontend : nullptr, info);

    // The top screen's 2D layer, built once and flushed on each eye at screen depth.
    im.buildTopQuads(info);
    const uint64_t prepTicks = svcGetSystemTick() - t0;
    pica::passProfile().add(pica::Pass::PrepFrame, prepTicks, 0, 0);
    im.prepareMs = static_cast<float>(static_cast<double>(prepTicks) * 1000.0 / SYSCLOCK_ARM11);
}

void GameFrame::drawTop(CtrContext& ctx, int eye, const FrameInfo& info) {
    Impl& im = *impl_;
    if (eye == 0) {
        ui::ui2dPicaResetStats();
        pica::frameBegin();
        // The wash detector judges the last frame's depth copies (complete by the fence).
        if (im.wash.enabled()) {
            const Terrain* terrain = info.session && info.session->hasLevel() ? info.session->world().terrain() : nullptr;
            if (im.wash.judge(terrain, info.frame)) im.washMarkPending = true;
        }
        im.submitTick = svcGetSystemTick();
        for (int p = 0; p < pica::kPassCount; ++p) im.passTicks0[p] = pica::passProfile().totals(static_cast<pica::Pass>(p)).ticks;
        im.lateQueued0 = pica::lateTextureStats().queued;
        // run.ini right_eye_rate (docs/3ds-port.md "Stereo GPU budget (round 4, R4.1)"): in a mission
        // in stereo, the right eye every other frame (half) or the two eyes in turn (alternate); a
        // skipped eye's target keeps its last picture and is transferred again. The first stereo frame
        // after mono, a level start or a menu draws both.
        im.eyeSkip[0] = im.eyeSkip[1] = false;
        const bool rated = im.run.rightEyeRate != 0 && info.eyes == 2 && im.stereo.eyes == 2 && im.worldDrawn &&
                           !im.modelActive && !im.bannerActive;
        if (!rated) {
            im.stereoFrames = 0;
        } else {
            if (im.stereoFrames > 0) {
                const bool odd = (im.stereoFrames & 1u) != 0;
                if (im.run.rightEyeRate == 1) im.eyeSkip[1] = odd;
                else im.eyeSkip[odd ? 0 : 1] = true;
            }
            ++im.stereoFrames;
        }
        ++im.lean.frames;
    }
    pica::gpuPassScreen(eye);
    pica::drawOn(pica::FrameTarget{ctx.topTarget(eye), kTopW, kTopH, true});
    if (im.eyeSkip[eye]) {
        // Nothing drawn: C3D_FrameDrawOn marked the target used, so C3D_FrameEnd transfers its last
        // picture again (the top screen stays in 3D: citro3d swaps it as stereo only when the right
        // target was transferred).
        ++im.lean.eyeSkipped[eye];
        if (eye == 0) {
            im.worldDrawsLeft = im.worldVertsLeft = 0;
            im.worldDrawsRight = im.worldVertsRight = 0;
        } else {
            im.worldDrawsRight = im.worldVertsRight = 0;
        }
        pica::gpuPassMark(pica::GpuExtra::Other);
        pica::kickGpu();
        if (eye == 0) im.kickTick = pica::gpuKick() ? pica::firstKickTick() : 0;
        return;
    }
    // Pass 14, the brightness (0.5 is neutral: dst * 0.5 + 0.5 * dst). run.ini brightness_skip
    // (default on): none within 1/255 of neutral either (the quad at 128/255 would scale by
    // 256/255). brightness_mode=fold (default, R4.6): no quad; every draw of the world and of the
    // 2D layer over it takes the quad's product itself (tev.h setBrightnessFold), the clear and
    // fog colours too.
    const bool neutralBrightness =
        im.topBrightness == 0.5f || (im.brightnessSkip() && std::fabs(im.topBrightness - 0.5f) <= 1.0f / 255.0f);
    const bool brightnessOn = im.worldDrawn && !neutralBrightness && pica::passEnabled(pica::Pass::Brightness);
    const bool fold = brightnessOn && im.run.brightnessMode == 0;
    if (fold) {
        pica::setBrightnessFold(static_cast<uint8_t>(std::min(im.topBrightness, 1.0f) * 255.0f + 0.5f));
        ++im.lean.brightnessFolded;
    }
    if (im.worldDrawn) {
        EyeView ev;
        if (im.stereo.eyes == 2) {
            ev.eyeOffset = eye == 0 ? -im.stereo.eyeOffset : im.stereo.eyeOffset;
            ev.focalDistance = im.stereo.focal;
            // The right eye's world from the left eye's commands (eye_replay_pica.h), unless the
            // right eye is skipped this frame (right_eye_rate).
            if (eye == 0 && !im.eyeSkip[1]) {
                EyeView right = ev;
                right.eyeOffset = im.stereo.eyeOffset;
                pica::eyeReplayRequest(right);
            }
        }
        const WorldRenderStats& ws0 = im.renderer->lastStats();
        const int d0 = ws0.drawCalls, v0 = ws0.vertices;
        im.renderer->draw(ev); // pass 0 clears to the level's clear colour
        const WorldRenderStats& ws1 = im.renderer->lastStats();
        if (eye == 0) {
            im.worldDrawsLeft = ws1.drawCalls;
            im.worldVertsLeft = ws1.vertices;
            im.worldDrawsRight = im.worldVertsRight = 0;
        } else {
            im.worldDrawsRight = ws1.drawCalls - d0;
            im.worldVertsRight = ws1.vertices - v0;
        }
    } else {
        clear(Vec4{0, 0, 0, 1}, true, 1.0f);
        pica::gpuPassMark(pica::Pass::Clear);
    }
    if (im.topQuads && pica::passEnabled(pica::Pass::Top2d)) {
        UiMeter m(pica::Pass::Top2d);
        im.r2d->flush();
    }
    // The quad covered the world and the 2D layer; what follows (the model view, the banner) is
    // drawn after it.
    if (fold) pica::setBrightnessFold(0);
    if (im.modelActive) {
        UiMeter m(pica::Pass::Model);
        im.drawModelEye(eye);
        im.r2dTopOver->flush();
        m.extraDraws = im.modelPass.lastStats().draws;
        m.extraVerts = im.modelPass.lastStats().vertices;
    }
    if (im.worldDrawn && neutralBrightness && im.topBrightness != 0.5f) ++im.lean.brightnessSkipped;
    if (brightnessOn && !fold) {
        UiMeter m(pica::Pass::Brightness);
        im.renderer->drawBrightness(kTopW, kTopH, im.topBrightness);
        m.extraDraws = 1;
        m.extraVerts = 4;
    }
    if (im.bannerActive) {
        UiMeter m(pica::Pass::Model);
        im.banner->drawEye(eye); // over the letterbox's band, as the desktop's main menu
        m.extraDraws = im.banner->lastStats().draws;
        m.extraVerts = im.banner->lastStats().vertices;
    }
    im.cmdUsageMax = std::max(im.cmdUsageMax, C3D_GetCmdBufUsage());
    pica::gpuPassMark(pica::GpuExtra::Other);
    if (im.wash.enabled() && im.worldDrawn) {
        im.queueWashCopy(ctx, eye, info);
        pica::gpuPassMark(pica::GpuExtra::Wash);
    }
    // gpu_kick=1: the GPU draws this eye while the CPU records the next (pica_frame.h).
    pica::kickGpu();
    if (eye == 0) im.kickTick = pica::gpuKick() ? pica::firstKickTick() : 0;
}

void GameFrame::drawBottom(CtrContext& ctx, const FrameInfo& info) {
    Impl& im = *impl_;
    pica::gpuPassScreen(2);
    // run.ini bottom_hz=30 (docs/3ds-port.md "Stereo GPU budget (round 4, R4.1)"): in a mission every
    // other frame leaves the bottom screen alone. Its target is not drawn on, so C3D_FrameEnd neither
    // transfers nor swaps it and the screen keeps showing its last picture. The menus stay at 60.
    const bool inMission = im.hudReady && (im.flow ? (im.haveLayers && im.layers.hud && info.playing) : im.showHud);
    const bool skipBottom = im.bottomHz() == 30 && inMission && im.bottomDrawnLast;
    im.bottomDrawnLast = !skipBottom;
    if (skipBottom) ++im.lean.bottomSkipped;
    else ++im.lean.bottomDrawn;
    if (!skipBottom) {
        pica::drawOn(pica::FrameTarget{ctx.bottomTarget(), kBottomW, kBottomH, false});
        clear(Vec4{0, 0, 0, 1}, false, 1.0f);
        pica::gpuPassMark(pica::Pass::Clear);
        if (im.hudReady && im.flow) {
            UiMeter m(pica::Pass::Bottom2d);
            for (int k = 0; k < 3; ++k) {
                im.listMs[k] = 0.0;
                im.listHit[k] = -1;
                im.listVerts[k] = 0;
            }
            if (im.haveLayers) {
                ui::Renderer2D* lists[3] = {im.r2dBottom.get(), im.touchQuads ? im.r2dTouch.get() : nullptr,
                                            im.menuQuads ? im.r2dMenu.get() : nullptr};
                for (int k = 0; k < 3; ++k) {
                    if (!lists[k]) continue;
                    const ui::Ui2dPicaStats s0 = ui::ui2dPicaStats();
                    const u64 t0 = svcGetSystemTick();
                    lists[k]->flush();
                    im.listMs[k] = static_cast<double>(svcGetSystemTick() - t0) * 1000.0 / SYSCLOCK_ARM11;
                    const ui::Ui2dPicaStats& s1 = ui::ui2dPicaStats();
                    im.listHit[k] = s1.cacheHits > s0.cacheHits ? 1 : 0;
                    im.listVerts[k] = s1.vertices - s0.vertices;
                    // The AS3D_UI_LISTS sums.
                    im.listSum.ms[k] += im.listMs[k];
                    im.listSum.units[k] += s1.units - s0.units;
                    im.listSum.built[k] += s1.unitsBuilt - s0.unitsBuilt;
                    im.listSum.draws[k] += s1.runs - s0.runs;
                    im.listSum.verts[k] += im.listVerts[k];
                    ++im.listSum.flushes[k];
                }
                ++im.listSum.frames;
            }
        } else if (im.hudReady) {
            const World& world = info.session->world();
            im.assets->hudTarget = ui::HudTarget::Bottom320x240;
            ui::Renderer2D& r = *im.r2d;
            if (im.showHud && pica::passEnabled(pica::Pass::Hud)) {
                UiMeter m(pica::Pass::Hud);
                ui::Renderer2D& h = *im.r2dBottom;
                h.begin(kBottomW, kBottomH);
                ui::drawHud(h, *im.assets, im.hud);
                if (Impl::tilesOn(info.touch)) im.hudExtras(h, info.touch);
                h.flush();
            }
            // The hint box over the HUD (render-pipeline.md 1.1 pass 13).
            if (info.session->hasLevel() && world.hintShowing() && pica::passEnabled(pica::Pass::Hint)) {
                UiMeter m(pica::Pass::Hint);
                r.begin(kBottomW, kBottomH);
                ui::drawHint(r, *im.assets, world.hintText());
                r.flush();
            }
            if (info.touch && !Impl::tilesOn(info.touch) && im.showHud && pica::passEnabled(pica::Pass::Touch)) {
                UiMeter m(pica::Pass::Touch);
                ui::TouchControlsState ts;
                ts.assets = im.assets.get();
                ts.player = &im.hud.players[0];
                ts.rules = &world.rules();
                ui::Renderer2D& t = *im.r2dTouch;
                const TouchKey key = touchKeyOf(*info.touch, ts);
                if (!im.touchKeyValid || std::memcmp(&key, &im.touchKey, sizeof key) != 0) {
                    t.begin(kBottomW, kBottomH);
                    ui::drawTouchControls(t, *info.touch, ts);
                    im.touchKey = key;
                    im.touchKeyValid = true;
                }
                t.flush();
            }
        }
        pica::gpuPassMark(pica::GpuExtra::Other);
    }
    im.arenaUsed = pica::frameArena().used();
    pica::frameEnd();
    const double k = static_cast<double>(SYSCLOCK_ARM11) / 1000.0;
    const double submitMs = static_cast<double>(svcGetSystemTick() - im.submitTick) / k;
    if (submitMs > kSlowSubmitMs && im.slowLogged < kSlowSubmitLogs) {
        ++im.slowLogged;
        // The passes over 0.3 ms this frame; "other" = the submit outside every pass.
        std::string split;
        double inPasses = 0.0;
        for (int p = 0; p < pica::kPassCount; ++p) {
            const pica::Pass pass = static_cast<pica::Pass>(p);
            if (pica::isProfileEntry(pass)) continue; // inside the 2D passes, or the prepare
            const double ms = static_cast<double>(pica::passProfile().totals(pass).ticks - im.passTicks0[p]) / k;
            inPasses += ms;
            if (ms < 0.3) continue;
            char item[48];
            std::snprintf(item, sizeof item, " %s=%.1f", pica::passName(pass), ms);
            split += item;
        }
        const WorldRenderStats& ws = im.renderer->lastStats();
        // The front end's bottom lists: ms/cached/vertices of the HUD, the touch band, the menus.
        char lists[128] = "";
        if (im.flow)
            std::snprintf(lists, sizeof lists, " lists hud=%.1f/%d/%d touch=%.1f/%d/%d menu=%.1f/%d/%d", im.listMs[0],
                          im.listHit[0], im.listVerts[0], im.listMs[1], im.listHit[1], im.listVerts[1], im.listMs[2],
                          im.listHit[2], im.listVerts[2]);
        AS3D_INFO("AS3D_SLOW_SUBMIT frame=%u step=%u ms=%.1f eyes=%d draws=%d verts=%d late_tex=%d lazy=%lld%s other=%.1f%s",
                  info.frame, info.step, submitMs, im.stereo.eyes, ws.drawCalls, ws.vertices,
                  pica::lateTextureStats().queued - im.lateQueued0, static_cast<long long>(LoadStats::lazyLoads().total()),
                  split.c_str(), submitMs - inPasses, lists);
    }
}

void GameFrame::stats(FrameStats& out) {
    Impl& im = *impl_;
    const WorldRenderStats& ws = im.renderer->lastStats();
    const ui::Ui2dPicaStats& us = ui::ui2dPicaStats();
    out.prepareMs = im.prepareMs;
    out.eyes = im.worldDrawn || im.modelActive ? im.stereo.eyes : 1;
    out.draws = (im.worldDrawn ? ws.drawCalls : 0) + (im.modelActive ? im.modelPass.lastStats().draws : 0) +
                (im.bannerActive ? im.banner->lastStats().draws : 0) + us.runs;
    out.verts = (im.worldDrawn ? ws.vertices : 0) + us.vertices;
    const pica::TextureStats ts = pica::textureStats();
    out.texVramKb = static_cast<unsigned>(ts.vramBytes / 1024);
    out.texLinearKb = static_cast<unsigned>(ts.linearBytes / 1024);
    out.lazyLoads = im.precached ? static_cast<unsigned>(LoadStats::lazyLoads().total()) : 0u;
    out.arenaKb = static_cast<unsigned>(im.arenaUsed / 1024);
    out.gpuStartTick = im.kickTick;
    out.drawsRight = im.worldDrawn && out.eyes == 2 ? im.worldDrawsRight : 0;
    out.vertsRight = im.worldDrawn && out.eyes == 2 ? im.worldVertsRight : 0;
    // The per-pass CPU table (pass_profile.h), every kPassReportFrames displayed frames.
    pica::PassProfile& pp = pica::passProfile();
    pp.endFrame();
    {
        const pica::MeshDrawStats m = pica::meshFrameStats();
        pica::MeshDrawStats& s = im.meshSum;
        s.draws += m.draws;
        s.vertices += m.vertices;
        s.envDraws += m.envDraws;
        s.textureBinds += m.textureBinds;
        s.stateChanges += m.stateChanges;
        s.mergedDraws += m.mergedDraws;
        s.mergedRecords += m.mergedRecords;
        s.mergeArenaBytes += m.mergeArenaBytes;
        im.culledSum += pica::worldPicaStats().culledRecords;
    }
    if (pp.frames() >= kPassReportFrames) {
        const double n = static_cast<double>(pp.frames());
        const pica::MeshDrawStats& s = im.meshSum;
        AS3D_INFO("AS3D_MESH (per frame, both mesh passes, all eyes) draws=%.1f env=%.1f tex_binds=%.1f states=%.1f "
                  "merged_draws=%.1f merged_records=%.1f merge_kb=%.2f sort=%d merge=%d culled_records=%.1f",
                  s.draws / n, s.envDraws / n, s.textureBinds / n, s.stateChanges / n, s.mergedDraws / n,
                  s.mergedRecords / n, static_cast<double>(s.mergeArenaBytes) / 1024.0 / n, pica::meshStateSort() ? 1 : 0,
                  pica::meshMerge() ? 1 : 0, static_cast<double>(im.culledSum) / n);
        im.meshSum = pica::MeshDrawStats();
        im.culledSum = 0;
    }
    if (pp.frames() >= kPassReportFrames)
        AS3D_INFO("AS3D_PREP_MS (ms/items per frame) %s", pp.reportPrepare(static_cast<double>(SYSCLOCK_ARM11) / 1000.0).c_str());
    if (pp.frames() >= kPassReportFrames && im.wash.enabled()) AS3D_INFO("%s", im.wash.statsLine().c_str());
    if (pp.frames() >= kPassReportFrames && out.eyes == 2) {
        // The second eye from the first eye's commands (eye_replay_pica.h), over the same frames.
        const pica::EyeReplayStats& rs = pica::eyeReplayStats();
        const unsigned replayed = rs.replayed - im.replaySeen.replayed;
        AS3D_INFO("AS3D_EYE_REPLAY enabled=%d eye_register=%d frames=%u armed=%u recorded=%u replayed=%u failed=%u last=%s "
                  "words_per_replay=%.0f replay_ms=%.3f",
                  pica::eyeReplayEnabled() ? 1 : 0, pica::shaders().eyeRegister(), pp.frames(), rs.armed - im.replaySeen.armed,
                  rs.recorded - im.replaySeen.recorded, replayed, rs.failed - im.replaySeen.failed, rs.lastFailure,
                  replayed ? static_cast<double>(rs.words - im.replaySeen.words) / replayed : 0.0,
                  replayed ? static_cast<double>(rs.replayTicks - im.replaySeen.replayTicks) * 1000.0 / SYSCLOCK_ARM11 / replayed : 0.0);
        im.replaySeen = rs;
    }
    if (pp.frames() >= kPassReportFrames) {
        AS3D_INFO("AS3D_PASS_MS eyes=%d (ms/draws/verts per frame) %s", out.eyes,
                  pp.report(static_cast<double>(SYSCLOCK_ARM11) / 1000.0).c_str());
        // The stereo-at-60 knobs over the same frames (docs/3ds-port.md "Stereo at 60").
        const pica::TerrainPicaStats& ts = pica::terrainPicaStats();
        const pica::ShadowPicaStats& ss = pica::shadowPicaStats();
        const pica::MeshMemoryStats ms = pica::meshMemoryStats();
        const size_t coarse = ts.coarseTotal - im.lodCoarseSeen, visible = ts.visibleTotal - im.lodVisibleSeen;
        im.lodCoarseSeen = ts.coarseTotal;
        im.lodVisibleSeen = ts.visibleTotal;
        const size_t switches = ts.lodSwitches >= im.lodSwitchSeen ? ts.lodSwitches - im.lodSwitchSeen : ts.lodSwitches;
        const size_t switchFrames =
            ts.lodSwitchFrames >= im.lodSwitchFramesSeen ? ts.lodSwitchFrames - im.lodSwitchFramesSeen : ts.lodSwitchFrames;
        im.lodSwitchSeen = ts.lodSwitches;
        im.lodSwitchFramesSeen = ts.lodSwitchFrames;
        // The terrain's chunks per eye: those the eye's own frustum sees and those drawn (the union
        // of both eyes' while the left eye records for the replay), per render (R3.3).
        const pica::TerrainEyeChunks& ec = pica::terrainEyeChunks();
        double own[2] = {0, 0}, drawn[2] = {0, 0};
        for (int e = 0; e < 2; ++e) {
            // (the sums restart at a level's build: then from zero)
            const bool restarted = ec.renders[e] < im.eyeChunksSeen.renders[e];
            const size_t renders = restarted ? ec.renders[e] : ec.renders[e] - im.eyeChunksSeen.renders[e];
            if (renders == 0) continue;
            own[e] = static_cast<double>(restarted ? ec.own[e] : ec.own[e] - im.eyeChunksSeen.own[e]) / static_cast<double>(renders);
            drawn[e] = static_cast<double>(restarted ? ec.drawn[e] : ec.drawn[e] - im.eyeChunksSeen.drawn[e]) /
                       static_cast<double>(renders);
        }
        im.eyeChunksSeen = ec;
        AS3D_INFO("AS3D_EYE_CHUNKS eyes=%d own=%.2f/%.2f drawn=%.2f/%.2f (left/right; a replayed right eye draws "
                  "the left's union and renders nothing itself)",
                  out.eyes, own[0], own[1], drawn[0], drawn[1]);
        AS3D_INFO("AS3D_STEREO60 eyes=%d terrain_lod=%d coarse_chunks=%zu/%zu (per terrain render) lod_switches=%zu "
                  "(in %zu frames) shadow_atlas=%d "
                  "pages=%d cells=%d full=%d atlas_kb=%.1f mesh_vertices=%zu->%zu mesh_indices=%zu->%zu",
                  out.eyes, pica::terrainLodActive() ? 1 : 0, coarse, visible, switches, switchFrames,
                  pica::renderQuality().shadowAtlas ? 1 : 0,
                  ss.atlasPages, ss.atlasCells, ss.atlasFull, static_cast<double>(ss.atlasBytes) / 1024.0,
                  ms.sourceVertices, ms.mergedVertices, ms.sourceIndices, ms.keptIndices);
        // R4.1 (docs/3ds-port.md "Stereo GPU budget (round 4, R4.1)"): the GPU cuts over the same frames,
        // and the per-pass GPU timing (gpu_passes) when frames were timed.
        {
            LeanSums& l = im.lean;
            AS3D_INFO("AS3D_GPU_LEAN frames=%u kicks=%u bottom_drawn=%u bottom_skipped=%u "
                      "eye_skipped=%u/%u brightness_skipped=%u far_tev_chunks=%zu brightness_folded=%u fold_overflows=%u",
                      l.frames, pica::gpuKicks() - im.kicksSeen, l.bottomDrawn, l.bottomSkipped, l.eyeSkipped[0],
                      l.eyeSkipped[1], l.brightnessSkipped, ts.farTevChunks, l.brightnessFolded,
                      pica::brightnessFoldOverflows());
            const unsigned leanFrames = std::max(1u, l.frames);
            l = LeanSums();
            im.kicksSeen = pica::gpuKicks();
            // R4.6: the terrain's dynamic-light streams over the same frames (terrain_light_cache);
            // the sums restart at a level's build.
            const pica::TerrainPicaStats& tls = pica::terrainPicaStats();
            Impl::LightSeen& sn = im.lightSeen;
            if (tls.lightChunks < sn.chunks || tls.lightTicks < sn.ticks) sn = Impl::LightSeen();
            const double lightMs =
                static_cast<double>(tls.lightTicks - sn.ticks) * 1000.0 / static_cast<double>(SYSCLOCK_ARM11);
            AS3D_INFO("AS3D_TERRAIN_LIGHTS frames=%u chunks=%zu reused=%zu light_vertex_evals=%zu ms=%.1f per_frame=%.3f "
                      "cache=%d",
                      leanFrames, tls.lightChunks - sn.chunks, tls.lightReuses - sn.reuses, tls.lightEvals - sn.evals,
                      lightMs, lightMs / leanFrames, im.run.terrainLightCache ? 1 : 0);
            sn.chunks = tls.lightChunks;
            sn.reuses = tls.lightReuses;
            sn.evals = tls.lightEvals;
            sn.ticks = tls.lightTicks;
            const std::string gp = pica::gpuPassReport();
            if (!gp.empty()) AS3D_INFO("AS3D_GPU_PASS %s", gp.c_str());
        }
        // Stereo comfort over the same frames (docs/3ds-port.md "Stereo comfort and frame polish").
        im.logStereoSums();
        // The front end's bottom lists: ms, cache units and units rebuilt, draws per flush.
        Impl::ListSums& ls = im.listSum;
        if (ls.frames > 0) {
            std::string parts;
            const char* names[3] = {"hud", "touch", "menu"};
            for (int k = 0; k < 3; ++k) {
                if (!ls.flushes[k]) continue;
                const double f = static_cast<double>(ls.flushes[k]);
                char item[96];
                std::snprintf(item, sizeof item, " %s=%.2fms/%.1f units/%.1f built/%.1f draws (%u flushes)", names[k],
                              ls.ms[k] / f, ls.units[k] / f, ls.built[k] / f, ls.draws[k] / f, ls.flushes[k]);
                parts += item;
            }
            AS3D_INFO("AS3D_UI_LISTS frames=%u%s", ls.frames, parts.c_str());
            // R4.7: the bottom screen's 2D vertices per frame (the HUD tiles replaced the circle
            // band: 8.3k vertices before).
            const double fr = static_cast<double>(ls.frames);
            AS3D_INFO("AS3D_HUD frames=%u bottom2d_vertices=%.0f hud=%.0f touch=%.0f menu=%.0f layout=%s", ls.frames,
                      static_cast<double>(ls.verts[0] + ls.verts[1] + ls.verts[2]) / fr, ls.verts[0] / fr, ls.verts[1] / fr,
                      ls.verts[2] / fr, im.assets && im.assets->hudLayout().pauseTile.w > 0 ? "tiles" : "band");
        }
        ls = Impl::ListSums();
    }
}

void GameFrame::afterFrame(CtrContext& ctx, const FrameInfo& info) {
    Impl& im = *impl_;
    // state_check: L marks the water wash on screen (the log's AS3D_WASH_MARK and the screens);
    // probe_every: a GPU mismatch found by a periodic probe marks the frame the same way
    // (AS3D_AUTO_MARK, the first kMaxAutoMarks of a level start). The file numbers run over the
    // whole launch (a soak's stages do not overwrite each other's marks).
    bool autoMark = pica::stateCheck() && pica::stateCheckTakeAutoMark();
    // The wash detector's first frame of an episode (ctr_wash.h): the same mark.
    if (im.washMarkPending) {
        im.washMarkPending = false;
        autoMark = autoMark || pica::stateCheck();
    }
    // wash_selftest=1: once, 420 frames into a level with water (outside wash_inject's first window), the decoding against a full read-back.
    if (im.run.washSelftest && im.wash.enabled() && im.worldDrawn && info.frame >= 420) {
        ctrapp::WashView v;
        ctrapp::WashScene s;
        const Terrain* terrain = nullptr;
        if (im.washInputs(0, info, &v, &s, &terrain)) im.wash.selfTest(ctx.topTarget(0), v, s);
    }
    if (pica::stateCheck() && ((info.padDown & as3d::pad::L) || autoMark)) {
        static int markFiles = 0;
        char base[32];
        std::snprintf(base, sizeof base, "mark%02d", ++markFiles);
        const int n = pica::stateCheckMark(info.frame, info.step);
        AS3D_INFO("%s n=%d files=%s frame=%u step=%u game=%s mission=%d", autoMark ? "AS3D_AUTO_MARK" : "AS3D_L_MARK", n,
                  base, info.frame, info.step, im.gameKey(), im.mission);
        std::remove((std::string(kDataDir) + "/" + base + "_right.ppm").c_str()); // a mono mark has none
        im.dumpPair(base, info);
        // Is the terrain's depth there where the wash is? (the left eye's depth buffer)
        const bool d = dumpRenderTargetDepth(ctx.topTarget(0), kTopW, std::string(kDataDir) + "/" + base + "_depth.ppm");
        AS3D_INFO("AS3D_DUMP %s_depth depth=%d", base, d ? 1 : 0);
        for (const char* part : {"_top.ppm", "_right.ppm", "_bottom.ppm", "_depth.ppm"})
            noteRunFile(std::string(base) + part);
    }
    // The front end's dumps: every N frames, and once a screen has settled.
    // With dump_from=S: every N simulation steps from step S (the first frame at or after each), named
    // by the step.
    const unsigned every = static_cast<unsigned>(std::max(im.run.dumpEvery, 0));
    if (every > 0 && im.run.dumpFrom > 0) {
        const unsigned from = static_cast<unsigned>(im.run.dumpFrom);
        if (im.nextDumpStep == 0) im.nextDumpStep = from;
        if (info.step >= im.nextDumpStep) {
            char base[64];
            std::snprintf(base, sizeof base, "%ss%05u", im.run.dumpPrefix.c_str(), info.step);
            im.dumpPair(base, info);
            im.nextDumpStep = info.step - (info.step - from) % every + every;
        }
    } else if (every > 0 && info.frame > 0 && info.frame % every == 0) {
        char base[64];
        std::snprintf(base, sizeof base, "%sf%05u", im.run.dumpPrefix.c_str(), info.frame);
        im.dumpPair(base, info);
    }
    if (im.run.dumpScreens && info.screen) {
        constexpr unsigned kSettleFrames = 40; // menus fade and slide in over about half a second
        if (im.screen != info.screen) {
            im.screen = info.screen;
            im.screenDumpFrame = info.frame + kSettleFrames;
            char base[96];
            std::snprintf(base, sizeof base, "%s%02d_%s", im.run.dumpPrefix.c_str(), im.screenSeq++, info.screen);
            im.screenDumpName = base;
        }
        if (!im.screenDumpName.empty() && info.frame >= im.screenDumpFrame) {
            im.dumpPair(im.screenDumpName, info);
            im.screenDumpName.clear();
        }
    }
    if (im.run.dumpAt <= 0 || im.dumped || info.step < static_cast<unsigned>(im.run.dumpAt)) return;
    if (!info.session->hasLevel()) return;
    im.dumped = true;
    const std::string dir = kDataDir;
    const bool l = dumpTarget(ctx.topTarget(0), kTopW, dir + "/top_left.ppm");
    const bool r = im.stereo.eyes == 2 ? dumpTarget(ctx.topTarget(1), kTopW, dir + "/top_right.ppm") : false;
    if (im.stereo.eyes != 2) std::remove((dir + "/top_right.ppm").c_str());
    const bool b = dumpTarget(ctx.bottomTarget(), kBottomW, dir + "/bottom.ppm");
    // Where the player is in each eye, for the parallax check (the central camera's x, and the
    // shift each eye adds: x_ndc(eye) = x_ndc(central, eye position) - B, eyeProjection).
    const World& world = info.session->world();
    const WorldView wv = worldViewOf(world, kAspect);
    float px[2] = {0, 0}, py = 0;
    const int pi = world.playerEntityIndex(0);
    if (pi >= 0) {
        const Vec3 p = world.entity(pi).v3(F_BASE_ORIGIN);
        for (int eye = 0; eye < 2; ++eye) {
            EyeView ev;
            if (im.stereo.eyes == 2) {
                ev.eyeOffset = eye == 0 ? -im.stereo.eyeOffset : im.stereo.eyeOffset;
                ev.focalDistance = im.stereo.focal;
            }
            const Vec4 c = eyeProjection(wv, ev, kAspect) * eyeViewMatrix(wv, ev) * Vec4{p.x, p.y, p.z, 1.0f};
            if (c.w > 1e-6f) {
                px[eye] = (c.x / c.w + 1.0f) * 0.5f * kTopW;
                py = (1.0f - c.y / c.w) * 0.5f * kTopH;
            }
        }
    }
    const float f = 1.0f / std::tan(degToRad(wv.fovY) * 0.5f), a = f / kAspect;
    const float far = im.stereo.eyes == 2 ? static_cast<float>(kTopW) * a * im.stereo.eyeOffset / im.stereo.focal : 0.0f;
    AS3D_INFO("AS3D_DUMP step=%u frame=%u top_left=%d top_right=%d bottom=%d eyes=%d slider=%.2f eye_offset=%.3f "
              "focal=%.1f player_depth=%.1f player_px=(%.1f|%.1f, %.1f) far_disparity_px=%.1f",
              info.step, info.frame, l ? 1 : 0, r ? 1 : 0, b ? 1 : 0, im.stereo.eyes, static_cast<double>(info.slider),
              static_cast<double>(im.stereo.eyeOffset), static_cast<double>(im.stereo.focal),
              static_cast<double>(im.playerDepth), static_cast<double>(px[0]), static_cast<double>(px[1]),
              static_cast<double>(py), static_cast<double>(far));
    // The comfort clamp of this frame (eye_offset above is the effective one): the slider's offset,
    // the target and smoothed ratios, the depths read and the parallax they get (px, effective).
    if (im.stereo.eyes == 2) {
        const pica::SceneDepth& sd = pica::sceneDepth();
        AS3D_INFO("AS3D_DUMP stereo eye_offset_slider=%.3f eye_offset=%.3f comfort_target=%.3f comfort=%.3f "
                  "near_depth=%.1f (terrain %.1f, model %.1f) far_depth=%.1f near_px=%.1f far_px=%.1f limits=%.1f/%.1f",
                  static_cast<double>(im.fullEyeOffset), static_cast<double>(im.stereo.eyeOffset),
                  static_cast<double>(im.comfortTarget), static_cast<double>(im.comfort.ratio),
                  static_cast<double>(im.nearDepth), static_cast<double>(sd.terrainHit ? sd.terrainNear : 0.0f),
                  static_cast<double>(sd.modelNear), static_cast<double>(im.farDepth), static_cast<double>(im.nearPx),
                  static_cast<double>(im.farPx), static_cast<double>(im.run.maxParallaxPx * info.slider),
                  static_cast<double>(im.run.maxFarParallaxPx * info.slider));
    }
    AS3D_INFO("AS3D_DUMP cmdbuf usage %.3f (max so far %.3f), brightness %.2f", static_cast<double>(C3D_GetCmdBufUsage()),
              static_cast<double>(im.cmdUsageMax), static_cast<double>(im.topBrightness));
    const pica::WorldPicaStats& wps = pica::worldPicaStats();
    AS3D_INFO("AS3D_DUMP model records %d, from linear-memory textures %d:%s", wps.records, wps.linearRecords,
              wps.linearSkins.c_str());
}

void GameFrame::end() {
    Impl& im = *impl_;
    im.wash.flush(); // the open wash episode is logged and counted
    AS3D_INFO("AS3D_CMDBUF max usage %.3f of the command list (top eyes, before the kick)", static_cast<double>(im.cmdUsageMax));
    im.logLazyLoads();
    if (im.backend && im.ctx) {
        // Two empty frames: C3D_FrameBegin waits until the GPU is done with every texture and
        // buffer the last frames read.
        for (int k = 0; k < 2; ++k) {
            if (!im.ctx->beginFrame()) break;
            pica::frameBegin();
            pica::frameEnd();
            im.ctx->endFrame();
        }
    }
    im.preview.reset();
    im.banner.reset();
    im.renderer.reset();
    im.assets.reset();
    im.r2d.reset();
    im.r2dBottom.reset();
    im.r2dTopOver.reset();
    im.r2dTouch.reset();
    im.r2dMenu.reset();
    im.menuView.setKeyGlyphs(nullptr);
    im.keyAtlas = ui::KeyGlyphAtlas();
    im.keysTexture = Texture2D();
    if (im.backend) pica::shutdownBackend();
    im.backend = false;
}

} // namespace ctrapp
