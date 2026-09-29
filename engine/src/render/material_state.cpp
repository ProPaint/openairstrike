// Implements as3d::applyMaterialState (as3d/scene.h): the single function + table that
// maps a Material's flags to GL blend/depth/cull state. See docs/spec/obj.md's
// blend/rflag tables and docs/graphics.md's "Material state table" section.
#include "as3d/scene.h"

#include <GLES3/gl3.h>

namespace as3d {

namespace {

struct BlendGLState {
    bool enable;
    GLenum src;
    GLenum dst;
};

// One row per MaterialBlend value, in declaration order. This table (plus the two
// setDepth/setCull calls below) is the ENTIRE flag/enum -> GL-state mapping for this
// package; a later render-pipeline spec can correct any single line here without
// touching anything else.
constexpr BlendGLState kBlendTable[] = {
    /* Opaque */ {false, GL_ONE, GL_ZERO},
    /* Alpha  */ {true, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA},
    /* Add    */ {true, GL_SRC_ALPHA, GL_ONE},
    // BLEND_FILTER ("multiplicative": dst *= src). as3d::setBlend (as3d/gfx.h) only
    // offers Off/AlphaBlend/Additive, so this one state is set with raw GL calls
    // instead of going through it -- everything else here does use as3d::setDepth /
    // as3d::setCull.
    /* Filter */ {true, GL_DST_COLOR, GL_ZERO},
};

// Settled empirically for this engine's own from-scratch camera/view pipeline (see
// docs/graphics.md, "Winding and culling", and the WP-30/33 report): buildRenderMesh's
// CCW-in-object-space triangles (docs/spec/mdl.md) stay CCW in window space after this
// engine's model/view/projection transforms, because all of them (model TRS, the
// Z-up-aware lookAt() view, and perspective()) are standard right-handed, non-mirroring
// transforms. That matches as3d::setCull's hardcoded glFrontFace(GL_CCW)
// (engine/src/render/gfx.cpp), so CullMode::Back ("cull back faces") is directly usable
// with buildRenderMesh's own winding, no reversal needed. Named here, once, so a future
// camera-convention change has exactly one place to flip.
constexpr CullMode kDefaultCullMode = CullMode::Back;

int gCullOverride = -1;

} // namespace

void setCullDebugOverride(int mode) { gCullOverride = mode; }

void applyMaterialState(const Material& material) {
    const BlendGLState& b = kBlendTable[static_cast<int>(material.blend)];
    if (b.enable) {
        glEnable(GL_BLEND);
        glBlendFunc(b.src, b.dst);
    } else {
        glDisable(GL_BLEND);
    }
    setDepth(!material.noDepthTest, !material.noDepthWrite);
    if (gCullOverride >= 0) {
        setCull(gCullOverride == 0 ? CullMode::Off : gCullOverride == 1 ? CullMode::Back : CullMode::Front);
    } else {
        setCull(material.noCulling ? CullMode::Off : kDefaultCullMode);
    }
}

} // namespace as3d
