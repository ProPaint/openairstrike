// Implements as3d::MeshRenderer (as3d/scene.h): begin()/submit()/end() with the
// SORT_OPAQUE, SORT_TRANS, SORT_EFFECT list order (no depth sorting, render-pipeline.md 1.2),
// drawing through the one shared shader program in shaders.cpp.
#include <GLES3/gl3.h>

#include <algorithm>

#include "as3d/envmap.h"
#include "as3d/gfx.h"
#include "as3d/scene.h"
#include "gpu_mesh_gl.h"
#include "shaders.h"

namespace as3d {

struct MeshRenderer::Impl {
    struct DrawItem {
        const GpuMesh* mesh = nullptr;
        const Material* material = nullptr;
        Mat4 model = Mat4::identity();
        Vec4 colour{1.0f, 1.0f, 1.0f, 1.0f};
        float distanceToCamera = 0.0f;
    };

    ShaderProgram program;
    bool initialized = false;
    Camera camera;
    SceneLighting lighting;
    std::vector<DrawItem> items;
    std::vector<DynamicLight> lights;
    float time = 0.0f;
    bool envViewNormal = false;

    void draw(const DrawItem& item);
};

MeshRenderer::MeshRenderer() : impl_(std::make_unique<Impl>()) {}
MeshRenderer::~MeshRenderer() = default;

bool MeshRenderer::init(std::string* error) {
    if (impl_->initialized) return true;
    impl_->initialized = impl_->program.compile(shaders::kMeshVertexSrc, shaders::kMeshFragmentSrc, error);
    return impl_->initialized;
}

void MeshRenderer::begin(const Camera& camera, const SceneLighting& lighting) {
    impl_->camera = camera;
    impl_->lighting = lighting;
    impl_->items.clear();
}

void MeshRenderer::setDynamicLights(const DynamicLight* lights, size_t count) {
    impl_->lights.assign(lights, lights + std::min(count, static_cast<size_t>(kMaxDynamicLights)));
}

void MeshRenderer::setTime(float seconds) { impl_->time = seconds; }

void MeshRenderer::setEnvViewNormal(bool on) { impl_->envViewNormal = on; }

void MeshRenderer::submit(const GpuMesh& mesh, const Material& material, const Mat4& modelMatrix,
                           const Vec4& colour) {
    if (!mesh.valid || mesh.indexCount == 0 || !mesh.buffers()) return; // placeholder mesh: nothing to draw
    Impl::DrawItem item;
    item.mesh = &mesh;
    item.material = &material;
    item.model = modelMatrix;
    item.colour = Vec4{colour.x * material.tint.x, colour.y * material.tint.y, colour.z * material.tint.z,
                        colour.w * material.tint.w};
    Vec3 worldPos{modelMatrix.at(3, 0), modelMatrix.at(3, 1), modelMatrix.at(3, 2)};
    item.distanceToCamera = length(worldPos - impl_->camera.eye);
    impl_->items.push_back(item);
}

void MeshRenderer::end() {
    Impl& im = *impl_;
    // docs/spec/render-pipeline.md 1.2: the original never sorts. The three lists (opaque,
    // SORT_TRANS, SORT_EFFECT) are drawn in that order, each in submission order.
    std::stable_sort(im.items.begin(), im.items.end(), [](const Impl::DrawItem& a, const Impl::DrawItem& b) {
        return static_cast<int>(a.material->sortBucket) < static_cast<int>(b.material->sortBucket);
    });

    if (!im.initialized) return; // caller forgot init(); fail safe rather than crash.
    ShaderProgram& program = im.program;
    program.use();
    program.setMat4("uViewProj", im.camera.viewProjMatrix());
    program.setMat4("uView", im.camera.viewMatrix());
    program.setVec3("uSunDir", im.lighting.sunDirection);
    program.setVec3("uSunColor", im.lighting.sunColor);
    program.setVec3("uAmbientColor", im.lighting.ambientColor);
    program.setFloat("uFogStart", im.lighting.fogStart);
    program.setFloat("uFogEnd", im.lighting.fogEnd);
    program.setInt("uTex", 0);
    program.setInt("uEnv", 1);
    program.setInt("uEnvView", im.envViewNormal ? 1 : 0);

    for (const Impl::DrawItem& item : im.items) im.draw(item);
}

void MeshRenderer::Impl::draw(const DrawItem& item) {
    const Material& mat = *item.material;
    MaterialKey key = materialKeyFor(mat);
    applyMaterialState(key);

    // Fog colour follows the blend mode (spec 2.2): additive fades to black, filter to white.
    program.setVec3("uFogColor", fogColorFor(key.fog, lighting.fogColor));
    program.setMat4("uModel", item.model);
    program.setVec4("uColor", item.colour);
    program.setInt("uNoLighting", mat.noLighting ? 1 : 0);
    // Dynamic lights at the model origin, evaluated per signed local axis (spec 2.4).
    Vec3 cube[6];
    if (!mat.noLighting && !(mat.rflag & 0x200u /* RF_NODLIGHT */) && !lights.empty()) {
        Vec3 axes[3];
        for (int i = 0; i < 3; i++) {
            axes[i] = normalize(Vec3{item.model.at(i, 0), item.model.at(i, 1), item.model.at(i, 2)});
        }
        Vec3 origin{item.model.at(3, 0), item.model.at(3, 1), item.model.at(3, 2)};
        accumulateModelLights(lights.data(), lights.size(), origin, axes, cube);
    }
    float cubeData[18];
    for (int i = 0; i < 6; i++) {
        cubeData[i * 3 + 0] = cube[i].x;
        cubeData[i * 3 + 1] = cube[i].y;
        cubeData[i * 3 + 2] = cube[i].z;
    }
    glUniform3fv(program.uniformLocation("uDynCube"), 6, cubeData);

    // Environment mapping: smooth-normal models with an envmap only (spec 4.3).
    EnvMapMode env = envMapModeFromInt(mat.envModeRaw);
    if (!mat.envTexture || !item.mesh->model.smoothNormals) env = EnvMapMode::None;
    program.setInt("uEnvMode", static_cast<int>(env));
    if (env != EnvMapMode::None) {
        Mat3 nm = envNormalMatrix(camera.viewMatrix(), item.model);
        program.setMat3("uNormalMat", nm);
        program.setFloat("uEnvAngle", degToRad(time * kEnvQuadDegreesPerSecond));
        mat.envTexture->bind(1);
    }
    if (mat.texture) mat.texture->bind(0);
    if (env != EnvMapMode::None) glActiveTexture(GL_TEXTURE0);

    const GpuMeshBuffers& gpu = *item.mesh->buffers();
    gpu.vao.bind();
    GLenum indexType = gpu.ibo.type() == IndexType::U16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(item.mesh->indexCount), indexType, nullptr);
}

} // namespace as3d
