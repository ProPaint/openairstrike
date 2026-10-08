// The effects test (fx_test.h).
#include "fx_test.h"

#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "as3d/defs.h"
#include "as3d/game_profile.h"
#include "as3d/health_bar.h"
#include "as3d/image.h"
#include "as3d/input.h"
#include "as3d/particle_render.h"
#include "as3d/render_rules.h"
#include "as3d/render_state.h"
#include "as3d/sprite_render.h"
#include "as3d/vfs.h"
#include "as3d/world.h"
#include "as3d/world_render.h"
#include "fx_pica.h"
#include "pica_frame.h"
#include "pica_space.h"
#include "shader_set.h"
#include "tev.h"
#include "texture_budget.h"

using namespace as3d;

namespace fxtest {
namespace {

#define FX_TRANSFER_FLAGS                                                                               \
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
// own copy or the function moves to eye_view.cpp).
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

void drawMarkers() {
    pica::disableFog();
    setBlend(GlBlend::Off);
    setDepth(false, false);
    setCull(CullMode::Off);
    solid(0, 0, 6, 6, 250, 10, 130);
    solid(394, 0, 400, 6, 10, 250, 130);
    solid(0, 234, 6, 240, 130, 10, 250);
}

// The top target as a PPM (see terrain_test.cpp dumpTarget).
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

// ---- the sprite list of the entity pass (engine/src/render/gles/world_render.cpp prepare():
// worldRenderOrder, the sprite and health bar parts), copied until the PICA WorldRenderer (M1.8).

int rootOfEntity(const World& w, int i) {
    for (int guard = 0; guard < 64 && w.validIndex(i) && w.entity(i).parent >= 0; ++guard) i = w.entity(i).parent;
    return i;
}

void visitThinkOrder(const World& w, int i, std::vector<int>& order, std::vector<char>& visited, int depth) {
    if (depth > 64 || !w.validIndex(i) || visited[static_cast<size_t>(i)]) return;
    const Entity& e = w.entity(i);
    if (e.rt & RT_REMOVED) return;
    visited[static_cast<size_t>(i)] = 1;
    if (e.inList && w.validIndex(e.parent)) {
        int r = rootOfEntity(w, e.parent);
        if (w.validIndex(r) && w.entity(r).inList) visitThinkOrder(w, r, order, visited, depth + 1);
    }
    order.push_back(i);
    if (e.emitter) return;
    for (int c : e.children) {
        if (w.validIndex(c) && w.entity(c).parent == i) visitThinkOrder(w, c, order, visited, depth + 1);
    }
}

DecalBlend decalBlendOf(decltype(ObjectDef::blend) b) {
    using B = decltype(ObjectDef::blend);
    switch (b) {
        case B::Alpha: return DecalBlend::Alpha;
        case B::Add: return DecalBlend::Add;
        case B::Filter: return DecalBlend::Filter;
        default: return DecalBlend::None;
    }
}

// Textures of the test, created outside frames (the ResourceCache of the GL renderer without the
// meshes).
struct TextureCache {
    Vfs* vfs = nullptr;
    std::map<std::string, std::unique_ptr<Texture2D>> map;
    Texture2D white;

    const Texture2D* get(const std::string& path) {
        const std::string key = normalizePath(path);
        auto it = map.find(key);
        if (it != map.end()) return it->second.get();
        Blob blob;
        Image image;
        auto tex = std::make_unique<Texture2D>();
        if (!path.empty() && vfs->read(path, blob) && decodeTga(blob.data(), blob.size(), image)) {
            TextureOptions opts;
            opts.mipmaps = true;
            tex->create(image, opts);
        }
        if (!tex->valid()) {
            say("texture '%s' missing, white\n", path.c_str());
            if (!white.valid()) {
                Image w;
                w.width = w.height = 8;
                w.rgba.assign(8 * 8 * 4, 255);
                white.create(w);
            }
            return &white;
        }
        const Texture2D* raw = tex.get();
        map[key] = std::move(tex);
        return raw;
    }
};

struct SpriteScene {
    std::vector<SpriteInstance> list;
    HealthBarSpriteDef hbEmpty, hbFull;
    bool hbReady = false;
    int healthBars = 0;
    int dropped = 0;
};

void resolveHealthBar(SpriteScene& sc, const DefDatabase& db, const GameRules& rules, TextureCache& cache) {
    const ObjectDef* he = rules.healthBarEmptyObject ? db.findObject(rules.healthBarEmptyObject) : nullptr;
    const ObjectDef* hf = rules.healthBarFullObject ? db.findObject(rules.healthBarFullObject) : nullptr;
    sc.hbReady = he && hf && he->hasBbox && hf->hasBbox;
    if (!sc.hbReady) return;
    HealthBarSpriteDef* outs[2] = {&sc.hbEmpty, &sc.hbFull};
    const ObjectDef* defs[2] = {he, hf};
    for (int k = 0; k < 2; ++k) {
        const ObjectDef& d = *defs[k];
        HealthBarSpriteDef& o = *outs[k];
        o.minX = d.bboxMin[0];
        o.minY = d.bboxMin[1];
        o.minS = d.bboxMin[2];
        o.minT = d.bboxMin[3];
        o.maxX = d.bboxMax[0];
        o.maxY = d.bboxMax[1];
        o.maxS = d.bboxMax[2];
        o.maxT = d.bboxMax[3];
        o.texture = d.skin.empty() ? nullptr : cache.get(d.skin);
        o.blend = decalBlendOf(d.blend);
        o.noDepthTest = (d.rflag & RF_NODEPTHTEST) != 0;
        o.noDepthWrite = (d.rflag & RF_NODEPTHWRITE) != 0;
    }
}

void collectSprites(const World& world, SpriteScene& sc, TextureCache& cache) {
    sc.list.clear();
    sc.healthBars = sc.dropped = 0;
    std::vector<int> order;
    std::vector<char> visited(static_cast<size_t>(kMaxEntitySlots), 0);
    for (int i : world.listEntities()) visitThinkOrder(world, i, order, visited, 0);
    auto addHealthBar = [&](const Entity& e) {
        if (!sc.hbReady) return;
        SpriteInstance bar[2];
        const int n = buildHealthBar(e, sc.hbEmpty, sc.hbFull, bar);
        for (int k = 0; k < n; ++k) {
            if (sc.list.size() >= kMaxSprites) {
                ++sc.dropped;
                break;
            }
            sc.list.push_back(bar[k]);
        }
        sc.healthBars += n > 0 ? 1 : 0;
    };
    for (int i : order) {
        const Entity& e = world.entity(i);
        if (!e.def || (e.flagBits() & FL_NODRAW)) {
            addHealthBar(e);
            continue;
        }
        const ObjectDef& def = *e.def;
        const Vec4 colour{e.f(F_COLOR), e.f(F_COLOR + 1), e.f(F_COLOR + 2), e.f(F_COLOR + 3)};
        const Vec3 origin = e.v3(F_BASE_ORIGIN);
        if ((def.type == ObjectType::Sprite || def.type == ObjectType::HSprite || def.type == ObjectType::VSprite) &&
            def.hasBbox) {
            const std::string& skin = e.skinPath.empty() ? def.skin : e.skinPath;
            if (sc.list.size() >= kMaxSprites) {
                ++sc.dropped;
            } else if (!skin.empty()) {
                SpriteInstance sp;
                sp.kind = def.type == ObjectType::Sprite    ? SpriteKind::Billboard
                          : def.type == ObjectType::HSprite ? SpriteKind::Horizontal
                                                            : SpriteKind::Vertical;
                sp.origin = origin;
                sp.yawDegrees = e.f(F_ANGLES + 2);
                sp.scale = e.f(F_SCALE);
                sp.colour = colour;
                sp.minX = def.bboxMin[0];
                sp.minY = def.bboxMin[1];
                sp.minS = def.bboxMin[2];
                sp.minT = def.bboxMin[3];
                sp.maxX = def.bboxMax[0];
                sp.maxY = def.bboxMax[1];
                sp.maxS = def.bboxMax[2];
                sp.maxT = def.bboxMax[3];
                if (def.hasFrames && def.frameStart > 0) {
                    sp.frameCols = def.frameStart;
                    sp.frameRows = std::max(def.frameEnd, 1);
                    const float fr = e.f(F_FRAME);
                    sp.frame = (fr > -1.0e7f && fr < 1.0e7f) ? static_cast<int>(fr) : 0;
                }
                sp.texture = cache.get(skin);
                sp.blend = decalBlendOf(def.blend);
                sp.noDepthTest = (def.rflag & RF_NODEPTHTEST) != 0;
                sp.noDepthWrite = (def.rflag & RF_NODEPTHWRITE) != 0;
                sc.list.push_back(sp);
            }
        }
        addHealthBar(e);
    }
}

constexpr int kPhases = 6;
const char* kPhaseNames[kPhases] = {"particles", "sprites", "all", "stress1536", "hbars", "eyes"};

// A synthetic emitter pool for the stress phase: `count` particles alive at once in a box in
// front of the camera, drawn with `texture`.
struct Stress {
    ParticleDesc descs[3];
    std::vector<std::unique_ptr<ParticleEmitter>> emitters;
    std::vector<const ParticleEmitter*> ptrs;

    void make(const std::string& texture, const Vec3& centre) {
        const ParticleBlend blends[3] = {ParticleBlend::Alpha, ParticleBlend::Add, ParticleBlend::Filter};
        for (int k = 0; k < 3; ++k) {
            ParticleDesc& d = descs[k];
            d.name = "fxtest_stress";
            d.texture = texture;
            d.blend = blends[k];
            d.emitOnce = true;
            d.emitRate = 512; // EMIT_ONCE: all 512 alive from the start
            d.lifeTime = 1.0e6f;
            d.initOffset[0] = centre.x;
            d.initOffset[1] = centre.y;
            d.initOffset[2] = centre.z;
            d.initOffset[3] = d.initOffset[4] = 70.0f;
            d.initOffset[5] = 40.0f;
            d.initSize[0] = 6.0f;
            d.initSize[1] = 10.0f;
            d.initColor[0] = d.initColor[1] = d.initColor[2] = 0.8f;
            d.initColor[3] = blends[k] == ParticleBlend::Alpha ? 0.4f : 0.7f;
            emitters.emplace_back(new ParticleEmitter(d, 77u + static_cast<u32>(k)));
            ptrs.push_back(emitters.back().get());
        }
    }
};

} // namespace

void run(Vfs& vfs, const DefDatabase& db, const GameProfile& game, const std::string& dir, int level, int botFrames,
         int holdFrames) {
    g_log = std::fopen((dir + "/fxtest_log.txt").c_str(), "w");
    const int hold = holdFrames > 0 ? holdFrames : 90;
    const int bot = botFrames > 0 ? botFrames : 300;
    say("Effects test (M1.6): mission %d, %d bot frames, %d frames per phase\n", level, bot, hold);

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
    const u64 ts0 = svcGetSystemTick();
    for (u32 f = 0; f < static_cast<u32>(bot); ++f) world->step(botInput(f).toPlayerInput());
    say("%d bot frames in %.0f ms\n", bot, static_cast<double>(svcGetSystemTick() - ts0) / (SYSCLOCK_ARM11 / 1000.0));
    const Terrain* terrain = world->terrain();

    if (void* warm = vramAlloc(0x100)) vramFree(warm);
    C3D_RenderTarget* top = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    if (!top) {
        say("cannot create the top target\n");
        if (g_log) std::fclose(g_log);
        return;
    }
    C3D_RenderTargetSetOutput(top, GFX_TOP, GFX_LEFT, FX_TRANSFER_FLAGS);
    if (!pica::initBackend()) {
        say("pica::initBackend failed\n");
        C3D_RenderTargetDelete(top);
        if (g_log) std::fclose(g_log);
        return;
    }

    auto particlesPtr = std::make_unique<ParticleRenderer>();
    auto cachePtr = std::make_unique<TextureCache>();
    ParticleRenderer& particles = *particlesPtr;
    TextureCache& cache = *cachePtr;
    SpriteRenderer sprites;
    if (!particles.init(vfs, &err) || !sprites.init(&err)) {
        say("renderer init failed: %s\n", err.c_str());
        pica::shutdownBackend();
        C3D_RenderTargetDelete(top);
        if (g_log) std::fclose(g_log);
        return;
    }
    cache.vfs = &vfs;
    SpriteScene scene;
    resolveHealthBar(scene, db, game.rules, cache);

    // Level-start work, outside frames: the particle textures of the live emitters, the sprites.
    std::vector<const ParticleEmitter*> emitters;
    world->particles().collect(emitters);
    std::string stressTexture;
    int liveParticles = 0;
    for (const ParticleEmitter* e : emitters) {
        particles.preload(e->desc().texture);
        liveParticles += e->liveCount();
        if (stressTexture.empty() && !e->desc().texture.empty() && e->desc().blend == ParticleBlend::Alpha && e->liveCount() > 0)
            stressTexture = e->desc().texture;
    }
    if (stressTexture.empty())
        for (const ParticleEmitter* e : emitters)
            if (!e->desc().texture.empty()) {
                stressTexture = e->desc().texture;
                break;
            }
    particles.preload(stressTexture);
    collectSprites(*world, scene, cache);
    {
        int blendCount[4] = {0, 0, 0, 0};
        for (const ParticleEmitter* e : emitters) blendCount[static_cast<int>(e->desc().blend)] += e->liveCount();
        say("world: %zu emitters, %d live particles (none %d, alpha %d, add %d, filter %d)\n", emitters.size(),
            liveParticles, blendCount[0], blendCount[1], blendCount[2], blendCount[3]);
        say("world: %zu sprites (%d health bars, %d dropped), stress texture '%s'\n", scene.list.size(),
            scene.healthBars, scene.dropped, stressTexture.c_str());
        const pica::FxPicaStats& fs = pica::fxPicaStats();
        const pica::TextureStats ts = pica::textureStats();
        say("particle textures: %d, %.1f KB; all textures: vram %.1f KB (%d), linear %.1f KB (%d), failures %d\n",
            fs.particleTextures, kb(fs.particleTextureBytes), kb(ts.vramBytes), ts.vramCount, kb(ts.linearBytes),
            ts.linearCount, ts.failures);
    }

    const float aspect = 400.0f / 240.0f;
    const WorldView wv = cameraOf(*world, aspect);
    Vec3 fogColour{0, 0, 0};
    float fogStart = 1.0e8f, fogEnd = 2.0e8f;
    if (terrain && terrain->style().hasFog) {
        const TerrainStyle& st = terrain->style();
        fogColour = Vec3{st.fogColor[0], st.fogColor[1], st.fogColor[2]};
        fogStart = st.fogNear;
        fogEnd = st.fogFar;
    }
    say("camera eye (%.1f, %.1f, %.1f), fov %.1f, far %.0f, fog %.0f..%.0f\n", static_cast<double>(wv.eye.x),
        static_cast<double>(wv.eye.y), static_cast<double>(wv.eye.z), static_cast<double>(wv.fovY),
        static_cast<double>(wv.farPlane), static_cast<double>(fogStart), static_cast<double>(fogEnd));

    // The point 220 units in front of the camera: the stress box and the synthetic bars.
    const Vec3 back{wv.view.at(0, 2), wv.view.at(1, 2), wv.view.at(2, 2)};
    const Vec3 ahead = wv.eye - back * 220.0f;

    Stress stress;
    stress.make(stressTexture, ahead);
    std::vector<SpriteInstance> bars;
    if (scene.hbReady) {
        const float fractions[3] = {1.0f, 0.5f, 0.15f};
        const Vec3 right{wv.view.at(0, 0), wv.view.at(1, 0), wv.view.at(2, 0)};
        for (int k = 0; k < 3; ++k) {
            const Vec3 pos = ahead + right * (static_cast<float>(k - 1) * 40.0f);
            SpriteInstance e, f;
            auto fill = [&](SpriteInstance& s, const HealthBarSpriteDef& d) {
                s.kind = SpriteKind::Billboard;
                s.origin = pos;
                s.minX = d.minX;
                s.minY = d.minY;
                s.minS = d.minS;
                s.minT = d.minT;
                s.maxX = d.maxX;
                s.maxY = d.maxY;
                s.maxS = d.maxS;
                s.maxT = d.maxT;
                s.texture = d.texture;
                s.blend = d.blend;
                s.noDepthTest = d.noDepthTest;
                s.noDepthWrite = d.noDepthWrite;
            };
            fill(e, scene.hbEmpty);
            fill(f, scene.hbFull);
            f.maxX = f.minX + (f.maxX - f.minX) * fractions[k];
            f.maxS = f.minS + (f.maxS - f.minS) * fractions[k];
            bars.push_back(e);
            bars.push_back(f);
        }
    }

    int phase = -1;
    double gpuSum = 0, gpuMax = 0, cpuSum = 0, cpuMax = 0;
    int n = 0;
    for (int frame = 0; aptMainLoop(); ++frame) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        const int want = std::min(frame / hold, kPhases - 1);
        if (want != phase) {
            if (phase >= 0 && n > 0)
                say("phase %d %s: GPU avg %.3f ms max %.3f ms, CPU avg %.3f ms max %.3f ms over %d frames\n", phase + 1,
                    kPhaseNames[phase], gpuSum / n, gpuMax, cpuSum / n, cpuMax, n);
            gpuSum = gpuMax = cpuSum = cpuMax = 0;
            n = 0;
            phase = want;
            std::printf("\x1b[2J\x1b[1;1HEffects test, phase %d: %s\n(START quits)\n", phase + 1, kPhaseNames[phase]);
        }

        const bool drawParticles = phase == 0 || phase == 2 || phase == 5;
        const bool drawSprites = phase == 1 || phase == 2 || phase == 5;
        const u64 t0 = svcGetSystemTick();
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        pica::frameBegin();
        pica::drawOn(pica::FrameTarget{top, 400, 240, true});
        clear(Vec4{fogColour.x, fogColour.y, fogColour.z, 1.0f}, true, 1.0f);

        auto drawEye = [&](const EyeView& eye, float eyeAspect) {
            ParticleViewParams pv;
            pv.view = wv.view;
            pv.projection = pica::picaEyeProjection(wv, eye, eyeAspect);
            pv.fogColour = fogColour;
            pv.fogStart = fogStart;
            pv.fogEnd = fogEnd;
            SpriteViewParams sv;
            sv.view = pv.view;
            sv.projection = pv.projection;
            sv.fogColour = fogColour;
            sv.fogStart = fogStart;
            sv.fogEnd = fogEnd;
            sv.cullBackFaces = renderRules(game.id).spriteCulling;
            if (phase == 3) {
                particles.draw(stress.ptrs.data(), stress.ptrs.size(), pv);
            } else if (phase == 4) {
                if (!bars.empty()) sprites.draw(bars.data(), bars.size(), sv);
            } else {
                if (drawParticles && !emitters.empty()) particles.draw(emitters.data(), emitters.size(), pv);
                if (drawSprites && !scene.list.empty()) sprites.draw(scene.list.data(), scene.list.size(), sv);
            }
        };
        if (phase == 5) {
            // Two stereo eyes in the halves of the screen, one build for both.
            setViewport(0, 0, 200, 240);
            drawEye(EyeView{-1.5f, 400.0f}, 200.0f / 240.0f);
            setViewport(200, 0, 200, 240);
            drawEye(EyeView{1.5f, 400.0f}, 200.0f / 240.0f);
            setViewport(0, 0, 400, 240);
        } else {
            drawEye(EyeView{}, aspect);
        }
        const pica::FxPicaStats st = pica::fxPicaStats();
        const size_t arenaUsed = pica::frameArena().used();
        drawMarkers();
        pica::frameEnd();
        C3D_FrameEnd(0);
        const double cpu = static_cast<double>(svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000.0);

        if (frame % hold == 30) {
            const std::string path = dir + "/fxtest_" + std::to_string(phase + 1) + ".ppm";
            dumpTarget(top, path);
            say("wrote %s\n", path.c_str());
        }
        if (frame % hold == 2) {
            say("phase %d %s: particle quads %d (dropped %d, %.1f KB arena, %d draws), sprite quads %d (dropped %d, "
                "%.1f KB arena, %d draws), frame arena used %.1f KB (high water %.1f KB), arena failures %u, late "
                "textures %d\n",
                phase + 1, kPhaseNames[phase], st.particleQuads, st.particleQuadsDropped, kb(st.particleArenaBytes),
                st.particleDraws, st.spriteQuads, st.spriteQuadsDropped, kb(st.spriteArenaBytes), st.spriteDraws,
                kb(arenaUsed), kb(pica::frameArena().highWater()), pica::frameArena().failures(),
                st.particleLateTextures);
        }
        if (frame % hold >= 2) {
            const double g = static_cast<double>(C3D_GetDrawingTime());
            gpuSum += g;
            gpuMax = std::max(gpuMax, g);
            cpuSum += cpu;
            cpuMax = std::max(cpuMax, cpu);
            ++n;
        }
        if (holdFrames > 0 && frame >= (kPhases + 1) * hold) break;
    }
    if (phase >= 0 && n > 0)
        say("phase %d %s: GPU avg %.3f ms max %.3f ms, CPU avg %.3f ms max %.3f ms over %d frames\n", phase + 1,
            kPhaseNames[phase], gpuSum / n, gpuMax, cpuSum / n, cpuMax, n);
    say("fxtest: done\n");
    stress.emitters.clear();
    bars.clear();
    scene.list.clear();
    scene.hbEmpty.texture = scene.hbFull.texture = nullptr;
    particlesPtr.reset();
    cachePtr.reset();
    pica::shutdownBackend();
    C3D_RenderTargetDelete(top);
    world.reset();
    if (g_log) std::fclose(g_log);
    g_log = nullptr;
}

} // namespace fxtest
