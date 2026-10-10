// The PICA backend's frame objects (pica_frame.h): arena, shader set, current target, depth
// mapping.
#include "pica_frame.h"

#include <3ds.h>

#include <citro3d.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "as3d/core.h"
#include "state_check_pica.h"

// citro3d's context (internal.h: C3D_Context __C3D_Context). Its first member is the GX command
// queue the frames go through (gxCmdQueue_s gxQueue); only that member is used here (kickGpu),
// so the declaration names the queue's type. citro3d version of the pinned devkitARM image.
extern "C" gxCmdQueue_s __C3D_Context;

namespace as3d::pica {
namespace {

void* linearAllocAligned(size_t bytes, size_t align) { return linearMemAlign(bytes, align); }
void linearRelease(void* p) { linearFree(p); }
void linearFlush(const void* p, size_t bytes) { GSPGPU_FlushDataCache(p, static_cast<u32>(bytes)); }
bool addrIsVram(const void* p) {
    const u32 a = reinterpret_cast<u32>(p);
    return a >= OS_VRAM_VADDR && a < OS_VRAM_VADDR + OS_VRAM_SIZE;
}

struct Backend {
    LinearArena arena;
    ShaderSet shaders;
    FrameTarget target;
    FogDepthMap depthMap = FogDepthMap::ZBuffer;
    float depthNear = 4.0f;
    float depthOffset = 0.0f; // the offset of the last applyDepthMap
    bool wAsZ = false; // setWBufferAsZBuffer: W-buffer requests get the Z-buffer mapping
    bool ready = false;
    struct Pending {
        void* p = nullptr;      // a linearAlloc block, or
        C3D_Tex* tex = nullptr; // a texture copy (C3D_TexDelete, then delete)
        uint32_t serial = 0;    // the last frame that may read it
    };
    std::vector<Pending> pending; // deferLinearFree / deferTextureDelete, freed after their fence
    // The fence (pica_frame.h): frame serials and the arena halves' last frames.
    uint32_t serial = 0;
    uint32_t completed = 0;
    uint32_t halfSerial[2] = {0, 0};
    bool inFrame = false;
    bool poison = false;
    FenceStats fence;
    std::vector<std::pair<const void*, size_t>> pendingFlush; // deferFlush ranges of this frame
};

Backend& backend() {
    static Backend b;
    return b;
}

// Inside a citro3d frame (C3D_FrameBegin .. C3D_FrameEnd) libctru's command buffer is set.
bool inC3dFrame() {
    u32* buf = nullptr;
    GPUCMD_GetBuffer(&buf, nullptr, nullptr);
    return buf != nullptr;
}

// Outside a citro3d frame an idle GX queue means every list submitted so far completed.
void pollCompletion(Backend& b) {
    if (b.completed < b.serial && !inC3dFrame() && gxCmdQueueWait(&__C3D_Context, 0)) b.completed = b.serial;
}

void releaseOne(Backend& b, Backend::Pending& e) {
    if (e.tex) {
        if (b.poison && e.tex->data && !addrIsVram(e.tex->data)) poisonLinear(e.tex->data, e.tex->size);
        C3D_TexDelete(e.tex);
        delete e.tex;
    } else if (e.p) {
        if (b.poison) poisonLinear(e.p, linearGetSize(e.p));
        linearFree(e.p);
    }
    ++b.fence.freedDeferred;
}

void releaseDone(Backend& b, bool all) {
    size_t keep = 0;
    for (Backend::Pending& e : b.pending) {
        if (all || e.serial <= b.completed) releaseOne(b, e);
        else b.pending[keep++] = e;
    }
    b.pending.resize(keep);
}

// A pseudo-random float in +-3000 per word (finite: no NaN or infinity reaches the GPU).
void fillPoison(uint32_t* w, size_t words, uint32_t seed) {
    uint32_t x = seed | 1u;
    for (size_t i = 0; i < words; ++i) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        const float f = static_cast<float>(static_cast<int32_t>(x % 6001u) - 3000) + 0.5f;
        std::memcpy(&w[i], &f, 4);
    }
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
    detail::c3dInvalidate();
    detail::forgetTextureBindings();
    setDepthMapping(FogDepthMap::ZBuffer, 4.0f);
    disableFog();
    detail::applyDefaultState();
    return true;
}

void shutdownBackend() {
    Backend& b = backend();
    releaseDone(b, true);
    b.arena.shutdown();
    b.shaders.unload();
    b.ready = false;
}

bool backendReady() { return backend().ready; }

void frameBegin() {
    Backend& b = backend();
    // The fence: C3D_FrameBegin waited for the GX queue to drain (docs/3ds-port.md "Frame
    // synchronisation"); check it, and wait here should it ever not have.
    if (!gxCmdQueueWait(&__C3D_Context, 0)) {
        ++b.fence.queueBusyAtBegin;
        if (b.fence.queueBusyAtBegin <= 20)
            AS3D_WARN("AS3D_FENCE frame=%u the GX queue was still running at frameBegin (entries %u, done %u): waiting",
                      static_cast<unsigned>(b.serial + 1), static_cast<unsigned>(__C3D_Context.numEntries),
                      static_cast<unsigned>(__C3D_Context.lastEntry));
        gxCmdQueueWait(&__C3D_Context, -1);
    }
    b.completed = b.serial;
    ++b.serial;
    b.inFrame = true;
    releaseDone(b, false);
    b.arena.beginFrame();
    const int h = b.arena.currentHalf();
    if (b.halfSerial[h] > b.completed) ++b.fence.halfHazards; // cannot happen: the half's frame is serial - 2
    b.halfSerial[h] = b.serial;
    if (b.poison && b.arena.valid()) {
        // The whole half, flushed: a GPU still reading it from an earlier frame draws garbage.
        fillPoison(reinterpret_cast<uint32_t*>(const_cast<uint8_t*>(b.arena.halfBase())), b.arena.capacity() / 4,
                   0x9E3779B9u * b.serial);
        GSPGPU_FlushDataCache(b.arena.halfBase(), static_cast<u32>(b.arena.capacity()));
        ++b.fence.poisonedHalves;
    }
    stateCheckFrameBegin();
    // Each frame issues its fragment state and texture bindings at least once (the shadow copies
    // of render_state_pica.cpp only skip repeats inside a frame and a target).
    detail::c3dInvalidate();
    detail::forgetTextureBindings();
}

void deferLinearFree(void* p) {
    if (!p) return;
    Backend& b = backend();
    if (!b.ready) {
        linearFree(p);
        return;
    }
    Backend::Pending e;
    e.p = p;
    e.serial = b.serial;
    b.pending.push_back(e);
    b.fence.deferredMax = std::max(b.fence.deferredMax, static_cast<unsigned>(b.pending.size()));
}

void deferTextureDelete(const C3D_Tex& tex) {
    Backend& b = backend();
    if (!b.ready) {
        C3D_Tex t = tex;
        C3D_TexDelete(&t);
        return;
    }
    Backend::Pending e;
    e.tex = new C3D_Tex(tex);
    e.serial = b.serial;
    b.pending.push_back(e);
    ++b.fence.texturesDeferred;
    b.fence.deferredMax = std::max(b.fence.deferredMax, static_cast<unsigned>(b.pending.size()));
}

void collectDeferred(bool waitIfTextures) {
    Backend& b = backend();
    if (b.pending.empty() || inC3dFrame()) return;
    if (waitIfTextures) {
        for (const Backend::Pending& e : b.pending)
            if (e.tex) {
                waitGpuIdle();
                break;
            }
    }
    pollCompletion(b);
    releaseDone(b, false);
}

size_t deferredFreeCount() { return backend().pending.size(); }

uint32_t frameSerial() { return backend().serial; }
uint32_t gpuCompletedSerial() { return backend().completed; }

bool gpuDone(uint32_t serial) {
    Backend& b = backend();
    if (serial <= b.completed) return true;
    pollCompletion(b);
    return serial <= b.completed;
}

void waitGpuIdle() {
    Backend& b = backend();
    if (inC3dFrame()) return; // only C3D_FrameBegin may wait inside the frame protocol
    gxCmdQueueWait(&__C3D_Context, -1);
    b.completed = b.serial;
}

bool inFrame() { return backend().inFrame; }

void setArenaPoison(bool on) { backend().poison = on; }
bool arenaPoison() { return backend().poison; }

void poisonLinear(void* p, size_t bytes) {
    if (!p || bytes < 4) return;
    fillPoison(static_cast<uint32_t*>(p), bytes / 4, reinterpret_cast<uint32_t>(p));
    GSPGPU_FlushDataCache(p, static_cast<u32>(bytes));
}

const FenceStats& fenceStats() { return backend().fence; }

void drawOn(const FrameTarget& t) {
    Backend& b = backend();
    b.target = t;
    if (t.target) C3D_FrameDrawOn(t.target);
    detail::c3dInvalidate(); // a new target: every state call is issued again
    detail::forgetTextureBindings();
    detail::resetViewState();
}

const FrameTarget& currentTarget() { return backend().target; }

namespace {
// The late mesh uploads of this frame: their cache flushes, before the GPU can read them.
void flushDeferred(Backend& b) {
    for (const auto& r : b.pendingFlush) GSPGPU_FlushDataCache(r.first, static_cast<u32>(r.second));
    b.pendingFlush.clear();
}
} // namespace

void deferFlush(const void* p, size_t bytes) {
    if (!p || !bytes) return;
    Backend& b = backend();
    if (!b.inFrame) GSPGPU_FlushDataCache(p, static_cast<u32>(bytes));
    else b.pendingFlush.emplace_back(p, bytes);
}

void frameEnd() {
    Backend& b = backend();
    flushDeferred(b);
    b.arena.flushPending();
    stateCheckFrameEnd();
    b.inFrame = false;
}

namespace {
bool g_kick = false;
unsigned g_kicks = 0;
} // namespace

void setGpuKick(bool on) { g_kick = on; }
bool gpuKick() { return g_kick; }
unsigned gpuKicks() { return g_kicks; }

void splitCommandList() {
    flushDeferred(backend());
    backend().arena.flushPending();
    C3D_FrameSplit(GX_CMDLIST_FLUSH);
}

void kickGpu() {
    if (!g_kick) return;
    splitCommandList();
    // citro3d's GX queue is the first member of its context (citro3d internal.h, C3D_Context:
    // gxQueue). C3D_FrameBegin stopped it; running it now starts the command lists queued so
    // far, and the queue then starts every later one as it is added (libctru gxqueue.c). The
    // queue's completion callback does nothing until C3D_FrameEnd sets the swap flags.
    gxCmdQueueRun(&__C3D_Context);
    ++g_kicks;
}

LinearArena& frameArena() { return backend().arena; }

namespace detail {
void* c3dContextAddress() { return &__C3D_Context; }
} // namespace detail
ShaderSet& shaders() { return backend().shaders; }

void setDepthMapping(FogDepthMap map, float nearPlane) {
    Backend& b = backend();
    b.depthMap = (map == FogDepthMap::WBuffer && b.wAsZ) ? FogDepthMap::ZBuffer : map;
    b.depthNear = nearPlane > 0.0f ? nearPlane : 1.0f;
    applyDepthMap(0.0f);
}

void setWBufferAsZBuffer(bool on) { backend().wAsZ = on; }

float appliedDepthOffset() { return backend().depthOffset; }

FogDepthMap depthMapping() { return backend().depthMap; }
float depthMappingNear() { return backend().depthNear; }

void applyDepthMap(float offset) {
    Backend& b = backend();
    b.depthOffset = offset;
    if (b.depthMap == FogDepthMap::WBuffer) detail::c3dDepthMap(false, -1.0f / b.depthNear, offset);
    else detail::c3dDepthMap(true, -1.0f, offset);
}

} // namespace as3d::pica
