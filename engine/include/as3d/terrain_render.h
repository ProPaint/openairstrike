// OpenGL ES 3.0 renderer for the terrain (as3d/terrain.h), its tile overlays and the
// water plane. See docs/spec/hmap.md "Terrain texturing", "Tile overlays", "Water",
// "Fog" and docs/graphics.md (terrain section). Needs a current GLES 3.0 context.
//
// The renderer takes a plain parameter struct rather than a camera object: whoever owns
// the camera (as3d/game_camera.h, or a general Camera elsewhere) fills TerrainViewParams.
// World space is Z-up (x right, y = scroll direction); `view`/`projection` are ordinary
// GL matrices (as3d/math.h) mapping that world to clip space.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/dynamic_lights.h"
#include "as3d/gfx.h"
#include "as3d/image.h"
#include "as3d/level.h"
#include "as3d/math.h"
#include "as3d/render_rules.h"
#include "as3d/terrain.h"
#include "as3d/terrain_grid.h"
#include "as3d/vfs.h"
#include "as3d/water.h"

namespace as3d {

struct TerrainViewParams {
    Mat4 view;
    Mat4 projection;
    Vec3 cameraPos;
    Vec3 fogColour;
    float fogStart = 1.0e9f; // linear fog; set fogEnd <= fogStart to disable
    float fogEnd = 1.0e9f;
    float time = 0.0f;       // seconds; drives the water animation
    float mapPos = 0.0f;     // g_map_pos (informational; visibility uses the frustum)
    // Dynamic lights added to the vertex colours (render-pipeline.md 2.4); null = none.
    // At most kMaxDynamicLights are used.
    const DynamicLight* lights = nullptr;
    size_t lightCount = 0;
};

struct TerrainRenderOptions {
    bool tiles = true;
    bool detail = true;
    bool wireframe = false;
};

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
// N = normalize(normalize(−sx, 0, 1) + normalize(0, −sy, 1)), sx and sy the central differences
// (z(c+1) − z(c−1))/80 and (z(r+1) − z(r−1))/80, 0 on the map border; colour channel
// min(255, (sun·d + ambient)·f·255), d = clamp(N·L, 0, 1), f the underwater factor of
// hmap.md. Both (W+1) x (H+1), row-major, from the grid's heights at the time of the call.
void centralDifferenceNormals(const TerrainGridView& grid, std::vector<Vec3>& out);
void sequelTerrainColours(const TerrainGridView& grid, const std::vector<Vec3>& normals, const TerrainStyle& style,
                          std::vector<TerrainVertexColor>& out);

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

class TerrainRenderer {
public:
    TerrainRenderer();
    ~TerrainRenderer();
    TerrainRenderer(const TerrainRenderer&) = delete;
    TerrainRenderer& operator=(const TerrainRenderer&) = delete;

    // Loads texture1..4/detail from def.textures and the tile atlases the level uses
    // through `vfs`, generates the base textures and uploads the chunked mesh. `terrain`
    // (and its LevelData/LevelDef) must outlive the renderer. `rules` selects the game's
    // normals, static colours and tile inset (as3d/render_rules.h).
    bool build(const Terrain& terrain, Vfs& vfs, std::string* error,
               const RenderRules& rules = renderRules(GameId::AirStrike3D));
    void render(const TerrainViewParams& params, const TerrainRenderOptions& options = {});

    // Terrain heights changed inside these vertex rectangles (TerraMorph, issue as2/200):
    // copies the vertex positions of every chunk a rectangle touches from `grid` (the terrain
    // itself once it is mutable: TerrainGridView::of(terrain)) into the chunk's vertices and its
    // tile overlays, re-uploads those chunks only and widens their culling boxes. Normals,
    // static and dynamic-light colours, base textures and tiles are not recomputed
    // (rcsl-builtins-semantics.delta.md TerraMorph step 5).
    void update(const VertexRect* rects, size_t count, const TerrainGridView& grid);

    // Textures that could not be loaded (a magenta placeholder was used, and a warning
    // logged). Zero for a healthy install.
    int missingTextures() const { return missing_; }
    int lastVisibleChunks() const { return lastVisible_; }
    int lastUpdatedChunks() const { return lastUpdated_; }
    int chunkCount() const;
    // Rows rowStart..rowStart+rows of chunk k (for tests); false for a bad index.
    bool chunkRows(int k, int& rowStart, int& rows) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    int missing_ = 0;
    int lastVisible_ = 0;
    int lastUpdated_ = 0;
};

// ---------------------------------------------------------------------------
// The sequels' water grid (as2/render-pipeline.delta.md 12.5; data in as3d/water.h).
// ---------------------------------------------------------------------------

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
                     std::vector<WaterGridVertex>& vertices, std::vector<u16>& indices);

// Per-frame values of the grid water at game time T (seconds): t = T·π·0.1 drives the scroll.
// The layers as the original binds them (as2/render-corrections.md C1): the shine texture in
// the first stage (×2 matrix), the base in the second (×1.5), colour lerp(shine, base, base.a).
struct WaterGridFrame {
    float waveTime = 0.0f; // T, for the wave term
    Vec2 baseOffset;       // (0.4 sin t, 0.4 sin(t/2)), added to 1.5·(c/4, r/4)
    Vec2 shineOffset;      // (0.4 sin(t/2) + 0.2, −0.2 sin(t/4) − 0.3), added to 2·(c/4, r/4)
};
WaterGridFrame computeWaterGridFrame(float timeSeconds);
// Texture coordinates of grid vertex (c, r) for the two layers (Direct3D v, which is this
// engine's convention: v = 0 at the top of the picture).
Vec2 waterBaseUv(int col, int row, const WaterGridFrame& f);
Vec2 waterShineUv(int col, int row, const WaterGridFrame& f);

class WaterRenderer {
public:
    WaterRenderer();
    ~WaterRenderer();
    WaterRenderer(const WaterRenderer&) = delete;
    WaterRenderer& operator=(const WaterRenderer&) = delete;

    // The first game's flat plane from the terrain's water fields. No-op (active() stays
    // false) for levels without water.
    bool build(const Terrain& terrain, Vfs& vfs, std::string* error);
    // The sequels' grid (surface.waves) over `grid`, with the surface's base and shine
    // textures; a surface without waves builds the flat plane as build() does. `terrain` must
    // outlive the renderer.
    bool build(const Terrain& terrain, const WaterSurface& surface, const TerrainGridView& grid, Vfs& vfs,
               std::string* error);
    // Heights changed inside these vertex rectangles (TerraMorph): re-uploads the non-animated
    // vertices of the touched chunks from `grid`; the weights and wet cells stay as built
    // (rcsl-builtins-semantics.delta.md TerraMorph). No-op for the flat plane.
    void update(const VertexRect* rects, size_t count, const TerrainGridView& grid);
    bool active() const;
    bool grid() const; // the sequels' grid is in use
    void render(const TerrainViewParams& params);
    int missingTextures() const { return missing_; }
    // Grid water: wet cells drawn, chunks drawn by the last render(), chunks re-uploaded by the
    // last update().
    int wetCells() const;
    int lastVisibleChunks() const { return lastVisible_; }
    int lastUpdatedChunks() const { return lastUpdated_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    int missing_ = 0;
    int lastVisible_ = 0;
    int lastUpdated_ = 0;
};

} // namespace as3d
