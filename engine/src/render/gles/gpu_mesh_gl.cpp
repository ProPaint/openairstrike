// GL backend of as3d::GpuMesh (as3d/scene.h): lifetime and upload of the mesh's GL buffers.
#include "gpu_mesh_gl.h"

#include <cstddef>

namespace as3d {

GpuMesh::GpuMesh() = default;
GpuMesh::~GpuMesh() = default;
GpuMesh::GpuMesh(GpuMesh&&) noexcept = default;
GpuMesh& GpuMesh::operator=(GpuMesh&&) noexcept = default;

bool GpuMesh::upload(const RenderVertex* vertices, size_t vertexCount, const u16* indices, size_t count) {
    buffers_ = std::make_unique<GpuMeshBuffers>();
    buffers_->vbo.upload(vertices, vertexCount * sizeof(RenderVertex));
    buffers_->ibo.upload(indices, count * sizeof(u16), IndexType::U16);
    VertexLayout layout;
    layout.strideBytes = sizeof(RenderVertex);
    layout.attribs = {
        {0, 3, offsetof(RenderVertex, position), false},
        {1, 3, offsetof(RenderVertex, normal), false},
        {2, 2, offsetof(RenderVertex, uv), false},
    };
    buffers_->vao.create(buffers_->vbo, layout, &buffers_->ibo);
    indexCount = count;
    valid = count > 0;
    return valid;
}

} // namespace as3d
