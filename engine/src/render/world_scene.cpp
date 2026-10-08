// WorldScene (world_scene.h): the entity pass and the per-frame lists of the world renderers,
// moved out of the GL WorldRenderer (engine/src/render/gles/world_render.cpp) unchanged, so both
// backends read the world the same way. Also worldViewOf and worldRenderOrder (as3d/world_render.h).
#include "world_scene.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "as3d/load_stats.h"
#include "as3d/particles.h"
#include "as3d/terrain.h"
#include "as3d/vfs.h"

namespace as3d {

// ---------------------------------------------------------------------------------------
// Camera and think order (as3d/world_render.h).
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

int wrapSteps(int s) { return ((s % 12) + 12) % 12; }

const std::string& skinOf(const Entity& e) { return e.skinPath.empty() ? e.def->skin : e.skinPath; }

} // namespace

void worldRenderOrder(const World& world, std::vector<int>& order, std::vector<char>& visited) {
    order.clear();
    visited.assign(static_cast<size_t>(kMaxEntitySlots), 0);
    for (int i : world.listEntities()) visitThinkOrder(world, i, order, visited, 0);
}

// ---------------------------------------------------------------------------------------
// Helpers.
// ---------------------------------------------------------------------------------------

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

DecalBlend decalBlendOf(BlendMode b) {
    switch (b) {
        case BlendMode::Alpha: return DecalBlend::Alpha;
        case BlendMode::Add: return DecalBlend::Add;
        case BlendMode::Filter: return DecalBlend::Filter;
        default: return DecalBlend::None;
    }
}

// ---------------------------------------------------------------------------------------
// WorldScene.
// ---------------------------------------------------------------------------------------

void WorldScene::init(ResourceCache& cache, Vfs& vfs, const DefDatabase& db) {
    cache_ = &cache;
    vfs_ = &vfs;
    db_ = &db;
    memory_.assign(static_cast<size_t>(kMaxEntitySlots), SlotMemory());
}

void WorldScene::resolveHealthBar(const GameRules& rules) {
    const ObjectDef* he = rules.healthBarEmptyObject ? db_->findObject(rules.healthBarEmptyObject) : nullptr;
    const ObjectDef* hf = rules.healthBarFullObject ? db_->findObject(rules.healthBarFullObject) : nullptr;
    hbarReady_ = he && hf && he->hasBbox && hf->hasBbox;
    if (!hbarReady_) return;
    HealthBarSpriteDef* outs[2] = {&hbarEmpty_, &hbarFull_};
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
        o.texture = d.skin.empty() ? nullptr : cache_->texture(d.skin).texture;
        o.blend = decalBlendOf(d.blend);
        o.noDepthTest = (d.rflag & RF_NODEPTHTEST) != 0;
        o.noDepthWrite = (d.rflag & RF_NODEPTHWRITE) != 0;
    }
}

void WorldScene::beginLevel(const World& world, const RenderRules& rules) {
    rules_ = rules;
    memory_.assign(static_cast<size_t>(kMaxEntitySlots), SlotMemory());
    markDescs.clear();
    // The meshes are built from the heights as they are now (craters of the load included).
    terrainRevision_ = world.terrainRevision();
    frame_.ready = false;
}

const Material& WorldScene::materialFor(const ObjectDef& def, const std::string& skinOverride) {
    auto key = std::make_pair(&def, skinOverride);
    auto it = materials_.find(key);
    if (it != materials_.end()) return *it->second;
    std::unique_ptr<Material> m(new Material(Material::fromObjectDef(def, *cache_)));
    if (!skinOverride.empty()) {
        ResourceCache::LoadedTexture t = cache_->texture(skinOverride);
        m->texture = t.texture;
        m->textureHasAlpha = t.hasAlpha;
    }
    const Material& ref = *m;
    materials_.emplace(key, std::move(m));
    return ref;
}

const std::vector<VertexRect>& WorldScene::terrainChanges(const World& world, const Terrain* built) {
    rects_.clear();
    if (!built || world.terrain() != built) return rects_;
    const u32 rev = world.terrainRevision();
    if (rev == terrainRevision_) return rects_;
    world.terrainChangesSince(terrainRevision_, changeScratch_);
    terrainRevision_ = rev;
    for (const TerrainChange& c : changeScratch_) {
        VertexRect v;
        v.c0 = c.c0;
        v.r0 = c.r0;
        v.c1 = c.c1;
        v.r1 = c.r1;
        if (!v.empty()) rects_.push_back(v);
    }
    return rects_;
}

void WorldScene::collectSkidTrails(const World& world) {
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

void WorldScene::prepare(const World& world, const Viewport& viewport, const WorldRenderOptions& options,
                         bool withShadows, bool withSkids, WorldRenderStats& stats) {
    Frame& fr = frame_;
    fr.ready = false;
    if (viewport.width <= 0 || viewport.height <= 0) return;
    fr.viewport = viewport;
    fr.options = options;
    fr.aspect = static_cast<float>(viewport.width) / static_cast<float>(viewport.height);
    fr.wv = worldViewOf(world, fr.aspect);
    fr.cull = eyeCullFrustum(fr.wv, fr.aspect, viewport.maxEyeOffset, viewport.minFocalDistance);
    const Terrain* terrain = world.terrain();
    fr.terrain = terrain;
    fr.time = world.time();
    fr.mapPos = world.mapPos();

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
    fr.clearColour = fr.hasFog ? light.fogColor : Vec3{0, 0, 0};

    // Dynamic lights (2.4): the PlaceLight queue of this frame, then the definition lights
    // of the drawn entities; the first 32 are kept.
    lights.clear();
    if (options.lights) {
        for (const QueuedLight& q : world.lights()) lights.add(DynamicLight::point(q.pos, q.color, q.radius));
    }

    // Render records in the order the entity pass submits them (1.2).
    opaque.clear();
    trans.clear();
    effect.clear();
    shadowRequests.clear();
    sprites.clear();
    markDescs.clear();
    dropped = 0;
    worldRenderOrder(world, order_, visited_);
    // The health bar follows the entity's own records (engine-behaviour.md 2, 6.5); the
    // think draws it whether or not the entity itself is drawn.
    auto addHealthBar = [&](const Entity& e) {
        if (!options.sprites || !hbarReady_) return;
        SpriteInstance bar[2];
        int n = buildHealthBar(e, hbarEmpty_, hbarFull_, bar);
        for (int k = 0; k < n; ++k) {
            if (sprites.size() >= kMaxSprites) {
                ++dropped;
                break;
            }
            sprites.push_back(bar[k]);
        }
        stats.healthBars += n > 0 ? 1 : 0;
    };
    for (int i : order_) {
        const Entity& e = world.entity(i);
        if (!e.def || (e.flagBits() & FL_NODRAW)) {
            addHealthBar(e);
            continue;
        }
        const ObjectDef& def = *e.def;
        SlotMemory& mem = memory_[static_cast<size_t>(i)];
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
                lights.add(DynamicLight::spotLight(origin, col, radius, d, def.lightConeAngle));
            } else {
                lights.add(DynamicLight::point(origin, col, radius));
            }
        }
        switch (def.type) {
            case ObjectType::Model: {
                // The entity's model handle: SetModel swaps it (damaged helicopters).
                if (!options.models || !e.model || e.modelPath.empty()) break;
                const GpuMesh& mesh = cache_->mesh(e.modelPath);
                if (!mesh.valid) break;
                std::vector<MeshRecord>* list = &opaque;
                int cap = kOpaqueCap;
                if (def.sort == SortMode::Trans) {
                    list = &trans;
                    cap = kTransCap;
                } else if (def.sort == SortMode::Effect) {
                    list = &effect;
                    cap = kEffectCap;
                }
                if (static_cast<int>(list->size()) >= cap) {
                    ++dropped;
                    break;
                }
                const Material& mat = materialFor(def, e.skinPath);
                MeshRecord rec;
                rec.mesh = &mesh;
                rec.material = &mat;
                rec.model = entityMatrix(e);
                rec.colour = colour;
                rec.entity = i;
                list->push_back(rec);
                // Shadows (5.3): opaque and transparent records only.
                ShadowSpec ss = shadowSpecOf(def.shadow);
                if (withShadows && options.shadows && ss.any && def.sort != SortMode::Effect) {
                    ShadowRequest sr;
                    sr.mesh = &mesh;
                    sr.material = &mat;
                    sr.modelPath = &e.modelPath;
                    sr.skinPath = &e.skinPath;
                    sr.spec = ss;
                    sr.steps = mem.shadowSteps;
                    if (ss.kind == ShadowKind::Projected) {
                        sr.origin = mem.spawnOrigin; // baked at spawn, never moves
                    } else {
                        sr.origin = origin;
                        Vec3 fw = e.v3(F_AXIS); // heading of the local X axis (issue 070)
                        sr.yawDegrees = radToDeg(std::atan2(fw.y, fw.x));
                    }
                    shadowRequests.push_back(sr);
                }
                break;
            }
            case ObjectType::Mark: {
                if (!options.sprites || !def.hasBbox) break;
                if (markDescs.size() >= GroundMarkRenderer::kMaxMarks) {
                    ++dropped;
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
                d.texture = cache_->texture(skin).texture;
                d.blend = decalBlendOf(def.blend);
                d.colour = Vec3{colour.x, colour.y, colour.z};
                d.alpha = rules_.markEntityAlpha ? colour.w : 1.0f;
                d.unflippedTexture = rules_.markTextureUnflipped;
                markDescs.push_back({d, (static_cast<std::uint64_t>(i) << 32) | e.generation});
                break;
            }
            case ObjectType::Sprite:
            case ObjectType::HSprite:
            case ObjectType::VSprite: {
                if (!options.sprites || !def.hasBbox) break;
                if (sprites.size() >= kMaxSprites) {
                    ++dropped;
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
                    float f = e.f(F_FRAME);
                    sp.frame = (f > -1.0e7f && f < 1.0e7f) ? static_cast<int>(f) : 0;
                }
                sp.texture = cache_->texture(skin).texture;
                sp.blend = decalBlendOf(def.blend);
                sp.noDepthTest = (def.rflag & RF_NODEPTHTEST) != 0;
                sp.noDepthWrite = (def.rflag & RF_NODEPTHWRITE) != 0;
                sprites.push_back(sp);
                break;
            }
            default: break;
        }
        addHealthBar(e);
    }

    // The sequels' skid marks (as2 delta 1.1 pass 5).
    if (terrain && withSkids && rules_.skidMarkPass && options.sprites) collectSkidTrails(world);
    else skidTrailCount = 0;
    // Lightning bolts: effect-list records (sort 3) submitted by the Lightning builtin during
    // the entity pass (7.1).
    boltStarts.clear();
    boltEnds.clear();
    for (const LightningBolt& b : world.lightningBolts()) {
        boltStarts.push_back(b.start);
        boltEnds.push_back(b.end);
    }
    fr.boltTexture = boltStarts.empty() ? nullptr : cache_->texture("gfx\\lightning2.tga").texture;
    // Particles: the world's live emitters.
    if (options.particles) world.particles().collect(emitters);
    else emitters.clear();

    stats.models = static_cast<int>(opaque.size() + trans.size() + effect.size());
    stats.sprites = static_cast<int>(sprites.size());
    stats.marks = static_cast<int>(markDescs.size());
    stats.lights = static_cast<int>(lights.size());
    stats.dropped = dropped;
    fr.ready = true;
}

// ---------------------------------------------------------------------------------------
// Precache.
// ---------------------------------------------------------------------------------------

void WorldScene::placedShadows(const ObjectDef* def, int steps, bool night, int depth,
                               const std::function<void(const GpuMesh&, const std::string&, const Material&,
                                                        const ShadowSpec&, int)>& shadow) {
    if (!def || depth > kMaxAttachDepth || !shadow) return;
    ShadowSpec ss = shadowSpecOf(def->shadow);
    if (ss.any && def->type == ObjectType::Model && !def->model.empty() && def->sort != SortMode::Effect) {
        const GpuMesh& mesh = cache_->mesh(def->model);
        if (mesh.valid) {
            const Material& mat = materialFor(*def, std::string());
            shadow(mesh, normalizePath(def->model), mat, ss, steps);
        }
    }
    for (const AttachDef& at : def->attachments) {
        if (at.nightOnly && !night) continue;
        if (db_->findParticleSystem(at.targetName)) continue;
        placedShadows(db_->findObject(at.targetName), steps, night, depth + 1, shadow);
    }
}

// One definition of the closure: what prepare() loads when an entity of it is first drawn.
// `swapped`: empty for the definition's own model, else one its script swaps in (SetModel).
void WorldScene::precacheObject(const ObjectDef& def, const std::string& swapped, const PrecacheHooks& hooks) {
    for (const SkidMarkDef& sk : def.skidMarks)
        if (!sk.texture.empty()) cache_->texture(sk.texture);
    switch (def.type) {
        case ObjectType::Model: {
            const std::string& model = swapped.empty() ? def.model : swapped;
            if (model.empty()) return;
            const GpuMesh& mesh = cache_->mesh(model);
            if (!mesh.valid) return;
            const Material& mat = materialFor(def, std::string());
            const ShadowSpec ss = shadowSpecOf(def.shadow);
            if (!hooks.shadow || !ss.any || def.sort == SortMode::Effect) return;
            // A spawned entity's projected shadow takes its root's yaw in 30 degree steps:
            // any of the 12.
            const int steps = ss.kind == ShadowKind::Projected ? 12 : 1;
            for (int s = 0; s < steps; ++s) hooks.shadow(mesh, normalizePath(model), mat, ss, s);
            return;
        }
        default:
            if (!def.skin.empty()) cache_->texture(def.skin);
            return;
    }
}

void WorldScene::precachePlaced(const ObjectDef* def, bool night, int depth) {
    if (!def || depth > kMaxAttachDepth) return;
    if (def->type == ObjectType::Model) {
        if (!def->model.empty() && cache_->mesh(def->model).valid) materialFor(*def, std::string());
    } else if (!def->skin.empty()) {
        cache_->texture(def->skin);
    }
    for (const AttachDef& at : def->attachments) {
        if (at.nightOnly && !night) continue;
        if (db_->findParticleSystem(at.targetName)) continue;
        precachePlaced(db_->findObject(at.targetName), night, depth + 1);
    }
}

void WorldScene::precacheDrawn(const World& world) {
    worldRenderOrder(world, order_, visited_);
    for (int i : order_) {
        const Entity& e = world.entity(i);
        if (!e.def) continue;
        if (e.def->type == ObjectType::Model) {
            if (!e.modelPath.empty() && cache_->mesh(e.modelPath).valid) materialFor(*e.def, e.skinPath);
        } else if (!skinOf(e).empty()) {
            cache_->texture(skinOf(e));
        }
    }
    const Terrain* t = world.terrain();
    const LevelData* level = t ? t->level() : nullptr;
    if (!level) return;
    for (const Placement& p : level->placements) precachePlaced(db_->findObject(level->typeName(p)), t->style().night, 0);
}

PrecacheReport WorldScene::precache(const World& world, const DefDatabase& db, const std::function<void(float)>& progress,
                                    const PrecacheHooks& hooks) {
    const auto t0 = std::chrono::steady_clock::now();
    const LoadCounts before = LoadStats::lazyLoads();
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
    // The particle textures of the level's particle systems, ahead of their first draw (the
    // particle renderer keeps a texture cache of its own).
    auto particleTextures = [&]() {
        if (hooks.particleTexture) {
            for (const ParticleSystemDef* ps : set->particleSystems) {
                const ParticleDesc d = ParticleDesc::fromDef(*ps);
                if (!d.texture.empty()) hooks.particleTexture(d.texture);
            }
        }
        step();
    };
    if (hooks.drawnFirst) precacheDrawn(world);
    if (hooks.particlesFirst) particleTextures();
    for (const ObjectDef* def : set->objects) {
        precacheObject(*def, std::string(), hooks);
        step();
    }
    for (const std::string& m : set->models) {
        cache_->mesh(m);
        step();
    }
    for (const auto& om : set->objectModels)
        if (om.first->type == ObjectType::Model) precacheObject(*om.first, om.second, hooks);
    for (const std::string& t : set->textures) {
        cache_->texture(t);
        step();
    }
    // The Lightning builtin's bolt (render-pipeline.md 7.1), wherever a script calls it.
    if (vfs_->exists("gfx\\lightning2.tga")) cache_->texture("gfx\\lightning2.tga");
    step();
    if (!hooks.particlesFirst) particleTextures();
    const LoadCounts d = LoadStats::lazyLoads() - before;
    PrecacheReport r;
    r.objects = static_cast<int>(set->objects.size());
    // Particle textures are read outside a counted site (Other).
    r.textures = static_cast<int>(d.of(LoadKind::Texture) + d.of(LoadKind::Other));
    r.models = static_cast<int>(d.of(LoadKind::Model));
    r.scripts = static_cast<int>(d.of(LoadKind::Script));
    r.bytes = d.bytes;
    r.capped = set->capped;
    r.seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

} // namespace as3d
