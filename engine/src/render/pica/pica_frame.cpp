// The PICA backend's frame objects (pica_frame.h): arena, shader set, current target, depth
// mapping.
#include "pica_frame.h"

#include <3ds.h>

#include <vector>

namespace as3d::pica {
namespace {

void* linearAllocAligned(size_t bytes, size_t align) { return linearMemAlign(bytes, align); }
void linearRelease(void* p) { linearFree(p); }
void linearFlush(const void* p, size_t bytes) { GSPGPU_FlushDataCache(p, static_cast<u32>(bytes)); }

struct Backend {
    LinearArena arena;
    ShaderSet shaders;
    FrameTarget target;
    FogDepthMap depthMap = FogDepthMap::ZBuffer;
    float depthNear = 4.0f;
    bool wAsZ = false; // setWBufferAsZBuffer: W-buffer requests get the Z-buffer mapping
    bool ready = false;
    std::vector<void*> pendingFree; // deferLinearFree blocks, freed at the next frameBegin
};

Backend& backend() {
    static Backend b;
    return b;
}

void releasePending(Backend& b) {
    for (void* p : b.pendingFree) linearFree(p);
    b.pendingFree.clear();
}

} // namespace

bool initBackend(size_t arenaHalfBytes) {
    Backend& b = backend();
    shutdownBackend();
    if (!b.shaders.load()) return false;
    ArenaMemory mem;
    mem.alloc = linearAllocAligned;
    mem.release = linearRelease;
    mem.flush = linearFlush;
    if (!b.arena.init(arenaHalfBytes, mem)) {
        b.shaders.unload();
        return false;
    }
    b.ready = true;
    setDepthMapping(FogDepthMap::ZBuffer, 4.0f);
    disableFog();
    detail::applyDefaultState();
    return true;
}

void shutdownBackend() {
    Backend& b = backend();
    releasePending(b);
    b.arena.shutdown();
    b.shaders.unload();
    b.ready = false;
}

bool backendReady() { return backend().ready; }

void frameBegin() {
    Backend& b = backend();
    releasePending(b); // C3D_FrameBegin waited for the GPU
    b.arena.beginFrame();
}

void deferLinearFree(void* p) {
    if (!p) return;
    Backend& b = backend();
    if (!b.ready) linearFree(p);
    else b.pendingFree.push_back(p);
}

size_t deferredFreeCount() { return backend().pendingFree.size(); }

void drawOn(const FrameTarget& t) {
    Backend& b = backend();
    b.target = t;
    if (t.target) C3D_FrameDrawOn(t.target);
    detail::resetViewState();
}

const FrameTarget& currentTarget() { return backend().target; }

void frameEnd() { backend().arena.flush(); }

LinearArena& frameArena() { return backend().arena; }
ShaderSet& shaders() { return backend().shaders; }

void setDepthMapping(FogDepthMap map, float nearPlane) {
    Backend& b = backend();
    b.depthMap = (map == FogDepthMap::WBuffer && b.wAsZ) ? FogDepthMap::ZBuffer : map;
    b.depthNear = nearPlane > 0.0f ? nearPlane : 1.0f;
    applyDepthMap(0.0f);
}

void setWBufferAsZBuffer(bool on) { backend().wAsZ = on; }

FogDepthMap depthMapping() { return backend().depthMap; }
float depthMappingNear() { return backend().depthNear; }

void applyDepthMap(float offset) {
    const Backend& b = backend();
    if (b.depthMap == FogDepthMap::WBuffer) C3D_DepthMap(false, -1.0f / b.depthNear, offset);
    else C3D_DepthMap(true, -1.0f, offset);
}

} // namespace as3d::pica
