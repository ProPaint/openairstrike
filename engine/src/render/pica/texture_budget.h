// TextureBudget: where the PICA backend puts a texture (VRAM or linear memory) and how many
// bytes each holds. Platform neutral, header only (desktop-tested in apps/tests/pica_core_test.cpp);
// the 3DS Texture2D (gfx_pica.cpp) owns the one instance, textureBudget().
//
// Policy (docs/3ds-port.md, M1.0: VRAM textures cost 2.65 ms of GPU per frame against 12.3 ms
// from linear memory): VRAM first while the textures stay within vramLimit (default 4.2 MB, what
// the 6 MB of VRAM leave after the render targets: two top eyes 400x240 RGBA8 + D24S8 and the
// bottom screen), linear memory beyond. TextureOptions::Residency::Vram / Linear force a
// placement (Vram still falls back to linear when the VRAM allocator itself fails).
#pragma once

#include <cstddef>

#include "as3d/texture.h"

namespace as3d::pica {

enum class Placement { Vram, Linear };

struct TextureStats {
    size_t vramBytes = 0;   // bytes of the live VRAM textures (mip chains included)
    size_t linearBytes = 0; // bytes of the live linear-memory textures
    int vramCount = 0;
    int linearCount = 0;
    size_t vramLimit = 0;
    int vramFallbacks = 0;  // textures the budget sent to VRAM but whose vramAlloc failed
    int failures = 0;       // textures not created at all (no memory, bad size)
};

class TextureBudget {
public:
    static constexpr size_t kDefaultVramLimit = 4404019; // 4.2 MiB

    explicit TextureBudget(size_t vramLimit = kDefaultVramLimit) { stats_.vramLimit = vramLimit; }

    void setVramLimit(size_t bytes) { stats_.vramLimit = bytes; }
    size_t vramLimit() const { return stats_.vramLimit; }

    // Where a texture of `bytes` should go.
    Placement choose(size_t bytes, TextureOptions::Residency res) const {
        if (res == TextureOptions::Residency::Vram) return Placement::Vram;
        if (res == TextureOptions::Residency::Linear) return Placement::Linear;
        return stats_.vramBytes + bytes <= stats_.vramLimit ? Placement::Vram : Placement::Linear;
    }
    void add(Placement p, size_t bytes) {
        if (p == Placement::Vram) {
            stats_.vramBytes += bytes;
            ++stats_.vramCount;
        } else {
            stats_.linearBytes += bytes;
            ++stats_.linearCount;
        }
    }
    void remove(Placement p, size_t bytes) {
        if (p == Placement::Vram) {
            stats_.vramBytes -= bytes < stats_.vramBytes ? bytes : stats_.vramBytes;
            if (stats_.vramCount > 0) --stats_.vramCount;
        } else {
            stats_.linearBytes -= bytes < stats_.linearBytes ? bytes : stats_.linearBytes;
            if (stats_.linearCount > 0) --stats_.linearCount;
        }
    }
    void noteVramFallback() { ++stats_.vramFallbacks; }
    void noteFailure() { ++stats_.failures; }
    const TextureStats& stats() const { return stats_; }

private:
    TextureStats stats_;
};

#if defined(__3DS__)
// The budget of every PICA Texture2D (gfx_pica.cpp).
TextureBudget& textureBudget();
// A copy of its counters (perf.csv: vram/linear texture bytes).
TextureStats textureStats();
// What a PICA Texture2D became (tests): placement, GPU size, format, mip levels. False when invalid.
bool textureInfo(const Texture2D& t, Placement* placement, int* texWidth, int* texHeight, PicaFormat* format,
                 int* levels);
#endif

} // namespace as3d::pica
