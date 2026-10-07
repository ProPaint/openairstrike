// Fixed-function style render state helpers (no hidden state beyond what the
// graphics API itself tracks). GLES implementation: engine/src/render/gles/render_state_gl.cpp.
#pragma once

#include "as3d/math.h"

namespace as3d {

enum class GlBlend { Off, AlphaBlend, Additive }; // GL blend state (defs.h BlendMode is the obj.md field)
enum class CullMode { Off, Back, Front };

void setBlend(GlBlend mode);
void setDepth(bool test, bool write);
void setCull(CullMode mode);
void setViewport(int x, int y, int w, int h);
// Enables or disables the scissor test; the rectangle is only applied when enabled.
void setScissor(bool enabled, int x = 0, int y = 0, int w = 0, int h = 0);
void clear(const Vec4& color, bool depth = true, float depthValue = 1.0f);

} // namespace as3d
