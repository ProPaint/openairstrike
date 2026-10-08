// The PICA ground-mark test (mark_test.h): the PICA WorldRenderer with its mark and skid pass
// (engine/src/render/pica/ground_marks_pica.h, world_renderer_pica.cpp).
#include "mark_test.h"

#include <3ds.h>
#include <citro3d.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/game_profile.h"
#include "as3d/input.h"
#include "as3d/defs.h"
#include "as3d/precache.h"
#include "as3d/skid_render.h"
#include "as3d/world.h"
#include "as3d/world_render.h"
#include "game_session.h"
#include "ground_marks_pica.h"
#include "pica_frame.h"
#include "texture_budget.h"

using namespace as3d;

namespace markstest {
namespace {

#define MT_TRANSFER_FLAGS                                                                               \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |                  \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |     \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))
#define MT_READBACK_FLAGS                                                                               \
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
                            GX_BUFFER_DIM(240, 400), MT_READBACK_FLAGS);
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
    const pica::MarkPicaStats& m = pica::markPicaStats();
    say("%s frame %d: marks %d (dropped %d, vertices %d%s), skid trails %d (segments %d, vertices %d), textures %d = "
        "%.1f KB, draws %d, arena %.1f KB, build %.2f ms; prepare %.2f ms; world draws %d, vertices %d; scene marks %d, "
        "rebuilds %d\n",
        phase, frame, m.marks, m.marksDropped, m.markVertices, m.rebuilt ? ", rebuilt" : "", m.skidTrails,
        m.skidSegments, m.skidVertices, m.textures, kb(m.textureBytes), m.draws, kb(m.arenaBytes), m.buildMs, prepareMs,
        ws.drawCalls, ws.vertices, ws.marks, m.rebuilds);
}

} // namespace

void run(const std::string& dir, int holdFrames, int botFrames, bool precache) {
    g_log = std::fopen((dir + "/markstest_log.txt").c_str(), "w");
    if (!std::freopen((dir + "/markstest_engine_log.txt").c_str(), "w", stderr)) say("(engine log not redirected)\n");
    setLogLevel(LogLevel::Info);
    say("PICA ground mark test (M3.2), precache=%d\n", precache ? 1 : 0);
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
    C3D_RenderTargetSetOutput(top, GFX_TOP, GFX_LEFT, MT_TRANSFER_FLAGS);
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
    if (precache) {
        const u64 t0 = svcGetSystemTick();
        PrecacheReport r = world.precacheScripts();
        r += renderer->precache(world, session->db(), {});
        const double total = ms(svcGetSystemTick() - t0);
        say("precache %.0f ms: %s\n", total, r.str().c_str());
        logTextures("precache");
    }

    std::string skidSkin;
    if (const PrecacheSet* set = world.precacheSet()) {
        std::string names;
        int n = 0;
        for (const ObjectDef* def : set->objects) {
            if (def->type != ObjectType::Mark) continue;
            ++n;
            names += " " + def->name + "(" + def->skin + ")";
            if (skidSkin.empty() && !def->skin.empty()) skidSkin = def->skin;
        }
        say("mark definitions of the closure: %d:%s\n", n, names.c_str());
    }

    std::vector<SkidTrail> synthetic;
    const int kSteps = botFrames > 0 ? botFrames : 1400;
    const u64 ts0 = svcGetSystemTick();
    for (int f = 0; f < kSteps; ++f) session->step(botInput(static_cast<u32>(f)));
    say("mission 1 after %d bot frames (%.0f ms): map_pos %.1f, list entities %d\n", kSteps,
        ms(svcGetSystemTick() - ts0), static_cast<double>(world.mapPos()), world.listCount());

    {
        const int pi = world.playerEntityIndex(0);
        const Vec3 o = pi >= 0 ? world.entity(pi).v3(F_ORIGIN) : Vec3{0.0f, 0.0f, 0.0f};
        for (int t = 0; t < 2; ++t) {
            SkidTrail tr;
            tr.texture = skidSkin;
            for (int k = 0; k < 23; ++k) {
                SkidNode n;
                const float fk = static_cast<float>(k);
                const float x = o.x - 120.0f + 90.0f * static_cast<float>(t) + (t ? 6.0f * std::sin(fk * 0.4f) * fk : 0.0f);
                n.position = Vec2{x, o.y - 100.0f + 9.0f * fk};
                n.direction = Vec2{0.0f, 1.0f};
                n.width = 16.0f;
                n.age = 0.4f * (22.0f - fk); // the oldest section is the oldest
                n.distance = 9.0f * fk;
                tr.nodes.push_back(n);
            }
            synthetic.push_back(tr);
        }
    }
    Viewport vp;
    vp.width = kW;
    vp.height = kH;
    // t: terrain only; m: terrain + marks + skid trails; f: everything.
    WorldRenderOptions terrainOnly;
    terrainOnly.models = terrainOnly.lights = terrainOnly.particles = terrainOnly.sprites = terrainOnly.shadows = false;
    terrainOnly.marks = terrainOnly.water = terrainOnly.lightning = false; // desktop "terrain" lists no water since M3.5
    WorldRenderOptions marksOnly = terrainOnly;
    marksOnly.marks = true;
    const WorldRenderOptions full;
    struct Phase {
        const char* name;
        WorldRenderOptions options;
    };
    const Phase phases[4] = {{"t", terrainOnly}, {"m", marksOnly}, {"s", marksOnly}, {"f", full}};
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
        if (ph.name[0] == 's') pica::setTestSkidTrails(&synthetic);
        else pica::setTestSkidTrails(nullptr);
        // A few frames so the world is settled (the first draw of each phase), then the dump.
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
        const std::string ppm = dir + "/markstest_" + ph.name + ".ppm";
        say("readback %s: %s\n", ppm.c_str(), dumpTarget(top, ppm) ? "ok" : "FAILED");
        // The prepare cost of the marks: the same frame prepared with and without them (each
        // measured in its own run of frames: toggling the set would cut the marks every frame).
        WorldRenderOptions off = ph.options;
        off.marks = false;
        const int kRep = 20;
        double without = 0.0, with = 0.0;
        for (int k = 0; k < kRep; ++k) {
            const u64 t0 = svcGetSystemTick();
            renderer->prepare(world, vp, off);
            without += ms(svcGetSystemTick() - t0);
        }
        for (int k = 0; k < kRep; ++k) {
            const u64 t0 = svcGetSystemTick();
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
        say("phase %s: prepare %.2f ms with marks, %.2f ms without (marks %.2f ms of CPU per frame, the cut set is "
            "kept between frames); draw: C3D cpu %.2f ms, GPU %.2f ms (Azahar: a constant)\n",
            ph.name, with / kRep, without / kRep, (with - without) / kRep, cpu / 10, gpu / 10);
        say("markstest: showing %s\n", ph.name);
        for (int k = 0; k < hold && !quit; ++k) {
            if (!aptMainLoop()) quit = true;
            hidScanInput();
            if (hidKeysDown() & KEY_START) quit = true;
            if (!quit) drawFrame();
        }
    }
    say("markstest: done; arena high water %u B, failures %u\n", static_cast<unsigned>(pica::frameArena().highWater()),
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

} // namespace markstest
