// Decoded images. See docs/spec/tga.md. Owned by the orchestrator.
#pragma once

#include <vector>

#include "as3d/core.h"

namespace as3d {

// Always RGBA8, row 0 is the TOP row, regardless of the source orientation.
struct Image {
    int width = 0;
    int height = 0;
    bool hasAlpha = false; // source carried an alpha channel
    std::vector<u8> rgba;
};

// Returns false and leaves `out` empty on malformed input.
bool decodeTga(const u8* data, size_t size, Image& out);

} // namespace as3d
