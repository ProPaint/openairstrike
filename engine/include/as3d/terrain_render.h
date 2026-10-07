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
#include "as3d/terrain_build.h"
#include "as3d/terrain_grid.h"
#include "as3d/vfs.h"
#include "as3d/water.h"
#include "as3d/water_build.h"

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
