// Implements the neutral half of as3d/material_key.h: the model Material -> MaterialKey
// mapping (the former GL kBlendTable) and the cull debug override (as3d/scene.h).
#include "as3d/material_key.h"

#include "as3d/scene.h"

namespace as3d {

namespace {

// One row per MaterialBlend value, in declaration order. This table (plus the depth and
// cull lines in materialKeyFor) is the ENTIRE flag/enum -> state mapping for models; a later
// render-pipeline spec can correct any single line here without touching anything else.
constexpr RenderBlend kBlendTable[] = {
    /* Opaque */ RenderBlend::None,
    /* Alpha  */ RenderBlend::Alpha,
    /* Add    */ RenderBlend::Add,    // render-pipeline.md 4.1: (ONE, ONE)
    /* Filter */ RenderBlend::Filter, // BLEND_FILTER ("multiplicative": dst *= src)
};

// Settled empirically for this engine's own from-scratch camera/view pipeline (see
// docs/graphics.md, "Winding and culling", and the WP-30/33 report): buildRenderMesh's
// CCW-in-object-space triangles (docs/spec/mdl.md) stay CCW in window space after this
// engine's model/view/projection transforms, because all of them (model TRS, the
// Z-up-aware lookAt() view, and perspective()) are standard right-handed, non-mirroring
// transforms. That matches as3d::setCull's hardcoded glFrontFace(GL_CCW)
// (engine/src/render/gles/render_state_gl.cpp), so CullMode::Back ("cull back faces") is
// directly usable with buildRenderMesh's own winding, no reversal needed. Named here, once,
// so a future camera-convention change has exactly one place to flip.
constexpr CullMode kDefaultCullMode = CullMode::Back;

int gCullOverride = -1;

} // namespace

void setCullDebugOverride(int mode) { gCullOverride = mode; }

FogColorMode fogColorModeFor(RenderBlend blend) {
    switch (blend) {
        case RenderBlend::Add: return FogColorMode::Black;
        case RenderBlend::Filter:
        case RenderBlend::Brightness: return FogColorMode::White;
        default: return FogColorMode::Level;
    }
}

Vec3 fogColorFor(FogColorMode mode, const Vec3& levelFog) {
    switch (mode) {
        case FogColorMode::Black: return Vec3{0.0f, 0.0f, 0.0f};
        case FogColorMode::White: return Vec3{1.0f, 1.0f, 1.0f};
        default: return levelFog;
    }
}

MaterialKey materialKeyFor(const Material& material) {
    MaterialKey key;
    key.blend = kBlendTable[static_cast<int>(material.blend)];
    key.depthTest = !material.noDepthTest;
    key.depthWrite = !material.noDepthWrite;
    if (gCullOverride >= 0) {
        key.cull = gCullOverride == 0 ? CullMode::Off : gCullOverride == 1 ? CullMode::Back : CullMode::Front;
    } else {
        key.cull = material.noCulling ? CullMode::Off : kDefaultCullMode;
    }
    key.polygonOffset = false;
    key.fog = fogColorModeFor(key.blend);
    key.alphaTest = material.alphaTest;
    return key;
}

void applyMaterialState(const Material& material) { applyMaterialState(materialKeyFor(material)); }

} // namespace as3d
