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
    // Stereo at 60 (docs/3ds-port.md "Stereo at 60 (round 2, P3)"). The terrain's far chunks drawn
    // from a coarse grid (every second vertex, terrain_renderer_pica.cpp): Off, only while two eyes
    // are drawn (frameEyes() == 2), or always; beyond terrainLodDistance world units from the
    // central camera.
    enum class TerrainLod { Off, Stereo, Always };
    TerrainLod terrainLod = TerrainLod::Stereo;
    float terrainLodDistance = 520.0f;
    // Hysteresis (docs/3ds-port.md "Stereo comfort and frame polish (round 2, P5)"): a fine chunk
    // turns coarse beyond terrainLodDistance + terrainLodBand, a coarse one fine again under
    // terrainLodDistance - terrainLodBand, so a chunk near the distance never flickers.
    float terrainLodBand = 40.0f;
    // The silhouettes packed into A8 atlas pages, one draw per page (shadow_renderer_pica.cpp).
    bool shadowAtlas = true;
    // The coarse far chunks without the detail texture: its mean colour as a TEV constant, texture
    // unit 1 off (docs/3ds-port.md "Stereo GPU budget (round 4, R4.1)", run.ini terrain_far_tev).
    bool terrainFarTev = false;
};

void setRenderQuality(const RenderQuality& q);
const RenderQuality& renderQuality();

// The number of eyes the frame being prepared draws (the frame composer sets it before the
// world's prepare; 1 by default). Read by the stereo-only quality knobs.
void setFrameEyes(int eyes);
int frameEyes();
// True when the terrain's far chunks are coarse this frame (terrainLod and frameEyes()).
bool terrainLodActive();

} // namespace as3d::pica
