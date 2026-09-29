// Implements as3d::MeshRenderer (as3d/scene.h): begin()/submit()/end() with the
// SORT_OPAQUE, SORT_TRANS, SORT_EFFECT list order (no depth sorting, render-pipeline.md 1.2),
// drawing through the one shared shader program in shaders.cpp.
#include "as3d/scene.h"

#include <GLES3/gl3.h>

#include <algorithm>

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
    if (mat.texture) mat.texture->bind(0);

    item.mesh->vao.bind();
    GLenum indexType = item.mesh->ibo.type() == IndexType::U16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(item.mesh->indexCount), indexType, nullptr);
}

} // namespace as3d
