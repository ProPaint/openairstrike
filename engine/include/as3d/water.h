// The water surface of a level as data: depth weights, wet cells and the animated height.
// No GL: the simulation reads it (WaterHeight, FL_ONWATER, FL_ONWATER_NORMAL) without the
// renderer, and the water renderer (as3d/terrain_render.h) draws the same numbers.
//
// Rules (docs/spec/as2/render-pipeline.delta.md 12.5 and 12.6,
// docs/spec/as2/engine-behaviour.delta.md 4.2, docs/spec/as2/issues/220-water-surface.md):
//  - weight of vertex (c, r): w = clamp((level − z)/16, 0, 1) from the terrain height z at
//    level load; fixed afterwards (a TerraMorph crater never gets water);
//  - a cell is wet (drawn) when at least one of its corners is at or below the level;
//  - animated height of a vertex with w ≥ 0.0001 that is not on the first or last map row:
//    level + 16·w·0.5·(sin(0.75·row + T) + sin(col + T)), T the game time in seconds; any other
//    vertex sits at the terrain's current height;
//  - per-vertex alpha = w × opacity.
// The first game's water (WaterSurface::waves false) is a flat plane at the level.
#pragma once

#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/terrain_grid.h"
#include "as3d/vec.h"

namespace as3d {

constexpr float kWaterWaveAmplitude = 16.0f; // as2@0x2103cb4
constexpr float kWaterFadeDepth = 16.0f;     // depth over which the alpha rises from 0 to 1
constexpr float kWaterMinWeight = 0.0001f;   // below it a vertex is not animated

struct WaterSurface {
    bool present = false;   // the level has a `water` statement
    bool waves = false;     // the sequels' animated grid; false: the first game's flat plane
    float level = 0.0f;
    float opacity = 0.0f;
    std::string baseTexture;  // `water` first texture
    std::string shineTexture; // the sequels' second texture; empty in the first game's form
    int width = 0, height = 0;   // cells W x H of the grid below
    std::vector<float> weight;   // (W+1) x (H+1), row-major
    std::vector<u8> wetCell;     // W x H, row-major: 1 when drawn

    float weightAt(int c, int r) const { return weight[static_cast<size_t>(r) * static_cast<size_t>(width + 1) + static_cast<size_t>(c)]; }
    bool wet(int c, int r) const { return wetCell[static_cast<size_t>(r) * static_cast<size_t>(width) + static_cast<size_t>(c)] != 0; }
};

// Builds the weights and wet cells from the grid's heights (call at level load).
// `present` false (no water) gives an empty surface.
WaterSurface buildWaterSurface(const TerrainGridView& grid, bool present, bool waves, float level, float opacity,
                               const std::string& baseTexture, const std::string& shineTexture);
// From a built Terrain's own water fields (TerrainStyle has no shine texture: pass it here).
WaterSurface buildWaterSurface(const Terrain& terrain, bool waves, const std::string& shineTexture);

// The wave term 16·w·0.5·(sin(0.75·row + T) + sin(col + T)) added to the level.
float waterWaveOffset(float weight, int col, int row, float timeSeconds);

// Height of grid vertex (c, r) of the animated surface at time T: the level plus the wave for
// an animated vertex, else the terrain's current height (flat plane: the level).
float waterVertexHeight(const WaterSurface& s, const TerrainGridView& grid, int c, int r, float timeSeconds);

struct WaterSample {
    float height = 0.0f;
    Vec3 normal{0.0f, 0.0f, 1.0f};
};

// WaterHeight(x, y) (as2@0x41d530) and the plane G_AlignToWater tilts FL_ONWATER_NORMAL
// entities to (as2@0x41d6d0): the surface height at (x, y), bilinear over the four surrounding
// grid vertices of waterVertexHeight, and the unit normal of the plane through the surface at
// (x − 10, y + 15), (x + 10, y + 15), (x, y − 25). Without water: the terrain height and the
// plane through the terrain at those points. Positions outside the map are clamped to its
// edges, as Terrain::heightAt (issue as2/251; the original returns 0 there). A pure function
// of the surface, the heights and T.
WaterSample waterHeightAt(const WaterSurface& s, const TerrainGridView& grid, float x, float y, float timeSeconds);
WaterSample waterHeightAt(const WaterSurface& s, const Terrain& terrain, float x, float y, float timeSeconds);

} // namespace as3d
