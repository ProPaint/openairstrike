// Terrain model built from a parsed level (as3d/level.h) and its levels.txt record
// (as3d/defs.h, LevelDef): vertex heights, positions, normals, static lighting, height
// queries, waypoint path sampling and spawn-order bookkeeping. See docs/spec/hmap.md
// ("Terrain geometry", "Static lighting", "TerrainHeight", "Waypoint paths",
// "Spawn order"/"Spawning") for the exact rules every function here follows.
//
// This header has no OpenGL: it is the CPU-side model the renderer (as3d/terrain_render.h)
// consumes. Owned by WP-31 (terrain/renderer package).
//
// Deliberate deviations from the original (see docs/spec/README.md's table):
//  - a terrain vertex facing away from the sun is lit with the ambient colour, not black
//    (the original scales ambient by 255 only on the lit branch -- a bug we do not
//    reproduce);
//  - heightAt()/normalAt() clamp queries to the map edges instead of reading one cell/row
//    past the edge like the original TerrainHeight.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/vfs.h"
#include "as3d/level.h"
#include "as3d/vec.h"

namespace as3d {

// defs.h and gfx.h both define an as3d::BlendMode, so no translation unit can include both;
// terrain code therefore takes a plain by-value copy of the LevelDef fields it needs.
struct LevelDef;

struct TerrainStyle {
    std::string id;
    std::string mapPath;
    std::string texturesDir; // e.g. "textures\\desert"
    float hmin = 0.0f, hmax = 0.0f;
    bool hasFog = false;
    float fogColor[3] = {0, 0, 0};
    float fogNear = 0.0f, fogFar = 0.0f;
    float sun[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0}; // colour, direction, ambient
    bool hasWater = false;
    std::string waterTexture;
    float waterLevel = 0.0f, waterAlpha = 0.0f;
    bool night = false;
    bool hasIntermission = false;
    float intermission[6] = {0, 0, 0, 0, 0, 0};
};
TerrainStyle terrainStyleFromDef(const LevelDef& def);

// A level loaded by mission reference (see loadLevelByRef).
struct LoadedLevel {
    LevelData data;
    TerrainStyle style;
};
// `ref`: a mission number ("1" = levels.txt id "mission1"), a levels.txt id ("intro1"), or a
// map game path ("maps\\level1.hsc"). Reads levels.txt and the map through `vfs`.
bool loadLevelByRef(Vfs& vfs, const std::string& ref, LoadedLevel& out, std::string* error);

// The engine's exact truncating 16.16 fixed-point bilinear resampler (VERIFIED-CODE
// 0x4185b0; docs/spec/hmap.md "Vertex grid"), used both for vertex heights and for the
// height-banded base ("mapTexture") generation (docs/spec/hmap.md "Terrain texturing").
// Reads a `sw` x `sh` window of 8-bit samples starting at `src[srcOffset]` (row stride
// `sw`) and writes a `dw` x `dh` resampled window to `dst`. Must match
// tools/ref/hmap.py's `resample()` bit for bit; apps/tests/terrain_test.cpp and
// testdata/golden/as3d/terrain_heights.json check this for every shipped level.
void resampleHeightField(const u8* src, int sw, int sh, size_t srcOffset, u8* dst, int dw, int dh);

// One terrain vertex's static colour, stored as unsigned bytes (VERIFIED-CODE: the
// original uses glColorPointer(GL_UNSIGNED_BYTE, ...) directly on this data).
struct TerrainVertexColor {
    u8 r = 0, g = 0, b = 0;
};

// CPU-side terrain model for one level. Does not own the LevelData/LevelDef it was built
// from; the caller must keep both alive for as long as the Terrain is used (same
// convention as ObjectDef::source in as3d/defs.h).
class Terrain {
public:
    // Builds vertex heights/positions/normals/colours from `level` and `def`. Returns
    // false (and leaves the Terrain in its default, empty state) only if `level`'s grid
    // is degenerate (width or height < 1); a well-formed LevelData from as3d::loadLevel
    // always succeeds.
    bool build(const LevelData& level, const TerrainStyle& style);
    bool build(const LevelData& level, const LevelDef& def); // needs as3d/defs.h at the call site

    int width() const { return static_cast<int>(width_); }   // W, cells
    int height() const { return static_cast<int>(height_); } // H, cells
    int vertsWide() const { return width() + 1; }
    int vertsHigh() const { return height() + 1; }

    const LevelData* level() const { return level_; }
    const TerrainStyle& style() const { return style_; }

    // Raw (unconverted, 0..255) resampled vertex heights, (W+1) x (H+1), row-major.
    // Exposed for the golden cross-check against tools/ref/hmap.py's vertex_heights().
    const std::vector<u8>& rawVertexHeights() const { return rawHeights_; }

    // World-space vertex data, (W+1) x (H+1), row-major (vertex (c, r) at index
    // r*vertsWide()+c), matching the grid/normals/lighting rules in docs/spec/hmap.md.
    const std::vector<Vec3>& positions() const { return positions_; }
    const std::vector<Vec3>& normals() const { return normals_; }
    const std::vector<TerrainVertexColor>& colors() const { return colors_; }

    // Height (z) of vertex (c, r), 0 outside the grid; setVertexZ changes it for every later
    // query and for the renderer's copy of the positions (the sequels' TerraMorph). Normals,
    // colours and the raw heights keep their load-time values, as in the original
    // (as2/rcsl-builtins-semantics.delta.md 95 step 5). False outside the grid.
    float vertexZ(int c, int r) const;
    bool setVertexZ(int c, int r, float z);

    // Bilinear height at world (x, y) over the *vertex* grid (docs/spec/hmap.md
    // "TerrainHeight"), clamped to the map edges (our deviation; see file header).
    float heightAt(float worldX, float worldY) const;
    // Bilinearly-interpolated, renormalized vertex normal at world (x, y); used to tilt
    // FL_ONGROUND_NORMAL objects to the terrain. Not a value the original computes this
    // way (it builds a 3-point local plane at spawn time instead, docs/spec/hmap.md
    // "Spawning"); this is a reasonable, testable engineering choice for a generic
    // "surface normal here" query, not a claimed reproduction of that spawn-time code.
    Vec3 normalAt(float worldX, float worldY) const;

    // Water (from the LevelDef; convenience accessors matching docs/spec/hmap.md "Water").
    bool hasWater() const { return style_.hasWater; }
    float waterLevel() const { return style_.waterLevel; }
    float waterAlpha() const { return style_.waterAlpha; }
    const std::string& waterTexture() const { return style_.waterTexture; }

private:
    const LevelData* level_ = nullptr;
    TerrainStyle style_;
    u32 width_ = 0, height_ = 0;
    std::vector<u8> rawHeights_;
    std::vector<Vec3> positions_;
    std::vector<Vec3> normals_;
    std::vector<TerrainVertexColor> colors_;
};

// ---------------------------------------------------------------------------
// Waypoint path sampling (docs/spec/hmap.md "Waypoint paths")
// ---------------------------------------------------------------------------

// A placement's waypoint path, preprocessed into an arc-length sample table (one sample
// every 8 world units, matching the original's preprocessing step at 0x4069a0) so that
// MoveToNextWP/RotateToNextWP/GetWaypointDelay can be thin wrappers over this class.
class WaypointPath {
public:
    // Builds from a placement's waypoints (already in the placement's own storage
    // units -- world control points are derived here via Waypoint::world*()). Returns
    // false (path stays invalid) if there are fewer than 2 waypoints, matching the
    // original: "paths with K < 2 are skipped" (docs/spec/hmap.md).
    bool build(const Placement& placement);

    bool valid() const { return !samples_.empty(); }
    bool loops() const { return loops_; }
    int waypointCount() const { return static_cast<int>(waypointCount_); }
    // Total arc length of the path (open: sum of K-1 segments; looping: sum of K).
    float length() const { return samples_.empty() ? 0.0f : samples_.back().distance; }

    struct Sample {
        Vec2 position;
        float headingDegrees = 0.0f; // direction of travel; see the .cpp for the convention
    };

    // Position/heading at arc length `s`. Looping paths wrap s into [0, length());
    // open paths clamp to [0, length()].
    Sample sampleAtDistance(float s) const;

    // Arc length at which waypoint `i` (0-based, i < waypointCount()) begins.
    float waypointArcLength(int i) const;
    // GetWaypointDelay(i): the delay recorded on waypoint (i mod K).
    float waypointDelay(int i) const;

    struct AdvanceResult {
        Vec2 position;
        float headingDegrees = 0.0f;
        bool waypointPassed = false; // a waypoint boundary was crossed this step
        int passedWaypointIndex = -1; // which one, if waypointPassed
        bool finished = false;       // an open path reached its end this step
    };
    // MoveToNextWP: advances `cursorDistance` (arc length along the path, owned by the
    // caller -- typically stored per-entity) by `distance` world units, wrapping for a
    // looping path or clamping at the end for an open one, and reports whether a
    // waypoint was crossed (and which) during this step.
    AdvanceResult advance(float& cursorDistance, float distance) const;

private:
    struct DenseSample {
        float distance = 0.0f;
        Vec2 position;
        float headingDegrees = 0.0f;
    };
    std::vector<DenseSample> samples_; // arc-length table, distance ascending, samples_[0].distance == 0
    std::vector<float> waypointArc_;   // arc length at which each waypoint begins
    std::vector<float> waypointDelay_;
    bool loops_ = false;
    size_t waypointCount_ = 0;
};

// ---------------------------------------------------------------------------
// Spawn bookkeeping (docs/spec/hmap.md "Spawn order", "Spawning")
// ---------------------------------------------------------------------------

// Walks a level's placements in the engine's spawn order (a stable sort by y) and, given
// the current g_map_pos each "frame", reports which placements enter the spawn window
// and which are dropped for good (behind the window and never spawned). Mirrors
// 0x40975d (sort) and 0x407590 (per-frame walk) exactly, including the documented
// spawn-window thresholds.
class SpawnCursor {
public:
    // `placements` must outlive the SpawnCursor (only pointers into it are kept).
    void build(const std::vector<Placement>& placements);

    void reset();
    bool atEnd() const { return cursor_ >= sorted_.size(); }

    struct Result {
        std::vector<const Placement*> toSpawn;
        std::vector<const Placement*> dropped;
    };
    // near = mapPos - 64, far = mapPos + 1000 (world units); walks the sorted list from
    // the cursor, spawning placements with near/40 <= y <= far/40, permanently dropping
    // ones with y < near/40, and stopping (for this call) at the first y > far/40.
    Result update(float mapPos);
    // The same with far = mapPos + farOffset: the sequels' first call during the level load
    // uses the reset camera's edge, mapPos + 800 (as2/engine-behaviour.delta.md 9.7).
    Result update(float mapPos, float farOffset);
    // Every placement not handed out yet, whatever its row: the sequels' intermission levels
    // skip the window test (as2/engine-behaviour.delta.md 3.4).
    Result takeAll();

private:
    std::vector<const Placement*> sorted_;
    size_t cursor_ = 0;
};

} // namespace as3d
