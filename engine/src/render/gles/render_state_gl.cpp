// GLES 3.0 implementation of engine/include/as3d/render_state.h.
#include "as3d/render_state.h"

#include <GLES3/gl3.h>

namespace as3d {

// ---------------------------------------------------------------------------
// State helpers
// ---------------------------------------------------------------------------
void setBlend(GlBlend mode) {
    if (mode == GlBlend::Off) {
        glDisable(GL_BLEND);
        return;
    }
    glEnable(GL_BLEND);
    if (mode == GlBlend::AlphaBlend) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else { // Additive
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    }
}

void setDepth(bool test, bool write) {
    if (test) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(write ? GL_TRUE : GL_FALSE);
}

void setCull(CullMode mode) {
    if (mode == CullMode::Off) {
        glDisable(GL_CULL_FACE);
        return;
    }
    glEnable(GL_CULL_FACE);
    glCullFace(mode == CullMode::Back ? GL_BACK : GL_FRONT);
    glFrontFace(GL_CCW);
}

void setViewport(int x, int y, int w, int h) { glViewport(x, y, w, h); }

void setScissor(bool enabled, int x, int y, int w, int h) {
    if (!enabled) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, w, h);
}

void clear(const Vec4& color, bool depth, float depthValue) {
    glClearColor(color.x, color.y, color.z, color.w);
    GLbitfield mask = GL_COLOR_BUFFER_BIT;
    if (depth) {
        glClearDepthf(depthValue);
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    glClear(mask);
}

} // namespace as3d
