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
#include "as3d/gfx.h"
#include "as3d/image.h"
#include "as3d/level.h"
#include "as3d/math.h"
#include "as3d/terrain.h"
#include "as3d/vfs.h"

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
// byte other than 3/6/9 behaves as 0.
void tileCornerUvs(int atlasWidth, int atlasHeight, int tileIndex, int rotation, Vec2 out[4]);

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
    // (and its LevelData/LevelDef) must outlive the renderer.
    bool build(const Terrain& terrain, Vfs& vfs, std::string* error);
    void render(const TerrainViewParams& params, const TerrainRenderOptions& options = {});

    // Textures that could not be loaded (a magenta placeholder was used, and a warning
    // logged). Zero for a healthy install.
    int missingTextures() const { return missing_; }
    int lastVisibleChunks() const { return lastVisible_; }
    int chunkCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    int missing_ = 0;
    int lastVisible_ = 0;
};

class WaterRenderer {
public:
    WaterRenderer();
    ~WaterRenderer();
    WaterRenderer(const WaterRenderer&) = delete;
    WaterRenderer& operator=(const WaterRenderer&) = delete;

    // No-op (active() stays false) for levels without water.
    bool build(const Terrain& terrain, Vfs& vfs, std::string* error);
    bool active() const;
    void render(const TerrainViewParams& params);
    int missingTextures() const { return missing_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    int missing_ = 0;
};

} // namespace as3d
