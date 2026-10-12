// WorldRenderer (as3d/world_render.h): the live world in the pass order of
// docs/spec/render-pipeline.md 1.1, using the entity fields the way section 3 describes
// (base origin 41..43, axis rows 44..52, scale 32, colour 28..31, frame 33, model handle,
// skin, FL_NODRAW). Every pass is one of the engine's renderers, fed from the entities.
//
// The GL-free half of prepare() (camera, entity pass, lists, lights) is WorldScene
// (engine/src/render/world_scene.h), shared with the PICA renderer; this file holds the GL passes.
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
#include "world_scene.h"

namespace as3d {

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

// ---------------------------------------------------------------------------------------
// Renderer state.
// ---------------------------------------------------------------------------------------

struct WorldRenderer::Impl {
    Vfs* vfs = nullptr;
    const DefDatabase* db = nullptr;
    std::unique_ptr<ResourceCache> cache;
    WorldScene scene;
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
    Vec3 towardsSun{0.0f, 0.0f, 1.0f};
    // Shadow maps by model|skin|kind|quality|steps; null when generation failed.
    std::unordered_map<std::string, std::unique_ptr<ShadowMap>> shadowMaps;
    int lateShadowMaps = 0;
    std::vector<ShadowInstance> shadowList;
    // Marks: the geometry is rebuilt only when the set (or a colour) changes.
    std::vector<std::pair<GroundMarkDesc, std::uint64_t>> lastMarkDescs;
    bool haveTerrain = false; // of the prepared frame

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
            LoadStats::count(LoadKind::Shadow, key, "shadow"); // a first-use load (as3d/load_stats.h)
        }
        const ShadowMap* p = map.get();
        shadowMaps.emplace(key, std::move(map));
        return p;
    }
    // WorldScene's shadow hook: the silhouette of a precached model (not counted as late).
    void precacheShadow(const GpuMesh& mesh, const std::string& modelPath, const Material& mat, const ShadowSpec& ss,
                        int steps) {
        shadowMap(mesh, modelPath, mat, std::string(), ss, steps, false);
    }
};

WorldRenderer::WorldRenderer() : impl_(new Impl) {}
WorldRenderer::~WorldRenderer() = default;

bool WorldRenderer::init(Vfs& vfs, const DefDatabase& db, std::string* error) {
    Impl& im = *impl_;
    im.vfs = &vfs;
    im.db = &db;
    im.cache.reset(new ResourceCache(vfs));
    im.scene.init(*im.cache, vfs, db);
    if (!im.meshes.init(error)) return false;
    if (!im.particles.init(vfs, error)) return false;
    if (!im.sprites.init(error)) return false;
    if (!im.marks.init(error)) return false;
    if (!im.shadows.init(error)) return false;
    if (!im.lightning.init(error)) return false;
    im.scene.resolveHealthBar(defaultGameRules());
    if (!im.brightness.init(error)) return false;
    return true;
}

bool WorldRenderer::beginLevel(const World& world, std::string* error) {
    Impl& im = *impl_;
    im.scene.resolveHealthBar(world.rules());
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
    im.marks.clear();
    im.lastMarkDescs.clear();
    im.scene.beginLevel(world, im.rules);
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
        auto hook = [&im](const GpuMesh& mesh, const std::string& path, const Material& mat, const ShadowSpec& ss,
                          int steps) { im.precacheShadow(mesh, path, mat, ss, steps); };
        for (const Placement& p : level->placements)
            im.scene.placedShadows(im.db->findObject(level->typeName(p)), p.rotationSteps, st.night, 0, hook);
    }
    return true;
}

PrecacheReport WorldRenderer::precache(const World& world, const DefDatabase& db, const std::function<void(float)>& progress) {
    Impl& im = *impl_;
    const size_t shadowsBefore = im.shadowMaps.size();
    WorldScene::PrecacheHooks hooks;
    hooks.shadow = [&im](const GpuMesh& mesh, const std::string& path, const Material& mat, const ShadowSpec& ss,
                         int steps) { im.precacheShadow(mesh, path, mat, ss, steps); };
    hooks.particleTexture = [&im](const std::string& path) { im.particles.preload(path); };
    PrecacheReport r = im.scene.precache(world, db, progress, hooks);
    r.shadows = static_cast<int>(im.shadowMaps.size() - shadowsBefore);
    return r;
}

const RenderRules& WorldRenderer::rules() const { return impl_->rules; }

int WorldRenderer::shadowMapCount() const { return static_cast<int>(impl_->shadowMaps.size()); }
int WorldRenderer::lateShadowMaps() const { return impl_->lateShadowMaps; }

// ---------------------------------------------------------------------------------------
// Frame.
// ---------------------------------------------------------------------------------------

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

const CullFrustum& WorldRenderer::preparedFrustum() const { return impl_->scene.frame().cull; }

void WorldRenderer::prepare(const World& world, const Viewport& viewport, const WorldRenderOptions& options) {
    Impl& im = *impl_;
    WorldScene& sc = im.scene;
    stats_ = WorldRenderStats();
    sc.invalidate();
    if (viewport.width <= 0 || viewport.height <= 0) return;
    const Terrain* terrain = world.terrain();
    im.haveTerrain = terrain && im.terrain && im.terrainOf == terrain;
    // Craters since the last frame: the meshes follow the world's heights (read only).
    const std::vector<VertexRect>& rects = sc.terrainChanges(world, im.terrainOf);
    if (!rects.empty()) {
        const TerrainGridView grid = TerrainGridView::of(*im.terrainOf);
        if (im.terrain) im.terrain->update(rects.data(), rects.size(), grid);
        if (im.water) im.water->update(rects.data(), rects.size(), grid);
    }
    sc.prepare(world, viewport, options, true, im.haveTerrain && im.skidsReady, stats_);

    // Shadows (5.3): the silhouettes of the records that cast one, generated on first sight.
    im.shadowList.clear();
    for (const ShadowRequest& r : sc.shadowRequests) {
        const ShadowMap* map = im.shadowMap(*r.mesh, *r.modelPath, *r.material, *r.skinPath, r.spec, r.steps, true);
        if (!map) continue;
        ShadowInstance si;
        si.map = map;
        si.origin = r.origin;
        si.yawDegrees = r.yawDegrees;
        im.shadowList.push_back(si);
    }
    // Ground marks: their geometry is cut from the terrain when the set changes.
    if (im.haveTerrain && !sameMarks(sc.markDescs, im.lastMarkDescs)) {
        im.marks.clear();
        for (const auto& m : sc.markDescs) im.marks.addMark(*terrain, m.first);
        im.lastMarkDescs = sc.markDescs;
    }
    stats_.shadows = static_cast<int>(im.shadowList.size());
}

void WorldRenderer::draw(const EyeView& eye) {
    const Viewport& vp = impl_->scene.frame().viewport;
    draw(eye, vp.x, vp.y);
}

void WorldRenderer::draw(const EyeView& eye, int vx, int vy) {
    Impl& im = *impl_;
    const WorldScene& sc = im.scene;
    const WorldScene::Frame& fr = sc.frame();
    stats_.terrainChunks = stats_.waterChunks = stats_.skidTrails = stats_.bolts = 0;
    stats_.emitters = stats_.particles = 0;
    if (!fr.ready) return;
    const WorldRenderOptions& options = fr.options;
    const SceneLighting& light = fr.light;
    const Terrain* terrain = fr.terrain;
    const bool haveTerrain = im.haveTerrain;
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
    tv.lights = sc.lights.data();
    tv.lightCount = sc.lights.size();
    DecalViewParams dv;
    dv.view = ev.view;
    dv.projection = ev.projection;
    dv.fogColour = light.fogColor;
    dv.fogStart = light.fogStart;
    dv.fogEnd = light.fogEnd;
    im.meshes.setDynamicLights(sc.lights.data(), sc.lights.size());
    im.meshes.setTime(fr.time);

    // Pass 0: clear to the fog colour (black without fog).
    setViewport(gx, vy, gw, height);
    const Vec3 cc = fr.clearColour;
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
        if (!sc.markDescs.empty()) im.marks.draw(dv);
        // The sequels' skid marks, between the marks and the shadows (as2 delta 1.1 pass 5).
        if (sc.skidTrailCount > 0) {
            im.skids.draw(sc.skidTrails.data(), sc.skidTrailCount, TerrainGridView::of(*terrain), dv);
            stats_.skidTrails = im.skids.lastTrailCount();
        }
        // Pass 5: shadows.
        if (!im.shadowList.empty()) im.shadows.draw(*terrain, im.shadowList.data(), im.shadowList.size(), dv);
    }
    // Pass 6: opaque list.
    im.meshes.begin(cam, light);
    for (const MeshRecord& r : sc.opaque) im.meshes.submit(*r.mesh, *r.material, r.model, r.colour);
    im.meshes.end();
    // Pass 7: water.
    if (options.terrain && options.water && haveTerrain && im.water) {
        im.water->render(tv);
        stats_.waterChunks = im.water->lastVisibleChunks();
    }
    // Passes 8 and 9: transparent then effect list.
    im.meshes.begin(cam, light);
    for (const MeshRecord& r : sc.trans) im.meshes.submit(*r.mesh, *r.material, r.model, r.colour);
    for (const MeshRecord& r : sc.effect) im.meshes.submit(*r.mesh, *r.material, r.model, r.colour);
    im.meshes.end();
    glDisable(GL_BLEND);
    setDepth(true, true);
    setCull(CullMode::Back);
    // Lightning bolts, drawn after the effect models (7.1).
    if (!sc.boltStarts.empty()) {
        LightningViewParams lv;
        lv.view = ev.view;
        lv.projection = ev.projection;
        lv.fogStart = light.fogStart;
        lv.fogEnd = light.fogEnd;
        lv.time = fr.time;
        im.lightning.draw(sc.boltStarts.data(), sc.boltEnds.data(), sc.boltStarts.size(), fr.boltTexture, lv);
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
        if (!sc.emitters.empty()) im.particles.draw(sc.emitters.data(), sc.emitters.size(), pv);
        stats_.emitters = static_cast<int>(sc.emitters.size());
        stats_.emittersCulled = sc.emittersCulled;
        stats_.skidTrailsCulled = sc.skidTrailsCulled;
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
    if (!sc.sprites.empty()) im.sprites.draw(sc.sprites.data(), sc.sprites.size(), sv);
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
