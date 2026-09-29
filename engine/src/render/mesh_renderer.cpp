// Implements as3d::MeshRenderer (as3d/scene.h): begin()/submit()/end() with the
// SORT_OPAQUE, SORT_TRANS, SORT_EFFECT list order (no depth sorting, render-pipeline.md 1.2),
// drawing through the one shared shader program in shaders.cpp.
#include "as3d/scene.h"

#include <GLES3/gl3.h>

#include <algorithm>

#include "as3d/envmap.h"
#include "shaders.h"

namespace as3d {

bool MeshRenderer::init(std::string* error) {
    if (initialized_) return true;
    initialized_ = program_.compile(shaders::kMeshVertexSrc, shaders::kMeshFragmentSrc, error);
    return initialized_;
}

void MeshRenderer::begin(const Camera& camera, const SceneLighting& lighting) {
    camera_ = camera;
    lighting_ = lighting;
    items_.clear();
}

void MeshRenderer::setDynamicLights(const DynamicLight* lights, size_t count) {
    lights_.assign(lights, lights + std::min(count, static_cast<size_t>(kMaxDynamicLights)));
}

void MeshRenderer::submit(const GpuMesh& mesh, const Material& material, const Mat4& modelMatrix,
                           const Vec4& colour) {
    if (!mesh.valid || mesh.indexCount == 0) return; // placeholder mesh: nothing to draw
    DrawItem item;
    item.mesh = &mesh;
    item.material = &material;
    item.model = modelMatrix;
    item.colour = Vec4{colour.x * material.tint.x, colour.y * material.tint.y, colour.z * material.tint.z,
                        colour.w * material.tint.w};
    Vec3 worldPos{modelMatrix.at(3, 0), modelMatrix.at(3, 1), modelMatrix.at(3, 2)};
    item.distanceToCamera = length(worldPos - camera_.eye);
    items_.push_back(item);
}

void MeshRenderer::end() {
    // docs/spec/render-pipeline.md 1.2: the original never sorts. The three lists (opaque,
    // SORT_TRANS, SORT_EFFECT) are drawn in that order, each in submission order.
    std::stable_sort(items_.begin(), items_.end(), [](const DrawItem& a, const DrawItem& b) {
        return static_cast<int>(a.material->sortBucket) < static_cast<int>(b.material->sortBucket);
    });

    if (!initialized_) return; // caller forgot init(); fail safe rather than crash.
    program_.use();
    program_.setMat4("uViewProj", camera_.viewProjMatrix());
    program_.setMat4("uView", camera_.viewMatrix());
    program_.setVec3("uSunDir", lighting_.sunDirection);
    program_.setVec3("uSunColor", lighting_.sunColor);
    program_.setVec3("uAmbientColor", lighting_.ambientColor);
    program_.setFloat("uFogStart", lighting_.fogStart);
    program_.setFloat("uFogEnd", lighting_.fogEnd);
    program_.setInt("uTex", 0);
    program_.setInt("uEnv", 1);

    for (const DrawItem& item : items_) draw(item);
}

void MeshRenderer::draw(const DrawItem& item) {
    const Material& mat = *item.material;
    applyMaterialState(mat);

    // Fog colour follows the blend mode (spec 2.2): additive fades to black, filter to white.
    Vec3 fog = lighting_.fogColor;
    if (mat.blend == MaterialBlend::Add) fog = Vec3{0.0f, 0.0f, 0.0f};
    else if (mat.blend == MaterialBlend::Filter) fog = Vec3{1.0f, 1.0f, 1.0f};
    program_.setVec3("uFogColor", fog);
    program_.setMat4("uModel", item.model);
    program_.setVec4("uColor", item.colour);
    program_.setInt("uNoLighting", mat.noLighting ? 1 : 0);
    // Dynamic lights at the model origin, evaluated per signed local axis (spec 2.4).
    Vec3 cube[6];
    if (!mat.noLighting && !(mat.rflag & 0x200u /* RF_NODLIGHT */) && !lights_.empty()) {
        Vec3 axes[3];
        for (int i = 0; i < 3; i++) {
            axes[i] = normalize(Vec3{item.model.at(i, 0), item.model.at(i, 1), item.model.at(i, 2)});
        }
        Vec3 origin{item.model.at(3, 0), item.model.at(3, 1), item.model.at(3, 2)};
        accumulateModelLights(lights_.data(), lights_.size(), origin, axes, cube);
    }
    float cubeData[18];
    for (int i = 0; i < 6; i++) {
        cubeData[i * 3 + 0] = cube[i].x;
        cubeData[i * 3 + 1] = cube[i].y;
        cubeData[i * 3 + 2] = cube[i].z;
    }
    glUniform3fv(program_.uniformLocation("uDynCube"), 6, cubeData);

    // Environment mapping: smooth-normal models with an envmap only (spec 4.3).
    EnvMapMode env = envMapModeFromInt(mat.envModeRaw);
    if (!mat.envTexture || !item.mesh->model.smoothNormals) env = EnvMapMode::None;
    program_.setInt("uEnvMode", static_cast<int>(env));
    if (env != EnvMapMode::None) {
        Mat3 nm = envNormalMatrix(camera_.viewMatrix(), item.model);
        program_.setMat3("uNormalMat", nm);
        program_.setFloat("uEnvAngle", degToRad(time_ * kEnvQuadDegreesPerSecond));
        mat.envTexture->bind(1);
    }
    if (mat.texture) mat.texture->bind(0);
    if (env != EnvMapMode::None) glActiveTexture(GL_TEXTURE0);

    item.mesh->vao.bind();
    GLenum indexType = item.mesh->ibo.type() == IndexType::U16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(item.mesh->indexCount), indexType, nullptr);
}

} // namespace as3d
