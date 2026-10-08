// Untextured rectangles in screen pixels on the current PICA target (3DS only): the brightness
// pass, the loading screen and the frame composer's placeholders (apps/3ds/source/ctr_frame.cpp).
#pragma once

#include <cstdint>

#include "as3d/material_key.h"

namespace as3d::pica {

// One rectangle (x0, y0)-(x1, y1) in upright pixels of pica::currentTarget() (origin top-left)
// with the constant colour (r, g, b, a) and the state of `key` (blend, depth, cull; fog off).
// The vertices come from the frame arena: call between pica::frameBegin and pica::frameEnd.
void fillRect(float x0, float y0, float x1, float y1, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
              const MaterialKey& key);
// The same with blending off, no depth test, no culling.
void fillRect(float x0, float y0, float x1, float y1, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

} // namespace as3d::pica
