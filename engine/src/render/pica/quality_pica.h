// The PICA renderer's quality knobs (3DS M4.1). A process-wide setting the app chooses before the
// level starts (apps/3ds/source/quality_profile.h: Old 3DS and New 3DS presets); the defaults are
// the New 3DS values, so every other user (the tests, the standalone probes) is unchanged.
// Set it before WorldRenderer::beginLevel: the terrain detail texture is only loaded when
// terrainDetail is on, and the shadow silhouettes are only generated when blobShadowsOnly is off.
#pragma once

#include "fx_pica.h"

namespace as3d::pica {

struct RenderQuality {
    int particleCap = kMaxParticleQuadsPica; // particle quads per frame
    int spriteCap = kMaxSpriteQuadsPica;     // sprite quads per frame
    bool blobShadowsOnly = false;            // no silhouettes generated: every shadow is the blob
    bool terrainDetail = true;               // the terrain's second TEV stage and detail texture
};

void setRenderQuality(const RenderQuality& q);
const RenderQuality& renderQuality();

} // namespace as3d::pica
