// The PICA backend's frame objects (pica_frame.h): arena, shader set, current target, depth
// mapping.
#include "pica_frame.h"

#include <3ds.h>

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
    bool ready = false;
};

Backend& backend() {
    static Backend b;
    return b;
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
    b.arena.shutdown();
    b.shaders.unload();
    b.ready = false;
}

bool backendReady() { return backend().ready; }

void frameBegin() { backend().arena.beginFrame(); }

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
    b.depthMap = map;
    b.depthNear = nearPlane > 0.0f ? nearPlane : 1.0f;
    applyDepthMap(0.0f);
}

FogDepthMap depthMapping() { return backend().depthMap; }
float depthMappingNear() { return backend().depthNear; }

void applyDepthMap(float offset) {
    const Backend& b = backend();
    if (b.depthMap == FogDepthMap::WBuffer) C3D_DepthMap(false, -1.0f / b.depthNear, offset);
    else C3D_DepthMap(true, -1.0f, offset);
}

} // namespace as3d::pica
