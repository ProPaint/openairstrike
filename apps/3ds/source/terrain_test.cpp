// The terrain test (terrain_test.h).
#include "terrain_test.h"

#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "as3d/defs.h"
#include "as3d/dynamic_lights.h"
#include "as3d/game_profile.h"
#include "as3d/input.h"
#include "as3d/render_rules.h"
#include "as3d/render_state.h"
#include "as3d/terrain_render.h"
#include "as3d/vfs.h"
#include "as3d/world.h"
#include "as3d/world_render.h"
#include "pica_frame.h"
#include "pica_space.h"
#include "shader_set.h"
#include "terrain_pica.h"
#include "tev.h"
#include "texture_budget.h"

using namespace as3d;

namespace terraintest {
namespace {

#define TT_TRANSFER_FLAGS                                                                               \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |                  \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |     \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

std::FILE* g_log = nullptr;

void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void say(const char* fmt, ...) {
    char buf[512];
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

// worldViewOf (engine/src/render/gles/world_render.cpp; the PICA WorldRenderer of M1.8 gets its
// own copy or the function moves to eye_view.cpp): the world camera, far = max(fog end, 1000)
// with fog, else 2000.
WorldView cameraOf(const World& world, float aspect) {
    const CameraState& c = world.camera();
    WorldView v;
    v.eye = Vec3{c.field[15], c.field[16], c.field[17]};
    v.fovY = c.field[14] > 1.0f ? c.field[14] : 60.0f;
    v.farPlane = 2000.0f;
    if (const Terrain* t = world.terrain()) {
        if (t->style().hasFog) v.farPlane = std::max(t->style().fogFar, 1000.0f);
    }
    v.view = rotationX(c.field[18]) * rotationY(c.field[19]) * rotationZ(c.field[20]) * translation(-v.eye);
    v.projection = perspective(v.fovY, aspect, v.nearPlane, v.farPlane);
    return v;
}

// Pixel space on the tilted top target (as pica_test).
Mat4 pixelMatrix() { return pica::picaTiltMatrix() * ortho(0.0f, 400.0f, 240.0f, 0.0f, -1.0f, 1.0f); }

void solid(float x0, float y0, float x1, float y1, u8 r, u8 g, u8 b) {
    auto* v = pica::frameArena().allocArray<pica::FlatVertex>(6);
    if (!v) return;
    const float xs[6] = {x0, x1, x1, x0, x1, x0}, ys[6] = {y1, y1, y0, y1, y0, y0};
    for (int i = 0; i < 6; ++i) {
        v[i].pos[0] = xs[i];
        v[i].pos[1] = ys[i];
        v[i].pos[2] = 0.0f;
        v[i].uv[0] = v[i].uv[1] = 0.0f;
        v[i].rgba[0] = v[i].rgba[1] = v[i].rgba[2] = v[i].rgba[3] = 255;
    }
    pica::applyTev(pica::tevConstantColor(r, g, b, 255));
    pica::ShaderSet& sh = pica::shaders();
    sh.bind(pica::Shader::Flat);
    sh.setMvp(pixelMatrix());
    sh.setVertices(v);
    C3D_DrawArrays(GPU_TRIANGLES, 0, 6);
}

// The corner markers of pica_test (tools/3ds_picatest_check.py finds the screen by them).
void drawMarkers() {
    pica::disableFog();
    setBlend(GlBlend::Off);
    setDepth(false, false);
    setCull(CullMode::Off);
    solid(0, 0, 6, 6, 250, 10, 130);
    solid(394, 0, 400, 6, 10, 250, 130);
    solid(0, 234, 6, 240, 130, 10, 250);
}

// The top target's colour buffer, untiled by a display transfer into linear memory, written as
// <dir>/terraintest_<n>.ppm (400x240 upright). Outside a frame only (synchronous GX transfer).
// The raw target is 240x400 (rotated, bytes A B G R per pixel); its row x is screen column x,
// its column y counted from the bottom.
void dumpTarget(C3D_RenderTarget* t, const std::string& path) {
    const size_t bytes = 240 * 400 * 4;
    auto* lin = static_cast<u8*>(linearAlloc(bytes));
    if (!lin) return;
    C3D_SyncDisplayTransfer(static_cast<u32*>(t->frameBuf.colorBuf), GX_BUFFER_DIM(240, 400), reinterpret_cast<u32*>(lin),
                            GX_BUFFER_DIM(240, 400),
                            GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
                                GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                                GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    GSPGPU_InvalidateDataCache(lin, bytes);
    if (std::FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fprintf(f, "P6\n400 240\n255\n");
        std::vector<u8> row(400 * 3);
        for (int y = 0; y < 240; ++y) {
            for (int x = 0; x < 400; ++x) {
                const u8* px = lin + (static_cast<size_t>(x) * 240 + static_cast<size_t>(239 - y)) * 4;
                row[static_cast<size_t>(x) * 3 + 0] = px[3];
                row[static_cast<size_t>(x) * 3 + 1] = px[2];
                row[static_cast<size_t>(x) * 3 + 2] = px[1];
            }
            std::fwrite(row.data(), 1, row.size(), f);
        }
        std::fclose(f);
    }
    linearFree(lin);
}

constexpr int kPhases = 6;
const char* kPhaseNames[kPhases] = {"plain", "crater", "lights", "grazing", "grazing-notiles", "grazing-nobias"};

} // namespace

void run(Vfs& vfs, const DefDatabase& db, const GameProfile& game, const std::string& dir, int level, int holdFrames) {
    g_log = std::fopen((dir + "/terraintest_log.txt").c_str(), "w");
    const int hold = holdFrames > 0 ? holdFrames : 300;
    say("Terrain test (M1.4): mission %d, %d frames per phase\n", level, hold);

    WorldConfig wc;
    wc.rules = &game.rules;
    auto world = std::make_unique<World>();
    world->init(vfs, db, wc);
    std::string err;
    if (!world->loadLevel(std::to_string(level), &err)) {
        say("mission %d: load failed: %s\n", level, err.c_str());
        if (g_log) std::fclose(g_log);
        return;
    }
    for (u32 f = 0; f < 120; ++f) world->step(botInput(f).toPlayerInput());
    const Terrain* terrain = world->terrain();
    if (!terrain) {
        say("no terrain\n");
        if (g_log) std::fclose(g_log);
        return;
    }

    if (void* warm = vramAlloc(0x100)) vramFree(warm);
    C3D_RenderTarget* top = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    if (!top) {
        say("cannot create the top target\n");
        if (g_log) std::fclose(g_log);
        return;
    }
    C3D_RenderTargetSetOutput(top, GFX_TOP, GFX_LEFT, TT_TRANSFER_FLAGS);
    if (!pica::initBackend()) {
        say("pica::initBackend failed\n");
        C3D_RenderTargetDelete(top);
        if (g_log) std::fclose(g_log);
        return;
    }

    const size_t linear0 = linearSpaceFree();
    const u32 vram0 = vramSpaceFree();
    const u64 tb0 = svcGetSystemTick();
    auto tr = std::make_unique<TerrainRenderer>();
    if (!tr->build(*terrain, vfs, &err, renderRules(game.id))) {
        say("TerrainRenderer::build failed: %s\n", err.c_str());
        pica::shutdownBackend();
        C3D_RenderTargetDelete(top);
        if (g_log) std::fclose(g_log);
        return;
    }
    const double buildMs = static_cast<double>(svcGetSystemTick() - tb0) / (SYSCLOCK_ARM11 / 1000.0);
    {
        const pica::TerrainPicaStats& st = pica::terrainPicaStats();
        say("map %d x %d cells: %d chunks, %zu vertices, %d tile sets, %d tile quads (%zu vertices)\n", terrain->width(),
            terrain->height(), st.chunks, st.vertices, st.tileSets, st.tileQuads, st.tileVertices);
        say("vertex buffers %.1f KB, index + zero stream %.1f KB (linear)\n", kb(st.vertexBytes), kb(st.indexBytes));
        say("textures: %d, %.1f KB (VRAM %.1f KB, linear %.1f KB), missing %d\n", st.textures, kb(st.textureBytes),
            kb(st.textureVramBytes), kb(st.textureLinearBytes), tr->missingTextures());
        say("build %.0f ms; linear free dropped %.1f KB, VRAM free dropped %.1f KB\n", buildMs,
            kb(linear0 - linearSpaceFree()), kb(vram0 - vramSpaceFree()));
        const pica::TextureStats ts = pica::textureStats();
        say("texture budget: vram %.1f KB (%d), linear %.1f KB (%d), fallbacks %d, failures %d\n", kb(ts.vramBytes),
            ts.vramCount, kb(ts.linearBytes), ts.linearCount, ts.vramFallbacks, ts.failures);
        say("overlay depth offset %g\n", static_cast<double>(pica::terrainOverlayDepthOffset()));
    }

    const float aspect = 400.0f / 240.0f;
    const WorldView wv = cameraOf(*world, aspect);
    say("camera eye (%.1f, %.1f, %.1f), fov %.1f, far %.0f\n", static_cast<double>(wv.eye.x),
        static_cast<double>(wv.eye.y), static_cast<double>(wv.eye.z), static_cast<double>(wv.fovY),
        static_cast<double>(wv.farPlane));
    const TerrainStyle& style = terrain->style();
    const bool hasFog = style.hasFog;
    const Vec3 fogColour{style.fogColor[0], style.fogColor[1], style.fogColor[2]};

    // The ground point a bit below the screen centre (crater and lights go there): the camera
    // ray through NDC (0, -0.3) marched to the terrain.
    Vec3 player = wv.eye;
    {
        const Vec3 right{wv.view.at(0, 0), wv.view.at(1, 0), wv.view.at(2, 0)};
        const Vec3 up{wv.view.at(0, 1), wv.view.at(1, 1), wv.view.at(2, 1)};
        const Vec3 back{wv.view.at(0, 2), wv.view.at(1, 2), wv.view.at(2, 2)};
        const float ty = std::tan(degToRad(wv.fovY * 0.5f));
        const Vec3 dir = normalize(Vec3{-back.x, -back.y, -back.z} + up * (-0.3f * ty) + right * 0.0f);
        for (float t = 0.0f; t < 3000.0f; t += 2.0f) {
            const Vec3 p = wv.eye + dir * t;
            if (p.z <= terrain->heightAt(p.x, p.y)) {
                player = p;
                break;
            }
        }
    }
    player.z = terrain->heightAt(player.x, player.y);
    say("crater and lights at (%.1f, %.1f), ground %.1f\n", static_cast<double>(player.x),
        static_cast<double>(player.y), static_cast<double>(player.z));

    // Phase 4 camera: 20 above the ground, 240 before the first tile overlay cell past row 8,
    // looking at that cell along +y (the scroll direction).
    WorldView graze = wv;
    {
        const LevelData& ld = *terrain->level();
        int tc = terrain->width() / 2, tr0 = 20;
        bool found = false;
        for (int r = 8; r < terrain->height() && !found; ++r)
            for (int c = 0; c < terrain->width() && !found; ++c)
                if (ld.cellAt(c, r).tileSet != 0) {
                    tc = c;
                    tr0 = r;
                    found = true;
                }
        const float tx = (static_cast<float>(tc) + 0.5f) * kHmapCellSize, ty = (static_cast<float>(tr0) + 0.5f) * kHmapCellSize;
        const float gy = ty - 240.0f;
        const float gz = terrain->heightAt(tx, gy) + 20.0f;
        graze.eye = Vec3{tx, gy, gz};
        graze.view = lookAt(graze.eye, Vec3{tx, ty, terrain->heightAt(tx, ty)}, Vec3{0, 0, 1});
        say("grazing camera at (%.1f, %.1f, %.1f) looking at tile cell (%d, %d)\n", static_cast<double>(tx),
            static_cast<double>(gy), static_cast<double>(gz), tc, tr0);
    }

    std::vector<Vec3> heights = terrain->positions();
    const DynamicLight lights[3] = {
        DynamicLight::point(Vec3{player.x - 90.0f, player.y, player.z + 40.0f}, Vec3{1.0f, 0.2f, 0.1f}, 160.0f),
        DynamicLight::point(Vec3{player.x + 90.0f, player.y, player.z + 40.0f}, Vec3{0.1f, 1.0f, 0.2f}, 160.0f),
        DynamicLight::point(Vec3{player.x, player.y + 120.0f, player.z + 40.0f}, Vec3{0.2f, 0.3f, 1.0f}, 160.0f),
    };

    int phase = -1;
    double gpuSum = 0, gpuMax = 0;
    int gpuN = 0;
    for (int frame = 0; aptMainLoop(); ++frame) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        const int want = std::min(frame / hold, kPhases - 1);
        if (want != phase) {
            if (phase >= 0 && gpuN > 0)
                say("phase %d %s: GPU avg %.3f ms, max %.3f ms over %d frames\n", phase + 1, kPhaseNames[phase],
                    gpuSum / gpuN, gpuMax, gpuN);
            gpuSum = gpuMax = 0;
            gpuN = 0;
            phase = want;
            std::printf("\x1b[2J\x1b[1;1HTerrain test, phase %d: %s\n(START quits)\n", phase + 1, kPhaseNames[phase]);
            if (phase == 1) {
                // A crater: heights lowered by up to 50 in a 7x7-vertex rectangle.
                const int W = terrain->width(), H = terrain->height();
                const int pc = static_cast<int>(std::lround(player.x / kHmapCellSize));
                const int pr = static_cast<int>(std::lround(player.y / kHmapCellSize));
                VertexRect rect;
                rect.c0 = std::max(pc - 3, 0);
                rect.c1 = std::min(pc + 3, W);
                rect.r0 = std::max(pr - 3, 0);
                rect.r1 = std::min(pr + 3, H);
                for (int r = rect.r0; r <= rect.r1; ++r)
                    for (int c = rect.c0; c <= rect.c1; ++c) {
                        const float d2 = static_cast<float>((c - pc) * (c - pc) + (r - pr) * (r - pr));
                        heights[static_cast<size_t>(r) * static_cast<size_t>(W + 1) + static_cast<size_t>(c)].z -=
                            std::max(0.0f, 50.0f * (1.0f - d2 / 12.0f));
                    }
                const TerrainGridView grid{heights.data(), W, H};
                const u64 t0 = svcGetSystemTick();
                tr->update(&rect, 1, grid);
                const double ums = static_cast<double>(svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000.0);
                say("crater: vertices (%d..%d, %d..%d), %d chunks re-uploaded in %.2f ms, %d buffers deferred\n",
                    rect.c0, rect.c1, rect.r0, rect.r1, tr->lastUpdatedChunks(), ums,
                    pica::terrainPicaStats().deferredFrees);
            }
        }

        const WorldView& cam = phase >= 3 ? graze : wv;
        TerrainViewParams tv;
        tv.view = cam.view;
        tv.projection = pica::picaEyeProjection(cam, EyeView{}, aspect);
        tv.cameraPos = cam.eye;
        tv.fogColour = fogColour;
        tv.fogStart = hasFog ? style.fogNear : 1.0e8f;
        tv.fogEnd = hasFog ? style.fogFar : 2.0e8f;
        if (phase == 2) {
            tv.lights = lights;
            tv.lightCount = 3;
        }
        TerrainRenderOptions ro;
        ro.tiles = phase != 4;
        pica::setTerrainOverlayDepthOffset(phase == 5 ? 0.0f : pica::kPolygonOffsetDepth);

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        pica::frameBegin();
        pica::drawOn(pica::FrameTarget{top, 400, 240, true});
        const Vec3 cc = hasFog ? fogColour : Vec3{0, 0, 0};
        clear(Vec4{cc.x, cc.y, cc.z, 1.0f}, true, 1.0f);
        tr->render(tv, ro);
        const pica::TerrainPicaStats st = pica::terrainPicaStats();
        drawMarkers();
        pica::frameEnd();
        C3D_FrameEnd(0);

        if (frame % hold == 30) {
            const std::string path = dir + "/terraintest_" + std::to_string(phase + 1) + ".ppm";
            dumpTarget(top, path);
            say("wrote %s\n", path.c_str());
        }
        if (frame % hold == 2) {
            say("phase %d %s: %d visible chunks, %d lit, %d draw calls, %zu indices, light arena %zu B, "
                "deferred frees %d, arena failures %u\n",
                phase + 1, kPhaseNames[phase], st.visibleChunks, st.litChunks, st.drawCalls, st.indicesDrawn,
                st.arenaBytes, st.deferredFrees, pica::frameArena().failures());
        }
        if (frame % hold >= 2) {
            const double g = static_cast<double>(C3D_GetDrawingTime());
            gpuSum += g;
            gpuMax = std::max(gpuMax, g);
            ++gpuN;
        }
        if (holdFrames > 0 && frame >= (kPhases + 1) * hold) break; // hold=N: one more period of the last phase, then quit
    }
    if (phase >= 0 && gpuN > 0)
        say("phase %d %s: GPU avg %.3f ms, max %.3f ms over %d frames\n", phase + 1, kPhaseNames[phase], gpuSum / gpuN,
            gpuMax, gpuN);
    say("terraintest: done\n");
    tr.reset();
    const pica::TextureStats ts = pica::textureStats();
    say("after free: vram %zu B (%d), linear %zu B (%d)\n", ts.vramBytes, ts.vramCount, ts.linearBytes, ts.linearCount);
    pica::shutdownBackend();
    C3D_RenderTargetDelete(top);
    world.reset();
    if (g_log) std::fclose(g_log);
    g_log = nullptr;
}

} // namespace terraintest
