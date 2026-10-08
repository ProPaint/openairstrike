// LinearArena (linear_arena.h). Platform neutral; also compiled into the desktop tests.
#include "linear_arena.h"

namespace as3d::pica {

bool LinearArena::init(size_t halfBytes, const ArenaMemory& memory) {
    shutdown();
    if (!memory.alloc || !memory.release || halfBytes == 0) return false;
    const size_t half = (halfBytes + kBaseAlign - 1) & ~(kBaseAlign - 1);
    void* p = memory.alloc(half * 2, kBaseAlign);
    if (!p) return false;
    mem_ = memory;
    base_ = static_cast<uint8_t*>(p);
    half_ = half;
    offset_ = highWater_ = flushed_ = 0;
    failures_ = frames_ = 0;
    current_ = 0;
    return true;
}

void LinearArena::shutdown() {
    if (base_) mem_.release(base_);
    base_ = nullptr;
    half_ = offset_ = flushed_ = 0;
}

void LinearArena::beginFrame() {
    current_ ^= 1;
    offset_ = 0;
    flushed_ = 0;
    ++frames_;
}

void* LinearArena::alloc(size_t bytes, size_t align) {
    if (align < 1) align = 1;
    if (!base_ || align > kBaseAlign || (align & (align - 1)) != 0) {
        ++failures_;
        return nullptr;
    }
    const size_t start = (offset_ + align - 1) & ~(align - 1);
    if (start > half_ || bytes > half_ - start) {
        ++failures_;
        return nullptr;
    }
    offset_ = start + bytes;
    if (offset_ > highWater_) highWater_ = offset_;
    return base_ + static_cast<size_t>(current_) * half_ + start;
}

void LinearArena::flush() {
    if (base_ && offset_ > 0 && mem_.flush) mem_.flush(halfBase(), offset_);
    flushed_ = offset_;
}

void LinearArena::flushPending() {
    if (base_ && offset_ > flushed_ && mem_.flush) mem_.flush(halfBase() + flushed_, offset_ - flushed_);
    flushed_ = offset_;
}

} // namespace as3d::pica
