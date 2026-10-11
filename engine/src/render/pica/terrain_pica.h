// The PICA terrain renderer's own interface (3DS only), next to the public TerrainRenderer
// (as3d/terrain_render.h) that terrain_renderer_pica.cpp implements: byte accounting and the
// overlay depth bias, for the test mode (apps/3ds/source/terrain_test.cpp) and the perf log.
//
// How TerrainRenderer::render() reads TerrainViewParams on the PICA:
//   * `projection` must be the eye's PICA projection (picaEyeProjection: the tilt and, for a
//     stereo eye, the eye translation inside) and `view` the CENTRAL view (WorldView::view, not
//     eyeViewMatrix). The renderer draws with mvp = projection * view and culls the chunks
//     against that matrix, so each eye culls itself.
//   * near and far come from the projection (citro3d convention: clip z = -w at near, 0 at
//     far); the renderer sets the W-buffer depth mapping for that near plane and the fog table
//     for near/far and fogStart/fogEnd (fog off when fogStart >= 1e7 or fogEnd <= fogStart).
//   * the dynamic lights are applied per chunk, at most kTerrainPicaLightsPerChunk, the lights
//     nearest to the chunk's box first. The light colours are computed once per frame and chunk
//     (cached by the frame arena's frame number), so the second eye reuses them.
#pragma once

#include <cstddef>
#include <vector>

namespace as3d::pica {

constexpr int kTerrainPicaLightsPerChunk = 8;

struct TerrainPicaStats {
    // build()
    int chunks = 0;
    int tileSets = 0;
    int tileQuads = 0;
    size_t vertices = 0;        // static chunk vertices
    size_t tileVertices = 0;    // overlay vertices
    size_t vertexBytes = 0;     // linear memory: chunk + tile vertex buffers
    size_t indexBytes = 0;      // linear memory: the shared index buffer and the zero light stream
    size_t textureBytes = 0;    // base + detail + atlases (mip chains included)
    size_t textureVramBytes = 0;
    size_t textureLinearBytes = 0;
    int textures = 0;
    // the last render()
    int visibleChunks = 0;
    int litChunks = 0;          // chunks that got a non-zero light stream
    int drawCalls = 0;
    size_t indicesDrawn = 0;    // chunk indices + overlay vertices submitted
    size_t arenaBytes = 0;      // light streams written to the frame arena
    int coarseChunks = 0;       // of visibleChunks: drawn from the coarse level (far-chunk LOD)
    size_t primeDraws = 0;      // since start: the priming draws (setTerrainPrime)
    // build(): chunks with a coarse level; since build(): coarse and visible chunks over all renders
    int coarseBuilt = 0;
    size_t coarseTotal = 0, visibleTotal = 0;
    // since build(): chunks that changed level (fine <-> coarse), and the frames that had a change
    size_t lodSwitches = 0, lodSwitchFrames = 0;
    // the last update()
    int updatedChunks = 0;
    int deferredFrees = 0;      // linear buffers waiting for the GPU to finish with them
};

// The counters of the live terrain renderer (there is one per level).
const TerrainPicaStats& terrainPicaStats();

// Depth bias of the tile overlay pass, C3D_DepthMap's offset (pica_frame.h applyDepthMap);
// pica::kPolygonOffsetDepth by default (see there for the value and why). The overlay is drawn
// with the terrain's own vertices, so the depths are equal and any non-negative bias passes the
// GEQUAL test; the bias only absorbs rounding.
void setTerrainOverlayDepthOffset(float offset); // tests: try other values
float terrainOverlayDepthOffset();

// run.ini terrain_prime (default on; docs/3ds-port.md "Wash detector"): before its first chunk the
// base pass draws one degenerate triangle (three times the first vertex: no pixel) that carries
// the pass's first state block, then issues the state again for the real draws. The console's
// marks of 2026-10-11 caught the PICA holding the 2D passes' depth state after the terrain's first
// state block; since the state is re-issued with the second draw (c314767) only the first chunk
// (the nearest: the lower half of the screen) can still lose its depth. With state_check on, the
// periodic probe reads the registers back right after this draw every other period
// (pass=terrain_prime): a mismatch there is the lost block, caught without a wash on screen.
void setTerrainPrime(bool on);
bool terrainPrime();
// run.ini wash_inject=N (the wash detector's self-test): the base pass writes no depth for N
// frames every 600 (frame serials 300..300+N-1 mod 600); wash_inject=1 means 60. 0 = never.
void setTerrainWashInject(int frames);
bool terrainWashInjectNow(); // this frame's base pass writes no depth
// The chunks the base pass drew this frame for `eye` (0: the frame's first render, 1: the second),
// in draw order; and the chunk holding map y (world units), -1 outside the map.
const std::vector<int>& terrainDrawnChunks(int eye);
int terrainChunkAt(float mapY);

} // namespace as3d::pica
