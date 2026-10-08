// The frame composer (ctr_frame.h).
#include "ctr_frame.h"

#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/frontend.h"
#include "as3d/load_stats.h"
#include "as3d/precache.h"
#include "as3d/render_state.h"
#include "as3d/scene.h"
#include "as3d/touch_controls.h"
#include "as3d/ui.h"
#include "as3d/world.h"
#include "as3d/world_render.h"
#include "ctr_context.h"
#include "flat_fill.h"
#include "game_flow.h"
#include "game_session.h"
#include "mesh_renderer_pica.h"
#include "model_view.h"
#include "pica_frame.h"
#include "texture_budget.h"
#include "ui_pica.h"
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
bool dumpTarget(C3D_RenderTarget* t, int width, const std::string& path) {
    const size_t bytes = static_cast<size_t>(240) * static_cast<size_t>(width) * 4;
    auto* lin = static_cast<u8*>(linearAlloc(bytes));
    if (!lin) return false;
    C3D_SyncDisplayTransfer(static_cast<u32*>(t->frameBuf.colorBuf), GX_BUFFER_DIM(240, width),
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

double kb(size_t b) { return static_cast<double>(b) / 1024.0; }

// The sequels' model view on the top screen (docs/3ds-port.md M5.2): the desktop draws it on its
// bottom screen inside the view's virtual rectangle, mapped at 0.4 px per virtual pixel; the top
// screen has nothing else to show while the helicopter selection is open, so the rectangle is
// drawn there at twice that, 0.8 (460 x 270 -> 368 x 216 px), centred, over the selection's dot
// grid at the same mapping. The desktop's dual-screen frame at --size 800x960 has the same
// mapping on its bottom screen (the comparison of the report).
constexpr float kModelPxPerVirtual = 0.8f;

} // namespace

struct GameFrame::Impl {
    RunConfig run;
    CtrContext* ctx = nullptr;
    as3d_game::GameSession* session = nullptr;
    bool backend = false;
    std::unique_ptr<WorldRenderer> renderer;
    std::unique_ptr<ui::Renderer2D> r2d;       // the top screen's 2D list (built once, flushed per eye)
    std::unique_ptr<ui::Renderer2D> r2dTopOver; // the top screen's list over the model view (its static fill)
    std::unique_ptr<ui::Renderer2D> r2dBottom; // the front end: the bottom screen's list, built in prepare
    std::unique_ptr<ui::UiAssets> assets;
    bool hudReady = false;
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
    int modelX = 0, modelY = 0, modelW = 0, modelH = 0; // the viewport, top screen pixels from the top left
    float topBrightness = 0.6f;
    // The frame.
    StereoParams stereo;
    float playerDepth = 0.0f;
    float prepareMs = 0.0f;
    ui::HudState hud;
    bool showHud = false;
    bool topQuads = false;
    size_t arenaUsed = 0;
    bool dumped = false;
    uint64_t lastLoadingFrame = 0;
    // dump_screens: the screen of the last frame, the pending dump.
    std::string screen;
    int screenSeq = 0;
    unsigned screenDumpFrame = 0;
    std::string screenDumpName;

    float maxEyeOffset() const { return run.eyeOffset > 0.0f ? run.eyeOffset : kMaxEyeOffset; }
    float minFocal() const { return run.minFocal > 0.0f ? run.minFocal : kMinFocalDistance; }

    // A standalone frame of the loading screen on both screens (and both eyes). The direct
    // modes: a progress bar, "LOADING" once the font is there. The front end: the original's
    // loading screen (ui::drawLoadingScreen, built before the frame opens: its picture may load
    // on first use) with the bar along the bottom edge. At most every 100 ms (each frame waits
    // for a vblank, which also means the GPU is done with every earlier frame).
    void loadingFrame(float progress, bool force = false, bool intermission = false) {
        const uint64_t now = svcGetSystemTick();
        if (!force && lastLoadingFrame && now - lastLoadingFrame < SYSCLOCK_ARM11 / 10) return;
        lastLoadingFrame = now;
        const float p = std::clamp(progress, 0.0f, 1.0f);
        const bool original = flow && hudReady;
        if (original) {
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
                if (intermission) return; // the attract level: black, as the desktop
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
        screen(ctx->bottomTarget(), kBottomW, kBottomH, false, r2dBottom.get());
        pica::frameEnd();
        ctx->endFrame();
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

    bool initBackend() {
        if (!pica::initBackend()) {
            AS3D_ERROR("pica::initBackend failed");
            return false;
        }
        backend = true;
        if (void* warm = vramAlloc(0x100)) vramFree(warm);
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
        assets.reset(new ui::UiAssets());
        std::string hudError;
        const GameId game = ses.game() ? ses.game()->id : GameId::AirStrike3D;
        assets->hudTarget = ui::HudTarget::Bottom320x240;
        hudReady = r2d->init(&hudError) && r2dBottom->init(&hudError) && r2dTopOver->init(&hudError) && assets->load(ses.vfs(), &hudError, game);
        if (!hudReady) AS3D_WARN("HUD unavailable: %s", hudError.c_str());
    }

    // The level-start precache (run.ini precache=1), then every later load counts as lazy.
    void precacheLevel(as3d_game::GameSession& ses, float from, float to) {
        if (!run.precache || !ses.hasLevel()) return;
        World& world = ses.world();
        PrecacheReport r = world.precacheScripts();
        loadingFrame(from, true);
        r += renderer->precache(world, ses.db(), [this, from, to](float q) { loadingFrame(from + (to - from) * q); });
        const LoadCounts before = LoadStats::lazyLoads();
        if (hudReady) assets->precachePlay();
        // The front end: the level's sounds (GameStack wires the view to its AudioBridge).
        if (precacheSounds && *precacheSounds && world.precacheSet()) (*precacheSounds)(*world.precacheSet());
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
    }

    void logLazyLoads() {
        if (precached) AS3D_INFO("AS3D_LAZY_LOADS mission=%d %s", mission, LoadStats::lazyLoads().str().c_str());
        precached = false;
    }

    // The front end's level start (FlowView::beginLevel).
    bool flowBeginLevel(as3d_game::GameSession& ses, std::string* error) {
        session = &ses;
        const bool intermission = ses.hasLevel() && ses.world().intermission();
        loadingFrame(0.2f, true, intermission); // also: the GPU is done with the last level's frames
        logLazyLoads();
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
            if (!makeRenderer(ses)) {
                if (error) *error = "renderer";
                return false;
            }
            loadUi(ses);
            loadingFrame(0.25f, true, intermission);
        }
        levelBegun = false;
        std::string err;
        if (!renderer->beginLevel(ses.world(), &err)) {
            AS3D_ERROR("WorldRenderer::beginLevel: %s", err.c_str());
            if (error) *error = err;
            return false;
        }
        levelBegun = true;
        const uint64_t t1 = svcGetSystemTick();
        loadingFrame(0.3f, true, intermission);
        precacheLevel(ses, 0.35f, 0.95f);
        const uint64_t t3 = svcGetSystemTick();
        logLevelStart(t0, t1, t1, t3);
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
        if (l.hud) ui::drawHud(r, *assets, l.hudState);
        if (info && info->playing && info->touch && l.hud) {
            ui::TouchControlsState ts;
            ts.assets = assets.get();
            ts.player = &l.hudState.players[0];
            ts.rules = &ses.rules();
            ui::drawTouchControls(r, *info->touch, ts);
        }
        // frontend.md 4.1: the menus after the HUD. The main menu's 3D banner (between the two
        // halves on the desktop) is the title on the top screen here; the sequels' model view
        // (between the halves as well) is prepared for the top screen (setModel).
        if (ui::Frontend* fe = l.frontend) {
            fe->drawUnder(r, *assets);
            if (fe->modelViewVisible()) fe->drawModelViews();
            fe->drawOver(r, *assets);
        }
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
            title = fe && !modelActive && (fe->bannerVisible() || (!worldDrawn && fe->state() != ui::FrontendState::Intro));
        }
        if (!paused && !title && !modelActive && !run.perf) return;
        r2d->begin(kTopW, kTopH);
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
        if (paused) {
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
        const bool b = dumpTarget(ctx->bottomTarget(), kBottomW, dir + base + "_bottom.ppm");
        AS3D_INFO("AS3D_DUMP %s frame=%u step=%u screen=%s top=%d bottom=%d", base.c_str(), fi.frame, fi.step,
                  fi.screen, t ? 1 : 0, b ? 1 : 0);
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

void GameFrame::loadingScreen(float progress, bool intermission) {
    if (!impl_->ctx || !impl_->backend) return;
    impl_->loadingFrame(progress, progress >= 1.0f, intermission);
}

bool GameFrame::begin(CtrContext& ctx, as3d_game::GameSession& session) {
    Impl& im = *impl_;
    im.ctx = &ctx;
    im.session = &session;
    im.mission = session.mission();
    if (!im.initBackend()) return false;
    im.loadingFrame(0.0f, true);
    const uint64_t t0 = svcGetSystemTick();

    World& world = session.world();
    std::string err;
    if (!im.makeRenderer(session)) return false;
    // The terrain first: its textures are the first VRAM tenants.
    if (!im.renderer->beginLevel(world, &err)) {
        AS3D_ERROR("WorldRenderer::beginLevel: %s", err.c_str());
        return false;
    }
    im.levelBegun = true;
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
    im.loadingFrame(1.0f, true);
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

    im.stereo = StereoParams();
    if (im.worldDrawn) {
        const World& world = ses.world();
        // Stereo: zero parallax at the player's depth.
        const WorldView wv = worldViewOf(world, kAspect);
        im.playerDepth = im.minFocal();
        const int pi = world.playerEntityIndex(0);
        if (pi >= 0) {
            const Vec3 p = world.entity(pi).v3(F_BASE_ORIGIN);
            const Vec4 v = wv.view * Vec4{p.x, p.y, p.z, 1.0f};
            if (-v.z > 0.0f) im.playerDepth = -v.z;
        }
        im.stereo = stereoFor(info.eyes == 2 ? info.slider : 0.0f, im.playerDepth, im.maxEyeOffset(), im.minFocal());

        Viewport vp;
        vp.width = kTopW;
        vp.height = kTopH;
        if (im.stereo.eyes == 2) {
            vp.maxEyeOffset = im.stereo.eyeOffset;
            vp.minFocalDistance = im.stereo.focal;
        }
        im.renderer->prepare(world, vp);
    }

    // The model view: zero parallax at the model's distance.
    if (im.modelActive)
        im.stereo = stereoFor(info.eyes == 2 ? info.slider : 0.0f, -im.modelView.origin[2], im.maxEyeOffset(), im.minFocal());

    // The top screen's 2D layer, built once and flushed on each eye at screen depth.
    im.buildTopQuads(info);
    im.prepareMs = static_cast<float>(static_cast<double>(svcGetSystemTick() - t0) * 1000.0 / SYSCLOCK_ARM11);
}

void GameFrame::drawTop(CtrContext& ctx, int eye, const FrameInfo&) {
    Impl& im = *impl_;
    if (eye == 0) {
        ui::ui2dPicaResetStats();
        pica::frameBegin();
    }
    pica::drawOn(pica::FrameTarget{ctx.topTarget(eye), kTopW, kTopH, true});
    if (im.worldDrawn) {
        EyeView ev;
        if (im.stereo.eyes == 2) {
            ev.eyeOffset = eye == 0 ? -im.stereo.eyeOffset : im.stereo.eyeOffset;
            ev.focalDistance = im.stereo.focal;
        }
        im.renderer->draw(ev); // pass 0 clears to the level's clear colour
    } else {
        clear(Vec4{0, 0, 0, 1}, true, 1.0f);
    }
    if (im.topQuads) im.r2d->flush();
    if (im.modelActive) {
        im.drawModelEye(eye);
        im.r2dTopOver->flush();
    }
    // Pass 14 (0.5 is neutral: dst * 0.5 + 0.5 * dst).
    if (im.worldDrawn && im.topBrightness != 0.5f) im.renderer->drawBrightness(kTopW, kTopH, im.topBrightness);
}

void GameFrame::drawBottom(CtrContext& ctx, const FrameInfo& info) {
    Impl& im = *impl_;
    pica::drawOn(pica::FrameTarget{ctx.bottomTarget(), kBottomW, kBottomH, false});
    clear(Vec4{0, 0, 0, 1}, false, 1.0f);
    if (im.hudReady && im.flow) {
        if (im.haveLayers) im.r2dBottom->flush();
    } else if (im.hudReady) {
        const World& world = info.session->world();
        im.assets->hudTarget = ui::HudTarget::Bottom320x240;
        ui::Renderer2D& r = *im.r2d;
        if (im.showHud) {
            r.begin(kBottomW, kBottomH);
            ui::drawHud(r, *im.assets, im.hud);
            r.flush();
        }
        // The hint box over the HUD (render-pipeline.md 1.1 pass 13).
        if (info.session->hasLevel() && world.hintShowing()) {
            r.begin(kBottomW, kBottomH);
            ui::drawHint(r, *im.assets, world.hintText());
            r.flush();
        }
        if (info.touch && im.showHud) {
            ui::TouchControlsState ts;
            ts.assets = im.assets.get();
            ts.player = &im.hud.players[0];
            ts.rules = &world.rules();
            r.begin(kBottomW, kBottomH);
            ui::drawTouchControls(r, *info.touch, ts);
            r.flush();
        }
    }
    im.arenaUsed = pica::frameArena().used();
    pica::frameEnd();
}

void GameFrame::stats(FrameStats& out) {
    Impl& im = *impl_;
    const WorldRenderStats& ws = im.renderer->lastStats();
    const ui::Ui2dPicaStats& us = ui::ui2dPicaStats();
    out.prepareMs = im.prepareMs;
    out.eyes = im.worldDrawn || im.modelActive ? im.stereo.eyes : 1;
    out.draws = (im.worldDrawn ? ws.drawCalls : 0) + (im.modelActive ? im.modelPass.lastStats().draws : 0) + us.runs;
    out.verts = (im.worldDrawn ? ws.vertices : 0) + us.vertices;
    const pica::TextureStats ts = pica::textureStats();
    out.texVramKb = static_cast<unsigned>(ts.vramBytes / 1024);
    out.texLinearKb = static_cast<unsigned>(ts.linearBytes / 1024);
    out.lazyLoads = im.precached ? static_cast<unsigned>(LoadStats::lazyLoads().total()) : 0u;
    out.arenaKb = static_cast<unsigned>(im.arenaUsed / 1024);
}

void GameFrame::afterFrame(CtrContext& ctx, const FrameInfo& info) {
    Impl& im = *impl_;
    // The front end's dumps: every N frames, and once a screen has settled.
    if (im.run.dumpEvery > 0 && info.frame > 0 && info.frame % static_cast<unsigned>(im.run.dumpEvery) == 0) {
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
    const pica::WorldPicaStats& wps = pica::worldPicaStats();
    AS3D_INFO("AS3D_DUMP model records %d, from linear-memory textures %d:%s", wps.records, wps.linearRecords,
              wps.linearSkins.c_str());
}

void GameFrame::end() {
    Impl& im = *impl_;
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
    im.renderer.reset();
    im.assets.reset();
    im.r2d.reset();
    im.r2dBottom.reset();
    im.r2dTopOver.reset();
    if (im.backend) pica::shutdownBackend();
    im.backend = false;
}

} // namespace ctrapp
