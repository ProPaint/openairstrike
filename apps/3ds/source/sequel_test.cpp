// The sequels' render test (sequel_test.h).
#include "sequel_test.h"

#include <3ds.h>
#include <citro3d.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/defs.h"
#include "as3d/game_profile.h"
#include "as3d/input.h"
#include "as3d/precache.h"
#include "as3d/render_rules.h"
#include "as3d/world.h"
#include "as3d/world_render.h"
#include "game_session.h"
#include "gpu_mesh_pica.h"
#include "pica_frame.h"
#include "terrain_pica.h"
#include "texture_budget.h"
#include "water_renderer_pica.h"

using namespace as3d;

namespace sequeltest {
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
    char buf[1024];
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

// The top target read back as an upright 400x240 binary PPM (as water_test.cpp).
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

void logMemory(const char* what) {
    const pica::TextureStats t = pica::textureStats();
    const pica::MeshMemoryStats m = pica::meshMemoryStats();
    say("[mem] %-10s textures VRAM %d = %.1f KB, linear %d = %.1f KB (fallbacks %d, failures %d); meshes %d = %.1f KB "
        "(env %.1f KB, failures %d); linear free %.1f KB, VRAM free %.1f KB\n",
        what, t.vramCount, kb(t.vramBytes), t.linearCount, kb(t.linearBytes), t.vramFallbacks, t.failures, m.meshes,
        kb(m.bytes), kb(m.envBytes), m.failures, kb(linearSpaceFree()), kb(vramSpaceFree()));
}

// The summary lines of as3d_sim (apps/sim_tool/main.cpp), to compare with the desktop.
void logEndState(const World& w, int level, int steps) {
    const WorldStats& st = w.stats();
    say("level %d: %d frames, map_pos %.1f, list entities %d (max %d), slots %d (max %d)\n", level, steps,
        static_cast<double>(w.mapPos()), w.listCount(), st.maxListEntities, w.slotsInUse(), st.maxSlotsInUse);
    say("created %llu, freed %llu, refused %llu, script errors %llu, stalls %llu\n",
        static_cast<unsigned long long>(st.entitiesCreated), static_cast<unsigned long long>(st.entitiesFreed),
        static_cast<unsigned long long>(st.spawnRefused), static_cast<unsigned long long>(st.scriptErrors),
        static_cast<unsigned long long>(st.stalls));
    const int pe = w.playerEntityIndex(0);
    say("kills %d of %d enemies, player origin (%.1f, %.1f), health %.0f, p_scores %.0f\n", w.player(0).kills,
        w.enemiesInLevel(), pe >= 0 ? static_cast<double>(w.entity(pe).f(F_ORIGIN)) : 0.0,
        pe >= 0 ? static_cast<double>(w.entity(pe).f(F_ORIGIN + 1)) : 0.0,
        pe >= 0 ? static_cast<double>(w.entity(pe).f(F_HEALTH)) : 0.0, static_cast<double>(w.player(0).scores));
}

void logFrame(const char* phase, int frame, const WorldRenderStats& ws, double prepareMs) {
    const pica::WaterPicaStats& w = pica::waterPicaStats();
    say("%s frame %d: water %s%s chunks visible %d (draws %d, triangles %d), animated %d chunks %d vertices %.2f KB "
        "arena in %.2f ms, T %.3f base (%.4f, %.4f) shine (%.4f, %.4f), max |dz| %.6f, arena failures %d; prepare %.2f "
        "ms; world draws %d, vertices %d, waterChunks %d, skid trails %d\n",
        phase, frame, w.active ? "on" : "off", w.grid ? " (grid)" : "", w.chunksVisible, w.draws, w.trianglesDrawn,
        w.chunksAnimated, w.verticesAnimated, kb(w.arenaBytes), w.animateMs, static_cast<double>(w.waveTime),
        static_cast<double>(w.uvOffset.x), static_cast<double>(w.uvOffset.y), static_cast<double>(w.shineOffset.x),
        static_cast<double>(w.shineOffset.y), static_cast<double>(w.maxHeightError), w.arenaFailures, prepareMs,
        ws.drawCalls, ws.vertices, ws.waterChunks, ws.skidTrails);
}

} // namespace

void run(const std::string& dir, const std::string& gameKey, int level, int holdFrames, int botFrames, bool precache) {
    g_log = std::fopen((dir + "/sequeltest_log.txt").c_str(), "w");
    if (!std::freopen((dir + "/sequeltest_engine_log.txt").c_str(), "w", stderr)) say("(engine log not redirected)\n");
    setLogLevel(LogLevel::Info);
    bool new3ds = false;
    APT_CheckNew3DS(&new3ds);
    if (new3ds) osSetSpeedupEnable(true);
    if (void* warm = vramAlloc(0x100)) vramFree(warm);

    const GameProfile* game = findGameProfile(gameKey.c_str());
    if (!game) {
        say("unknown game '%s'\n", gameKey.c_str());
        if (g_log) std::fclose(g_log);
        g_log = nullptr;
        return;
    }
    say("PICA sequel render test (M5.1): game %s, mission %d, precache=%d\n", game->key, level, precache ? 1 : 0);
    as3d_game::GameOptions go;
    go.game = game;
    go.extractedDir = dir + "/" + game->key;
    for (const char* const* p = game->paks; *p; ++p) go.paks.push_back(go.extractedDir + "/" + *p);
    go.mission = level;
    go.startLevel = true;
    go.levelFlow = false;
    auto session = std::make_unique<as3d_game::GameSession>();
    std::string err;
    const u64 tl0 = svcGetSystemTick();
    if (!session->init(go, &err)) {
        say("cannot start mission %d: %s\n", level, err.c_str());
        if (g_log) std::fclose(g_log);
        g_log = nullptr;
        return;
    }
    say("mission loaded in %.0f ms\n", ms(svcGetSystemTick() - tl0));

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
    logMemory("start");

    World& world = session->world();
    auto renderer = std::make_unique<WorldRenderer>();
    const u64 tb0 = svcGetSystemTick();
    if (!renderer->init(session->vfs(), session->db(), &err) || !renderer->beginLevel(world, &err)) {
        say("WorldRenderer: %s\n", err.c_str());
        pica::shutdownBackend();
        C3D_RenderTargetDelete(top);
        return;
    }
    const RenderRules& rules = renderer->rules();
    say("beginLevel %.0f ms; rules: water %s, central normals %d, half-texel inset %d, shadow multiply %d, sprite "
        "culling %d, env view normal %d, missing texture white %d, mark entity alpha %d, mark unflipped %d, skid pass %d, "
        "2D half-pixel shift %d\n",
        ms(svcGetSystemTick() - tb0), rules.water == WaterStyle::WaveGrid ? "wave grid" : "flat plane",
        rules.terrainCentralNormals, rules.tileHalfTexelInset, rules.shadowMultiply, rules.spriteCulling,
        rules.envViewNormal, rules.missingTextureWhite, rules.markEntityAlpha, rules.markTextureUnflipped,
        rules.skidMarkPass, rules.ui2dHalfPixelShift);
    {
        const pica::TerrainPicaStats& ts = pica::terrainPicaStats();
        say("terrain: %d chunks, %zu vertices + %zu tile vertices, %.1f KB vertices + %.1f KB indices (linear), %d "
            "textures %.1f KB (VRAM %.1f, linear %.1f)\n",
            ts.chunks, ts.vertices, ts.tileVertices, kb(ts.vertexBytes), kb(ts.indexBytes), ts.textures,
            kb(ts.textureBytes), kb(ts.textureVramBytes), kb(ts.textureLinearBytes));
        const pica::WaterPicaStats& w = pica::waterPicaStats();
        say("water: %s %s, grid %dx%d cells, %d chunks, %d wet cells, %d grid vertices (the GL buffer: %.1f KB), %d "
            "used (static %.1f KB), %d indices (%.1f KB); base %dx%d %.1f KB %s %s, shine %dx%d %.1f KB %s, missing %d\n",
            w.active ? "active" : "inactive", w.grid ? "wave grid" : "plane", w.gridWidth, w.gridHeight, w.gridChunks,
            w.wetCells, w.gridVertices, kb(static_cast<size_t>(w.gridVertices) * sizeof(WaterGridVertex)),
            w.usedVertices, kb(w.staticBytes), w.indices, kb(w.indexBytes), w.textureWidth, w.textureHeight,
            kb(w.textureBytes), w.baseTrueColor ? "RGBA8" : "RGB565", w.textureVram ? "VRAM" : "linear", w.shineWidth,
            w.shineHeight, kb(w.shineBytes), w.shineVram ? "VRAM" : "linear", w.missingTextures);
    }
    logMemory("beginLevel");
    if (precache) {
        const u64 t0 = svcGetSystemTick();
        PrecacheReport r = world.precacheScripts();
        r += renderer->precache(world, session->db(), {});
        say("precache %.0f ms: %s\n", ms(svcGetSystemTick() - t0), r.str().c_str());
        logMemory("precache");
    }

    const int kSteps = botFrames > 0 ? botFrames : 600;
    const u64 ts0 = svcGetSystemTick();
    for (int f = 0; f < kSteps; ++f) session->step(botInput(static_cast<u32>(f)));
    say("stepped %d bot frames in %.0f ms\n", kSteps, ms(svcGetSystemTick() - ts0));
    logEndState(world, level, kSteps);

    Viewport vp;
    vp.width = kW;
    vp.height = kH;
    WorldRenderOptions terrainOnly;
    terrainOnly.models = terrainOnly.lights = terrainOnly.particles = terrainOnly.sprites = terrainOnly.shadows = false;
    terrainOnly.marks = terrainOnly.water = terrainOnly.lightning = false;
    WorldRenderOptions waterOnly = terrainOnly;
    waterOnly.water = true;
    const WorldRenderOptions full;
    struct Phase {
        const char* name;
        WorldRenderOptions options;
    };
    const Phase phases[4] = {{"t", terrainOnly}, {"w", waterOnly}, {"f", full}, {"w2", waterOnly}};
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
        if (ph.name[1] == '2') { // 30 more frames: the waves and the scroll must have moved
            for (int f = 0; f < 30; ++f) session->step(botInput(static_cast<u32>(kSteps + f)));
            say("stepped to %d bot frames: map_pos %.1f, list entities %d\n", kSteps + 30,
                static_cast<double>(world.mapPos()), world.listCount());
        }
        for (int frame = 0; frame < 3; ++frame) {
            if (!aptMainLoop()) {
                quit = true;
                break;
            }
            const u64 p0 = svcGetSystemTick();
            renderer->prepare(world, vp, ph.options);
            const double prepMs = ms(svcGetSystemTick() - p0);
            drawFrame();
            logFrame(ph.name, frame, renderer->lastStats(), prepMs);
        }
        if (quit) break;
        drawFrame();
        const std::string ppm = dir + "/sequeltest_" + game->key + "_" + ph.name + ".ppm";
        say("readback %s: %s\n", ppm.c_str(), dumpTarget(top, ppm) ? "ok" : "FAILED");
        double gpu = 0.0, cpu = 0.0;
        for (int k = 0; k < 10; ++k) {
            drawFrame();
            gpu += C3D_GetDrawingTime();
            cpu += C3D_GetProcessingTime();
        }
        say("phase %s: draw: C3D cpu %.2f ms, GPU %.2f ms (Azahar: a constant)\n", ph.name, cpu / 10, gpu / 10);
        for (int k = 0; k < hold && !quit; ++k) {
            if (!aptMainLoop()) quit = true;
            hidScanInput();
            if (hidKeysDown() & KEY_START) quit = true;
            if (!quit) drawFrame();
        }
    }
    say("sequeltest: done; arena high water %u B, failures %u\n", static_cast<unsigned>(pica::frameArena().highWater()),
        pica::frameArena().failures());
    logMemory("end");
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

} // namespace sequeltest
