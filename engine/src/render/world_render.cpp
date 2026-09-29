// WorldRenderer (as3d/world_render.h): the live world in the pass order of
// docs/spec/render-pipeline.md 1.1, using the entity fields the way section 3 describes
// (base origin 41..43, axis rows 44..52, scale 32, colour 28..31, frame 33, skin, FL_NODRAW).
//
// as3d/defs.h and as3d/gfx.h both define as3d::BlendMode; defs.h's is renamed locally for
// this file, as in engine/src/render/material.cpp.
#include "as3d/world_render.h"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

#include "as3d/particle_render.h"
#include "as3d/scene.h"
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

// List capacities of render-pipeline.md 1.2.
constexpr int kOpaqueCap = 512;
constexpr int kTransCap = 128;
constexpr int kEffectCap = 256;
constexpr int kSpriteCap = 512;
constexpr int kMarkCap = 128;

// ---------------------------------------------------------------------------------------
// Unlit batch for sprites, marks and 2D quads (interim, see as3d/world_render.h).
// ---------------------------------------------------------------------------------------

const char* const kBatchVertexSrc = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
uniform mat4 uView;
uniform mat4 uProj;
out vec2 vUv;
out vec4 vColor;
out float vDepth;
void main() {
    vec4 eye = uView * vec4(aPos, 1.0);
    vDepth = -eye.z;
    vUv = aUv;
    vColor = aColor;
    gl_Position = uProj * eye;
}
)";

// uTexMode 0: vertex colour only; 1: texture * colour (GL_MODULATE).
const char* const kBatchFragmentSrc = R"(#version 300 es
precision highp float;
in vec2 vUv;
in vec4 vColor;
in float vDepth;
uniform sampler2D uTex;
uniform int uTexMode;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
out vec4 fragColor;
void main() {
    vec4 c = clamp(vColor, 0.0, 1.0);
    vec4 o = uTexMode == 1 ? texture(uTex, vUv) * c : c;
    float f = clamp((uFogEnd - vDepth) / max(uFogEnd - uFogStart, 1e-3), 0.0, 1.0);
    o.rgb = mix(uFogColor, o.rgb, f);
    fragColor = o;
}
)";

struct BatchVertex {
    float x, y, z;
    float u, v;
    float r, g, b, a;
};

enum class QuadBlend { None, Alpha, Add, Filter, Modulate2x };

struct QuadBatch {
    const Texture2D* texture = nullptr; // null: untextured
    QuadBlend blend = QuadBlend::None;
    bool depthTest = true;
    bool depthWrite = true;
    bool polygonOffset = false;
    bool cull = true;
    size_t first = 0;
    size_t count = 0;
};

QuadBlend quadBlendOf(AS3D_DEFS_BlendMode b) {
    switch (b) {
        case AS3D_DEFS_BlendMode::None: return QuadBlend::None;
        case AS3D_DEFS_BlendMode::Alpha: return QuadBlend::Alpha;
        case AS3D_DEFS_BlendMode::Add: return QuadBlend::Add;
        case AS3D_DEFS_BlendMode::Filter: return QuadBlend::Filter;
    }
    return QuadBlend::None;
}

class QuadRenderer {
public:
    bool init(std::string* error) {
        if (!program_.compile(kBatchVertexSrc, kBatchFragmentSrc, error)) return false;
        BatchVertex dummy{};
        vbo_.upload(&dummy, sizeof dummy, true);
        VertexLayout layout;
        layout.strideBytes = static_cast<int>(sizeof(BatchVertex));
        layout.attribs = {{0, 3, 0, false}, {1, 2, 3 * sizeof(float), false}, {2, 4, 5 * sizeof(float), false}};
        vao_.create(vbo_, layout);
        return true;
    }

    void clear() {
        verts_.clear();
        batches_.clear();
    }

    // Starts (or continues) a batch with this state and appends one quad as two triangles.
    void quad(const QuadBatch& state, const BatchVertex q[4]) {
        if (batches_.empty() || !sameState(batches_.back(), state)) {
            QuadBatch b = state;
            b.first = verts_.size();
            b.count = 0;
            batches_.push_back(b);
        }
        verts_.insert(verts_.end(), {q[0], q[1], q[2], q[0], q[2], q[3]});
        batches_.back().count += 6;
    }

    bool empty() const { return batches_.empty(); }

    void draw(const Mat4& view, const Mat4& proj, const Vec3& fogColour, float fogStart, float fogEnd) {
        if (batches_.empty() || !program_.valid()) return;
        vbo_.upload(verts_.data(), verts_.size() * sizeof(BatchVertex), true);
        program_.use();
        program_.setMat4("uView", view);
        program_.setMat4("uProj", proj);
        program_.setFloat("uFogStart", fogStart);
        program_.setFloat("uFogEnd", fogEnd);
        program_.setInt("uTex", 0);
        vao_.bind();
        for (const QuadBatch& b : batches_) {
            Vec3 fog = fogColour;
            switch (b.blend) {
                case QuadBlend::None: glDisable(GL_BLEND); break;
                case QuadBlend::Alpha:
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    break;
                case QuadBlend::Add:
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_ONE, GL_ONE);
                    fog = {0, 0, 0};
                    break;
                case QuadBlend::Filter:
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_DST_COLOR, GL_ZERO);
                    fog = {1, 1, 1};
                    break;
                case QuadBlend::Modulate2x:
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
                    break;
            }
            setDepth(b.depthTest, b.depthWrite);
            setCull(b.cull ? CullMode::Back : CullMode::Off);
            if (b.polygonOffset) {
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(-1.0f, -1.0f);
            } else {
                glDisable(GL_POLYGON_OFFSET_FILL);
            }
            program_.setVec3("uFogColor", fog);
            program_.setInt("uTexMode", b.texture ? 1 : 0);
            if (b.texture) b.texture->bind(0);
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(b.first), static_cast<GLsizei>(b.count));
        }
        glDisable(GL_POLYGON_OFFSET_FILL);
        glDisable(GL_BLEND);
        setDepth(true, true);
        setCull(CullMode::Back);
    }

private:
    static bool sameState(const QuadBatch& a, const QuadBatch& b) {
        return a.texture == b.texture && a.blend == b.blend && a.depthTest == b.depthTest &&
               a.depthWrite == b.depthWrite && a.polygonOffset == b.polygonOffset && a.cull == b.cull;
    }

    ShaderProgram program_;
    VertexBuffer vbo_;
    VertexArray vao_;
    std::vector<BatchVertex> verts_;
    std::vector<QuadBatch> batches_;
};

struct MeshRecord {
    const GpuMesh* mesh;
    const Material* material;
    Mat4 model;
    Vec4 colour;
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
    QuadRenderer quads;
    QuadRenderer overlay;
    std::unique_ptr<TerrainRenderer> terrain;
    std::unique_ptr<WaterRenderer> water;
    const Terrain* terrainOf = nullptr;
    // Materials by (definition, skin override); unique_ptr keeps addresses stable.
    std::map<std::pair<const ObjectDef*, std::string>, std::unique_ptr<Material>> materials;
    std::vector<MeshRecord> opaque, trans, effect;
    std::vector<const ParticleEmitter*> emitters;
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

WorldRenderer::WorldRenderer() : impl_(new Impl) {}
WorldRenderer::~WorldRenderer() = default;

bool WorldRenderer::init(Vfs& vfs, const DefDatabase& db, std::string* error) {
    Impl& im = *impl_;
    im.vfs = &vfs;
    im.db = &db;
    im.cache.reset(new ResourceCache(vfs));
    if (!im.meshes.init(error)) return false;
    if (!im.particles.init(vfs, error)) return false;
    if (!im.quads.init(error)) return false;
    if (!im.overlay.init(error)) return false;
    return true;
}

bool WorldRenderer::beginLevel(const World& world, std::string* error) {
    Impl& im = *impl_;
    im.terrain.reset();
    im.water.reset();
    im.terrainOf = nullptr;
    particles_.reset(1);
    const Terrain* t = world.terrain();
    if (!t) return true; // an empty test level: nothing to build
    std::unique_ptr<TerrainRenderer> tr(new TerrainRenderer());
    std::unique_ptr<WaterRenderer> wr(new WaterRenderer());
    if (!tr->build(*t, *im.vfs, error)) return false;
    if (!wr->build(*t, *im.vfs, error)) return false;
    im.terrain = std::move(tr);
    im.water = std::move(wr);
    im.terrainOf = t;
    return true;
}

// ---------------------------------------------------------------------------------------
// Frame.
// ---------------------------------------------------------------------------------------

namespace {

struct FrameContext {
    const World* world;
    ResourceCache* cache;
    QuadRenderer* sprites;
    int* dropped;
    Vec3 camRight, camUp;
    int spriteCount = 0, markCount = 0;
};

Mat4 entityMatrix(const Entity& e, bool applyScale) {
    Vec3 a0 = e.v3(F_AXIS), a1 = e.v3(F_AXIS + 3), a2 = e.v3(F_AXIS + 6), o = e.v3(F_BASE_ORIGIN);
    Mat4 m;
    m.at(0, 0) = a0.x; m.at(0, 1) = a0.y; m.at(0, 2) = a0.z; m.at(0, 3) = 0.0f;
    m.at(1, 0) = a1.x; m.at(1, 1) = a1.y; m.at(1, 2) = a1.z; m.at(1, 3) = 0.0f;
    m.at(2, 0) = a2.x; m.at(2, 1) = a2.y; m.at(2, 2) = a2.z; m.at(2, 3) = 0.0f;
    m.at(3, 0) = o.x;  m.at(3, 1) = o.y;  m.at(3, 2) = o.z;  m.at(3, 3) = 1.0f;
    float s = e.f(F_SCALE);
    if (applyScale && s > 0.001f) m = m * scale(Vec3{s, s, s});
    return m;
}

const std::string& skinOf(const Entity& e) { return e.skinPath.empty() ? e.def->skin : e.skinPath; }

// Sprite texture coordinates of render-pipeline.md 3.3, converted to this engine's
// v = 1 - t. out: s0, v0 (corner x0,y0) and s1, v1 (corner x1,y1).
void spriteUv(const ObjectDef& def, const Entity& e, float& s0, float& v0, float& s1, float& v1) {
    float t0 = def.bboxMin[3], t1 = def.bboxMax[3];
    s0 = def.bboxMin[2];
    s1 = def.bboxMax[2];
    if (def.hasFrames && def.frameStart > 0) {
        int cols = def.frameStart;
        int rows = def.frameEnd > 0 ? def.frameEnd : 1;
        float fr = e.f(F_FRAME);
        int f = (fr > -1.0e7f && fr < 1.0e7f) ? static_cast<int>(fr) : 0;
        if (f < 0) f = 0;
        float du = 1.0f / static_cast<float>(cols), dv = 1.0f / static_cast<float>(rows);
        s0 = static_cast<float>(f % cols) * du;
        s1 = s0 + du;
        t0 = 1.0f - dv - static_cast<float>(f / cols) * dv;
        t1 = t0 + dv;
    }
    v0 = 1.0f - t0;
    v1 = 1.0f - t1;
}

void addSprite(FrameContext& fc, const Entity& e) {
    const ObjectDef& def = *e.def;
    if (fc.spriteCount >= kSpriteCap) {
        ++*fc.dropped;
        return;
    }
    const std::string& skin = skinOf(e);
    if (skin.empty()) return;
    ResourceCache::LoadedTexture tex = fc.cache->texture(skin);
    float s0, v0, s1, v1;
    spriteUv(def, e, s0, v0, s1, v1);
    float x0 = def.bboxMin[0], y0 = def.bboxMin[1], x1 = def.bboxMax[0], y1 = def.bboxMax[1];
    Vec3 o = e.v3(F_BASE_ORIGIN);
    float yaw = e.f(F_ANGLES + 2);
    Vec3 X, Y;
    float sc = 1.0f;
    if (def.type == ObjectType::Sprite) {
        // Billboard, rotated in the view plane by the yaw (counter-clockwise), scaled.
        float a = degToRad(yaw), c = std::cos(a), s = std::sin(a);
        X = fc.camRight * c + fc.camUp * s;
        Y = fc.camRight * -s + fc.camUp * c;
        float es = e.f(F_SCALE);
        if (es > 0.001f) sc = es;
    } else {
        float a = degToRad(yaw), c = std::cos(a), s = std::sin(a);
        X = Vec3{c, s, 0.0f};
        Y = def.type == ObjectType::HSprite ? Vec3{-s, c, 0.0f} : Vec3{0.0f, 0.0f, 1.0f};
    }
    auto P = [&](float x, float y) { return o + X * (x * sc) + Y * (y * sc); };
    float r = e.f(F_COLOR), g = e.f(F_COLOR + 1), b = e.f(F_COLOR + 2), al = e.f(F_COLOR + 3);
    Vec3 p0 = P(x0, y0), p1 = P(x1, y0), p2 = P(x1, y1), p3 = P(x0, y1);
    BatchVertex q[4] = {
        {p0.x, p0.y, p0.z, s0, v0, r, g, b, al},
        {p1.x, p1.y, p1.z, s1, v0, r, g, b, al},
        {p2.x, p2.y, p2.z, s1, v1, r, g, b, al},
        {p3.x, p3.y, p3.z, s0, v1, r, g, b, al},
    };
    QuadBatch st;
    st.texture = tex.texture;
    st.blend = quadBlendOf(def.blend);
    st.depthTest = !(def.rflag & RF_NODEPTHTEST);
    st.depthWrite = !(def.rflag & RF_NODEPTHWRITE);
    fc.sprites->quad(st, q);
    ++fc.spriteCount;
}

// TYPE_MARK (render-pipeline.md 3.4): a decal over origin.xy + [min, max] following the
// terrain. The original clips the terrain triangles to the rectangle; this interim version
// samples the terrain height on a 10-unit grid over the rectangle.
void addMark(FrameContext& fc, const Entity& e, QuadRenderer& marks) {
    const ObjectDef& def = *e.def;
    if (fc.markCount >= kMarkCap) {
        ++*fc.dropped;
        return;
    }
    const std::string& skin = skinOf(e);
    if (skin.empty()) return;
    ResourceCache::LoadedTexture tex = fc.cache->texture(skin);
    Vec3 o = e.v3(F_BASE_ORIGIN);
    float X0 = o.x + def.bboxMin[0], X1 = o.x + def.bboxMax[0];
    float Y0 = o.y + def.bboxMin[1], Y1 = o.y + def.bboxMax[1];
    if (!(X1 > X0) || !(Y1 > Y0)) return;
    int nx = std::min(std::max(static_cast<int>((X1 - X0) / 10.0f), 1), 40);
    int ny = std::min(std::max(static_cast<int>((Y1 - Y0) / 10.0f), 1), 40);
    float r = e.f(F_COLOR), g = e.f(F_COLOR + 1), b = e.f(F_COLOR + 2);
    QuadBatch st;
    st.texture = tex.texture;
    st.blend = quadBlendOf(def.blend);
    st.depthWrite = false;
    st.polygonOffset = true;
    const World& w = *fc.world;
    auto V = [&](int i, int j) {
        float x = X0 + (X1 - X0) * static_cast<float>(i) / static_cast<float>(nx);
        float y = Y0 + (Y1 - Y0) * static_cast<float>(j) / static_cast<float>(ny);
        float s = (x - X0) / (X1 - X0), t = (y - Y0) / (Y1 - Y0);
        return BatchVertex{x, y, w.terrainHeight(x, y), s, 1.0f - t, r, g, b, 1.0f};
    };
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            BatchVertex q[4] = {V(i, j), V(i + 1, j), V(i + 1, j + 1), V(i, j + 1)};
            marks.quad(st, q);
        }
    }
    ++fc.markCount;
}

} // namespace

void WorldRenderer::render(const World& world, int width, int height, const WorldRenderOptions& options) {
    Impl& im = *impl_;
    stats_ = WorldRenderStats();
    if (width <= 0 || height <= 0) return;
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const WorldView wv = worldViewOf(world, aspect);
    const Terrain* terrain = world.terrain();

    // Lighting and fog from the level (render-pipeline.md 2.2, 2.3).
    SceneLighting light;
    bool hasFog = false;
    if (terrain) {
        const TerrainStyle& st = terrain->style();
        light.sunColor = Vec3{st.sun[0], st.sun[1], st.sun[2]};
        light.sunDirection = Vec3{st.sun[3], st.sun[4], st.sun[5]};
        light.ambientColor = Vec3{st.sun[6], st.sun[7], st.sun[8]};
        light.fogColor = Vec3{st.fogColor[0], st.fogColor[1], st.fogColor[2]};
        light.fogStart = st.fogNear;
        light.fogEnd = st.fogFar;
        hasFog = st.hasFog;
    }
    if (!hasFog) {
        light.fogStart = 1.0e8f;
        light.fogEnd = 2.0e8f;
    }
    const Camera cam = cameraFromView(wv, aspect);

    // Pass 0: clear to the fog colour (black without fog).
    setViewport(0, 0, width, height);
    Vec3 cc = hasFog ? light.fogColor : Vec3{0, 0, 0};
    clear({cc.x, cc.y, cc.z, 1.0f}, true);
    setDepth(true, true);
    setCull(CullMode::Back);

    // Render records from the entity pass order: newest root first, parents before
    // children (render-pipeline.md 1.2).
    im.opaque.clear();
    im.trans.clear();
    im.effect.clear();
    im.quads.clear();
    im.dropped = 0;
    QuadRenderer& markBatch = im.overlay; // reused: marks are drawn before the overlay pass
    markBatch.clear();
    FrameContext fc{&world, im.cache.get(), &im.quads, &im.dropped, Vec3{wv.view.at(0, 0), wv.view.at(1, 0), wv.view.at(2, 0)},
                    Vec3{wv.view.at(0, 1), wv.view.at(1, 1), wv.view.at(2, 1)}};
    std::vector<int> stack;
    const std::vector<int> roots = world.listEntities();
    for (int root : roots) {
        stack.clear();
        stack.push_back(root);
        int guard = 0;
        while (!stack.empty() && guard++ < kMaxEntitySlots) {
            int i = stack.back();
            stack.pop_back();
            if (!world.validIndex(i)) continue;
            const Entity& e = world.entity(i);
            if ((e.rt & RT_REMOVED) || e.emitter) continue;
            for (auto c = e.children.rbegin(); c != e.children.rend(); ++c) {
                if (world.validIndex(*c) && world.entity(*c).parent == i) stack.push_back(*c);
            }
            if (!e.def || (e.flagBits() & FL_NODRAW)) continue;
            const ObjectDef& def = *e.def;
            switch (def.type) {
                case ObjectType::Model: {
                    if (!options.models || def.model.empty()) break;
                    const GpuMesh& mesh = im.cache->mesh(def.model);
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
                    list->push_back({&mesh, &mat, entityMatrix(e, true),
                                     Vec4{e.f(F_COLOR), e.f(F_COLOR + 1), e.f(F_COLOR + 2), e.f(F_COLOR + 3)}});
                    break;
                }
                case ObjectType::Mark:
                    if (options.sprites) addMark(fc, e, markBatch);
                    break;
                case ObjectType::Sprite:
                case ObjectType::HSprite:
                case ObjectType::VSprite:
                    if (options.sprites) addSprite(fc, e);
                    break;
            }
        }
    }

    TerrainViewParams tv;
    tv.view = wv.view;
    tv.projection = wv.projection;
    tv.cameraPos = wv.eye;
    tv.fogColour = light.fogColor;
    tv.fogStart = light.fogStart;
    tv.fogEnd = light.fogEnd;
    tv.time = world.time();
    tv.mapPos = world.mapPos();

    // Pass 2: terrain.
    if (options.terrain && im.terrain && im.terrainOf == terrain) {
        im.terrain->render(tv);
        stats_.terrainChunks = im.terrain->lastVisibleChunks();
    }
    setDepth(true, true);
    // Pass 4: ground marks. (Pass 5, shadows: not yet.)
    markBatch.draw(wv.view, wv.projection, light.fogColor, light.fogStart, light.fogEnd);
    // Pass 6: opaque list.
    im.meshes.begin(cam, light);
    for (const MeshRecord& r : im.opaque) im.meshes.submit(*r.mesh, *r.material, r.model, r.colour);
    im.meshes.end();
    // Pass 7: water.
    if (options.terrain && im.water && im.terrainOf == terrain) im.water->render(tv);
    // Passes 8 and 9: transparent then effect list.
    im.meshes.begin(cam, light);
    for (const MeshRecord& r : im.trans) im.meshes.submit(*r.mesh, *r.material, r.model, r.colour);
    for (const MeshRecord& r : im.effect) im.meshes.submit(*r.mesh, *r.material, r.model, r.colour);
    im.meshes.end();
    glDisable(GL_BLEND);
    setDepth(true, true);
    setCull(CullMode::Back);
    // Pass 10: particles.
    if (options.particles) {
        particles_.collect(im.emitters);
        ParticleViewParams pv;
        pv.view = wv.view;
        pv.projection = wv.projection;
        pv.fogColour = light.fogColor;
        pv.fogStart = light.fogStart;
        pv.fogEnd = light.fogEnd;
        if (!im.emitters.empty()) im.particles.draw(im.emitters.data(), im.emitters.size(), pv);
        stats_.emitters = static_cast<int>(im.emitters.size());
        stats_.particles = im.particles.lastParticleCount();
    }
    // Pass 12: sprites.
    im.quads.draw(wv.view, wv.projection, light.fogColor, light.fogStart, light.fogEnd);

    stats_.models = static_cast<int>(im.opaque.size() + im.trans.size() + im.effect.size());
    stats_.sprites = fc.spriteCount;
    stats_.marks = fc.markCount;
    stats_.dropped = im.dropped;

    // Pass 14: brightness, dst * b + b * dst over the whole screen (render-pipeline.md 1.5).
    if (options.brightness > 0.0f) {
        float b = options.brightness;
        im.overlay.clear();
        QuadBatch st;
        st.blend = QuadBlend::Modulate2x;
        st.depthTest = false;
        st.depthWrite = false;
        st.cull = false;
        BatchVertex q[4] = {{-1, -1, 0, 0, 0, b, b, b, 1}, {1, -1, 0, 0, 0, b, b, b, 1},
                            {1, 1, 0, 0, 0, b, b, b, 1}, {-1, 1, 0, 0, 0, b, b, b, 1}};
        im.overlay.quad(st, q);
        im.overlay.draw(Mat4::identity(), Mat4::identity(), Vec3{0, 0, 0}, 1.0e8f, 2.0e8f);
    }
    setDepth(true, true);
}

void WorldRenderer::drawOverlay(const OverlayRect* rects, size_t count, int width, int height) {
    Impl& im = *impl_;
    if (!count || width <= 0 || height <= 0) return;
    setViewport(0, 0, width, height);
    im.overlay.clear();
    QuadBatch st;
    st.blend = QuadBlend::Alpha;
    st.depthTest = false;
    st.depthWrite = false;
    st.cull = false;
    for (size_t i = 0; i < count; ++i) {
        const OverlayRect& r = rects[i];
        const Vec4& c = r.colour;
        BatchVertex q[4] = {{r.x, r.y, 0, 0, 0, c.x, c.y, c.z, c.w},
                            {r.x + r.w, r.y, 0, 0, 0, c.x, c.y, c.z, c.w},
                            {r.x + r.w, r.y + r.h, 0, 0, 0, c.x, c.y, c.z, c.w},
                            {r.x, r.y + r.h, 0, 0, 0, c.x, c.y, c.z, c.w}};
        im.overlay.quad(st, q);
    }
    // Virtual 800x600, y down (render-pipeline.md 8.1).
    im.overlay.draw(Mat4::identity(), ortho(0.0f, 800.0f, 600.0f, 0.0f, -1.0f, 1.0f), Vec3{0, 0, 0}, 1.0e8f, 2.0e8f);
    setDepth(true, true);
}

} // namespace as3d
