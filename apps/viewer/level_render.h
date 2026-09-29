// Renders one level of the original game the way the game shows it: terrain, water and every
// placed object with its full attachment hierarchy, seen from the in-game camera at a scroll
// position (or from high above for the whole map). Used by the `level` viewer command and,
// by including level_render.cpp, by apps/tests/level_render_test.cpp. Needs a current
// GLES 3.0 context; owns no GL state beyond what a call creates.
#pragma once

#include <string>
#include <vector>

#include "as3d/dynamic_lights.h"
#include "as3d/game_profile.h"
#include "as3d/image.h"
#include "as3d/scene.h"
#include "as3d/vfs.h"

namespace viewer {

struct LevelRenderOptions {
    int width = 800;
    int height = 600;
    float scroll = 32.0f;     // g_map_pos, the world y the camera follows (game start value 32)
    int cameraMode = 1;       // in-game camera preset 0..3 (default 1)
    float cameraX = 640.0f;   // clamped to [578, 702]
    bool overview = false;    // high top-down view instead of the game camera
    float span = 0.0f;        // overview only: world y length shown from `scroll`; 0 = whole map
    bool objects = true;
    int msaa = 4;
    // WP-35 passes, all on by default; the --no-* options switch them off for before/after pairs.
    int shadowFirst = 0;   // tests: skip this many shadows,
    int shadowLimit = -1;  // then draw only N of them (-1 = all)
    bool shadows = true;   // baked silhouette shadows under objects that declare `shadow`
    bool sprites = true;   // TYPE_SPRITE / HSPRITE / VSPRITE objects
    bool marks = true;     // TYPE_MARK objects the map places
    bool dataLights = true; // `light` / `light_dir` of placed objects (night lights included)
    bool envmaps = true;   // environment-mapped materials
    // Extra lights (--lights "x,y,z,r,g,b,radius;...") and extra ground marks (--marks
    // "x,y[,object];..." or "demo"; the object defaults to "mark").
    std::vector<as3d::DynamicLight> extraLights;
    std::string extraMarks;
    // The game whose render rules and game rules apply (as3d/render_rules.h,
    // as3d/game_profile.h): water, shadows, sprites, environment maps, skid marks.
    as3d::GameId game = as3d::GameId::AirStrike3D;
    float time = 0.0f;  // game time T in seconds: water scroll and waves
    // --morph "x,y,stamp;...": TerraMorph stamps (morphmaps/<stamp>.tga, 8-bit grey) applied to
    // the heights before rendering, with the builtin's placement, skip and height rules
    // (as2/rcsl-builtins-semantics.delta.md entry 95); the renderers then re-upload only the
    // touched chunks. The viewer keeps its own copy of the heights (Terrain is not mutable).
    std::string morphs;
    // --skid "x0,y0,x1,y1,width,texture;...": a skid trail laid from (x0,y0) to (x1,y1), newest
    // section at (x1,y1), one section every 25 units (5/12 s at 60 units/s), ages from that
    // speed; `texture` a path or a name in gfx/marks (jeepmark1, tankmark1).
    std::string skids;
};

struct LevelRenderStats {
    int placements = 0;      // placements considered
    int drawnObjects = 0;    // object nodes submitted to the renderer
    int visibleChunks = 0;
    int missingTextures = 0; // terrain/water textures that fell back to magenta
    bool hasWater = false;
    int shadowsDrawn = 0;    // shadow instances submitted
    int spritesDrawn = 0;
    int marksDrawn = 0;
    int lightsUsed = 0;      // dynamic lights in the frame's list (max 32)
    int envParts = 0;        // drawn object parts with an environment map
    std::string levelId;
    bool waterGrid = false;  // the sequels' water grid
    int wetCells = 0;
    int waterChunks = 0;     // water chunks (grid) or quads (plane) drawn
    int morphStamps = 0;     // stamps applied (a stamp that failed to load is not counted)
    int terrainChunksUpdated = 0;
    int waterChunksUpdated = 0;
    int skidTrails = 0;      // trails drawn
};

// `db` must be the loaded definition database of `vfs`. Returns false with `error` set on
// failure (unknown level, no context, ...). Warnings go to the game log.
bool renderLevel(as3d::Vfs& vfs, const as3d::DefDatabase& db, as3d::ResourceCache& cache,
                 as3d::MeshRenderer& renderer, const std::string& levelRef, const LevelRenderOptions& opts,
                 as3d::Image& out, LevelRenderStats* stats, std::string& error);

} // namespace viewer
