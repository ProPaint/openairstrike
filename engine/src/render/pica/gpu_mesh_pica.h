// PICA body of as3d::GpuMeshBuffers (as3d/scene.h): one uploaded model in linear memory.
// Private to the PICA backend (gpu_mesh_pica.cpp, mesh_renderer_pica.cpp, the 3DS test app).
//
// GpuMesh::upload takes the buildRenderMesh triangle list (positions, normals, uv) and bakes
// the six ambient-cube weights of each vertex from its normal (as3d/ambient_cube.h,
// bakeAmbientCubeWeights), so the stream is pica::MeshVertex (position, two u8x4 weight
// triples, uv; 28 bytes) plus u16 indices, both in linear memory and flushed once at upload.
// Smooth-normal meshes (ModelData::smoothNormals: the only ones the GL environment-maps) also keep
// a second stream for env.v.pica (M3.4): pica::EnvNormal, the folded normal |n| * 255 as u8 x4,
// 4 bytes per vertex, in its own linear block (`envNormals`, null for faceted meshes), so the
// MeshVertex stream of every mesh stays 28 bytes. The other meshes keep no normals.
//
// Index range: buildRenderMesh refuses models of more than 65535 distinct corners (u16
// indices), so one mesh is always one vertex buffer; upload() logs and refuses a larger list
// should a caller ever pass one. A .mdl has one texture (the object's skin overrides it), so a
// mesh is one surface; `surfaces` keeps the ranges anyway so the renderer draws ranges.
//
// Lifetime: the destructor hands both blocks to pica::deferLinearFree (pica_frame.h), which
// frees them at the next pica::frameBegin(): C3D_FrameBegin has waited for the GPU to finish
// every command list that may still read them.
#pragma once

#include <cstddef>
#include <vector>

#include "as3d/core.h"
#include "as3d/scene.h"
#include "shader_set.h"

namespace as3d {

struct GpuMeshBuffers {
    struct Surface {
        u32 firstIndex = 0;
        u32 indexCount = 0;
    };

    pica::MeshVertex* vertices = nullptr; // linear memory, vertexCount entries
    u16* indices = nullptr;               // linear memory, indexCount entries
    pica::EnvNormal* envNormals = nullptr; // linear memory, vertexCount entries; smooth-normal meshes only
    u32 vertexCount = 0;
    u32 indexCount = 0;
    size_t bytes = 0; // vertex + index (+ env normal) bytes in linear memory
    size_t envBytes = 0; // of which the env normal stream
    std::vector<Surface> surfaces;

    GpuMeshBuffers() = default;
    ~GpuMeshBuffers();
    GpuMeshBuffers(const GpuMeshBuffers&) = delete;
    GpuMeshBuffers& operator=(const GpuMeshBuffers&) = delete;
};

namespace pica {

// Linear memory held by the live GpuMesh buffers (perf logs, the memory budget of the plan:
// static geometry <= 6 MB).
struct MeshMemoryStats {
    size_t bytes = 0;     // live vertex + index bytes
    size_t peakBytes = 0; // most bytes live at once since start
    int meshes = 0;       // live meshes
    size_t envBytes = 0;  // of `bytes`: the env normal streams (smooth-normal meshes)
    int envMeshes = 0;    // live meshes with an env normal stream
    int failures = 0;     // uploads refused: no linear memory, or more than 65535 vertices
};
MeshMemoryStats meshMemoryStats();

} // namespace pica
} // namespace as3d
