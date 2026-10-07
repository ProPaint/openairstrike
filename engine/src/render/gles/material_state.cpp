// GL backend of as3d::applyMaterialState (as3d/material_key.h): one MaterialKey -> GL blend,
// depth, cull and polygon offset state. The Material -> MaterialKey mapping (the flag table)
// is neutral, in engine/src/render/material_key.cpp.
#include <GLES3/gl3.h>

#include "as3d/material_key.h"

namespace as3d {

namespace {

struct BlendGLState {
    bool enable;
    GLenum src;
    GLenum dst;
};

// One row per RenderBlend value, in declaration order.
constexpr BlendGLState kBlendGL[] = {
    /* None       */ {false, GL_ONE, GL_ZERO},
    /* Alpha      */ {true, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA},
    /* Add        */ {true, GL_ONE, GL_ONE},
    /* Filter     */ {true, GL_DST_COLOR, GL_ZERO},
    /* Brightness */ {true, GL_DST_COLOR, GL_SRC_COLOR},
};

} // namespace

void applyMaterialState(const MaterialKey& key) {
    const BlendGLState& b = kBlendGL[static_cast<int>(key.blend)];
    if (b.enable) {
        glEnable(GL_BLEND);
        glBlendFunc(b.src, b.dst);
    } else {
        glDisable(GL_BLEND);
    }
    setDepth(key.depthTest, key.depthWrite);
    setCull(key.cull);
    if (key.polygonOffset) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -1.0f);
    } else {
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
}

} // namespace as3d
