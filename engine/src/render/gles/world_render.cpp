// WorldRenderer (as3d/world_render.h): the live world in the pass order of
// docs/spec/render-pipeline.md 1.1, using the entity fields the way section 3 describes
// (base origin 41..43, axis rows 44..52, scale 32, colour 28..31, frame 33, model handle,
// skin, FL_NODRAW). Every pass is one of the engine's renderers, fed from the entities.
//
// as3d/defs.h and as3d/gfx.h both define as3d::BlendMode; defs.h's is renamed locally for
// this file, as in engine/src/render/material.cpp.
#include "as3d/world_render.h"

#include <GLES3/gl3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <unordered_map>

#include "as3d/dynamic_lights.h"
#include "as3d/ground_marks.h"
#include "as3d/health_bar.h"
#include "as3d/lightning_render.h"
#include "as3d/load_stats.h"
#include "as3d/particle_render.h"
#include "as3d/scene.h"
#include "as3d/shadow_render.h"
#include "as3d/sprite_render.h"
#include "as3d/terrain_render.h"
#include "as3d/vfs.h"

#define BlendMode AS3D_DEFS_BlendMode
#include "as3d/defs.h"
#undef BlendMode

namespace as3d {

// ---------------------------------------------------------------------------------------
// Camera.
// ---------------------------------------------------------------------------------------

WorldView worldViewOf(const World& world, float aspect) {
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

namespace {

// A lookAt camera reproducing an arbitrary rigid view matrix: MeshRenderer takes a Camera.
Camera cameraFromView(const WorldView& wv, float aspect) {
    Camera cam;
    cam.eye = wv.eye;
    // Rows of the view rotation are the camera right, up and backward axes in world space.
    Vec3 up{wv.view.at(0, 1), wv.view.at(1, 1), wv.view.at(2, 1)};
    Vec3 back{wv.view.at(0, 2), wv.view.at(1, 2), wv.view.at(2, 2)};
    cam.target = wv.eye - back;
    cam.worldUp = up;
    cam.fovYDegrees = wv.fovY;
    cam.aspect = aspect;
    cam.nearPlane = wv.nearPlane;
    cam.farPlane = wv.farPlane;
    return cam;
}

// List capacities of render-pipeline.md 1.2 (sprites and marks are bounded by their
// renderers: kMaxSprites, GroundMarkRenderer::kMaxMarks).
constexpr int kOpaqueCap = 512;
constexpr int kTransCap = 128;
constexpr int kEffectCap = 256;

// ---------------------------------------------------------------------------------------
// Brightness overlay (render-pipeline.md 1.5): one untextured quad, blend (DST_COLOR,
// SRC_COLOR), colour (b, b, b).
// ---------------------------------------------------------------------------------------

const char* const kBrightnessVertexSrc = R"(#version 300 es
layout(location = 0) in vec2 aPos;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); }
)";

const char* const kBrightnessFragmentSrc = R"(#version 300 es
precision mediump float;
uniform vec3 uColor;
out vec4 fragColor;
void main() { fragColor = vec4(uColor, 1.0); }
)";

class BrightnessPass {
public:
    bool init(std::string* error) {
        if (!program_.compile(kBrightnessVertexSrc, kBrightnessFragmentSrc, error)) return false;
        const float quad[12] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
        vbo_.upload(quad, sizeof quad);
        VertexLayout layout;
        layout.strideBytes = static_cast<int>(2 * sizeof(float));
        layout.attribs = {{0, 2, 0, false}};
        vao_.create(vbo_, layout);
        return true;
    }
    void draw(float b) {
        if (!program_.valid()) return;
        program_.use();
        program_.setVec3("uColor", Vec3{b, b, b});
        glEnable(GL_BLEND);
        glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
        setDepth(false, false);
        setCull(CullMode::Off);
        vao_.bind();
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisable(GL_BLEND);
        setDepth(true, true);
        setCull(CullMode::Back);
    }

private:
    ShaderProgram program_;
    VertexBuffer vbo_;
    VertexArray vao_;
};

struct MeshRecord {
    const GpuMesh* mesh;
    const Material* material;
    Mat4 model;
    Vec4 colour;
};

DecalBlend decalBlendOf(AS3D_DEFS_BlendMode b) {
    switch (b) {
        case AS3D_DEFS_BlendMode::Alpha: return DecalBlend::Alpha;
        case AS3D_DEFS_BlendMode::Add: return DecalBlend::Add;
        case AS3D_DEFS_BlendMode::Filter: return DecalBlend::Filter;
        default: return DecalBlend::None;
    }
}

struct ShadowSpec {
    bool any = false;
    ShadowKind kind = ShadowKind::Planar;
    ShadowQuality quality = ShadowQuality::Normal;
};

// obj.md "shadow" after the parser's remap (render-pipeline.md 5.1).
ShadowSpec shadowSpecOf(ShadowMode m) {
    switch (m) {
        case ShadowMode::Projected: return {true, ShadowKind::Projected, ShadowQuality::Normal};
        case ShadowMode::ProjectedLow: return {true, ShadowKind::Projected, ShadowQuality::Low};
        case ShadowMode::ProjectedHigh: return {true, ShadowKind::Projected, ShadowQuality::High};
        case ShadowMode::Planar: return {true, ShadowKind::Planar, ShadowQuality::Normal};
        case ShadowMode::PlanarLow: return {true, ShadowKind::Planar, ShadowQuality::Low};
        case ShadowMode::PlanarHigh: return {true, ShadowKind::Planar, ShadowQuality::High};
        default: return {};
    }
}

int wrapSteps(int s) { return ((s % 12) + 12) % 12; }

// Per entity slot: what the renderer remembers about the entity since it first saw it
// (the original builds mark and projected-shadow display lists once, at spawn).
struct SlotMemory {
    u32 generation = 0;
    bool seen = false;
    Vec3 spawnOrigin;
    int shadowSteps = 0;
};

} // namespace

// ---------------------------------------------------------------------------------------
// Renderer state.
// ---------------------------------------------------------------------------------------

struct WorldRenderer::Impl {
    Vfs* vfs = nullptr;
    const DefDatabase* db = nullptr;
    std::unique_ptr<ResourceCache> cache;
    MeshRenderer meshes;
    ParticleRenderer particles;
    SpriteRenderer sprites;
    GroundMarkRenderer marks;
    ShadowRenderer shadows;
    LightningRenderer lightning;
    BrightnessPass brightness;
    std::unique_ptr<TerrainRenderer> terrain;
    std::unique_ptr<WaterRenderer> water;
    const Terrain* terrainOf = nullptr;
    RenderRules rules = renderRules(GameId::AirStrike3D);
    SkidTrailRenderer skids;
    bool skidsReady = false;
    std::vector<SkidTrail> skidTrails; // the world's trails of this frame, as the pass takes them
    size_t skidTrailCount = 0;
    // The world's terrain revision whose heights the meshes hold (World::terrainChangesSince).
    u32 terrainRevision = 0;
    std::vector<TerrainChange> changeScratch;
    std::vector<VertexRect> rectScratch;

    void followTerrainChanges(const World& world);
    void collectSkidTrails(const World& world);
    Vec3 towardsSun{0.0f, 0.0f, 1.0f};
    // Materials by (definition, model, skin); unique_ptr keeps addresses stable.
    std::map<std::pair<const ObjectDef*, std::string>, std::unique_ptr<Material>> materials;
    // Shadow maps by model|skin|kind|quality|steps; null when generation failed.
    std::unordered_map<std::string, std::unique_ptr<ShadowMap>> shadowMaps;
    int lateShadowMaps = 0;
    std::vector<SlotMemory> memory;
    // Frame scratch.
    std::vector<MeshRecord> opaque, trans, effect;
    std::vector<ShadowInstance> shadowList;
    std::vector<SpriteInstance> spriteList;
    std::vector<const ParticleEmitter*> emitters;
    std::vector<Vec3> boltStarts, boltEnds;
    // Enemy health bars (engine-behaviour.md 6.5): hbar_empty / hbar_full.
    HealthBarSpriteDef hbarEmpty, hbarFull;
    bool hbarReady = false;
    std::vector<int> order;
    std::vector<char> visited;
    DynamicLightList lights;
    // Marks: rebuilt only when the set (or a colour) changes.
    std::vector<std::pair<GroundMarkDesc, std::uint64_t>> markDescs, lastMarkDescs;
    int dropped = 0;
    // What prepare() read from the world for the draw() calls that follow it.
    struct Frame {
        bool ready = false;
        Viewport viewport;
        WorldRenderOptions options;
        float aspect = 1.0f;
        WorldView wv;
        CullFrustum cull;
        const Terrain* terrain = nullptr;
        bool haveTerrain = false;
        SceneLighting light;
        bool hasFog = false;
        float time = 0.0f;
        float mapPos = 0.0f;
        const Texture2D* boltTexture = nullptr;
    } frame;

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

    // The silhouette of a model record (render-pipeline.md 5.2), generated on first request.
    const ShadowMap* shadowMap(const GpuMesh& mesh, const std::string& modelPath, const Material& mat,
                               const std::string& skinKey, const ShadowSpec& ss, int steps, bool late) {
        if (ss.kind == ShadowKind::Planar) steps = 0;
        std::string key = modelPath + "|" + skinKey + "|" + std::to_string(static_cast<int>(ss.kind)) + "|" +
                          std::to_string(static_cast<int>(ss.quality)) + "|" + std::to_string(steps);
        auto it = shadowMaps.find(key);
        if (it != shadowMaps.end()) return it->second.get();
        std::unique_ptr<ShadowMap> map(new ShadowMap());
        if (!shadows.generate(mesh.model, mat.texture, ss.kind, towardsSun, steps, ss.quality, *map)) map.reset();
        if (late) {
            ++lateShadowMaps;
            LoadStats::count(LoadKind::Shadow); // a first-use load (as3d/load_stats.h)
        }
        const ShadowMap* p = map.get();
        shadowMaps.emplace(key, std::move(map));
        return p;
    }

    // Level-load precache of the shadows of every placed object and its attachments.
    void precacheShadows(const ObjectDef* def, int steps, bool night, int depth) {
        if (!def || depth > kMaxAttachDepth) return;
        ShadowSpec ss = shadowSpecOf(def->shadow);
        if (ss.any && def->type == ObjectType::Model && !def->model.empty() && def->sort != SortMode::Effect) {
            const GpuMesh& mesh = cache->mesh(def->model);
            if (mesh.valid) {
                const Material& mat = materialFor(*def, std::string());
                shadowMap(mesh, normalizePath(def->model), mat, std::string(), ss, steps, false);
            }
        }
        for (const AttachDef& at : def->attachments) {
            if (at.nightOnly && !night) continue;
            if (db->findParticleSystem(at.targetName)) continue;
            precacheShadows(db->findObject(at.targetName), steps, night, depth + 1);
        }
    }

    // ---------------------------------------------------------------------------------------
    // Level-start precache (WorldRenderer::precache).
    // ---------------------------------------------------------------------------------------

    // One definition of the closure: what prepare() loads when an entity of it is first drawn.
    // `swapped`: empty for the definition's own model, else one its script swaps in (SetModel).
    void precacheObject(const ObjectDef& def, const std::string& swapped = std::string()) {
        for (const SkidMarkDef& sk : def.skidMarks)
            if (!sk.texture.empty()) cache->texture(sk.texture);
        switch (def.type) {
            case ObjectType::Model: {
                const std::string& model = swapped.empty() ? def.model : swapped;
                if (model.empty()) return;
                const GpuMesh& mesh = cache->mesh(model);
                if (!mesh.valid) return;
                const Material& mat = materialFor(def, std::string());
                const ShadowSpec ss = shadowSpecOf(def.shadow);
                if (!ss.any || def.sort == SortMode::Effect) return;
                // A spawned entity's projected shadow takes its root's yaw in 30 degree steps:
                // any of the 12.
                const int steps = ss.kind == ShadowKind::Projected ? 12 : 1;
                for (int s = 0; s < steps; ++s) shadowMap(mesh, normalizePath(model), mat, std::string(), ss, s, false);
                return;
            }
            default:
                if (!def.skin.empty()) cache->texture(def.skin);
                return;
        }
    }

    // ParticleRenderer loads an emitter's texture the first time it draws it, from a cache of
    // its own (engine/src/render/gles/particle_renderer.cpp, which has no preload): draw one
    // invisible particle (size 0, alpha 0, scissored to nothing) per texture.
    void precacheParticleTextures(const std::vector<const ParticleSystemDef*>& systems) {
        std::vector<std::string> seen;
        setScissor(true, 0, 0, 0, 0);
        for (const ParticleSystemDef* ps : systems) {
            ParticleDesc d = ParticleDesc::fromDef(*ps);
            if (d.texture.empty()) continue;
            const std::string key = normalizePath(d.texture);
            if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
            seen.push_back(key);
            d.emitOnce = true;
            d.emitRate = 1;
            d.lifeTime = 1.0f;
            d.initSize[0] = d.initSize[1] = 0.0f;
            d.sizeRate = 0.0f;
            for (float& c : d.initColor) c = 0.0f;
            ParticleEmitter e(d, 1u);
            ParticleViewParams pv;
            pv.view = Mat4();
            pv.projection = Mat4();
            particles.draw(e, pv);
        }
        setScissor(false);
    }

    // Looks up the health bar objects named by the rules; hbarReady is false without them.
    void resolveHealthBar(const GameRules& rules) {
        Impl& im = *this;
        const ObjectDef* he = rules.healthBarEmptyObject ? im.db->findObject(rules.healthBarEmptyObject) : nullptr;
        const ObjectDef* hf = rules.healthBarFullObject ? im.db->findObject(rules.healthBarFullObject) : nullptr;
        im.hbarReady = he && hf && he->hasBbox && hf->hasBbox;
        if (im.hbarReady) {
            HealthBarSpriteDef* outs[2] = {&im.hbarEmpty, &im.hbarFull};
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
                o.texture = d.skin.empty() ? nullptr : im.cache->texture(d.skin).texture;
                o.blend = decalBlendOf(d.blend);
                o.noDepthTest = (d.rflag & RF_NODEPTHTEST) != 0;
                o.noDepthWrite = (d.rflag & RF_NODEPTHWRITE) != 0;
            }
        }
    }
};

WorldRenderer::WorldRenderer() : impl_(new Impl) {}
WorldRenderer::~WorldRenderer() = default;

bool WorldRenderer::init(Vfs& vfs, const DefDatabase& db, std::string* error) {
    Impl& im = *impl_;
    im.vfs = &vfs;
    im.db = &db;
    im.cache.reset(new ResourceCache(vfs));
    im.memory.assign(static_cast<size_t>(kMaxEntitySlots), SlotMemory());
    if (!im.meshes.init(error)) return false;
    if (!im.particles.init(vfs, error)) return false;
    if (!im.sprites.init(error)) return false;
    if (!im.marks.init(error)) return false;
    if (!im.shadows.init(error)) return false;
    if (!im.lightning.init(error)) return false;
    im.resolveHealthBar(defaultGameRules());
    if (!im.brightness.init(error)) return false;
    return true;
}

bool WorldRenderer::beginLevel(const World& world, std::string* error) {
    Impl& im = *impl_;
    im.resolveHealthBar(world.rules());
    // The game's render rules (as3d/render_rules.h), from the world's game rules.
    im.rules = renderRulesFor(world.rules());
    im.cache->setMissingTextureWhite(im.rules.missingTextureWhite);
    im.meshes.setEnvViewNormal(im.rules.envViewNormal);
    im.shadows.setRules(im.rules);
    if (im.rules.skidMarkPass && !im.skidsReady) {
        if (!im.skids.init(*im.cache, skidTrailParams(world.rules()), error)) return false;
        im.skidsReady = true;
    }
    im.terrain.reset();
    im.water.reset();
    im.terrainOf = nullptr;
    im.shadowMaps.clear();
    im.lateShadowMaps = 0;
    im.memory.assign(static_cast<size_t>(kMaxEntitySlots), SlotMemory());
    im.marks.clear();
    im.lastMarkDescs.clear();
    // The meshes are built from the heights as they are now (craters of the load included).
    im.terrainRevision = world.terrainRevision();
    const Terrain* t = world.terrain();
    if (!t) return true; // an empty test level: nothing to build
    std::unique_ptr<TerrainRenderer> tr(new TerrainRenderer());
    std::unique_ptr<WaterRenderer> wr(new WaterRenderer());
    if (!tr->build(*t, *im.vfs, error, im.rules)) return false;
    if (im.rules.water == WaterStyle::WaveGrid) {
        // The shine texture is not in TerrainStyle: the level's definition has it.
        std::string shine;
        for (const LevelDef& d : im.db->levels())
            if (d.id == t->style().id) shine = d.waterShine;
        const WaterSurface surface = buildWaterSurface(*t, true, shine);
        if (!wr->build(*t, surface, TerrainGridView::of(*t), *im.vfs, error)) return false;
    } else if (!wr->build(*t, *im.vfs, error)) {
        return false;
    }
    im.terrain = std::move(tr);
    im.water = std::move(wr);
    im.terrainOf = t;
    const TerrainStyle& st = t->style();
    Vec3 sun{st.sun[3], st.sun[4], st.sun[5]};
    im.towardsSun = length(sun) > 1e-6f ? normalize(sun) : Vec3{0.0f, 0.0f, 1.0f};
    // Shadow silhouettes of the map objects, at level load like the original's precache
    // (engine-behaviour.md 10.2 step 7). Objects created later by scripts get theirs on
    // first sight (counted in lateShadowMaps()).
    if (const LevelData* level = t->level()) {
        for (const Placement& p : level->placements)
            im.precacheShadows(im.db->findObject(level->typeName(p)), p.rotationSteps, st.night, 0);
    }
    return true;
}

PrecacheReport WorldRenderer::precache(const World& world, const DefDatabase& db, const std::function<void(float)>& progress) {
    Impl& im = *impl_;
    const auto t0 = std::chrono::steady_clock::now();
    const LoadCounts before = LoadStats::lazyLoads();
    const size_t shadowsBefore = im.shadowMaps.size();
    PrecacheSet local;
    const PrecacheSet* set = world.precacheSet();
    if (!set) {
        local = collectPrecacheSet(world, db, [&world](const std::string& path) { return world.compiledProgram(path); });
        set = &local;
    }
    const size_t total = set->objects.size() + set->models.size() + set->textures.size() + 2;
    size_t done = 0;
    auto step = [&]() {
        ++done;
        if (progress && (done % 16 == 0 || done == total)) progress(static_cast<float>(done) / static_cast<float>(total));
    };
    for (const ObjectDef* def : set->objects) {
        im.precacheObject(*def);
        step();
    }
    for (const std::string& m : set->models) {
        im.cache->mesh(m);
        step();
    }
    for (const auto& om : set->objectModels)
        if (om.first->type == ObjectType::Model) im.precacheObject(*om.first, om.second);
    for (const std::string& t : set->textures) {
        im.cache->texture(t);
        step();
    }
    // The Lightning builtin's bolt (render-pipeline.md 7.1), wherever a script calls it.
    if (im.vfs->exists("gfx\\lightning2.tga")) im.cache->texture("gfx\\lightning2.tga");
    step();
    im.precacheParticleTextures(set->particleSystems);
    step();
    const LoadCounts d = LoadStats::lazyLoads() - before;
    PrecacheReport r;
    r.objects = static_cast<int>(set->objects.size());
    // Particle textures are read outside a counted site (Other).
    r.textures = static_cast<int>(d.of(LoadKind::Texture) + d.of(LoadKind::Other));
    r.models = static_cast<int>(d.of(LoadKind::Model));
    r.scripts = static_cast<int>(d.of(LoadKind::Script));
    r.shadows = static_cast<int>(im.shadowMaps.size() - shadowsBefore);
    r.bytes = d.bytes;
    r.capped = set->capped;
    r.seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

void WorldRenderer::Impl::followTerrainChanges(const World& world) {
    if (!terrainOf || world.terrain() != terrainOf) return;
    const u32 rev = world.terrainRevision();
    if (rev == terrainRevision) return;
    world.terrainChangesSince(terrainRevision, changeScratch);
    terrainRevision = rev;
    rectScratch.clear();
    for (const TerrainChange& c : changeScratch) {
        VertexRect v;
        v.c0 = c.c0;
        v.r0 = c.r0;
        v.c1 = c.c1;
        v.r1 = c.r1;
        if (!v.empty()) rectScratch.push_back(v);
    }
    if (rectScratch.empty()) return;
    const TerrainGridView grid = TerrainGridView::of(*terrainOf);
    if (terrain) terrain->update(rectScratch.data(), rectScratch.size(), grid);
    if (water) water->update(rectScratch.data(), rectScratch.size(), grid);
}

void WorldRenderer::Impl::collectSkidTrails(const World& world) {
    const size_t n = world.liveSkidTrailCount();
    if (skidTrails.size() < n) skidTrails.resize(n);
    skidTrailCount = 0;
    for (size_t i = 0; i < n; ++i) {
        const WorldSkidTrail& src = world.liveSkidTrail(i);
        if (src.nodeCount < 2) continue; // a strip needs two sections
        SkidTrail& t = skidTrails[skidTrailCount++];
        t.texture = src.texture ? *src.texture : std::string();
        t.nodes.resize(static_cast<size_t>(src.nodeCount));
        for (int k = 0; k < src.nodeCount; ++k) {
            const WorldSkidNode& a = src.nodes[static_cast<size_t>(k)];
            SkidNode& b = t.nodes[static_cast<size_t>(k)];
            b.position = a.position;
            b.direction = a.direction;
            b.width = src.width;
            b.age = a.age;
            b.distance = a.distance;
        }
    }
}

const RenderRules& WorldRenderer::rules() const { return impl_->rules; }

int WorldRenderer::shadowMapCount() const { return static_cast<int>(impl_->shadowMaps.size()); }
int WorldRenderer::lateShadowMaps() const { return impl_->lateShadowMaps; }

// ---------------------------------------------------------------------------------------
// Frame.
// ---------------------------------------------------------------------------------------

namespace {

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

const std::string& skinOf(const Entity& e) { return e.skinPath.empty() ? e.def->skin : e.skinPath; }

int rootOfEntity(const World& w, int i) {
    for (int guard = 0; guard < 64 && w.validIndex(i) && w.entity(i).parent >= 0; ++guard) i = w.entity(i).parent;
    return i;
}

void visitThinkOrder(const World& w, int i, std::vector<int>& order, std::vector<char>& visited, int depth) {
    if (depth > 64 || !w.validIndex(i) || visited[static_cast<size_t>(i)]) return;
    const Entity& e = w.entity(i);
    if (e.rt & RT_REMOVED) return;
    visited[static_cast<size_t>(i)] = 1;
    // An attached pool entity (AttachEntity) makes its root think first
    // (engine-behaviour.md 4.1), so the root's records are submitted first.
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

std::uint64_t colourKey(const Vec3& c) {
    u32 r, g, b;
    std::memcpy(&r, &c.x, 4);
    std::memcpy(&g, &c.y, 4);
    std::memcpy(&b, &c.z, 4);
    return (static_cast<std::uint64_t>(r) << 32) ^ (static_cast<std::uint64_t>(g) << 16) ^ b;
}

bool sameMarks(const std::vector<std::pair<GroundMarkDesc, std::uint64_t>>& a, const std::vector<std::pair<GroundMarkDesc, std::uint64_t>>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].second != b[i].second || colourKey(a[i].first.colour) != colourKey(b[i].first.colour) ||
            a[i].first.alpha != b[i].first.alpha || a[i].first.texture != b[i].first.texture)
            return false;
    }
    return true;
}

} // namespace

void worldRenderOrder(const World& world, std::vector<int>& order, std::vector<char>& visited) {
    order.clear();
    visited.assign(static_cast<size_t>(kMaxEntitySlots), 0);
    for (int i : world.listEntities()) visitThinkOrder(world, i, order, visited, 0);
}

void WorldRenderer::render(const World& world, int width, int height, const WorldRenderOptions& options) {
    render(world, 0, 0, width, height, options);
}

void WorldRenderer::render(const World& world, int vx, int vy, int width, int height, const WorldRenderOptions& options) {
    Viewport vp;
    vp.x = vx;
    vp.y = vy;
    vp.width = width;
    vp.height = height;
    prepare(world, vp, options);
    draw(EyeView{});
}

const CullFrustum& WorldRenderer::preparedFrustum() const { return impl_->frame.cull; }

void WorldRenderer::prepare(const World& world, const Viewport& viewport, const WorldRenderOptions& options) {
    Impl& im = *impl_;
    Impl::Frame& fr = im.frame;
    stats_ = WorldRenderStats();
    fr.ready = false;
    if (viewport.width <= 0 || viewport.height <= 0) return;
    fr.viewport = viewport;
    fr.options = options;
    fr.aspect = static_cast<float>(viewport.width) / static_cast<float>(viewport.height);
    fr.wv = worldViewOf(world, fr.aspect);
    fr.cull = eyeCullFrustum(fr.wv, fr.aspect, viewport.maxEyeOffset, viewport.minFocalDistance);
    const Terrain* terrain = world.terrain();
    fr.terrain = terrain;
    fr.haveTerrain = terrain && im.terrain && im.terrainOf == terrain;
    fr.time = world.time();
    fr.mapPos = world.mapPos();
    // Craters since the last frame: the meshes follow the world's heights (read only).
    im.followTerrainChanges(world);

    // Lighting and fog from the level (render-pipeline.md 2.2, 2.3).
    SceneLighting& light = fr.light;
    light = SceneLighting();
    fr.hasFog = false;
    if (terrain) {
        const TerrainStyle& st = terrain->style();
        light.sunColor = Vec3{st.sun[0], st.sun[1], st.sun[2]};
        light.sunDirection = Vec3{st.sun[3], st.sun[4], st.sun[5]};
        light.ambientColor = Vec3{st.sun[6], st.sun[7], st.sun[8]};
        light.fogColor = Vec3{st.fogColor[0], st.fogColor[1], st.fogColor[2]};
        light.fogStart = st.fogNear;
        light.fogEnd = st.fogFar;
        fr.hasFog = st.hasFog;
    }
    if (!fr.hasFog) {
        light.fogStart = 1.0e8f;
        light.fogEnd = 2.0e8f;
    }

    // Dynamic lights (2.4): the PlaceLight queue of this frame, then the definition lights
    // of the drawn entities; the first 32 are kept.
    im.lights.clear();
    if (options.lights) {
        for (const QueuedLight& q : world.lights()) im.lights.add(DynamicLight::point(q.pos, q.color, q.radius));
    }

    // Render records in the order the entity pass submits them (1.2).
    im.opaque.clear();
    im.trans.clear();
    im.effect.clear();
    im.shadowList.clear();
    im.spriteList.clear();
    im.markDescs.clear();
    im.dropped = 0;
    worldRenderOrder(world, im.order, im.visited);
    // The health bar follows the entity's own records (engine-behaviour.md 2, 6.5); the
    // think draws it whether or not the entity itself is drawn.
    auto addHealthBar = [&](const Entity& e) {
        if (!options.sprites || !im.hbarReady) return;
        SpriteInstance bar[2];
        int n = buildHealthBar(e, im.hbarEmpty, im.hbarFull, bar);
        for (int k = 0; k < n; ++k) {
            if (im.spriteList.size() >= kMaxSprites) {
                ++im.dropped;
                break;
            }
            im.spriteList.push_back(bar[k]);
        }
        stats_.healthBars += n > 0 ? 1 : 0;
    };
    for (int i : im.order) {
        const Entity& e = world.entity(i);
        if (!e.def || (e.flagBits() & FL_NODRAW)) {
            addHealthBar(e);
            continue;
        }
        const ObjectDef& def = *e.def;
        SlotMemory& mem = im.memory[static_cast<size_t>(i)];
        if (!mem.seen || mem.generation != e.generation) {
            mem.seen = true;
            mem.generation = e.generation;
            mem.spawnOrigin = e.v3(F_BASE_ORIGIN);
            // Projected shadow key (render-pipeline.md 5.3): the root's spawn key (placement
            // byte, or int(yaw) for `create`); other spawns: the root's yaw in 30 degree
            // steps when first seen (docs/spec/issues/050, 112).
            int r = rootOfEntity(world, i);
            if (world.validIndex(r) && world.entity(r).hasShadowKey) {
                mem.shadowSteps = wrapSteps(world.entity(r).shadowKey);
            } else {
                float yaw = world.validIndex(r) ? world.entity(r).f(F_ANGLES + 2) : 0.0f;
                mem.shadowSteps = (yaw > -1.0e6f && yaw < 1.0e6f) ? wrapSteps(static_cast<int>(std::lround(yaw / 30.0f))) : 0;
            }
        }
        const Vec4 colour{e.f(F_COLOR), e.f(F_COLOR + 1), e.f(F_COLOR + 2), e.f(F_COLOR + 3)};
        const Vec3 origin = e.v3(F_BASE_ORIGIN);
        if (options.lights && (def.hasLight || def.hasLightDir) && def.lightRadius > 0) {
            Vec3 col{def.lightColor[0], def.lightColor[1], def.lightColor[2]};
            float radius = static_cast<float>(def.lightRadius);
            if (def.hasLightDir) {
                Vec3 d = e.v3(F_AXIS) * def.lightDir[0] + e.v3(F_AXIS + 3) * def.lightDir[1] +
                         e.v3(F_AXIS + 6) * def.lightDir[2];
                im.lights.add(DynamicLight::spotLight(origin, col, radius, d, def.lightConeAngle));
            } else {
                im.lights.add(DynamicLight::point(origin, col, radius));
            }
        }
        switch (def.type) {
            case ObjectType::Model: {
                // The entity's model handle: SetModel swaps it (damaged helicopters).
                if (!options.models || !e.model || e.modelPath.empty()) break;
                const GpuMesh& mesh = im.cache->mesh(e.modelPath);
                if (!mesh.valid) break;
                std::vector<MeshRecord>* list = &im.opaque;
                int cap = kOpaqueCap;
                if (def.sort == SortMode::Trans) {
                    list = &im.trans;
                    cap = kTransCap;
                } else if (def.sort == SortMode::Effect) {
                    list = &im.effect;
                    cap = kEffectCap;
                }
                if (static_cast<int>(list->size()) >= cap) {
                    ++im.dropped;
                    break;
                }
                const Material& mat = im.materialFor(def, e.skinPath);
                list->push_back({&mesh, &mat, entityMatrix(e), colour});
                // Shadows (5.3): opaque and transparent records only.
                ShadowSpec ss = shadowSpecOf(def.shadow);
                if (options.shadows && ss.any && def.sort != SortMode::Effect) {
                    const ShadowMap* map = im.shadowMap(mesh, e.modelPath, mat, e.skinPath, ss, mem.shadowSteps, true);
                    if (map) {
                        ShadowInstance si;
                        si.map = map;
                        if (ss.kind == ShadowKind::Projected) {
                            si.origin = mem.spawnOrigin; // baked at spawn, never moves
                        } else {
                            si.origin = origin;
                            Vec3 fw = e.v3(F_AXIS); // heading of the local X axis (issue 070)
                            si.yawDegrees = radToDeg(std::atan2(fw.y, fw.x));
                        }
                        im.shadowList.push_back(si);
                    }
                }
                break;
            }
            case ObjectType::Mark: {
                if (!options.sprites || !def.hasBbox) break;
                if (im.markDescs.size() >= GroundMarkRenderer::kMaxMarks) {
                    ++im.dropped;
                    break;
                }
                const std::string& skin = skinOf(e);
                if (skin.empty()) break;
                GroundMarkDesc d;
                d.origin = mem.spawnOrigin; // the decal is fixed where it spawned (3.4)
                d.minX = def.bboxMin[0];
                d.minY = def.bboxMin[1];
                d.maxX = def.bboxMax[0];
                d.maxY = def.bboxMax[1];
                d.texture = im.cache->texture(skin).texture;
                d.blend = decalBlendOf(def.blend);
                d.colour = Vec3{colour.x, colour.y, colour.z};
                d.alpha = im.rules.markEntityAlpha ? colour.w : 1.0f;
                d.unflippedTexture = im.rules.markTextureUnflipped;
                im.markDescs.push_back({d, (static_cast<std::uint64_t>(i) << 32) | e.generation});
                break;
            }
            case ObjectType::Sprite:
            case ObjectType::HSprite:
            case ObjectType::VSprite: {
                if (!options.sprites || !def.hasBbox) break;
                if (im.spriteList.size() >= kMaxSprites) {
                    ++im.dropped;
                    break;
                }
                const std::string& skin = skinOf(e);
                if (skin.empty()) break;
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
                    sp.frameCols = def.frameStart; // columns and rows (render-pipeline.md 11.3)
                    sp.frameRows = std::max(def.frameEnd, 1);
                    float fr = e.f(F_FRAME);
                    sp.frame = (fr > -1.0e7f && fr < 1.0e7f) ? static_cast<int>(fr) : 0;
                }
                sp.texture = im.cache->texture(skin).texture;
                sp.blend = decalBlendOf(def.blend);
                sp.noDepthTest = (def.rflag & RF_NODEPTHTEST) != 0;
                sp.noDepthWrite = (def.rflag & RF_NODEPTHWRITE) != 0;
                im.spriteList.push_back(sp);
                break;
            }
        }
        addHealthBar(e);
    }

    // Ground marks: their geometry is cut from the terrain when the set changes.
    if (fr.haveTerrain && !sameMarks(im.markDescs, im.lastMarkDescs)) {
        im.marks.clear();
        for (const auto& m : im.markDescs) im.marks.addMark(*terrain, m.first);
        im.lastMarkDescs = im.markDescs;
    }
    // The sequels' skid marks (as2 delta 1.1 pass 5).
    if (fr.haveTerrain && im.rules.skidMarkPass && im.skidsReady && options.sprites) im.collectSkidTrails(world);
    else im.skidTrailCount = 0;
    // Lightning bolts: effect-list records (sort 3) submitted by the Lightning builtin during
    // the entity pass (7.1).
    im.boltStarts.clear();
    im.boltEnds.clear();
    for (const LightningBolt& b : world.lightningBolts()) {
        im.boltStarts.push_back(b.start);
        im.boltEnds.push_back(b.end);
    }
    fr.boltTexture = im.boltStarts.empty() ? nullptr : im.cache->texture("gfx\\lightning2.tga").texture;
    // Particles: the world's live emitters.
    if (options.particles) world.particles().collect(im.emitters);
    else im.emitters.clear();

    stats_.models = static_cast<int>(im.opaque.size() + im.trans.size() + im.effect.size());
    stats_.sprites = static_cast<int>(im.spriteList.size());
    stats_.marks = static_cast<int>(im.markDescs.size());
    stats_.shadows = static_cast<int>(im.shadowList.size());
    stats_.lights = static_cast<int>(im.lights.size());
    stats_.dropped = im.dropped;
    fr.ready = true;
}

void WorldRenderer::draw(const EyeView& eye) { draw(eye, impl_->frame.viewport.x, impl_->frame.viewport.y); }

void WorldRenderer::draw(const EyeView& eye, int vx, int vy) {
    Impl& im = *impl_;
    const Impl::Frame& fr = im.frame;
    stats_.terrainChunks = stats_.waterChunks = stats_.skidTrails = stats_.bolts = 0;
    stats_.emitters = stats_.particles = 0;
    if (!fr.ready) return;
    const WorldRenderOptions& options = fr.options;
    const SceneLighting& light = fr.light;
    const Terrain* terrain = fr.terrain;
    const bool haveTerrain = fr.haveTerrain;
    const int width = fr.viewport.width, height = fr.viewport.height;

    // The eye's camera. An off-axis eye is drawn with the symmetric projection of the same
    // eye position over a viewport moved by the image shift (x_ndc = x_ndc(symmetric) - B,
    // eyeProjection) and widened on both sides by it, so the scale stays the prepared one and
    // the rectangle stays covered; the scissor keeps the passes inside the rectangle. The
    // Camera the mesh pass takes has no off-axis projection, hence this form for every pass.
    WorldView ev = fr.wv;
    float aspect = fr.aspect;
    int gx = vx, gw = width;
    if (eye.eyeOffset != 0.0f) {
        ev.view = eyeViewMatrix(fr.wv, eye);
        ev.eye = eyePosition(fr.wv, eye);
        const float b = eyeProjection(fr.wv, eye, fr.aspect).at(2, 0);
        const int shift = static_cast<int>(std::lround(-b * static_cast<float>(width) * 0.5f));
        if (shift != 0) {
            const int pad = shift < 0 ? -shift : shift;
            gx = vx - pad + shift;
            gw = width + 2 * pad;
            aspect = static_cast<float>(gw) / static_cast<float>(height);
        }
        ev.projection = eyeProjection(ev, EyeView{}, aspect);
    }
    const bool shifted = gw != width;
    const Camera cam = cameraFromView(ev, aspect);

    TerrainViewParams tv;
    tv.view = ev.view;
    tv.projection = ev.projection;
    tv.cameraPos = ev.eye;
    tv.fogColour = light.fogColor;
    tv.fogStart = light.fogStart;
    tv.fogEnd = light.fogEnd;
    tv.time = fr.time;
    tv.mapPos = fr.mapPos;
    tv.lights = im.lights.data();
    tv.lightCount = im.lights.size();
    DecalViewParams dv;
    dv.view = ev.view;
    dv.projection = ev.projection;
    dv.fogColour = light.fogColor;
    dv.fogStart = light.fogStart;
    dv.fogEnd = light.fogEnd;
    im.meshes.setDynamicLights(im.lights.data(), im.lights.size());
    im.meshes.setTime(fr.time);

    // Pass 0: clear to the fog colour (black without fog).
    setViewport(gx, vy, gw, height);
    Vec3 cc = fr.hasFog ? light.fogColor : Vec3{0, 0, 0};
    const bool part = vx != 0 || vy != 0;
    if (part || shifted) {
        // A sub-rectangle (or a widened viewport): the clear must not reach outside it.
        setScissor(true, vx, vy, width, height);
    }
    clear({cc.x, cc.y, cc.z, 1.0f}, true);
    if (part && !shifted) setScissor(false);
    setDepth(true, true);
    setCull(CullMode::Back);

    // Pass 2: terrain.
    if (options.terrain && haveTerrain) {
        im.terrain->render(tv);
        stats_.terrainChunks = im.terrain->lastVisibleChunks();
    }
    setDepth(true, true);
    // Pass 4: ground marks (geometry cut in prepare).
    if (haveTerrain) {
        if (!im.markDescs.empty()) im.marks.draw(dv);
        // The sequels' skid marks, between the marks and the shadows (as2 delta 1.1 pass 5).
        if (im.skidTrailCount > 0) {
            im.skids.draw(im.skidTrails.data(), im.skidTrailCount, TerrainGridView::of(*terrain), dv);
            stats_.skidTrails = im.skids.lastTrailCount();
        }
        // Pass 5: shadows.
        if (!im.shadowList.empty()) im.shadows.draw(*terrain, im.shadowList.data(), im.shadowList.size(), dv);
    }
    // Pass 6: opaque list.
    im.meshes.begin(cam, light);
    for (const MeshRecord& r : im.opaque) im.meshes.submit(*r.mesh, *r.material, r.model, r.colour);
    im.meshes.end();
    // Pass 7: water.
    if (options.terrain && haveTerrain && im.water) {
        im.water->render(tv);
        stats_.waterChunks = im.water->lastVisibleChunks();
    }
    // Passes 8 and 9: transparent then effect list.
    im.meshes.begin(cam, light);
    for (const MeshRecord& r : im.trans) im.meshes.submit(*r.mesh, *r.material, r.model, r.colour);
    for (const MeshRecord& r : im.effect) im.meshes.submit(*r.mesh, *r.material, r.model, r.colour);
    im.meshes.end();
    glDisable(GL_BLEND);
    setDepth(true, true);
    setCull(CullMode::Back);
    // Lightning bolts, drawn after the effect models (7.1).
    if (!im.boltStarts.empty()) {
        LightningViewParams lv;
        lv.view = ev.view;
        lv.projection = ev.projection;
        lv.fogStart = light.fogStart;
        lv.fogEnd = light.fogEnd;
        lv.time = fr.time;
        im.lightning.draw(im.boltStarts.data(), im.boltEnds.data(), im.boltStarts.size(), fr.boltTexture, lv);
        stats_.bolts = im.lightning.lastBoltCount();
    }
    // Pass 10: particles.
    if (options.particles) {
        ParticleViewParams pv;
        pv.view = ev.view;
        pv.projection = ev.projection;
        pv.fogColour = light.fogColor;
        pv.fogStart = light.fogStart;
        pv.fogEnd = light.fogEnd;
        if (!im.emitters.empty()) im.particles.draw(im.emitters.data(), im.emitters.size(), pv);
        stats_.emitters = static_cast<int>(im.emitters.size());
        stats_.particles = im.particles.lastParticleCount();
    }
    // Pass 12: sprites.
    SpriteViewParams sv;
    sv.view = ev.view;
    sv.projection = ev.projection;
    sv.fogColour = light.fogColor;
    sv.fogStart = light.fogStart;
    sv.fogEnd = light.fogEnd;
    sv.cullBackFaces = im.rules.spriteCulling;
    if (!im.spriteList.empty()) im.sprites.draw(im.spriteList.data(), im.spriteList.size(), sv);
    glDisable(GL_BLEND);
    setDepth(true, true);
    setCull(CullMode::Back);
    if (shifted) {
        glDisable(GL_SCISSOR_TEST);
        setViewport(vx, vy, width, height);
    }
}

void WorldRenderer::drawBrightness(int width, int height, float brightness) { drawBrightness(0, 0, width, height, brightness); }

void WorldRenderer::drawBrightness(int x, int y, int width, int height, float brightness) {
    if (brightness <= 0.0f || width <= 0 || height <= 0) return;
    setViewport(x, y, width, height);
    impl_->brightness.draw(brightness);
}

} // namespace as3d
