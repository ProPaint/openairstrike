// Implements as3d::MeshRenderer (as3d/scene.h): begin()/submit()/end() with the
// SORT_OPAQUE, SORT_TRANS (back-to-front), SORT_EFFECT ordering from docs/spec/obj.md's
// "sort" statement, drawing through the one shared shader program in shaders.cpp.
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
    // SORT_OPAQUE (0) < SORT_TRANS (2) < SORT_EFFECT (3) in bucket order; within a
    // bucket, Trans/Effect draw back-to-front by distance to the camera (the usual
    // fix for correct blending of overlapping translucent geometry), Opaque keeps
    // submission order (depth testing makes ordering within it irrelevant to
    // correctness).
    std::stable_sort(items_.begin(), items_.end(), [](const DrawItem& a, const DrawItem& b) {
        int ba = static_cast<int>(a.material->sortBucket);
        int bb = static_cast<int>(b.material->sortBucket);
        if (ba != bb) return ba < bb;
        if (a.material->sortBucket == SortBucket::Opaque) return false;
        return a.distanceToCamera > b.distanceToCamera; // farther first
    });

    if (!initialized_) return; // caller forgot init(); fail safe rather than crash.
    program_.use();
    program_.setMat4("uViewProj", camera_.viewProjMatrix());
    program_.setVec3("uSunDir", lighting_.sunDirection);
    program_.setVec3("uSunColor", lighting_.sunColor);
    program_.setVec3("uAmbientColor", lighting_.ambientColor);
    program_.setVec3("uFogColor", lighting_.fogColor);
    program_.setFloat("uFogStart", lighting_.fogStart);
    program_.setFloat("uFogEnd", lighting_.fogEnd);
    program_.setVec3("uCameraPos", camera_.eye);
    program_.setInt("uTex", 0);

    for (const DrawItem& item : items_) draw(item);
}

void MeshRenderer::draw(const DrawItem& item) {
    const Material& mat = *item.material;
    applyMaterialState(mat);

    Mat3 normalMat = transpose(inverse(mat3FromMat4(item.model)));
    program_.setMat4("uModel", item.model);
    program_.setMat3("uNormalMat", normalMat);
    program_.setVec4("uColor", item.colour);
    program_.setInt("uNoLighting", mat.noLighting ? 1 : 0);
    program_.setInt("uAlphaTest", mat.alphaTest ? 1 : 0);
    if (mat.texture) mat.texture->bind(0);

    item.mesh->vao.bind();
    GLenum indexType = item.mesh->ibo.type() == IndexType::U16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(item.mesh->indexCount), indexType, nullptr);
}

} // namespace as3d
