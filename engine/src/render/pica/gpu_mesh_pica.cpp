// PICA backend of as3d::GpuMesh (as3d/scene.h): the mesh in linear memory as baked
// pica::MeshVertex + u16 indices (gpu_mesh_pica.h).
#include "gpu_mesh_pica.h"

#include <3ds.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <unordered_map>

#include "as3d/ambient_cube.h"
#include "as3d/core.h"
#include "as3d/envmap.h"
#include "as3d/load_stats.h"
#include "pass_profile.h"
#include "pica_frame.h"

namespace as3d {

namespace {

pica::MeshMemoryStats& memStats() {
    static pica::MeshMemoryStats s;
    return s;
}

// One PICA vertex as the GPU reads it: the MeshVertex bytes and, for env meshes, the normal.
struct PicaVertexKey {
    uint8_t b[sizeof(pica::MeshVertex) + sizeof(pica::EnvNormal)];
    bool operator==(const PicaVertexKey& o) const { return std::memcmp(b, o.b, sizeof b) == 0; }
};
struct PicaVertexKeyHash {
    size_t operator()(const PicaVertexKey& k) const {
        uint32_t h = 2166136261u;
        for (uint8_t c : k.b) h = (h ^ c) * 16777619u;
        return h;
    }
};

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
    if (envNormals) {
        s.envBytes -= envBytes < s.envBytes ? envBytes : s.envBytes;
        if (s.envMeshes > 0) --s.envMeshes;
        pica::deferLinearFree(envNormals);
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
    const u64 tLoad = svcGetSystemTick();
    std::vector<AmbientCubeWeightsU8> weights;
    bakeAmbientCubeWeights(vertices, vertexCount, weights);

    // The PICA vertices (what the GPU reads: position, the u8 cube weights, uv and, for env
    // meshes, the folded u8 normal). buildRenderMesh keeps corners apart by their file indices
    // (a faceted mesh has three per face); corners whose PICA vertex is byte-identical are merged
    // here, and triangles with two corners on one position (no area, no pixel) are dropped, so the
    // picture is the same with fewer vertices and indices (docs/3ds-port.md "Stereo at 60").
    std::vector<pica::MeshVertex> pv(vertexCount);
    std::vector<pica::EnvNormal> pn(model.smoothNormals ? vertexCount : 0);
    for (size_t i = 0; i < vertexCount; ++i) {
        pica::MeshVertex& v = pv[i];
        const RenderVertex& r = vertices[i];
        v.pos[0] = r.position.x;
        v.pos[1] = r.position.y;
        v.pos[2] = r.position.z;
        std::memcpy(v.wPos, weights[i].pos, 4);
        std::memcpy(v.wNeg, weights[i].neg, 4);
        v.uv[0] = r.uv.x;
        v.uv[1] = r.uv.y;
    }
    // The GL env coordinates see only the folded normal (as3d/envmap.h foldEnvNormal).
    for (size_t i = 0; i < pn.size(); ++i) {
        const Vec3 n = foldEnvNormal(vertices[i].normal);
        pn[i].n[0] = static_cast<u8>(std::min(n.x, 1.0f) * 255.0f + 0.5f);
        pn[i].n[1] = static_cast<u8>(std::min(n.y, 1.0f) * 255.0f + 0.5f);
        pn[i].n[2] = static_cast<u8>(std::min(n.z, 1.0f) * 255.0f + 0.5f);
        pn[i].n[3] = 0;
    }
    std::vector<u16> remap(vertexCount);
    std::vector<pica::MeshVertex> uv;
    std::vector<pica::EnvNormal> un;
    uv.reserve(vertexCount);
    if (!pn.empty()) un.reserve(vertexCount);
    {
        std::unordered_map<PicaVertexKey, u16, PicaVertexKeyHash> seen;
        seen.reserve(vertexCount * 2);
        for (size_t i = 0; i < vertexCount; ++i) {
            PicaVertexKey k;
            std::memset(k.b, 0, sizeof k.b);
            std::memcpy(k.b, &pv[i], sizeof(pica::MeshVertex));
            if (!pn.empty()) std::memcpy(k.b + sizeof(pica::MeshVertex), &pn[i], sizeof(pica::EnvNormal));
            auto ins = seen.emplace(k, static_cast<u16>(uv.size()));
            if (ins.second) {
                uv.push_back(pv[i]);
                if (!pn.empty()) un.push_back(pn[i]);
            }
            remap[i] = ins.first->second;
        }
    }
    std::vector<u16> ix;
    ix.reserve(count);
    {
        auto samePos = [&](u16 a, u16 b) {
            return a == b || std::memcmp(uv[a].pos, uv[b].pos, sizeof uv[a].pos) == 0;
        };
        for (size_t t = 0; t + 2 < count; t += 3) {
            const u16 a = indices[t] < vertexCount ? remap[indices[t]] : 0;
            const u16 b = indices[t + 1] < vertexCount ? remap[indices[t + 1]] : 0;
            const u16 c = indices[t + 2] < vertexCount ? remap[indices[t + 2]] : 0;
            if (samePos(a, b) || samePos(b, c) || samePos(a, c)) continue;
            ix.push_back(a);
            ix.push_back(b);
            ix.push_back(c);
        }
    }
    {
        pica::MeshMemoryStats& ms = memStats();
        ms.sourceVertices += vertexCount;
        ms.mergedVertices += uv.size();
        ms.sourceIndices += count;
        ms.keptIndices += ix.size();
    }
    if (ix.empty()) { // every triangle degenerate: nothing to draw (keep one so the mesh stays valid)
        ix.assign(indices, indices + std::min<size_t>(count, 3));
        for (u16& i : ix) i = i < vertexCount ? remap[i] : 0;
    }
    vertexCount = uv.size();
    count = ix.size();

    const size_t vbytes = vertexCount * sizeof(pica::MeshVertex);
    const size_t ibytes = count * sizeof(u16);
    const size_t nbytes = model.smoothNormals ? vertexCount * sizeof(pica::EnvNormal) : 0;
    auto b = std::make_unique<GpuMeshBuffers>();
    auto* vbuf = static_cast<pica::MeshVertex*>(linearAlloc(vbytes));
    auto* ibuf = static_cast<u16*>(linearAlloc(ibytes));
    auto* nbuf = nbytes ? static_cast<pica::EnvNormal*>(linearAlloc(nbytes)) : nullptr;
    if (!vbuf || !ibuf || (nbytes && !nbuf)) {
        AS3D_WARN("GpuMesh: no linear memory for %u bytes", static_cast<unsigned>(vbytes + ibytes + nbytes));
        // Never drawn: freed at once.
        if (vbuf) linearFree(vbuf);
        if (ibuf) linearFree(ibuf);
        if (nbuf) linearFree(nbuf);
        ++s.failures;
        return false;
    }
    std::memcpy(vbuf, uv.data(), vbytes);
    std::memcpy(ibuf, ix.data(), ibytes);
    if (nbuf) {
        std::memcpy(nbuf, un.data(), nbytes);
        pica::deferFlush(nbuf, nbytes);
    }
    pica::deferFlush(vbuf, vbytes); // inside a frame: flushed by frameEnd (pica_frame.h)
    pica::deferFlush(ibuf, ibytes);
    if (pica::inFrame()) AS3D_INFO("AS3D_LATE_MESH path=%s frame=%ld vertices=%zu", LoadStats::lastPath(), LoadStats::frame(), vertexCount);
    b->vertices = vbuf;
    b->indices = ibuf;
    b->envNormals = nbuf;
    b->vertexCount = static_cast<u32>(vertexCount);
    b->indexCount = static_cast<u32>(count);
    b->bytes = vbytes + ibytes + nbytes;
    b->envBytes = nbytes;
    b->surfaces.push_back(GpuMeshBuffers::Surface{0, static_cast<u32>(count)});

    s.bytes += b->bytes;
    ++s.meshes;
    if (nbuf) {
        s.envBytes += nbytes;
        ++s.envMeshes;
    }
    if (s.bytes > s.peakBytes) s.peakBytes = s.bytes;
    pica::LoadProfile& lp = pica::loadProfile();
    lp.meshTicks += svcGetSystemTick() - tLoad;
    ++lp.meshCount;
    buffers_ = std::move(b);
    indexCount = count;
    valid = true;
    return true;
}

} // namespace as3d
