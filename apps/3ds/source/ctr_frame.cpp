// The frame composer (ctr_frame.h).
#include "ctr_frame.h"

#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/load_stats.h"
#include "as3d/precache.h"
#include "as3d/render_state.h"
#include "as3d/touch_controls.h"
#include "as3d/ui.h"
#include "as3d/world.h"
#include "as3d/world_render.h"
#include "ctr_context.h"
#include "flat_fill.h"
#include "game_session.h"
#include "pica_frame.h"
#include "texture_budget.h"
#include "ui_pica.h"

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

} // namespace

struct GameFrame::Impl {
    RunConfig run;
    CtrContext* ctx = nullptr;
    as3d_game::GameSession* session = nullptr;
    bool backend = false;
    std::unique_ptr<WorldRenderer> renderer;
    std::unique_ptr<ui::Renderer2D> r2d;
    std::unique_ptr<ui::UiAssets> assets;
    bool hudReady = false;
    bool precached = false;
    int mission = 0;
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

    float maxEyeOffset() const { return run.eyeOffset > 0.0f ? run.eyeOffset : kMaxEyeOffset; }
    float minFocal() const { return run.minFocal > 0.0f ? run.minFocal : kMinFocalDistance; }

    // A standalone frame of the loading screen: a progress bar on both screens (and both eyes),
    // "LOADING" once the font is there. At most every 100 ms (each frame waits for a vblank).
    void loadingFrame(float progress, bool force = false) {
        const uint64_t now = svcGetSystemTick();
        if (!force && lastLoadingFrame && now - lastLoadingFrame < SYSCLOCK_ARM11 / 10) return;
        lastLoadingFrame = now;
        if (!ctx->beginFrame()) return;
        pica::frameBegin();
        const float p = std::clamp(progress, 0.0f, 1.0f);
        auto screen = [&](C3D_RenderTarget* t, int w, int h, bool depth) {
            pica::drawOn(pica::FrameTarget{t, w, h, depth});
            clear(Vec4{0.02f, 0.03f, 0.06f, 1.0f}, depth, 1.0f);
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
        for (int eye = 0; eye < 2; ++eye) screen(ctx->topTarget(eye), kTopW, kTopH, true);
        screen(ctx->bottomTarget(), kBottomW, kBottomH, false);
        pica::frameEnd();
        ctx->endFrame();
    }
};

GameFrame::GameFrame(const RunConfig& run) : impl_(new Impl) { impl_->run = run; }
GameFrame::~GameFrame() = default;

bool GameFrame::begin(CtrContext& ctx, as3d_game::GameSession& session) {
    Impl& im = *impl_;
    im.ctx = &ctx;
    im.session = &session;
    im.mission = session.mission();
    if (!pica::initBackend()) {
        AS3D_ERROR("pica::initBackend failed");
        return false;
    }
    im.backend = true;
    if (void* warm = vramAlloc(0x100)) vramFree(warm);
    im.loadingFrame(0.0f, true);
    const uint64_t t0 = svcGetSystemTick();
    auto ms = [](uint64_t a, uint64_t b) { return static_cast<double>(b - a) * 1000.0 / SYSCLOCK_ARM11; };

    World& world = session.world();
    std::string err;
    im.renderer.reset(new WorldRenderer());
    if (!im.renderer->init(session.vfs(), session.db(), &err)) {
        AS3D_ERROR("WorldRenderer::init: %s", err.c_str());
        return false;
    }
    // The terrain first: its textures are the first VRAM tenants.
    if (!im.renderer->beginLevel(world, &err)) {
        AS3D_ERROR("WorldRenderer::beginLevel: %s", err.c_str());
        return false;
    }
    const uint64_t t1 = svcGetSystemTick();
    im.loadingFrame(0.3f, true);

    // The 2D layer: the font and the HUD atlases (optional, as on the desktop).
    im.r2d.reset(new ui::Renderer2D());
    im.assets.reset(new ui::UiAssets());
    std::string hudError;
    const GameId game = session.game() ? session.game()->id : GameId::AirStrike3D;
    im.assets->hudTarget = ui::HudTarget::Bottom320x240;
    im.hudReady = im.r2d->init(&hudError) && im.assets->load(session.vfs(), &hudError, game);
    if (!im.hudReady) AS3D_WARN("HUD unavailable: %s", hudError.c_str());
    im.loadingFrame(0.4f, true);
    const uint64_t t2 = svcGetSystemTick();

    if (im.run.precache && session.hasLevel()) {
        PrecacheReport r = world.precacheScripts();
        im.loadingFrame(0.5f, true);
        r += im.renderer->precache(world, session.db(), [&im](float p) { im.loadingFrame(0.5f + 0.45f * p); });
        const LoadCounts before = LoadStats::lazyLoads();
        if (im.hudReady) im.assets->precachePlay();
        const LoadCounts d = LoadStats::lazyLoads() - before;
        r.textures += static_cast<int>(d.of(LoadKind::Texture));
        r.bytes += d.bytes;
        AS3D_INFO("AS3D_PRECACHE mission=%d %s", im.mission, r.str().c_str());
        // From here on every load is a lazy one (AS3D_LAZY_LOADS at the end).
        LoadStats::reset();
        im.precached = true;
    }
    const uint64_t t3 = svcGetSystemTick();
    const pica::TextureStats ts = pica::textureStats();
    AS3D_INFO("level start: beginLevel %.0f ms, UI %.0f ms, precache %.0f ms; textures VRAM %.1f KB (%d), linear "
              "%.1f KB (%d), VRAM fallbacks %d, failures %d; linear free %.1f KB",
              ms(t0, t1), ms(t1, t2), ms(t2, t3), kb(ts.vramBytes), ts.vramCount, kb(ts.linearBytes), ts.linearCount,
              ts.vramFallbacks, ts.failures, kb(linearSpaceFree()));
    AS3D_INFO("stereo: eye offset %.2f at slider 1, focal distance >= %.1f (the camera-to-player depth)",
              static_cast<double>(im.maxEyeOffset()), static_cast<double>(im.minFocal()));
    im.loadingFrame(1.0f, true);
    return true;
}

void GameFrame::prepare(const FrameInfo& info) {
    Impl& im = *impl_;
    const uint64_t t0 = svcGetSystemTick();
    as3d_game::GameSession& ses = *info.session;
    const World& world = ses.world();
    im.showHud = ses.hasLevel() && !(world.intermission() || world.levelComplete() || world.gameOver());
    if (im.hudReady) im.hud = as3d_game::hudStateOf(ses);

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

    // The top screen's 2D layer, built once and flushed on each eye at screen depth.
    im.topQuads = false;
    if (im.hudReady && (info.paused || im.run.perf)) {
        im.r2d->begin(kTopW, kTopH);
        ui::TextStyle st;
        if (info.paused) {
            st.scale = 3.0f;
            st.color = ui::orange();
            ui::drawText(*im.r2d, im.assets->uiFont(), 290, 260, "PAUSED", st);
        }
        if (im.run.perf) {
            char line[96];
            std::snprintf(line, sizeof line, "cpu %.1f gpu %.1f prep %.1f", static_cast<double>(info.lastCpuMs),
                          static_cast<double>(info.lastGpuMs), static_cast<double>(im.prepareMs));
            st.scale = 1.0f;
            st.color = ui::Color{1, 1, 1, 0.8f};
            ui::drawText(*im.r2d, im.assets->uiFont(), 12, 572, line, st);
        }
        im.topQuads = true;
    }
    im.prepareMs = static_cast<float>(static_cast<double>(svcGetSystemTick() - t0) * 1000.0 / SYSCLOCK_ARM11);
}

void GameFrame::drawTop(CtrContext& ctx, int eye, const FrameInfo&) {
    Impl& im = *impl_;
    if (eye == 0) {
        ui::ui2dPicaResetStats();
        pica::frameBegin();
    }
    pica::drawOn(pica::FrameTarget{ctx.topTarget(eye), kTopW, kTopH, true});
    EyeView ev;
    if (im.stereo.eyes == 2) {
        ev.eyeOffset = eye == 0 ? -im.stereo.eyeOffset : im.stereo.eyeOffset;
        ev.focalDistance = im.stereo.focal;
    }
    im.renderer->draw(ev); // pass 0 clears to the level's clear colour
    if (im.topQuads) im.r2d->flush();
    // Pass 14 (0.5 is neutral: dst * 0.5 + 0.5 * dst).
    if (im.run.brightness != 0.5f) im.renderer->drawBrightness(kTopW, kTopH, im.run.brightness);
}

void GameFrame::drawBottom(CtrContext& ctx, const FrameInfo& info) {
    Impl& im = *impl_;
    pica::drawOn(pica::FrameTarget{ctx.bottomTarget(), kBottomW, kBottomH, false});
    clear(Vec4{0, 0, 0, 1}, false, 1.0f);
    if (im.hudReady) {
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
    out.eyes = im.stereo.eyes;
    out.draws = ws.drawCalls + us.runs;
    out.verts = ws.vertices + us.vertices;
    const pica::TextureStats ts = pica::textureStats();
    out.texVramKb = static_cast<unsigned>(ts.vramBytes / 1024);
    out.texLinearKb = static_cast<unsigned>(ts.linearBytes / 1024);
    out.lazyLoads = im.precached ? static_cast<unsigned>(LoadStats::lazyLoads().total()) : 0u;
    out.arenaKb = static_cast<unsigned>(im.arenaUsed / 1024);
}

void GameFrame::afterFrame(CtrContext& ctx, const FrameInfo& info) {
    Impl& im = *impl_;
    if (im.run.dumpAt <= 0 || im.dumped || info.step < static_cast<unsigned>(im.run.dumpAt)) return;
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
}

void GameFrame::end() {
    Impl& im = *impl_;
    if (im.precached) AS3D_INFO("AS3D_LAZY_LOADS mission=%d %s", im.mission, LoadStats::lazyLoads().str().c_str());
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
    im.renderer.reset();
    im.assets.reset();
    im.r2d.reset();
    if (im.backend) pica::shutdownBackend();
    im.backend = false;
}

} // namespace ctrapp
