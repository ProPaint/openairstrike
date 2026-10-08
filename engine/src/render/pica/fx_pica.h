// Shared parts of the PICA particle and sprite renderers (particle_renderer_pica.cpp,
// sprite_renderer_pica.cpp): the caps, the per-frame byte accounting and the cut of the built
// lists. The neutral builders (particle_build, sprite_build) produce 9 floats per vertex and
// DrawRuns; the renderers copy them as FlatVertex into the frame arena and draw the runs
// through the flat shader.
//
// Per-eye contract (M1.8). ParticleRenderer::draw / SpriteRenderer::draw keep the public API
// and are called once per eye, between pica::drawOn and the next pica::drawOn:
//   * the quads are built ONCE per frame: the first draw() of an arena frame
//     (frameArena().frames()) builds with the billboard axes of params.view (rows 0 and 1 of
//     its rotation) into the frame arena; every further draw() of the same frame with the same
//     emitter/sprite array, count and billboard axes (the second eye: the eye offset is a
//     translation, so the central view and the eye's view share the axes) reuses that vertex
//     data and only re-issues the runs with the new matrix. The world must not step between the
//     two calls (the WorldRenderer contract already says so).
//   * per eye: the matrix params.projection * params.view (the eye's PICA projection from
//     picaEyeProjection with the CENTRAL view, as the terrain), the depth mapping (W-buffer
//     for the near plane of that projection) and the fog table, the fog colour, the shader
//     bind and one C3D_DrawArrays per run.
//   * textures are never created inside draw(): ParticleRenderer::preload(path) creates them
//     (level-start precache, outside C3D_FrameBegin); a texture first seen inside draw() is
//     drawn as a white 1x1 fallback and counted in FxPicaStats::particleLateTextures. Sprite
//     textures arrive in SpriteInstance::texture from the caller's ResourceCache.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "as3d/draw_run.h"

namespace as3d::pica {

constexpr int kMaxParticleQuadsPica = 1536; // plan: particle cap of the PICA
constexpr int kMaxSpriteQuadsPica = 768;    // plan: sprite cap (the builder's own kMaxSprites is 512)

struct FxPicaStats {
    // The last build (the first eye of a frame).
    int particleQuads = 0;          // quads drawn
    int particleQuadsDropped = 0;   // quads built beyond the cap and cut
    size_t particleArenaBytes = 0;  // FlatVertex bytes written to the frame arena
    int spriteQuads = 0;
    int spriteQuadsDropped = 0;
    size_t spriteArenaBytes = 0;
    // The last draw() of each.
    int particleDraws = 0;          // C3D_DrawArrays calls
    int spriteDraws = 0;
    // Cumulative.
    int particleLateTextures = 0;   // textures that were not preloaded when a draw needed them
    int arenaFailures = 0;          // builds skipped because the arena was full
    // The particle texture cache.
    int particleTextures = 0;
    size_t particleTextureBytes = 0;
};

// The counters of the PICA particle and sprite renderers.
const FxPicaStats& fxPicaStats();

// Keeps the first `maxVerts` vertices of a built vertex list: cuts `runs` to match (a run that
// straddles the limit is shortened, later runs are dropped). Returns the vertex count kept.
inline int capRuns(std::vector<DrawRun>& runs, int totalVerts, int maxVerts) {
    if (totalVerts <= maxVerts) return totalVerts;
    while (!runs.empty() && runs.back().first >= maxVerts) runs.pop_back();
    if (!runs.empty() && runs.back().first + runs.back().count > maxVerts) runs.back().count = maxVerts - runs.back().first;
    return maxVerts;
}

namespace detail {
FxPicaStats& fxStats();
// Near and far plane of a PICA projection (clip z = -w at near, 0 at far; `m16` is the
// column-major as3d::Mat4), 4 and 2000 when the matrix is not one; the terrain renderer's
// derivation.
void planesOfProjection(const float* m16, float* nearPlane, float* farPlane);
} // namespace detail

} // namespace as3d::pica
