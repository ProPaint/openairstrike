// GL body of as3d::GpuMeshBuffers (as3d/scene.h): the three GL objects of one uploaded mesh.
// Private to the GL backend (mesh renderer, GpuMesh::upload).
#pragma once

#include "as3d/gfx.h"
#include "as3d/scene.h"

namespace as3d {

struct GpuMeshBuffers {
    VertexBuffer vbo;
    IndexBuffer ibo;
    VertexArray vao;
};

} // namespace as3d
