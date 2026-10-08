// The PICA shadow test (shadow_test.h): the PICA WorldRenderer with its shadow pass
// (engine/src/render/pica/shadow_pica.h, shadow_renderer_pica.cpp, world_renderer_pica.cpp).
#include "shadow_test.h"

#include <3ds.h>
#include <citro3d.h>

#include <cstdarg>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/game_profile.h"
#include "as3d/input.h"
#include "as3d/precache.h"
#include "as3d/world.h"
#include "as3d/world_render.h"
#include "game_session.h"
#include "pica_frame.h"
#include "shadow_pica.h"
#include "texture_budget.h"

using namespace as3d;

namespace shadowtest {
namespace {

#define ST_TRANSFER_FLAGS                                                                               \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |                  \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |     \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))
#define ST_READBACK_FLAGS                                                                               \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |                  \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |    \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

constexpr int kW = 400, kH = 240;

std::FILE* g_log = nullptr;

void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void say(const char* fmt, ...) {
    char buf[768];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::fputs(buf, stdout);
    if (g_log) {
        std::fputs(buf, g_log);
        std::fflush(g_log);
    }
}

double kb(size_t b) { return static_cast<double>(b) / 1024.0; }
double ms(u64 ticks) { return static_cast<double>(ticks) / (SYSCLOCK_ARM11 / 1000.0); }

// The top target read back as an upright 400x240 binary PPM (as mesh_test.cpp).
bool dumpTarget(C3D_RenderTarget* t, const std::string& path) {
    const size_t n = 240 * 400;
    auto* buf = static_cast<u32*>(linearAlloc(n * 4));
    if (!buf) return false;
    C3D_SyncDisplayTransfer(static_cast<u32*>(t->frameBuf.colorBuf), GX_BUFFER_DIM(240, 400), buf,
                            GX_BUFFER_DIM(240, 400), ST_READBACK_FLAGS);
    GSPGPU_InvalidateDataCache(buf, n * 4);
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        linearFree(buf);
        return false;
    }
    std::fprintf(f, "P6\n%d %d\n255\n", kW, kH);
    std::vector<u8> row(kW * 3);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const u32 v = buf[static_cast<size_t>(x) * 240 + static_cast<size_t>(239 - y)];
            row[static_cast<size_t>(x) * 3 + 0] = static_cast<u8>(v >> 24);
            row[static_cast<size_t>(x) * 3 + 1] = static_cast<u8>(v >> 16);
            row[static_cast<size_t>(x) * 3 + 2] = static_cast<u8>(v >> 8);
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    linearFree(buf);
    return true;
}

void logTextures(const char* what) {
    const pica::TextureStats t = pica::textureStats();
    say("[mem] %-12s textures VRAM %d = %.1f KB, linear %d = %.1f KB (fallbacks %d, failures %d); linear free %.1f "
        "KB, VRAM free %.1f KB\n",
        what, t.vramCount, kb(t.vramBytes), t.linearCount, kb(t.linearBytes), t.vramFallbacks, t.failures,
        kb(linearSpaceFree()), kb(vramSpaceFree()));
}

void logFrame(const char* phase, int frame, const WorldRenderStats& ws, double prepareMs) {
    const pica::ShadowPicaStats& s = pica::shadowPicaStats();
    say("%s frame %d: shadows %d (silhouettes %d, blobs %d, late %d), draped vertices %d, shadow draws %d, arena "
        "%.1f KB; prepare %.2f ms (shadow placement %.2f, of which draping %.2f for %d not cached, besides "
        "generation); world draws %d, vertices %d; silhouettes held %d\n",
        phase, frame, ws.shadows, s.drawn, s.blobs, s.late, s.vertices, s.draws, kb(s.arenaBytes), prepareMs, s.placeMs,
        s.buildMs, s.drapedNow, ws.drawCalls, ws.vertices, s.generated);
    if (s.late > 0) {
        const pica::ShadowGenInfo g = pica::lastShadowGenInfo();
        say("  late silhouette: %dx%d (texture %dx%d), %.1f KB, %.1f ms%s\n", g.width, g.height, g.texWidth,
            g.texHeight, kb(g.bytes), g.ms, g.skinAlpha ? ", skin alpha" : "");
    }
}

} // namespace

void run(const std::string& dir, int holdFrames, bool precache, bool allRotations) {
    g_log = std::fopen((dir + "/shadowtest_log.txt").c_str(), "w");
    if (!std::freopen((dir + "/shadowtest_engine_log.txt").c_str(), "w", stderr)) say("(engine log not redirected)\n");
    setLogLevel(LogLevel::Info);
    say("PICA shadow test (M3.1), precache=%d shadow_rotations=%d\n", precache ? 1 : 0, allRotations ? 1 : 0);
    pica::setShadowPrecacheRotations(allRotations);
    bool new3ds = false;
    APT_CheckNew3DS(&new3ds);
    if (new3ds) osSetSpeedupEnable(true);
    if (void* warm = vramAlloc(0x100)) vramFree(warm);

    const GameProfile* game = findGameProfile("as3d");
    as3d_game::GameOptions go;
    go.game = game;
    go.extractedDir = dir + "/" + game->key;
    for (const char* const* p = game->paks; *p; ++p) go.paks.push_back(go.extractedDir + "/" + *p);
    go.mission = 1;
    go.startLevel = true;
    go.levelFlow = false;
    auto session = std::make_unique<as3d_game::GameSession>();
    std::string err;
    if (!session->init(go, &err)) {
        say("cannot start mission 1: %s\n", err.c_str());
        if (g_log) std::fclose(g_log);
        g_log = nullptr;
        return;
    }

    C3D_RenderTarget* top = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    if (!top) {
        say("cannot create the top target\n");
        return;
    }
    C3D_RenderTargetSetOutput(top, GFX_TOP, GFX_LEFT, ST_TRANSFER_FLAGS);
    if (!pica::initBackend()) {
        say("pica::initBackend failed\n");
        C3D_RenderTargetDelete(top);
        return;
    }
    logTextures("start");

    // Level start as the composer (ctr_frame.cpp): beginLevel, then the precache.
    World& world = session->world();
    auto renderer = std::make_unique<WorldRenderer>();
    if (!renderer->init(session->vfs(), session->db(), &err) || !renderer->beginLevel(world, &err)) {
        say("WorldRenderer: %s\n", err.c_str());
        pica::shutdownBackend();
        C3D_RenderTargetDelete(top);
        return;
    }
    logTextures("beginLevel");
    say("blob texture %dx%d A8, %.1f KB\n", pica::kShadowBlobSide, pica::kShadowBlobSide,
        kb(pica::shadowPicaStats().blobBytes));
    if (precache) {
        const u64 t0 = svcGetSystemTick();
        PrecacheReport r = world.precacheScripts();
        r += renderer->precache(world, session->db(), {});
        const double total = ms(svcGetSystemTick() - t0);
        const pica::ShadowPicaStats& s = pica::shadowPicaStats();
        say("precache %.0f ms: %s\n", total, r.str().c_str());
        say("silhouettes: %d generated, %d failed, %d capped to %d; %.1f KB of A8 textures (linear memory); "
            "generation %.0f ms in all, %.2f ms each, max %.1f ms\n",
            s.generated, s.failed, s.capped, pica::kMaxShadowSide, kb(s.textureBytes), s.generateMs,
            s.generated > 0 ? s.generateMs / s.generated : 0.0, s.maxGenerateMs);
        say("silhouette sizes (larger side): <=8 %d, 16 %d, 32 %d, 64 %d, 128 %d, 256 %d, 512 %d, 1024 %d\n",
            s.bySide[0], s.bySide[1], s.bySide[2], s.bySide[3], s.bySide[4], s.bySide[5], s.bySide[6], s.bySide[7]);
        logTextures("precache");
    }

    const int kSteps = 120;
    const u64 ts0 = svcGetSystemTick();
    for (int f = 0; f < kSteps; ++f) session->step(botInput(static_cast<u32>(f)));
    say("mission 1 after %d bot frames (%.0f ms): map_pos %.1f, list entities %d\n", kSteps,
        ms(svcGetSystemTick() - ts0), static_cast<double>(world.mapPos()), world.listCount());

    Viewport vp;
    vp.width = kW;
    vp.height = kH;
    WorldRenderOptions shadowsOnly;
    shadowsOnly.models = shadowsOnly.lights = shadowsOnly.particles = shadowsOnly.sprites = false;
    WorldRenderOptions terrainOnly = shadowsOnly;
    terrainOnly.shadows = false;
    const WorldRenderOptions full;
    struct Phase {
        const char* name;
        WorldRenderOptions options;
    };
    const Phase phases[3] = {{"t", terrainOnly}, {"a", shadowsOnly}, {"b", full}};
    const int hold = holdFrames > 0 ? holdFrames : 60;
    bool quit = false;
    auto drawFrame = [&]() {
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        pica::frameBegin();
        pica::drawOn(pica::FrameTarget{top, kW, kH, true});
        renderer->draw(EyeView{});
        pica::frameEnd();
        C3D_FrameEnd(0);
    };
    for (const Phase& ph : phases) {
        if (quit) break;
        // Prepare and draw until every shadow has its silhouette (late path: one per frame).
        int frame = 0;
        for (;; ++frame) {
            if (!aptMainLoop()) {
                quit = true;
                break;
            }
            const u64 p0 = svcGetSystemTick();
            renderer->prepare(world, vp, ph.options);
            const double prepMs = ms(svcGetSystemTick() - p0);
            drawFrame();
            const WorldRenderStats& ws = renderer->lastStats();
            const pica::ShadowPicaStats& s = pica::shadowPicaStats();
            if (frame < 4 || s.blobs == 0 || frame % 20 == 0) logFrame(ph.name, frame, ws, prepMs);
            if (!precache && frame < 2 && ph.options.shadows) {
                const std::string ppm = dir + "/shadowtest_" + ph.name + "_late" + std::to_string(frame) + ".ppm";
                say("readback %s: %s\n", ppm.c_str(), dumpTarget(top, ppm) ? "ok" : "FAILED");
            }
            if (s.blobs == 0 || frame >= 2000) break;
        }
        if (quit) break;
        drawFrame();
        const std::string ppm = dir + "/shadowtest_" + ph.name + ".ppm";
        say("readback %s: %s\n", ppm.c_str(), dumpTarget(top, ppm) ? "ok" : "FAILED");
        // The prepare cost of the shadows: the same frame prepared with and without them.
        double with = 0.0, without = 0.0;
        WorldRenderOptions off = ph.options;
        off.shadows = false;
        const int kRep = 20;
        for (int k = 0; k < kRep; ++k) {
            u64 t0 = svcGetSystemTick();
            renderer->prepare(world, vp, off);
            without += ms(svcGetSystemTick() - t0);
            t0 = svcGetSystemTick();
            renderer->prepare(world, vp, ph.options);
            with += ms(svcGetSystemTick() - t0);
        }
        drawFrame();
        double gpu = 0.0, cpu = 0.0;
        for (int k = 0; k < 10; ++k) {
            drawFrame();
            gpu += C3D_GetDrawingTime();
            cpu += C3D_GetProcessingTime();
        }
        say("phase %s: prepare %.2f ms with shadows, %.2f ms without (shadows %.2f ms of CPU per frame); draw: C3D "
            "cpu %.2f ms, GPU %.2f ms (Azahar: a constant)\n",
            ph.name, with / kRep, without / kRep, (with - without) / kRep, cpu / 10, gpu / 10);
        say("shadowtest: showing %s\n", ph.name);
        for (int k = 0; k < hold && !quit; ++k) {
            if (!aptMainLoop()) quit = true;
            hidScanInput();
            if (hidKeysDown() & KEY_START) quit = true;
            if (!quit) drawFrame();
        }
    }
    say("shadowtest: done; silhouettes held %d, late %d; arena high water %u B, failures %u\n",
        renderer->shadowMapCount(), renderer->lateShadowMaps(), static_cast<unsigned>(pica::frameArena().highWater()),
        pica::frameArena().failures());
    logTextures("end");
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    pica::frameBegin();
    pica::frameEnd();
    C3D_FrameEnd(0);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C3D_FrameEnd(0);
    renderer.reset();
    pica::shutdownBackend();
    C3D_RenderTargetDelete(top);
    session.reset();
    if (g_log) std::fclose(g_log);
    g_log = nullptr;
}

} // namespace shadowtest
