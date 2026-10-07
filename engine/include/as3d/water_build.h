// The water's CPU side, shared by every rendering backend: the first game's flat-plane values,
// the sequels' wave grid (as2/render-pipeline.delta.md 12.5, data in as3d/water.h) as chunked
// vertices and indices, their update after a crater, the cull, and the vertex shader's wave
// and texture-coordinate math on the CPU (animateWaterGrid, for a backend without that
// shader). No GL.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "as3d/core.h"
#include "as3d/math.h"
#include "as3d/terrain.h"
#include "as3d/terrain_build.h"
#include "as3d/terrain_grid.h"
#include "as3d/water.h"

namespace as3d {

struct WaterParams {
    bool present = false;
    float level = 0.0f;
    float alpha = 0.0f;
    Vec3 colour;          // sun * sun_dir.z + ambient (sun direction normalised), clamped to [0,1]
    float phase = 0.0f;   // t = time * pi * 0.1
    float planeZ = 0.0f;  // level + sin(phase)
    Vec2 texOffset;       // (0.4 sin(phase), 0.4 sin(phase / 2))
};
WaterParams computeWaterParams(const TerrainStyle& style, float timeSeconds);

// One grid vertex as the water program receives it: the wave, the alpha and both texture
// layers are computed in the vertex shader from these and the time.
struct WaterGridVertex {
    float col = 0, row = 0; // grid indices
    float terrainZ = 0;     // the terrain's current height (non-animated vertices sit there)
    float weight = 0;       // depth weight w (as3d/water.h)
};

// The vertices of rows rowStart..rowStart+rows (all columns, row-major) and the triangles of
// its wet cells (two per cell: (v00, v10, v11), (v00, v11, v01), the terrain's split) as
// indices into those vertices.
void buildWaterChunk(const WaterSurface& surface, const TerrainGridView& grid, int rowStart, int rows,
                     std::vector<WaterGridVertex>& vertices, std::vector<uint16_t>& indices);

// Per-frame values of the grid water at game time T (seconds): t = T*pi*0.1 drives the scroll.
// The layers as the original binds them (as2/render-corrections.md C1): the shine texture in
// the first stage (x2 matrix), the base in the second (x1.5), colour lerp(shine, base, base.a).
struct WaterGridFrame {
    float waveTime = 0.0f; // T, for the wave term
    Vec2 baseOffset;       // (0.4 sin t, 0.4 sin(t/2)), added to 1.5*(c/4, r/4)
    Vec2 shineOffset;      // (0.4 sin(t/2) + 0.2, -0.2 sin(t/4) - 0.3), added to 2*(c/4, r/4)
};
WaterGridFrame computeWaterGridFrame(float timeSeconds);
// Texture coordinates of grid vertex (c, r) for the two layers (Direct3D v, which is this
// engine's convention: v = 0 at the top of the picture).
Vec2 waterBaseUv(int col, int row, const WaterGridFrame& f);
Vec2 waterShineUv(int col, int row, const WaterGridFrame& f);

// A grid vertex after the vertex shader of the wave grid (kWaterGridVertexSrc): world position
// (col*40, row*40, z), the two texture layers' coordinates and alpha = weight * opacity.
struct WaterGridAnimated {
    Vec3 pos;
    Vec2 uvBase, uvShine;
    float alpha = 0.0f;
};
// z = surface.level + 16*w*0.5*(sin(0.75*row + T) + sin(col + T)) for a vertex with
// w >= 0.0001 off the first and last map rows, else the terrain's height held in the vertex.
WaterGridAnimated animateWaterGrid(const WaterSurface& surface, const WaterGridVertex& v, const WaterGridFrame& f);
void animateWaterGrid(const WaterSurface& surface, const WaterGridVertex* vertices, size_t count, float timeSeconds,
                      std::vector<WaterGridAnimated>& out);

constexpr int kWaterChunkRows = 8; // the terrain's chunks (as2@0x437c60)

// One 8-row chunk of the wave grid inside WaterGridGeometry: its vertices are
// verts[firstVertex, firstVertex + vertexCount) ((rows + 1) x (W + 1)), its wet-cell triangles
// indices[firstIndex, firstIndex + indexCount), already offset into verts (a shared buffer).
struct WaterGridChunk {
    int rowStart = 0, rows = 0;
    size_t firstVertex = 0, vertexCount = 0;
    size_t firstIndex = 0, indexCount = 0;
    Aabb bounds; // x over the whole map, z from the level - 16 .. level + 16 widened by the terrain
};

struct WaterGridGeometry {
    std::vector<WaterGridChunk> chunks;
    std::vector<WaterGridVertex> verts;
    std::vector<uint16_t> indices;
    int wetCells = 0;
    bool valid() const { return !chunks.empty(); }
};

// All chunks of the grid over `grid` (empty for a surface that is not present, a bad grid or a
// size that differs from the surface's). Callers must reject vertex counts above 65535 before
// using the 16-bit indices.
WaterGridGeometry buildWaterGridGeometry(const WaterSurface& surface, const TerrainGridView& grid);

// Heights changed inside these vertex rectangles (TerraMorph): refreshes the terrainZ of the
// touched chunks' vertices from `grid` and widens their boxes. Weights and wet cells stay.
// Returns the touched chunk indices, ascending.
std::vector<int> updateWaterGridHeights(WaterGridGeometry& geometry, const WaterSurface& surface,
                                        const VertexRect* rects, size_t count, const TerrainGridView& grid);

// Chunks with wet cells whose boxes are not entirely outside one clip plane of `viewProjection`.
std::vector<int> visibleWaterChunks(const WaterGridGeometry& geometry, const Mat4& viewProjection);

} // namespace as3d
