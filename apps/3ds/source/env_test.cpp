// The PICA environment map test (env_test.h). The records come from the neutral entity pass
// (WorldScene), as in mesh_test.cpp.
#include "env_test.h"

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
#include "as3d/render_rules.h"
#include "as3d/render_state.h"
#include "as3d/scene.h"
#include "as3d/world.h"
#include "as3d/world_render.h"
#include "game_session.h"
#include "gpu_mesh_pica.h"
#include "mesh_renderer_pica.h"
#include "pica_frame.h"
#include "texture_budget.h"
#include "world_scene.h"

using namespace as3d;

namespace envtest {
namespace {

#define ET_TRANSFER_FLAGS                                                                               \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |                  \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |     \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))
#define ET_READBACK_FLAGS                                                                               \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |                  \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |    \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

constexpr int kW = 400, kH = 240;
constexpr float kAspect = 400.0f / 240.0f;

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
                            GX_BUFFER_DIM(240, 400), ET_READBACK_FLAGS);
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
    const pica::MeshMemoryStats m = pica::meshMemoryStats();
    const pica::TextureStats t = pica::textureStats();
    say("[mem] %-10s meshes %d = %.1f KB linear (env normal streams %d = %.1f KB; peak %.1f KB, failures %d); "
        "textures VRAM %d = %.1f KB, linear %d = %.1f KB; linear free %.1f KB, VRAM free %.1f KB\n",
        what, m.meshes, kb(m.bytes), m.envMeshes, kb(m.envBytes), kb(m.peakBytes), m.failures, t.vramCount,
        kb(t.vramBytes), t.linearCount, kb(t.linearBytes), kb(linearSpaceFree()), kb(vramSpaceFree()));
}

bool envMapped(const MeshRecord& r) {
    const Material& m = *r.material;
    return m.envModeRaw >= 1 && m.envModeRaw <= 3 && m.envTexture && m.envTexture->valid() &&
           r.mesh->model.smoothNormals;
}

const char* envName(int mode) {
    switch (mode) {
    case 1: return "glitter";
    case 2: return "chrome";
    case 3: return "quad";
    default: return "none";
    }
}

struct Subset {
    const char* name;
    std::vector<const MeshRecord*> recs;
    pica::MeshPass opaque, rest;
};

// only=f: the full frame through the PICA WorldRenderer (as water_test.cpp's phase f).
void runFull(const std::string& dir, as3d_game::GameSession& session, C3D_RenderTarget* top, int hold, int kSteps) {
    World& world = session.world();
    std::string err;
    auto renderer = std::make_unique<WorldRenderer>();
    if (!renderer->init(session.vfs(), session.db(), &err) || !renderer->beginLevel(world, &err)) {
        say("WorldRenderer: %s\n", err.c_str());
        return;
    }
    {
        const u64 t0 = svcGetSystemTick();
        PrecacheReport r = world.precacheScripts();
        r += renderer->precache(world, session.db(), {});
        say("precache %.0f ms: %s\n", ms(svcGetSystemTick() - t0), r.str().c_str());
    }
    logMemory("precache");
    const u64 ts0 = svcGetSystemTick();
    for (int f = 0; f < kSteps; ++f) session.step(botInput(static_cast<u32>(f)));
    say("mission 1 after %d bot frames (%.0f ms): map_pos %.1f, list entities %d\n", kSteps,
        ms(svcGetSystemTick() - ts0), static_cast<double>(world.mapPos()), world.listCount());
    Viewport vp;
    vp.width = kW;
    vp.height = kH;
    const WorldRenderOptions full;
    auto drawFrame = [&]() {
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        pica::frameBegin();
        pica::drawOn(pica::FrameTarget{top, kW, kH, true});
        renderer->draw(EyeView{});
        const pica::MeshDrawStats st = pica::meshFrameStats();
        pica::frameEnd();
        C3D_FrameEnd(0);
        return st;
    };
    pica::MeshDrawStats st;
    for (int k = 0; k < 3; ++k) {
        renderer->prepare(world, vp, full);
        st = drawFrame();
    }
    const std::string ppm = dir + "/envtest_f.ppm";
    say("readback %s: %s\n", ppm.c_str(), dumpTarget(top, ppm) ? "ok" : "FAILED");
    const WorldRenderStats& ws = renderer->lastStats();
    say("test f: world draws %d, vertices %d; mesh draws %d (env %d), mesh vertices %d, mesh bytes %.1f KB\n",
        ws.drawCalls, ws.vertices, st.draws, st.envDraws, st.vertices, kb(st.bytes));
    say("envtest: showing f\n");
    bool quit = false;
    for (int k = 0; k < hold && !quit; ++k) {
        if (!aptMainLoop()) quit = true;
        hidScanInput();
        if (hidKeysDown() & KEY_START) quit = true;
        if (!quit) drawFrame();
    }
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    pica::frameBegin();
    pica::frameEnd();
    C3D_FrameEnd(0);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C3D_FrameEnd(0);
    renderer.reset();
}

} // namespace

void run(const std::string& dir, int holdFrames, int botFrames, const std::string& only) {
    g_log = std::fopen((dir + "/envtest_log.txt").c_str(), "w");
    if (!std::freopen((dir + "/envtest_engine_log.txt").c_str(), "w", stderr)) say("(engine log not redirected)\n");
    setLogLevel(LogLevel::Info);
    say("PICA environment map test (M3.4)\n");
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
    const int kSteps = botFrames > 0 ? botFrames : 120;
    const int hold = holdFrames > 0 ? holdFrames : 60;
    C3D_RenderTarget* top = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    if (!top) {
        say("cannot create the top target\n");
        return;
    }
    C3D_RenderTargetSetOutput(top, GFX_TOP, GFX_LEFT, ET_TRANSFER_FLAGS);
    if (!pica::initBackend()) {
        say("pica::initBackend failed\n");
        C3D_RenderTargetDelete(top);
        return;
    }
    logMemory("start");
    if (only.find('f') != std::string::npos) {
        runFull(dir, *session, top, hold, kSteps);
        say("envtest: done; arena high water %u B, failures %u\n",
            static_cast<unsigned>(pica::frameArena().highWater()), pica::frameArena().failures());
        logMemory("freed");
        pica::shutdownBackend();
        C3D_RenderTargetDelete(top);
        session.reset();
        if (g_log) std::fclose(g_log);
        g_log = nullptr;
        return;
    }
    for (int f = 0; f < kSteps; ++f) session->step(botInput(static_cast<u32>(f)));
    const World& world = session->world();
    say("mission 1 after %d bot frames: map_pos %.1f, list entities %d\n", kSteps, static_cast<double>(world.mapPos()),
        world.listCount());

    // Everything that creates textures or meshes runs before the first frame.
    auto cache = std::make_unique<ResourceCache>(session->vfs());
    auto ws = std::make_unique<WorldScene>();
    ws->init(*cache, session->vfs(), session->db());
    {
        const u64 t0 = svcGetSystemTick();
        const PrecacheReport r = ws->precache(world, session->db(), {}, WorldScene::PrecacheHooks{});
        say("precache %.0f ms: %s\n", ms(svcGetSystemTick() - t0), r.str().c_str());
    }
    logMemory("mission 1");
    Viewport vp;
    vp.width = kW;
    vp.height = kH;
    WorldRenderOptions o;
    o.sprites = o.particles = o.shadows = false;
    WorldRenderStats wstats;
    ws->prepare(world, vp, o, false, false, wstats);
    const WorldScene::Frame& fr = ws->frame();
    const RenderRules& rules = renderRulesFor(world.rules());
    const int player = world.playerEntityIndex(0);
    say("records: opaque %d, trans %d, effect %d; dynamic lights %d; time %.3f; env view normal %d\n",
        static_cast<int>(ws->opaque.size()), static_cast<int>(ws->trans.size()), static_cast<int>(ws->effect.size()),
        static_cast<int>(ws->lights.size()), static_cast<double>(fr.time), rules.envViewNormal ? 1 : 0);

    Subset subsets[3];
    subsets[0].name = "a";
    subsets[1].name = "b";
    subsets[2].name = "c";
    int envRecords = 0;
    for (const auto* l : {&ws->opaque, &ws->trans, &ws->effect}) {
        for (const MeshRecord& r : *l) {
            const bool onPlayer = r.entity == player || rootOfEntity(world, r.entity) == player;
            const bool env = envMapped(r);
            if (env) {
                ++envRecords;
                say("env record: entity %d (%s, root %d), mode %s, skin '%s', mesh %u vertices\n", r.entity,
                    onPlayer ? "player" : "other", rootOfEntity(world, r.entity), envName(r.material->envModeRaw),
                    r.material->texturePath.c_str(), static_cast<unsigned>(r.mesh->buffers()->vertexCount));
            }
            if (onPlayer) subsets[0].recs.push_back(&r);
            if (env && !onPlayer) subsets[1].recs.push_back(&r);
            subsets[2].recs.push_back(&r);
        }
    }
    say("env records %d; test a %d records, b %d, c %d\n", envRecords, static_cast<int>(subsets[0].recs.size()),
        static_cast<int>(subsets[1].recs.size()), static_cast<int>(subsets[2].recs.size()));
    for (Subset& s : subsets) {
        s.opaque.begin(fr.light, ws->lights.data(), ws->lights.size());
        s.opaque.setEnvironment(fr.wv.view, fr.time, rules.envViewNormal);
        s.rest.begin(fr.light, ws->lights.data(), ws->lights.size());
        s.rest.setEnvironment(fr.wv.view, fr.time, rules.envViewNormal);
        for (const MeshRecord* r : s.recs)
            (r->material->sortBucket == SortBucket::Opaque ? s.opaque : s.rest)
                .submit(*r->mesh, *r->material, r->model, r->colour);
        s.opaque.finish();
        s.rest.finish();
    }

    auto drawFrame = [&](Subset& s) {
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        pica::frameBegin();
        pica::drawOn(pica::FrameTarget{top, kW, kH, true});
        clear(Vec4{fr.clearColour.x, fr.clearColour.y, fr.clearColour.z, 1.0f}, true, 1.0f);
        pica::setDepthMapping(FogDepthMap::WBuffer, fr.wv.nearPlane);
        pica::setLevelFogColor(fr.light.fogColor);
        if (fr.hasFog) pica::setFog(fr.wv.nearPlane, fr.wv.farPlane, fr.light.fogStart, fr.light.fogEnd);
        else pica::disableFog();
        const Mat4 viewProj = pica::picaEyeProjection(fr.wv, EyeView{}, kAspect) * fr.wv.view;
        s.opaque.draw(viewProj);
        s.rest.draw(viewProj);
        const pica::MeshDrawStats st = pica::meshFrameStats();
        pica::frameEnd();
        C3D_FrameEnd(0);
        return st;
    };

    bool quit = false;
    for (Subset& s : subsets) {
        if (quit) break;
        pica::MeshDrawStats st;
        for (int k = 0; k < 3; ++k) st = drawFrame(s);
        const std::string ppm = dir + "/envtest_" + s.name + ".ppm";
        say("readback %s: %s\n", ppm.c_str(), dumpTarget(top, ppm) ? "ok" : "FAILED");
        double gpu = 0.0, cpu = 0.0;
        for (int k = 0; k < 10; ++k) {
            drawFrame(s);
            gpu += C3D_GetDrawingTime();
            cpu += C3D_GetProcessingTime();
        }
        say("test %s: draws %d (env %d), vertices %d, triangles %d, bytes %.1f KB; C3D cpu %.2f ms, GPU %.2f ms "
            "(Azahar: a constant)\n",
            s.name, st.draws, st.envDraws, st.vertices, st.triangles, kb(st.bytes), cpu / 10, gpu / 10);
        say("envtest: showing %s\n", s.name);
        for (int k = 0; k < hold && !quit; ++k) {
            if (!aptMainLoop()) quit = true;
            hidScanInput();
            if (hidKeysDown() & KEY_START) quit = true;
            if (!quit) drawFrame(s);
        }
    }
    say("envtest: done; arena high water %u B, failures %u\n", static_cast<unsigned>(pica::frameArena().highWater()),
        pica::frameArena().failures());
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    pica::frameBegin();
    pica::frameEnd();
    C3D_FrameEnd(0);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C3D_FrameEnd(0);
    ws.reset();
    cache.reset();
    logMemory("freed");
    pica::shutdownBackend();
    C3D_RenderTargetDelete(top);
    session.reset();
    if (g_log) std::fclose(g_log);
    g_log = nullptr;
}

} // namespace envtest
