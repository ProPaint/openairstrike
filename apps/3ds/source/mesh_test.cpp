// The PICA mesh test (mesh_test.h).
//
// The model records of the frame are collected as WorldRenderer::prepare does on the desktop
// (engine/src/render/gles/world_render.cpp: the think order, FL_NODRAW, the entity matrix and
// colour, the material per (definition, skin), the three lists and their caps, the PlaceLight
// queue plus the definition lights of the drawn entities). The GL file is not part of the 3DS
// build, so the few helpers are repeated here; M1.8 moves them to a neutral file.
#include "mesh_test.h"

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

#include "as3d/core.h"
#include "as3d/dynamic_lights.h"
#include "as3d/game_profile.h"
#include "as3d/input.h"
#include "as3d/precache.h"
#include "as3d/render_state.h"
#include "as3d/scene.h"
#include "as3d/terrain.h"
#include "as3d/world.h"
#include "as3d/world_render.h"
#include "game_session.h"
#include "gpu_mesh_pica.h"
#include "mesh_renderer_pica.h"
#include "pica_frame.h"
#include "pica_space.h"
#include "tev.h"
#include "texture_budget.h"
#include "as3d/defs.h"

using namespace as3d;

namespace meshtest {
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
constexpr float kAspect = 400.0f / 240.0f;

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

double mb(size_t b) { return static_cast<double>(b) / (1024.0 * 1024.0); }
u8 byte(float v) { return static_cast<u8>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); }

// ---------------------------------------------------------------------------------------
// The desktop entity pass, models only (world_render.cpp).
// ---------------------------------------------------------------------------------------

WorldView worldViewOfWorld(const World& world, float aspect) {
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

Mat4 entityMatrix(const Entity& e) {
    Vec3 a0 = e.v3(F_AXIS), a1 = e.v3(F_AXIS + 3), a2 = e.v3(F_AXIS + 6), o = e.v3(F_BASE_ORIGIN);
    Mat4 m;
    m.at(0, 0) = a0.x; m.at(0, 1) = a0.y; m.at(0, 2) = a0.z; m.at(0, 3) = 0.0f;
    m.at(1, 0) = a1.x; m.at(1, 1) = a1.y; m.at(1, 2) = a1.z; m.at(1, 3) = 0.0f;
    m.at(2, 0) = a2.x; m.at(2, 1) = a2.y; m.at(2, 2) = a2.z; m.at(2, 3) = 0.0f;
    m.at(3, 0) = o.x;  m.at(3, 1) = o.y;  m.at(3, 2) = o.z;  m.at(3, 3) = 1.0f;
    float s = e.f(F_SCALE);
    if (s > 0.001f) m = m * scale(Vec3{s, s, s});
    return m;
}

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

struct Record {
    const GpuMesh* mesh = nullptr;
    const Material* material = nullptr;
    Mat4 model;
    Vec4 colour;
    int entity = -1;
    int root = -1;
};

struct Scene {
    std::unique_ptr<ResourceCache> cache;
    std::map<std::pair<const ObjectDef*, std::string>, std::unique_ptr<Material>> materials;
    std::vector<Record> opaque, trans, effect;
    std::vector<DynamicLight> lights;
    SceneLighting light;
    bool hasFog = false;
    WorldView wv;
    Vec3 clear{0.0f, 0.0f, 0.0f};
    int player = -1;
    int dropped = 0;

    const Material& materialFor(const ObjectDef& def, const std::string& skinOverride) {
        auto key = std::make_pair(&def, skinOverride);
        auto it = materials.find(key);
        if (it != materials.end()) return *it->second;
        std::unique_ptr<Material> m(new Material(Material::fromObjectDef(def, *cache)));
        if (!skinOverride.empty()) {
            ResourceCache::LoadedTexture t = cache->texture(skinOverride);
            m->texture = t.texture;
            m->textureHasAlpha = t.hasAlpha;
        }
        const Material& ref = *m;
        materials.emplace(key, std::move(m));
        return ref;
    }
};

// The level's closure (as WorldRenderer::precache, without shadows): every mesh and skin the
// mission can draw, so the memory numbers are the mission's.
void precacheMission(const World& world, const DefDatabase& db, Scene& s) {
    PrecacheSet local;
    const PrecacheSet* set = world.precacheSet();
    if (!set) {
        local = collectPrecacheSet(world, db, [&world](const std::string& path) { return world.compiledProgram(path); });
        set = &local;
    }
    for (const ObjectDef* def : set->objects) {
        for (const SkidMarkDef& sk : def->skidMarks)
            if (!sk.texture.empty()) s.cache->texture(sk.texture);
        if (def->type == ObjectType::Model && !def->model.empty()) {
            if (s.cache->mesh(def->model).valid) s.materialFor(*def, std::string());
        } else if (!def->skin.empty()) {
            s.cache->texture(def->skin);
        }
    }
    for (const std::string& m : set->models) s.cache->mesh(m);
    for (const auto& om : set->objectModels)
        if (om.first->type == ObjectType::Model && !om.second.empty()) s.cache->mesh(om.second);
    for (const std::string& t : set->textures) s.cache->texture(t);
    say("precache: %d objects, %d models, %d textures\n", static_cast<int>(set->objects.size()),
        static_cast<int>(set->models.size()), static_cast<int>(set->textures.size()));
}

void collect(const World& world, Scene& s) {
    constexpr int kOpaqueCap = 512, kTransCap = 128, kEffectCap = 256;
    s.wv = worldViewOfWorld(world, kAspect);
    s.light = SceneLighting();
    s.hasFog = false;
    if (const Terrain* terrain = world.terrain()) {
        const TerrainStyle& st = terrain->style();
        s.light.sunColor = Vec3{st.sun[0], st.sun[1], st.sun[2]};
        s.light.sunDirection = Vec3{st.sun[3], st.sun[4], st.sun[5]};
        s.light.ambientColor = Vec3{st.sun[6], st.sun[7], st.sun[8]};
        s.light.fogColor = Vec3{st.fogColor[0], st.fogColor[1], st.fogColor[2]};
        s.light.fogStart = st.fogNear;
        s.light.fogEnd = st.fogFar;
        s.hasFog = st.hasFog;
    }
    if (!s.hasFog) {
        s.light.fogStart = 1.0e8f;
        s.light.fogEnd = 2.0e8f;
    }
    s.clear = s.hasFog ? s.light.fogColor : Vec3{0.0f, 0.0f, 0.0f};
    DynamicLightList lights;
    for (const QueuedLight& q : world.lights()) lights.add(DynamicLight::point(q.pos, q.color, q.radius));
    s.opaque.clear();
    s.trans.clear();
    s.effect.clear();
    s.dropped = 0;
    std::vector<int> order;
    std::vector<char> visited(static_cast<size_t>(kMaxEntitySlots), 0);
    for (int i : world.listEntities()) visitThinkOrder(world, i, order, visited, 0);
    for (int i : order) {
        const Entity& e = world.entity(i);
        if (!e.def || (e.flagBits() & FL_NODRAW)) continue;
        const ObjectDef& def = *e.def;
        const Vec4 colour{e.f(F_COLOR), e.f(F_COLOR + 1), e.f(F_COLOR + 2), e.f(F_COLOR + 3)};
        const Vec3 origin = e.v3(F_BASE_ORIGIN);
        if ((def.hasLight || def.hasLightDir) && def.lightRadius > 0) {
            Vec3 col{def.lightColor[0], def.lightColor[1], def.lightColor[2]};
            float radius = static_cast<float>(def.lightRadius);
            if (def.hasLightDir) {
                Vec3 d = e.v3(F_AXIS) * def.lightDir[0] + e.v3(F_AXIS + 3) * def.lightDir[1] +
                         e.v3(F_AXIS + 6) * def.lightDir[2];
                lights.add(DynamicLight::spotLight(origin, col, radius, d, def.lightConeAngle));
            } else {
                lights.add(DynamicLight::point(origin, col, radius));
            }
        }
        if (def.type != ObjectType::Model || !e.model || e.modelPath.empty()) continue;
        const GpuMesh& mesh = s.cache->mesh(e.modelPath);
        if (!mesh.valid) continue;
        std::vector<Record>* list = &s.opaque;
        int cap = kOpaqueCap;
        if (def.sort == SortMode::Trans) {
            list = &s.trans;
            cap = kTransCap;
        } else if (def.sort == SortMode::Effect) {
            list = &s.effect;
            cap = kEffectCap;
        }
        if (static_cast<int>(list->size()) >= cap) {
            ++s.dropped;
            continue;
        }
        Record r;
        r.mesh = &mesh;
        r.material = &s.materialFor(def, e.skinPath);
        r.model = entityMatrix(e);
        r.colour = colour;
        r.entity = i;
        r.root = rootOfEntity(world, i);
        list->push_back(r);
    }
    s.lights.assign(lights.data(), lights.data() + lights.size());
    s.player = world.playerEntityIndex(0);
}

// ---------------------------------------------------------------------------------------
// Checks: pixels whose colour follows from the geometry alone.
// ---------------------------------------------------------------------------------------

struct Px {
    float x = 0, y = 0, w = 0; // upright pixel (origin top-left), clip w
};

Px project(const Mat4& glViewProj, const Vec3& p) {
    const Vec4 c = glViewProj * Vec4{p.x, p.y, p.z, 1.0f};
    Px o;
    o.w = c.w;
    if (c.w <= 1e-6f) return o;
    o.x = (c.x / c.w + 1.0f) * 0.5f * kW;
    o.y = (1.0f - c.y / c.w) * 0.5f * kH;
    return o;
}

bool inMarker(float x, float y) {
    const bool left = x < 10, right = x > kW - 10, top = y < 10, bottom = y > kH - 10;
    return (left || right) && (top || bottom);
}

struct CheckWriter {
    std::FILE* f = nullptr;
    void line(const char* name, int x, int y, int r, int g, int b, int tol) {
        if (f) std::fprintf(f, "%s %d %d %d %d %d %d\n", name, x, y, r, g, b, tol);
    }
};

// For each opaque record with a plain blend, the centroid of its largest projected front
// triangle must not show the clear colour (fogged ones excepted: the fog colour is the clear).
// Then a grid of pixels outside every record's projected bounding sphere must show it.
void writeChecks(const std::string& path, const std::vector<const Record*>& recs, const Mat4& glViewProj,
                 const Vec3& clear, float nearPlane, float fovY, float fogStart) {
    CheckWriter cw;
    cw.f = std::fopen(path.c_str(), "w");
    if (!cw.f) return;
    const int cr = byte(clear.x), cg = byte(clear.y), cb = byte(clear.z);
    // The corner markers drawMarkers() puts on top.
    cw.line("marker_tl", 3, 3, 250, 10, 130, 4);
    cw.line("marker_tr", 396, 3, 10, 250, 130, 4);
    cw.line("marker_bl", 3, 236, 130, 10, 250, 4);
    int modelChecks = 0;
    for (const Record* r : recs) {
        if (modelChecks >= 16) break;
        const Material& m = *r->material;
        if (m.blend != MaterialBlend::Opaque || m.sortBucket != SortBucket::Opaque || m.noDepthTest) continue;
        const ModelData& md = r->mesh->model;
        const Mat4 mvp = glViewProj * r->model;
        float best = 0.0f, bx = 0, by = 0, bw = 0;
        for (const Face& f : md.faces) {
            Px p[3];
            bool ok = true;
            for (int k = 0; k < 3 && ok; ++k) {
                if (f.v[k] >= md.positions.size()) ok = false;
                else p[k] = project(mvp, md.positions[f.v[k]]);
                if (ok && p[k].w < nearPlane) ok = false;
            }
            if (!ok) continue;
            // GL window y is up: CCW on screen (front) is a negative cross in these y-down pixels.
            const float cross = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[1].y - p[0].y) * (p[2].x - p[0].x);
            const float area = m.noCulling ? std::fabs(cross) * 0.5f : -cross * 0.5f;
            if (area > best) {
                best = area;
                bx = (p[0].x + p[1].x + p[2].x) / 3.0f;
                by = (p[0].y + p[1].y + p[2].y) / 3.0f;
                bw = (p[0].w + p[1].w + p[2].w) / 3.0f;
            }
        }
        if (best < 40.0f || bx < 4 || bx > kW - 5 || by < 4 || by > kH - 5 || inMarker(bx, by)) continue;
        if (bw > fogStart) continue;
        char name[48];
        std::snprintf(name, sizeof name, "model_e%d", r->entity);
        cw.line(name, static_cast<int>(bx), static_cast<int>(by), cr, cg, cb, -8);
        ++modelChecks;
    }
    // Clear-colour grid.
    const float f = 1.0f / std::tan(degToRad(fovY) * 0.5f);
    struct Disc {
        float x, y, r;
        bool all;
    };
    std::vector<Disc> discs;
    for (const Record* r : recs) {
        const ModelData& md = r->mesh->model;
        const Vec3 c = (md.boundsMin + md.boundsMax) * 0.5f;
        float sx = length(Vec3{r->model.at(0, 0), r->model.at(0, 1), r->model.at(0, 2)});
        float sy = length(Vec3{r->model.at(1, 0), r->model.at(1, 1), r->model.at(1, 2)});
        float sz = length(Vec3{r->model.at(2, 0), r->model.at(2, 1), r->model.at(2, 2)});
        const float rad = length(md.boundsMax - md.boundsMin) * 0.5f * std::max(sx, std::max(sy, sz));
        const Vec4 wc = r->model * Vec4{c.x, c.y, c.z, 1.0f};
        const Px p = project(glViewProj, Vec3{wc.x, wc.y, wc.z});
        if (p.w - rad <= nearPlane) {
            discs.push_back({0, 0, 0, true});
            continue;
        }
        discs.push_back({p.x, p.y, rad * f * 0.5f * kH / (p.w - rad) * 1.5f + 4.0f, false});
    }
    int clearChecks = 0;
    for (int y = 20; y < kH; y += 40) {
        for (int x = 20; x < kW; x += 40) {
            bool covered = false;
            for (const Disc& d : discs) {
                if (d.all || (x - d.x) * (x - d.x) + (y - d.y) * (y - d.y) <= d.r * d.r) {
                    covered = true;
                    break;
                }
            }
            if (covered) continue;
            char name[32];
            std::snprintf(name, sizeof name, "clear_%d_%d", x, y);
            cw.line(name, x, y, cr, cg, cb, 3);
            ++clearChecks;
        }
    }
    std::fclose(cw.f);
    say("checks %s: %d model, %d clear\n", path.c_str(), modelChecks, clearChecks);
}

// ---------------------------------------------------------------------------------------
// Drawing.
// ---------------------------------------------------------------------------------------

void drawMarkers() {
    setBlend(GlBlend::Off);
    setDepth(false, false);
    setCull(CullMode::Off);
    pica::disableFog();
    const Mat4 px = pica::picaTiltMatrix() * ortho(0.0f, 400.0f, 240.0f, 0.0f, -1.0f, 1.0f);
    struct M {
        float x0, y0;
        u8 r, g, b;
    };
    const M ms[3] = {{0, 0, 250, 10, 130}, {394, 0, 10, 250, 130}, {0, 234, 130, 10, 250}};
    for (const M& m : ms) {
        auto* v = pica::frameArena().allocArray<pica::FlatVertex>(4);
        if (!v) return;
        const float xs[4] = {m.x0, m.x0 + 6, m.x0, m.x0 + 6}, ys[4] = {m.y0 + 6, m.y0 + 6, m.y0, m.y0};
        for (int i = 0; i < 4; ++i) {
            v[i].pos[0] = xs[i];
            v[i].pos[1] = ys[i];
            v[i].pos[2] = 0.0f;
            v[i].uv[0] = v[i].uv[1] = 0.0f;
            v[i].rgba[0] = v[i].rgba[1] = v[i].rgba[2] = v[i].rgba[3] = 255;
        }
        pica::ShaderSet& sh = pica::shaders();
        sh.bind(pica::Shader::Flat);
        sh.setMvp(px);
        sh.setVertices(v);
        pica::applyTev(pica::tevConstantColor(m.r, m.g, m.b, 255));
        C3D_DrawArrays(GPU_TRIANGLE_STRIP, 0, 4);
    }
}

// Test a: the helicopter and what hangs on it, through the public MeshRenderer.
struct HeliView {
    Camera cam;
    std::vector<const Record*> recs;
};

HeliView heliView(const Scene& s, const World& world) {
    HeliView h;
    Vec3 target{0, 0, 0};
    if (world.validIndex(s.player)) target = world.entity(s.player).v3(F_BASE_ORIGIN);
    const std::vector<Record>* lists[3] = {&s.opaque, &s.trans, &s.effect};
    for (const auto* l : lists)
        for (const Record& r : *l)
            if (r.root == s.player || r.entity == s.player) h.recs.push_back(&r);
    // Framed on the helicopter body's world box (Camera::framing, as the model viewer), from
    // behind-left and above.
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (const Record* r : h.recs) {
        if (r->entity != s.player) continue;
        const ModelData& md = r->mesh->model;
        for (int k = 0; k < 8; ++k) {
            const Vec3 c{(k & 1) ? md.boundsMax.x : md.boundsMin.x, (k & 2) ? md.boundsMax.y : md.boundsMin.y,
                         (k & 4) ? md.boundsMax.z : md.boundsMin.z};
            const Vec4 w = r->model * Vec4{c.x, c.y, c.z, 1.0f};
            lo = Vec3{std::min(lo.x, w.x), std::min(lo.y, w.y), std::min(lo.z, w.z)};
            hi = Vec3{std::max(hi.x, w.x), std::max(hi.y, w.y), std::max(hi.z, w.z)};
        }
    }
    if (lo.x > hi.x) {
        lo = target - Vec3{5.0f, 5.0f, 5.0f};
        hi = target + Vec3{5.0f, 5.0f, 5.0f};
    }
    h.cam = Camera::framing(lo, hi, 215.0f, 35.0f, 1.0f, kAspect);
    return h;
}

const Vec3 kHeliClear{0.22f, 0.24f, 0.30f};

void drawHeli(const Scene& s, const HeliView& h, MeshRenderer& mr) {
    clear(Vec4{kHeliClear.x, kHeliClear.y, kHeliClear.z, 1.0f}, true, 1.0f);
    mr.setDynamicLights(s.lights.data(), s.lights.size());
    // Two lists as the world pass: opaque, then trans + effect.
    mr.begin(h.cam, s.light);
    for (const Record* r : h.recs)
        if (r->material->sortBucket == SortBucket::Opaque) mr.submit(*r->mesh, *r->material, r->model, r->colour);
    mr.end();
    mr.begin(h.cam, s.light);
    for (const Record* r : h.recs)
        if (r->material->sortBucket != SortBucket::Opaque) mr.submit(*r->mesh, *r->material, r->model, r->colour);
    mr.end();
}

struct Passes {
    pica::MeshPass opaque, rest;
};

void preparePasses(const Scene& s, const std::vector<DynamicLight>& lights, Passes& p) {
    p.opaque.begin(s.light, lights.data(), lights.size());
    for (const Record& r : s.opaque) p.opaque.submit(*r.mesh, *r.material, r.model, r.colour);
    p.opaque.finish();
    p.rest.begin(s.light, lights.data(), lights.size());
    for (const Record& r : s.trans) p.rest.submit(*r.mesh, *r.material, r.model, r.colour);
    for (const Record& r : s.effect) p.rest.submit(*r.mesh, *r.material, r.model, r.colour);
    p.rest.finish();
}

// Tests b and c: the world camera, prepared once, drawn as one eye.
void drawWorld(const Scene& s, Passes& p) {
    clear(Vec4{s.clear.x, s.clear.y, s.clear.z, 1.0f}, true, 1.0f);
    pica::setDepthMapping(FogDepthMap::WBuffer, s.wv.nearPlane);
    pica::setLevelFogColor(s.light.fogColor);
    if (s.hasFog) pica::setFog(s.wv.nearPlane, s.wv.farPlane, s.light.fogStart, s.light.fogEnd);
    else pica::disableFog();
    const Mat4 viewProj = pica::picaEyeProjection(s.wv, EyeView{}, kAspect) * s.wv.view;
    p.opaque.draw(viewProj);
    p.rest.draw(viewProj);
}

// The top target read back as an upright 400x240 binary PPM. The colour buffer is 240 x 400
// (portrait, tilted); the display transfer gives it as the LCD sees it: memory row i is screen
// column i, from the bottom of the screen up.
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

void logMemory(const char* what) {
    const pica::MeshMemoryStats m = pica::meshMemoryStats();
    const pica::TextureStats t = pica::textureStats();
    say("[mem] %-10s meshes %d = %.2f MB linear (peak %.2f, failures %d); textures VRAM %d = %.2f MB, linear %d = "
        "%.2f MB (fallbacks %d, failures %d); linear free %.2f MB, VRAM free %.2f MB\n",
        what, m.meshes, mb(m.bytes), mb(m.peakBytes), m.failures, t.vramCount, mb(t.vramBytes), t.linearCount,
        mb(t.linearBytes), t.vramFallbacks, t.failures, mb(linearSpaceFree()), mb(vramSpaceFree()));
}

} // namespace

void run(const std::string& dir, int holdFrames) {
    g_log = std::fopen((dir + "/meshtest_log.txt").c_str(), "w");
    if (!std::freopen((dir + "/meshtest_engine_log.txt").c_str(), "w", stderr)) say("(engine log not redirected)\n");
    setLogLevel(LogLevel::Warn);
    say("PICA mesh test (M1.5)\n");
    bool new3ds = false;
    APT_CheckNew3DS(&new3ds);
    if (new3ds) osSetSpeedupEnable(true);
    if (void* warm = vramAlloc(0x100)) vramFree(warm);

    // Mission 1, 120 bot frames, as the desktop's --headless --bot --frames 120.
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
    const int kSteps = 120;
    for (int f = 0; f < kSteps; ++f) session->step(botInput(static_cast<u32>(f)));
    const World& world = session->world();
    say("mission 1 after %d bot frames: map_pos %.1f, list entities %d\n", kSteps, static_cast<double>(world.mapPos()),
        world.listCount());

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
    logMemory("start");

    // Everything that creates textures or meshes runs before the first frame.
    auto scene = std::make_unique<Scene>();
    scene->cache.reset(new ResourceCache(session->vfs()));
    const u64 t0 = svcGetSystemTick();
    precacheMission(world, session->db(), *scene);
    say("precache %.0f ms\n", static_cast<double>(svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000.0));
    logMemory("mission 1");
    collect(world, *scene);
    logMemory("frame 120");
    const Scene& s = *scene;
    say("records: opaque %d, trans %d, effect %d (dropped %d), dynamic lights %d, fog %d (%.0f..%.0f), "
        "camera fov %.1f far %.0f\n",
        static_cast<int>(s.opaque.size()), static_cast<int>(s.trans.size()), static_cast<int>(s.effect.size()),
        s.dropped, static_cast<int>(s.lights.size()), s.hasFog ? 1 : 0, static_cast<double>(s.light.fogStart),
        static_cast<double>(s.light.fogEnd), static_cast<double>(s.wv.fovY), static_cast<double>(s.wv.farPlane));

    MeshRenderer mr;
    if (!mr.init(&err)) say("MeshRenderer::init: %s\n", err.c_str());
    const HeliView heli = heliView(s, world);
    say("test a: %d records on the player (entity %d)\n", static_cast<int>(heli.recs.size()), s.player);

    Passes worldPasses, litPasses;
    preparePasses(s, s.lights, worldPasses);
    std::vector<DynamicLight> extra;
    Vec3 pp{0, 0, 0};
    if (world.validIndex(s.player)) pp = world.entity(s.player).v3(F_BASE_ORIGIN);
    // First, so the 32-light cap never drops them.
    extra.push_back(DynamicLight::point(pp + Vec3{14.0f, 0.0f, 8.0f}, Vec3{1.0f, 0.15f, 0.15f}, 40.0f));
    extra.push_back(DynamicLight::point(pp + Vec3{-10.0f, 12.0f, 8.0f}, Vec3{0.15f, 1.0f, 0.15f}, 40.0f));
    extra.push_back(DynamicLight::point(pp + Vec3{-4.0f, -14.0f, 6.0f}, Vec3{0.25f, 0.25f, 1.0f}, 40.0f));
    extra.insert(extra.end(), s.lights.begin(), s.lights.end());
    preparePasses(s, extra, litPasses);

    // Checks per test (GL projections, upright pixels).
    {
        std::vector<const Record*> all;
        for (const auto* l : {&s.opaque, &s.trans, &s.effect})
            for (const Record& r : *l) all.push_back(&r);
        const Mat4 glWorld = s.wv.projection * s.wv.view;
        writeChecks(dir + "/meshtest_checks_b.txt", all, glWorld, s.clear, s.wv.nearPlane, s.wv.fovY,
                    s.hasFog ? s.light.fogStart : 1e30f);
        writeChecks(dir + "/meshtest_checks_c.txt", all, glWorld, s.clear, s.wv.nearPlane, s.wv.fovY,
                    s.hasFog ? s.light.fogStart : 1e30f);
        writeChecks(dir + "/meshtest_checks_a.txt", heli.recs, heli.cam.viewProjMatrix(), kHeliClear,
                    heli.cam.nearPlane, heli.cam.fovYDegrees, 1e30f);
    }

    const int hold = holdFrames > 0 ? holdFrames : 300;
    const char* names[3] = {"a", "b", "c"};
    bool quit = false;
    for (int cycle = 0; !quit; ++cycle) {
        for (int t = 0; t < 3 && !quit; ++t) {
            double gpuSum = 0, cpuSum = 0;
            int measured = 0;
            pica::MeshDrawStats stats;
            for (int frame = 0; frame < hold; ++frame) {
                if (!aptMainLoop()) {
                    quit = true;
                    break;
                }
                hidScanInput();
                if (hidKeysDown() & KEY_START) {
                    quit = true;
                    break;
                }
                C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
                pica::frameBegin();
                pica::drawOn(pica::FrameTarget{top, kW, kH, true});
                if (t == 0) drawHeli(s, heli, mr);
                else drawWorld(s, t == 1 ? worldPasses : litPasses);
                stats = pica::meshFrameStats();
                drawMarkers();
                pica::frameEnd();
                C3D_FrameEnd(0);
                if (frame >= 2) {
                    gpuSum += C3D_GetDrawingTime();
                    cpuSum += C3D_GetProcessingTime();
                    ++measured;
                }
                if (frame == 2 && cycle == 0) {
                    const std::string ppm = dir + "/meshtest_" + names[t] + ".ppm";
                    say("readback %s: %s\n", ppm.c_str(), dumpTarget(top, ppm) ? "ok" : "FAILED");
                    say("meshtest: showing %s\n", names[t]);
                }
            }
            if (measured > 0 && cycle == 0)
                say("test %s: draws %d, vertices %d, triangles %d, bytes %u; GPU %.2f ms, C3D cpu %.2f ms (%d frames)\n",
                    names[t], stats.draws, stats.vertices, stats.triangles, static_cast<unsigned>(stats.bytes),
                    gpuSum / measured, cpuSum / measured, measured);
        }
        if (holdFrames > 0) break;
    }
    say("meshtest: done; arena high water %u B, failures %u; deferred frees pending %u\n",
        static_cast<unsigned>(pica::frameArena().highWater()), pica::frameArena().failures(),
        static_cast<unsigned>(pica::deferredFreeCount()));
    // An empty frame: C3D_FrameBegin waits until the GPU is done with the textures. Meshes and
    // textures then go back (the mesh frees are deferred to the backend's shutdown).
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    pica::frameBegin();
    pica::frameEnd();
    C3D_FrameEnd(0);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C3D_FrameEnd(0);
    scene.reset();
    logMemory("freed");
    pica::shutdownBackend();
    C3D_RenderTargetDelete(top);
    session.reset();
    if (g_log) std::fclose(g_log);
    g_log = nullptr;
}

} // namespace meshtest
