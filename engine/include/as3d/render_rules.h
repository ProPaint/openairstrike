// What the renderer draws differently from one game of the family to the next.
//
// The simulation's differences live in as3d/game_profile.h (GameRules, orchestrator-owned);
// these are the renderer's own switches. Each field names the section of
// docs/spec/as2/render-pipeline.delta.md it comes from. The first game's values are the
// renderer's behaviour before the sequels existed, so AirStrike 3D draws exactly as it did;
// AirStrike 2 and Gulf Thunder share the as2 values (docs/spec/README.md: a `gulf` delta would
// win over the `as2` one; there is no gulf render delta).
//
// Plain data, no GL.
#pragma once

#include "as3d/game_profile.h"

namespace as3d {

enum class WaterStyle : u8 {
    // One textured plane per chunk at the level, bobbing by sin t, lit by the sun
    // (hmap.md "Water", render-pipeline.md pass 7).
    FlatPlane,
    // A grid on the terrain vertices of the wet cells: depth-faded alpha, vertex waves, two
    // scrolled layers, the base over the shine by the base texture's alpha, unlit (delta 12.5,
    // render-corrections.md C1).
    WaveGrid,
};

struct RenderRules {
    // Delta 12.5: water surface.
    WaterStyle water = WaterStyle::FlatPlane;

    // Delta 12.2: vertex normals by central differences (as2) instead of the acos average of
    // the face normals the terrain builds (as3d); the static colours are recomputed from
    // them with `min(255, (sun·d + ambient)·f·255)`, d = clamp(N·L, 0, 1).
    bool terrainCentralNormals = false;
    // Delta 12.4: each tile's texture rectangle inset by half a texel on every side.
    bool tileHalfTexelInset = false;

    // Delta 5.2 step 3: the white quad at z = 0 that erases the silhouette at and below the
    // ground (base step 4) is gone.
    bool shadowEraseBelowGround = true;
    // Delta 5.2 step 1: exponent cap of the shadow texture height (7: 128 texels, 8: 256), and
    // after LOW/HIGH each side halved until it is at most shadowMaxSide (0: no such halving,
    // the base, whose HIGH shadows reach 512 texels).
    int shadowHeightExpCap = 7;
    int shadowMaxSide = 0;
    // Delta 4.4: shadow textures without mipmaps.
    bool shadowMipmaps = true;
    // Delta 5.3: shadows multiply the ground (FILTER, dst × grey, fog towards white) instead
    // of blending black by alpha (fog towards the level colour). Same maximum darkening.
    bool shadowMultiply = false;

    // Delta 3.3: face culling off for the whole sprite pass.
    bool spriteCulling = true;
    // Delta 2.1: billboards (sprites, particles) use the true camera axes from the view
    // matrix. Our renderer always did (the base's roll mirroring was never reproduced), so
    // both games say true; kept to name the rule.
    bool billboardTrueAxes = true;

    // Delta 4.3: environment coordinates from the view-space normal,
    // (0.5 + 0.5·n.x, 0.5 + 0.5·n.y) with n = view × world × |normal|, instead of the GL
    // sphere map; ENV_GLITTER alpha no longer multiplied by the environment texel's alpha.
    bool envViewNormal = false;

    // Delta 3.2, 9.1: an object whose texture is missing is drawn untextured (white × vertex
    // colour) instead of with the magenta placeholder of our first-game renderer.
    bool missingTextureWhite = false;

    // Delta 3.4: marks carry the entity alpha and use their texture coordinate unflipped
    // (mirrored along world y against the first game).
    bool markEntityAlpha = false;
    bool markTextureUnflipped = false;

    // Delta 7.7: the skid-mark pass (pass 5) between the ground marks and the shadows.
    bool skidMarkPass = false;

    // Delta 8.2: 2D quads shifted by -0.5 virtual pixel in x and y.
    bool ui2dHalfPixelShift = false;
};

const RenderRules& renderRules(GameId id);
// The rules of the game whose profile holds `rules` (World::rules() is one of them); the first
// game's for any other GameRules object (tests that build their own).
const RenderRules& renderRulesFor(const GameRules& rules);

} // namespace as3d
