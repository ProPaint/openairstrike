// PICA backend of as3d::MeshRenderer (as3d/scene.h) and the MeshPass behind it
// (mesh_renderer_pica.h).
#include "mesh_renderer_pica.h"

#include <citro3d.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

#include "as3d/ambient_cube.h"
#include "as3d/defs.h"
#include "as3d/envmap.h"
#include "gpu_mesh_pica.h"
#include "pica_frame.h"
#include "pica_space.h"
#include "tev.h"
#include "texture_budget.h"

namespace as3d {
namespace pica {

namespace {

u8 toByte(float v) { return static_cast<u8>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); }

// The vertex colour alone (lit, untextured).
TevConfig tevPrimary() {
    TevConfig c;
    c.count = 1;
    TevStage& s = c.stages[0];
    s.srcRgb[0] = TevSrc::Primary;
    s.srcA[0] = TevSrc::Primary;
    s.funcRgb = TevFunc::Replace;
    s.funcA = TevFunc::Replace;
    return c;
}

struct FrameStats {
    unsigned frame = ~0u;
    MeshDrawStats stats;
};

FrameStats& frameStats() {
    static FrameStats s;
    return s;
}

// The accumulator of the current frame (reset when the arena's frame counter moved on).
MeshDrawStats& currentFrameStats() {
    FrameStats& f = frameStats();
    const unsigned now = frameArena().frames();
    if (f.frame != now) {
        f.frame = now;
        f.stats = MeshDrawStats();
    }
    return f.stats;
}

bool g_sortOpaque = true;
bool g_merge = false;

// The opaque-bucket records whose order only matters for equal depths: no blending, depth test
// and write on (the GEQUAL test: a later fragment at exactly the same depth wins, so reordering
// changes only coplanar overlaps).
bool sortable(const MeshPass::Record& r) {
    return r.bucket == SortBucket::Opaque && r.key.blend == RenderBlend::None && r.key.depthTest && r.key.depthWrite;
}

// The state order of a run of sortable records: the shader (plain, env), the skin, the TEV kind
// and its constant, the material state, then the mesh (its vertex buffer).
bool stateLess(const MeshPass::Record& a, const MeshPass::Record& b) {
    if (a.env != b.env) return a.env < b.env;
    if (a.texture != b.texture) return std::less<const Texture2D*>()(a.texture, b.texture);
    if (a.envTexture != b.envTexture) return std::less<const Texture2D*>()(a.envTexture, b.envTexture);
    if (a.unlit != b.unlit) return a.unlit < b.unlit;
    if (a.unlit) {
        const int c = std::memcmp(a.constant, b.constant, 4);
        if (c != 0) return c < 0;
    }
    if (a.key.packed() != b.key.packed()) return a.key.packed() < b.key.packed();
    return std::less<const GpuMeshBuffers*>()(a.gpu, b.gpu);
}

} // namespace

void setMeshStateSort(bool on) { g_sortOpaque = on; }
void setMeshMerge(bool on) { g_merge = on; }
bool meshMerge() { return g_merge; }
bool meshStateSort() { return g_sortOpaque; }

MeshDrawStats meshFrameStats() {
    const FrameStats& f = frameStats();
    return f.frame == frameArena().frames() ? f.stats : MeshDrawStats();
}

void MeshPass::begin(const SceneLighting& lighting, const DynamicLight* lights, size_t lightCount) {
    lighting_ = lighting;
    lights_.assign(lights, lights + std::min(lightCount, static_cast<size_t>(kMaxDynamicLights)));
    records_.clear();
    order_.clear();
    items_.clear();
}

void MeshPass::setEnvironment(const Mat4& view, float timeSeconds, bool viewNormal) {
    envView_ = view;
    envTime_ = timeSeconds;
    envViewNormal_ = viewNormal;
}

void MeshPass::submit(const GpuMesh& mesh, const Material& material, const Mat4& model, const Vec4& colour) {
    const GpuMeshBuffers* gpu = mesh.buffers();
    if (!mesh.valid || mesh.indexCount == 0 || !gpu || !gpu->vertices) return;
    records_.emplace_back(); // filled in place (a record is about 300 bytes)
    Record& r = records_.back();
    r.gpu = gpu;
    r.texture = material.texture && material.texture->valid() ? material.texture : nullptr;
    r.textured = r.texture != nullptr;
    r.key = materialKeyFor(material);
    r.model = model;
    r.bucket = material.sortBucket;
    r.unlit = material.noLighting;
    if (r.unlit) {
        // GL: uColor = entity colour * material tint, replacing the light.
        r.constant[0] = toByte(colour.x * material.tint.x);
        r.constant[1] = toByte(colour.y * material.tint.y);
        r.constant[2] = toByte(colour.z * material.tint.z);
        r.constant[3] = toByte(colour.w * material.tint.w);
    } else {
        // The GL vertex shader's cPos/cNeg, once per draw (ambient_cube.h): the dynamic
        // lights at the model origin per signed local axis, then sun and ambient.
        Vec3 axes[3];
        modelAxes(model, axes);
        Vec3 dyn[6];
        if (!(material.rflag & RF_NODLIGHT) && !lights_.empty()) {
            // GL (mesh_renderer.cpp) normalises the axes the same way for the lights.
            const Vec3 origin{model.at(3, 0), model.at(3, 1), model.at(3, 2)};
            accumulateModelLights(lights_.data(), lights_.size(), origin, axes, dyn);
        }
        ambientCubeColors(lighting_, axes, dyn, r.cube);
    }
    // Environment map: as GL (mesh_renderer.cpp), smooth-normal models with an envmap only.
    EnvMapMode env = envMapModeFromInt(material.envModeRaw);
    if (!material.envTexture || !material.envTexture->valid() || !mesh.model.smoothNormals || !gpu->envNormals)
        env = EnvMapMode::None;
    if (env != EnvMapMode::None && env != EnvMapMode::Quad && !r.textured) env = EnvMapMode::None;
    if (env != EnvMapMode::None) {
        r.env = static_cast<int>(env);
        r.envTexture = material.envTexture;
        r.envAlpha = !envViewNormal_;
        r.modelView = envView_ * model;
        Mat3 n;
        if (envViewNormal_) {
            // The sequels: the folded normal through the world matrix (scale kept) and the view.
            n = mat3FromMat4(envView_) * mat3FromMat4(model);
            r.envSelView = 0.5f;
            r.envSelSphere = 0.0f;
        } else {
            n = envNormalMatrix(envView_, model); // GL uNormalMat
            r.envSelView = 0.0f;
            r.envSelSphere = 1.0f;
        }
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col) r.normalRows[row][col] = n.at(col, row);
        if (env == EnvMapMode::Quad) {
            const float a = degToRad(envTime_ * kEnvQuadDegreesPerSecond);
            const float c = std::cos(a), sn = std::sin(a);
            r.rot[0][0] = c;
            r.rot[0][1] = -sn;
            r.rot[1][0] = sn;
            r.rot[1][1] = c;
        }
    }
}

void MeshPass::finish() {
    // The drawing order as indices (the records stay where submit() put them): by bucket, in
    // submission order inside a bucket.
    const size_t n = records_.size();
    order_.clear();
    order_.reserve(n);
    for (int b = 0; b < 3; ++b)
        for (size_t k = 0; k < n; ++k)
            if (static_cast<int>(records_[k].bucket) == b) order_.push_back(static_cast<u16>(k));
    if (!g_sortOpaque) {
        buildItems();
        return;
    }
    // Each run of sortable records (between blended or depth-write-off ones, which keep their
    // place) in state order, ties in submission order.
    auto less = [this](u16 a, u16 b) { return stateLess(records_[a], records_[b]); };
    size_t i = 0;
    while (i < n) {
        if (!sortable(records_[order_[i]])) {
            ++i;
            continue;
        }
        size_t j = i + 1;
        while (j < n && sortable(records_[order_[j]])) ++j;
        if (j - i > 1)
            std::stable_sort(order_.begin() + static_cast<std::ptrdiff_t>(i), order_.begin() + static_cast<std::ptrdiff_t>(j), less);
        i = j;
    }
    buildItems();
}

void MeshPass::buildItems() {
    // The draws: one per record, or one per run of small records in a row (drawing order) with
    // the same skin and material state, merged (the triangles keep their order).
    items_.clear();
    const size_t n = order_.size();
    auto small = [](const Record& r) {
        return g_merge && r.env == 0 && r.gpu->vertexCount <= kMergeMaxVertices && r.gpu->indexCount <= kMergeMaxIndices;
    };
    size_t i = 0;
    while (i < n) {
        const Record& a = records_[order_[i]];
        size_t j = i + 1;
        size_t vertices = a.gpu->indexCount;
        if (small(a)) {
            while (j < n) {
                const Record& b = records_[order_[j]];
                if (!small(b) || b.texture != a.texture || b.key.packed() != a.key.packed() ||
                    vertices + b.gpu->indexCount > kMergeMaxRunVertices)
                    break;
                vertices += b.gpu->indexCount;
                ++j;
            }
        }
        DrawItem item;
        item.first = static_cast<u16>(i);
        item.count = static_cast<u16>(j - i);
        item.vertices = item.count > 1 ? vertices : 0;
        items_.push_back(item);
        i = j;
    }
}

// The merge of small meshes (setMeshMerge): one record's triangles in world space, each corner
// as a FlatVertex with the colour the mesh shader would give it (lit: clamp(sum w_i cube_i) with
// alpha 1; unlit: the constant), for the flat shader and tevModulateTexColor / tevPrimary.
void MeshPass::bakeRecord(const Record& r, FlatVertex* out) {
    const GpuMeshBuffers& g = *r.gpu;
    const Mat4& m = r.model;
    scratch_.resize(g.vertexCount);
    for (u32 k = 0; k < g.vertexCount; ++k) {
        const MeshVertex& v = g.vertices[k];
        FlatVertex& o = scratch_[k];
        const float x = v.pos[0], y = v.pos[1], z = v.pos[2];
        o.pos[0] = m.m[0] * x + m.m[4] * y + m.m[8] * z + m.m[12];
        o.pos[1] = m.m[1] * x + m.m[5] * y + m.m[9] * z + m.m[13];
        o.pos[2] = m.m[2] * x + m.m[6] * y + m.m[10] * z + m.m[14];
        o.uv[0] = v.uv[0];
        o.uv[1] = v.uv[1];
        if (r.unlit) {
            std::memcpy(o.rgba, r.constant, 4);
        } else {
            const float w[6] = {v.wPos[0] * (1.0f / 255.0f), v.wPos[1] * (1.0f / 255.0f), v.wPos[2] * (1.0f / 255.0f),
                                v.wNeg[0] * (1.0f / 255.0f), v.wNeg[1] * (1.0f / 255.0f), v.wNeg[2] * (1.0f / 255.0f)};
            float c[3] = {0.0f, 0.0f, 0.0f};
            for (int i = 0; i < 6; ++i) {
                c[0] += w[i] * r.cube[i].x;
                c[1] += w[i] * r.cube[i].y;
                c[2] += w[i] * r.cube[i].z;
            }
            for (int i = 0; i < 3; ++i) o.rgba[i] = toByte(c[i]);
            o.rgba[3] = 255;
        }
    }
    for (u32 k = 0; k < g.indexCount; ++k) out[k] = scratch_[g.indices[k]];
}

void MeshPass::draw(const Mat4& viewProj) {
    stats_ = MeshDrawStats();
    if (records_.empty() || !backendReady()) return;
    ShaderSet& sh = shaders();
    sh.bind(Shader::Mesh);
    sh.setMeshTint(Vec4{1.0f, 1.0f, 1.0f, 1.0f});
    // TEV only changes between kinds (and unlit colours): 0 tex*colour, 1 primary, 2 constant,
    // 3 tex*constant.
    int lastTev = -1;
    u8 lastConstant[4] = {0, 0, 0, 0};
    Shader bound = Shader::Mesh;
    // Repeats skipped inside this loop (nothing else issues state between two records): the
    // material state and the vertex buffer (C3D_GetBufInfo always re-emits the attribute setup).
    u32 lastKey = ~0u;
    const void* lastVertices = nullptr;
    auto bindShader = [&](Shader want) {
        if (want == bound) return;
        // Float uniforms are global registers: everything again after the switch.
        sh.bind(want);
        if (want != Shader::Flat) sh.setMeshTint(Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        bound = want;
        lastVertices = nullptr;
    };
    auto setTev = [&](int tev, const u8* constant) {
        const bool constantChanged = !std::equal(constant, constant + 4, lastConstant);
        if (tev == lastTev && !(tev >= 2 && constantChanged)) return;
        switch (tev) {
        case 0: applyTev(tevModulateTexColor()); break;
        case 1: applyTev(tevPrimary()); break;
        case 2: applyTev(tevConstantColor(constant[0], constant[1], constant[2], constant[3])); break;
        default: applyTev(tevModulateTexConstant(constant[0], constant[1], constant[2], constant[3])); break;
        }
        lastTev = tev;
        std::copy(constant, constant + 4, lastConstant);
    };
    const unsigned arenaFrame = frameArena().frames();
    for (DrawItem& item : items_) {
        const Record& first = records_[order_[item.first]];
        if (first.key.packed() != lastKey) {
            applyMaterialState(first.key);
            lastKey = first.key.packed();
            ++stats_.stateChanges;
        }
        if (first.textured) {
            if (boundTexture(0) != c3dTexOf(*first.texture)) ++stats_.textureBinds;
            first.texture->bind(0);
        }
        if (item.count > 1) {
            // A merged run: built into the arena by the first eye of the frame, reused by the second.
            if (item.arenaFrame != arenaFrame) {
                item.arenaFrame = arenaFrame;
                item.verts = frameArena().allocArray<FlatVertex>(item.vertices);
                if (item.verts) {
                    FlatVertex* o = item.verts;
                    for (u16 k = 0; k < item.count; ++k) {
                        const Record& r = records_[order_[item.first + k]];
                        bakeRecord(r, o);
                        o += r.gpu->indexCount;
                    }
                    stats_.mergeArenaBytes += item.vertices * sizeof(FlatVertex);
                }
            }
            if (item.verts) {
                bindShader(Shader::Flat);
                sh.setMvp(viewProj);
                setTev(first.textured ? 0 : 1, first.constant);
                if (lastVertices != item.verts) {
                    sh.setVertices(item.verts);
                    lastVertices = item.verts;
                }
                C3D_DrawArrays(GPU_TRIANGLES, 0, static_cast<int>(item.vertices));
                ++stats_.draws;
                ++stats_.mergedDraws;
                stats_.mergedRecords += item.count;
                for (u16 k = 0; k < item.count; ++k) {
                    const GpuMeshBuffers& g = *records_[order_[item.first + k]].gpu;
                    stats_.vertices += static_cast<int>(g.vertexCount);
                    stats_.triangles += static_cast<int>(g.indexCount / 3);
                }
                stats_.bytes += item.vertices * sizeof(FlatVertex);
                continue;
            }
            // No arena left: each record on its own below.
        }
        for (u16 k = 0; k < item.count; ++k) {
            const Record& r = records_[order_[item.first + k]];
            bindShader(r.env != 0 ? Shader::Env : Shader::Mesh);
            sh.setMvp(viewProj * r.model);
            if (!r.unlit) sh.setMeshCube(r.cube);
            if (r.env != 0) {
                sh.setEnvUniforms(r.modelView, r.normalRows, r.envSelView, r.envSelSphere, r.rot);
                r.envTexture->bind(1);
                const TevSrc colour = r.unlit ? TevSrc::Constant : TevSrc::Primary;
                const u8* c = r.constant;
                switch (static_cast<EnvMapMode>(r.env)) {
                case EnvMapMode::Glitter: applyTev(tevEnvGlitter(colour, r.envAlpha, c[0], c[1], c[2], c[3])); break;
                case EnvMapMode::Chrome: applyTev(tevEnvChrome(colour, c[0], c[1], c[2], c[3])); break;
                default: applyTev(tevEnvQuad(colour, c[0], c[1], c[2], c[3])); break;
                }
                lastTev = -1;
                sh.setEnvVertices(r.gpu->vertices, r.gpu->envNormals);
                lastVertices = nullptr;
                for (const GpuMeshBuffers::Surface& s : r.gpu->surfaces) {
                    C3D_DrawElements(GPU_TRIANGLES, static_cast<int>(s.indexCount), C3D_UNSIGNED_SHORT,
                                     r.gpu->indices + s.firstIndex);
                    ++stats_.draws;
                    ++stats_.envDraws;
                }
                stats_.vertices += static_cast<int>(r.gpu->vertexCount);
                stats_.triangles += static_cast<int>(r.gpu->indexCount / 3);
                stats_.bytes += r.gpu->bytes;
                continue;
            }
            int tev;
            if (r.unlit) tev = r.textured ? 3 : 2;
            else tev = r.textured ? 0 : 1;
            setTev(tev, r.constant);
            if (lastVertices != r.gpu->vertices) {
                sh.setVertices(r.gpu->vertices);
                lastVertices = r.gpu->vertices;
            }
            for (const GpuMeshBuffers::Surface& s : r.gpu->surfaces) {
                C3D_DrawElements(GPU_TRIANGLES, static_cast<int>(s.indexCount), C3D_UNSIGNED_SHORT,
                                 r.gpu->indices + s.firstIndex);
                ++stats_.draws;
            }
            stats_.vertices += static_cast<int>(r.gpu->vertexCount);
            stats_.triangles += static_cast<int>(r.gpu->indexCount / 3);
            stats_.bytes += r.gpu->bytes - r.gpu->envBytes; // the env normals are not read here
        }
    }
    MeshDrawStats& f = currentFrameStats();
    f.draws += stats_.draws;
    f.vertices += stats_.vertices;
    f.triangles += stats_.triangles;
    f.bytes += stats_.bytes;
    f.envDraws += stats_.envDraws;
    f.textureBinds += stats_.textureBinds;
    f.stateChanges += stats_.stateChanges;
    f.mergedDraws += stats_.mergedDraws;
    f.mergedRecords += stats_.mergedRecords;
    f.mergeArenaBytes += stats_.mergeArenaBytes;
}

} // namespace pica

// ---------------------------------------------------------------------------------------
// MeshRenderer: begin/submit/end over one MeshPass, drawn with the camera's own projection
// (tilted for the top screen), the W-buffer depth mapping of the world and the level fog.
// ---------------------------------------------------------------------------------------

struct MeshRenderer::Impl {
    pica::MeshPass pass;
    Camera camera;
    SceneLighting lighting;
    std::vector<DynamicLight> lights;
    bool initialized = false;
    float time = 0.0f;          // ENV_QUAD rotation
    bool envViewNormal = false; // the sequels' env coordinates
};

MeshRenderer::MeshRenderer() : impl_(std::make_unique<Impl>()) {}
MeshRenderer::~MeshRenderer() = default;

bool MeshRenderer::init(std::string* error) {
    if (impl_->initialized) return true;
    impl_->initialized = pica::backendReady();
    if (!impl_->initialized && error) *error = "MeshRenderer: the PICA backend is not initialised (pica::initBackend)";
    return impl_->initialized;
}

void MeshRenderer::begin(const Camera& camera, const SceneLighting& lighting) {
    impl_->camera = camera;
    impl_->lighting = lighting;
    impl_->pass.begin(lighting, impl_->lights.data(), impl_->lights.size());
    impl_->pass.setEnvironment(camera.viewMatrix(), impl_->time, impl_->envViewNormal);
}

void MeshRenderer::setDynamicLights(const DynamicLight* lights, size_t count) {
    impl_->lights.assign(lights, lights + std::min(count, static_cast<size_t>(kMaxDynamicLights)));
}

void MeshRenderer::setTime(float seconds) { impl_->time = seconds; }

void MeshRenderer::setEnvViewNormal(bool on) { impl_->envViewNormal = on; }

void MeshRenderer::submit(const GpuMesh& mesh, const Material& material, const Mat4& modelMatrix,
                          const Vec4& colour) {
    impl_->pass.submit(mesh, material, modelMatrix, colour);
}

void MeshRenderer::end() {
    Impl& im = *impl_;
    im.pass.finish();
    if (!im.initialized || !pica::backendReady()) return;
    const Camera& cam = im.camera;
    // The world's depth mapping (docs/3ds-port.md M1.3: W-buffer) for this camera's near plane.
    if (pica::depthMapping() != FogDepthMap::WBuffer || pica::depthMappingNear() != cam.nearPlane)
        pica::setDepthMapping(FogDepthMap::WBuffer, cam.nearPlane);
    pica::setLevelFogColor(im.lighting.fogColor);
    // GL's linear fog; without fog the level sets fogStart far beyond the far plane.
    if (im.lighting.fogStart < cam.farPlane) pica::setFog(cam.nearPlane, cam.farPlane, im.lighting.fogStart, im.lighting.fogEnd);
    else pica::disableFog();
    const Mat4 viewProj = pica::picaTiltMatrix() * cam.projMatrix() * cam.viewMatrix();
    im.pass.draw(viewProj);
}

} // namespace as3d
