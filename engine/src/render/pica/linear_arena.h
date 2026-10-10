// LinearArena: the per-frame scratch memory of the PICA backend (vertex data the CPU builds
// every frame: particles, sprites, 2D quads, the terrain's dynamic-light stream).
//
// Two halves of linear (GPU-visible) memory, 1.5 MB each by default. Frame N writes into one
// half while the GPU may still read the other half from frame N-1; beginFrame() switches to
// the other half and empties it. That is safe because C3D_FrameBegin waits for citro3d's GX
// queue to drain (every command list of the frames before, the ones kicked mid-frame included)
// before frame N+1 starts writing over frame N-1's half (pica_frame.h: the fence that checks it;
// docs/3ds-port.md "Frame synchronisation").
// flush() writes the CPU data cache back for the used range of the current half, once per
// frame, before C3D_FrameEnd hands the command list to the GPU.
//
// Platform neutral: the memory comes from an ArenaMemory (linearAlloc/linearFree/
// GSPGPU_FlushDataCache on the 3DS, see pica_frame.cpp; a stub in apps/tests/pica_core_test.cpp).
#pragma once

#include <cstddef>
#include <cstdint>

namespace as3d::pica {

struct ArenaMemory {
    void* (*alloc)(size_t bytes, size_t align) = nullptr;
    void (*release)(void* p) = nullptr;
    void (*flush)(const void* p, size_t bytes) = nullptr; // may be null (nothing to flush)
};

class LinearArena {
public:
    static constexpr size_t kDefaultHalfBytes = 1536 * 1024;
    static constexpr size_t kBaseAlign = 128; // alignment of each half; alloc() accepts align <= this

    LinearArena() = default;
    ~LinearArena() { shutdown(); }
    LinearArena(const LinearArena&) = delete;
    LinearArena& operator=(const LinearArena&) = delete;

    // Allocates both halves (one block of 2 * halfBytes, halfBytes rounded up to kBaseAlign).
    // Returns false when the memory is not available. A second init() shuts down first.
    bool init(size_t halfBytes, const ArenaMemory& memory);
    void shutdown();
    bool valid() const { return base_ != nullptr; }

    // Switches to the other half and empties it. Call once per frame before the first alloc().
    void beginFrame();
    // `bytes` from the current half, aligned to `align` (a power of two <= kBaseAlign; 0 or 1
    // mean no alignment). Returns nullptr (and counts a failure) when the half is full; the
    // caller skips that draw.
    void* alloc(size_t bytes, size_t align = 16);
    template <class T> T* allocArray(size_t count) {
        return static_cast<T*>(alloc(sizeof(T) * count, alignof(T) < 4 ? 4 : alignof(T)));
    }
    // One cache flush of [start of the current half, start + used()).
    void flush();
    // A cache flush of what was allocated since the last flushPending() (or beginFrame()): the
    // vertices of the draws recorded so far, before their command list goes to the GPU in the
    // middle of a frame (pica_frame.h kickGpu, the clears' command list splits).
    void flushPending();
    size_t flushed() const { return flushed_; } // bytes of the current half flushed by flushPending

    size_t capacity() const { return half_; }       // bytes per half
    size_t used() const { return offset_; }         // bytes used in the current half
    size_t highWater() const { return highWater_; } // most bytes one frame used since init/resetStats
    unsigned failures() const { return failures_; } // allocations refused since init/resetStats
    int currentHalf() const { return current_; }    // 0 or 1
    unsigned frames() const { return frames_; }     // beginFrame() calls since init
    void resetStats() {
        highWater_ = offset_;
        failures_ = 0;
    }
    // The start of the current half (allocations are offsets from it).
    const uint8_t* halfBase() const { return base_ ? base_ + static_cast<size_t>(current_) * half_ : nullptr; }

private:
    ArenaMemory mem_;
    uint8_t* base_ = nullptr;
    size_t half_ = 0;
    size_t offset_ = 0;
    size_t flushed_ = 0;
    size_t highWater_ = 0;
    unsigned failures_ = 0;
    unsigned frames_ = 0;
    int current_ = 0;
};

} // namespace as3d::pica
