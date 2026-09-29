// Level map (`maps\*.hsc`, magic "HMAP") loader. See docs/spec/hmap.md for the file
// format and for everything the engine builds from it (terrain, texturing, water,
// waypoints, spawning). Reference implementation: tools/ref/hmap.py, exercised on every
// shipped file by apps/tests/hmap_test.cpp against testdata/golden/hmap_summary.json.
//
// This header only covers the file itself: the raw grid and placement/waypoint records,
// kept in the units the file stores them in (cell units, 1-based placement row, etc.),
// plus the handful of conversions the spec documents (world position, yaw). The terrain
// mesh/lighting/camera built from a LevelData is as3d/terrain.h's job (WP-31, `game`
// module); this header has no OpenGL, no LevelDef and no dependency on defs.h.
//
// No exceptions, no RTTI: malformed input is reported by returning false / setting
// *error, never by throwing or asserting, and every buffer access is bounds-checked.
#pragma once

#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/vec.h"

namespace as3d {

// World units per cell (VERIFIED-CODE, docs/spec/hmap.md "World frame").
constexpr float kHmapCellSize = 40.0f;
// Placement rotation unit: rotation steps are 0..11, 30 degrees each.
constexpr float kHmapRotationStepDeg = 30.0f;

// One grid cell (4 bytes on disk). See docs/spec/hmap.md "Grid".
struct LevelCell {
    u8 height = 0;       // terrain height sample, 0..255
    u8 tileSet = 0;       // 0 = no overlay; n = tiles\tilesN.tga
    u8 tileIndex = 0;     // tile number inside the atlas (undefined atlas bounds: see spec)
    u8 tileRotation = 0;  // 0/3/6/9 = upright/90ccw/180/90cw; anything else behaves as 0
};

// One waypoint (32 bytes on disk). Fields are kept exactly as read (cell units, raw
// control points) so the canonical hash in apps/tests/hmap_test.cpp can reproduce
// tools/ref/hmap.py's placements_sha1 bit for bit; world-unit versions are one call
// away via the world*() helpers (docs/spec/hmap.md: "v*40+20", VERIFIED-CODE 0x409646).
struct Waypoint {
    i32 x = 0;            // cell column of the curve point (may be slightly outside the map)
    i32 y = 0;             // cell row, 0-based (unlike Placement::y)
    i32 unknown8 = 0;      // never read by any analysed engine function (GUESS: editor leftover)
    Vec2 inCtrl{};         // incoming Bezier control point, RAW cell units
    Vec2 outCtrl{};        // outgoing Bezier control point, RAW cell units
    float delay = 0.0f;    // returned by the script builtin GetWaypointDelay

    Vec2 worldPoint() const { return {x * kHmapCellSize + kHmapCellSize / 2.0f, y * kHmapCellSize + kHmapCellSize / 2.0f}; }
    Vec2 worldInCtrl() const { return {inCtrl.x * kHmapCellSize + kHmapCellSize / 2.0f, inCtrl.y * kHmapCellSize + kHmapCellSize / 2.0f}; }
    Vec2 worldOutCtrl() const { return {outCtrl.x * kHmapCellSize + kHmapCellSize / 2.0f, outCtrl.y * kHmapCellSize + kHmapCellSize / 2.0f}; }
};

// One placement record. See docs/spec/hmap.md "Placement record" and "Why y is 1-based".
struct Placement {
    u16 typeIndex = 0;      // 0-based index into LevelData::typeNames
    u16 x = 0;               // cell column, 0..W-1
    u16 y = 0;                // 1-based cell row: the object stands in row y-1
    u8 rotationSteps = 0;     // 0..11, 30 degrees per step
    u8 dropItem = 0;          // 0 = none, else 1-based index into LevelData::itemNames
    std::string scriptOverride; // per-placement script path, or empty
    u16 pathFlag = 0;         // raw path flag; only meaningful when !waypoints.empty()
    std::vector<Waypoint> waypoints; // file order; open path if K-1 segments, else looping

    int row() const { return static_cast<int>(y) - 1; }
    // A path loops iff it has waypoints and the flag is nonzero (VERIFIED-CODE 0x409604).
    bool loopingPath() const { return !waypoints.empty() && pathFlag != 0; }

    // Spawn position in world units, as computed by the spawner (0x4075c5-0x4075f5): the
    // centre of cell (x, y-1). z is always 0 here; the engine replaces it with the
    // terrain/water height at spawn time (see docs/spec/hmap.md "Objects: spawning").
    Vec3 spawnPosition() const {
        return {x * kHmapCellSize + kHmapCellSize / 2.0f, y * kHmapCellSize + kHmapCellSize / 2.0f - kHmapCellSize, 0.0f};
    }
    float yawDegrees() const { return static_cast<float>(rotationSteps) * kHmapRotationStepDeg; }
};

// A fully parsed .hsc file.
struct LevelData {
    u32 width = 0;   // W: grid width in cells (world x), 32 in every shipped file
    u32 height = 0;  // H: grid height in cells (world y, the scroll direction), 128 or 256
    std::vector<std::string> typeNames; // the object-type palette, T entries
    std::vector<std::string> itemNames; // drop-item table, I entries
    std::vector<LevelCell> cells;       // width*height, row-major; row 0 = smallest world y
    std::vector<Placement> placements;  // file order (NOT spawn order; see terrain.h)

    // Bounds-checked: the file format does not itself guarantee a placement's type/item
    // index or a cell coordinate stays inside these tables (only tools/ref/test_hmap.py's
    // corpus check does, against the *shipped* data); a caller handed arbitrary/fuzzed
    // input must not be able to read out of bounds through these accessors.
    const LevelCell& cellAt(int col, int row) const {
        static const LevelCell kEmptyCell{};
        if (col < 0 || row < 0 || static_cast<u32>(col) >= width || static_cast<u32>(row) >= height) return kEmptyCell;
        return cells[static_cast<size_t>(row) * width + static_cast<size_t>(col)];
    }
    const std::string& typeName(const Placement& p) const {
        static const std::string kEmpty;
        return p.typeIndex < typeNames.size() ? typeNames[p.typeIndex] : kEmpty;
    }
    // Null if the placement carries no drop item, or if its index is out of range.
    const std::string* itemName(const Placement& p) const {
        if (p.dropItem == 0 || static_cast<size_t>(p.dropItem) - 1 >= itemNames.size()) return nullptr;
        return &itemNames[static_cast<size_t>(p.dropItem) - 1];
    }
};

// Parses an in-memory .hsc file. Returns false and leaves `out` default-constructed on
// any malformed input (bad magic/version, a count or string length that runs past the
// end, more placements/types/items than the engine's own limits, trailing bytes after
// the last placement, ...) -- see docs/spec/hmap.md "File layout" and the loader
// evidence cited there (0x4091f0). Mirrors tools/ref/hmap.py's `parse()` check for
// check, including the same MAX_PLACEMENTS/MAX_TYPES/MAX_ITEMS limits. Never crashes or
// reads out of bounds regardless of `data`/`size`.
bool loadLevel(const u8* data, size_t size, LevelData& out, std::string* error = nullptr);

} // namespace as3d
