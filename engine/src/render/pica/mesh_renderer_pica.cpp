// PICA backend of as3d::MeshRenderer (as3d/scene.h) and the MeshPass behind it
// (mesh_renderer_pica.h).
#include "mesh_renderer_pica.h"

#include <citro3d.h>

#include <algorithm>

#include "as3d/ambient_cube.h"
#include "as3d/defs.h"
#include "gpu_mesh_pica.h"
#include "pica_frame.h"
#include "pica_space.h"
#include "tev.h"

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

} // namespace

MeshDrawStats meshFrameStats() {
    const FrameStats& f = frameStats();
    return f.frame == frameArena().frames() ? f.stats : MeshDrawStats();
}

void MeshPass::begin(const SceneLighting& lighting, const DynamicLight* lights, size_t lightCount) {
    lighting_ = lighting;
    lights_.assign(lights, lights + std::min(lightCount, static_cast<size_t>(kMaxDynamicLights)));
    records_.clear();
}

void MeshPass::submit(const GpuMesh& mesh, const Material& material, const Mat4& model, const Vec4& colour) {
    const GpuMeshBuffers* gpu = mesh.buffers();
    if (!mesh.valid || mesh.indexCount == 0 || !gpu || !gpu->vertices) return;
    Record r;
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
    records_.push_back(r);
}

void MeshPass::finish() {
    std::stable_sort(records_.begin(), records_.end(), [](const Record& a, const Record& b) {
        return static_cast<int>(a.bucket) < static_cast<int>(b.bucket);
    });
}

void MeshPass::draw(const Mat4& viewProj) {
    stats_ = MeshDrawStats();
    if (records_.empty() || !backendReady()) return;
    ShaderSet& sh = shaders();
    sh.bind(Shader::Mesh);
    sh.setMeshTint(Vec4{1.0f, 1.0f, 1.0f, 1.0f});
    // TEV only changes between kinds (and unlit colours): 0 tex*colour, 1 primary, 2 constant.
    int lastTev = -1;
    u8 lastConstant[4] = {0, 0, 0, 0};
    for (const Record& r : records_) {
        applyMaterialState(r.key);
        sh.setMvp(viewProj * r.model);
        if (!r.unlit) sh.setMeshCube(r.cube);
        if (r.textured) r.texture->bind(0);
        int tev;
        if (r.unlit) tev = r.textured ? 3 : 2;
        else tev = r.textured ? 0 : 1;
        const bool constantChanged = std::equal(r.constant, r.constant + 4, lastConstant) == false;
        if (tev != lastTev || (r.unlit && constantChanged)) {
            switch (tev) {
            case 0: applyTev(tevModulateTexColor()); break;
            case 1: applyTev(tevPrimary()); break;
            case 2: applyTev(tevConstantColor(r.constant[0], r.constant[1], r.constant[2], r.constant[3])); break;
            default:
                applyTev(tevModulateTexConstant(r.constant[0], r.constant[1], r.constant[2], r.constant[3]));
                break;
            }
            lastTev = tev;
            std::copy(r.constant, r.constant + 4, lastConstant);
        }
        sh.setVertices(r.gpu->vertices);
        for (const GpuMeshBuffers::Surface& s : r.gpu->surfaces) {
            C3D_DrawElements(GPU_TRIANGLES, static_cast<int>(s.indexCount), C3D_UNSIGNED_SHORT,
                             r.gpu->indices + s.firstIndex);
            ++stats_.draws;
        }
        stats_.vertices += static_cast<int>(r.gpu->vertexCount);
        stats_.triangles += static_cast<int>(r.gpu->indexCount / 3);
        stats_.bytes += r.gpu->bytes;
    }
    MeshDrawStats& f = currentFrameStats();
    f.draws += stats_.draws;
    f.vertices += stats_.vertices;
    f.triangles += stats_.triangles;
    f.bytes += stats_.bytes;
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
    float time = 0.0f;         // ENV_QUAD rotation (environment maps: M3.4)
    bool envViewNormal = false; // the sequels' env coordinates (M3.4)
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
