// The terrain's CPU geometry, shared by every rendering backend (docs/spec/hmap.md "Vertex
// grid", "Terrain texturing", "Tile overlays"): 8-row chunks of vertices with 16-bit
// triangle and wire indices, per-chunk boxes, the tile overlay quads, the height-banded base
// textures, the crater update, the CPU frustum cull and a CPU version of the vertex shader's
// dynamic-light loop. No GL: the GLES renderer (terrain_render.h) uploads these, the PICA
// backend feeds them to its own pipeline.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "as3d/core.h"
#include "as3d/dynamic_lights.h"
#include "as3d/image.h"
#include "as3d/level.h"
#include "as3d/math.h"
#include "as3d/render_rules.h"
#include "as3d/terrain.h"
#include "as3d/terrain_grid.h"

namespace as3d {

// Base texture generation (docs/spec/hmap.md "Height-banded base texture"): block
// `block` covers level rows 32*block .. 32*block+31 resampled to 256x256; each texel
// blends texture1..4 by height/86. `textures` are texture1..texture4 (256x256 RGBA,
// row 0 = top). Output: 256*256*3 RGB bytes, texel row 0 = the block's first level row.
constexpr int kBaseTextureSize = 256;
constexpr int kBaseTextureRows = 32;
void generateBaseTextureRgb(const LevelData& level, const Image textures[4], int block, std::vector<u8>& rgb);

// Tile atlas UVs (docs/spec/hmap.md "Tile overlays"). Atlas is split into 64x64 tiles;
// out[0..3] are the UVs of cell corners v00, v10, v11, v01 in this engine's texture
// convention (v = 0 at the top of the image). Indices past the atlas wrap; a rotation
// byte other than 3/6/9 behaves as 0. `halfTexelInset` (the sequels, as2 delta 12.4) moves
// every edge of the tile's rectangle half a texel inwards.
void tileCornerUvs(int atlasWidth, int atlasHeight, int tileIndex, int rotation, Vec2 out[4],
                   bool halfTexelInset = false);

// The sequels' terrain vertex normals and static colours (as2/render-pipeline.delta.md 12.2):
// N = normalize(normalize(-sx, 0, 1) + normalize(0, -sy, 1)), sx and sy the central differences
// (z(c+1) - z(c-1))/80 and (z(r+1) - z(r-1))/80, 0 on the map border; colour channel
// min(255, (sun*d + ambient)*f*255), d = clamp(N.L, 0, 1), f the underwater factor of
// hmap.md. Both (W+1) x (H+1), row-major, from the grid's heights at the time of the call.
void centralDifferenceNormals(const TerrainGridView& grid, std::vector<Vec3>& out);
void sequelTerrainColours(const TerrainGridView& grid, const std::vector<Vec3>& normals, const TerrainStyle& style,
                          std::vector<TerrainVertexColor>& out);

constexpr int kTerrainChunkRows = 8;
constexpr float kTerrainHeightBand = 86.0f;

// One terrain vertex as the GLES program receives it (location 0..4). uv1 (the detail
// texture, 0.25 per cell) is always (col, row) * 0.25 on the chunk meshes and 0 on tile
// quads, so a backend that computes it from the position may drop it. uv0 of the chunk meshes
// is (col/32, (row - 32*block)/32); on tile quads the atlas corner UV. col is the baked colour
// (byte / 255), nrm the unit normal.
struct TerrainVertex {
    float pos[3];
    float col[3];
    float uv0[2];
    float uv1[2];
    float nrm[3];
};

struct Aabb {
    Vec3 min{0, 0, 0};
    Vec3 max{0, 0, 0};
};

// Eight rows of cells (fewer for the last chunk): (rows + 1) x (W + 1) vertices, row-major,
// first vertex = grid vertex (0, firstRow). `tris` (6 per cell: v00 v10 v11, v00 v11 v01) and
// `wire` (6 per cell: three lines from v00) index into `verts` (chunk-local, not the whole
// mesh). `block` is the base texture block (firstRow / 32). `baseVertex` is the position of
// verts[0] in the concatenation of all chunks' vertices (what a shared vertex buffer uses).
struct TerrainChunkGeometry {
    std::vector<TerrainVertex> verts;
    std::vector<uint16_t> tris, wire;
    Aabb bounds;
    int firstRow = 0, rows = 0;
    int block = 0;
    size_t baseVertex = 0;
};

// The tile overlay quads of one atlas (tileSet n = tiles\tilesN.tga). Six vertices per quad
// (v00 v11 v01, v00 v10 v11), grouped by chunk: ranges[k] = {first vertex, count} of chunk k.
// cells[q] is the (column, row) of quad q.
struct TerrainTileGeometry {
    int set = 0;
    std::vector<TerrainVertex> verts;
    std::vector<std::pair<uint16_t, uint16_t>> cells;
    std::vector<std::pair<int, int>> ranges;
};

struct TerrainGeometry {
    int width = 0, height = 0; // W, H cells
    std::vector<TerrainChunkGeometry> chunks;
    std::vector<TerrainTileGeometry> tiles; // ascending tileSet, one per atlas given to the build
    size_t vertexCount = 0;                 // all chunks
    bool valid() const { return !chunks.empty(); }
};

// The tile sets (atlas numbers, ascending) that some cell of `level` uses.
std::vector<int> terrainTileSets(const LevelData& level);

// The chunk meshes and tile quads of a built Terrain. `atlasSizes` maps each tile set to its
// atlas (width, height) in pixels (for the UVs); a set missing from it gets no batch. Normals
// and colours are the terrain's own, or the sequels' (rules.terrainCentralNormals); the UVs
// use rules.tileHalfTexelInset. Empty geometry for an unbuilt terrain.
TerrainGeometry buildTerrainGeometry(const Terrain& terrain, const RenderRules& rules,
                                     const std::map<int, std::pair<int, int>>& atlasSizes);

// Heights changed inside these vertex rectangles (TerraMorph): copies the positions of every
// chunk a rectangle touches from `grid` into the chunk's vertices and tile quads and widens
// its box in z. Normals and colours stay as built. Returns the touched chunk indices,
// ascending; a backend re-uploads those chunks (verts) and tiles[b].verts in tiles[b].ranges[k].
std::vector<int> updateChunkHeights(TerrainGeometry& geometry, const VertexRect* rects, size_t count,
                                    const TerrainGridView& grid);

struct TerrainFrustum {
    float p[6][4]; // left, right, bottom, top, near, far; inside when dot(p, (x, y, z, 1)) >= 0
};
TerrainFrustum extractTerrainFrustum(const Mat4& viewProjection);
bool terrainBoxVisible(const TerrainFrustum& f, const Aabb& box);
// Indices of the chunks whose boxes touch the frustum, ascending.
std::vector<int> visibleTerrainChunks(const TerrainGeometry& geometry, const TerrainFrustum& frustum);
std::vector<int> visibleTerrainChunks(const TerrainGeometry& geometry, const Mat4& viewProjection);

// The vertex shader's dynamic-light loop on the CPU (docs/spec/render-pipeline.md 2.4; the same
// float operations as kTerrainVertexSrc): for every vertex of `chunk`, min(baked colour +
// sum of the lights' colour * factor, 1) as RGBA8 (alpha 255), 4 bytes per vertex, rounded to
// nearest. At most kMaxDynamicLights lights are used.
void terrainDynamicLightColors(const TerrainChunkGeometry& chunk, const DynamicLight* lights, int count,
                               std::vector<uint8_t>& out);

} // namespace as3d
