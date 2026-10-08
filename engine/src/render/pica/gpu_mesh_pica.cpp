// PICA backend of as3d::GpuMesh (as3d/scene.h): the mesh in linear memory as baked
// pica::MeshVertex + u16 indices (gpu_mesh_pica.h).
#include "gpu_mesh_pica.h"

#include <3ds.h>

#include <cstring>
#include <memory>

#include "as3d/ambient_cube.h"
#include "pica_frame.h"

namespace as3d {

namespace {

pica::MeshMemoryStats& memStats() {
    static pica::MeshMemoryStats s;
    return s;
}

} // namespace

namespace pica {
MeshMemoryStats meshMemoryStats() { return memStats(); }
} // namespace pica

GpuMeshBuffers::~GpuMeshBuffers() {
    pica::MeshMemoryStats& s = memStats();
    if (vertices || indices) {
        s.bytes -= bytes < s.bytes ? bytes : s.bytes;
        if (s.meshes > 0) --s.meshes;
    }
    if (vertices) pica::deferLinearFree(vertices);
    if (indices) pica::deferLinearFree(indices);
}

GpuMesh::GpuMesh() = default;
GpuMesh::~GpuMesh() = default;
GpuMesh::GpuMesh(GpuMesh&&) noexcept = default;
GpuMesh& GpuMesh::operator=(GpuMesh&&) noexcept = default;

bool GpuMesh::upload(const RenderVertex* vertices, size_t vertexCount, const u16* indices, size_t count) {
    buffers_.reset();
    indexCount = 0;
    valid = false;
    if (!vertices || !indices || vertexCount == 0 || count == 0) return false;
    pica::MeshMemoryStats& s = memStats();
    if (vertexCount > 65535) {
        // buildRenderMesh never returns this many (u16 indices); a caller that does would need
        // the list split into parts.
        AS3D_WARN("GpuMesh: %u vertices exceed the u16 index range, mesh not uploaded",
                  static_cast<unsigned>(vertexCount));
        ++s.failures;
        return false;
    }
    std::vector<AmbientCubeWeightsU8> weights;
    bakeAmbientCubeWeights(vertices, vertexCount, weights);

    const size_t vbytes = vertexCount * sizeof(pica::MeshVertex);
    const size_t ibytes = count * sizeof(u16);
    auto b = std::make_unique<GpuMeshBuffers>();
    auto* vbuf = static_cast<pica::MeshVertex*>(linearAlloc(vbytes));
    auto* ibuf = static_cast<u16*>(linearAlloc(ibytes));
    if (!vbuf || !ibuf) {
        AS3D_WARN("GpuMesh: no linear memory for %u bytes", static_cast<unsigned>(vbytes + ibytes));
        // Never drawn: freed at once.
        if (vbuf) linearFree(vbuf);
        if (ibuf) linearFree(ibuf);
        ++s.failures;
        return false;
    }
    for (size_t i = 0; i < vertexCount; ++i) {
        pica::MeshVertex& v = vbuf[i];
        const RenderVertex& r = vertices[i];
        v.pos[0] = r.position.x;
        v.pos[1] = r.position.y;
        v.pos[2] = r.position.z;
        std::memcpy(v.wPos, weights[i].pos, 4);
        std::memcpy(v.wNeg, weights[i].neg, 4);
        v.uv[0] = r.uv.x;
        v.uv[1] = r.uv.y;
    }
    std::memcpy(ibuf, indices, ibytes);
    GSPGPU_FlushDataCache(vbuf, static_cast<u32>(vbytes));
    GSPGPU_FlushDataCache(ibuf, static_cast<u32>(ibytes));
    b->vertices = vbuf;
    b->indices = ibuf;
    b->vertexCount = static_cast<u32>(vertexCount);
    b->indexCount = static_cast<u32>(count);
    b->bytes = vbytes + ibytes;
    b->surfaces.push_back(GpuMeshBuffers::Surface{0, static_cast<u32>(count)});

    s.bytes += b->bytes;
    ++s.meshes;
    if (s.bytes > s.peakBytes) s.peakBytes = s.bytes;
    buffers_ = std::move(b);
    indexCount = count;
    valid = true;
    return true;
}

} // namespace as3d
