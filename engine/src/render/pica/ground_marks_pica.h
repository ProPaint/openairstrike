// Ground marks and skid trails on the PICA (3DS only; the neutral halves are buildTerrainDecal /
// buildDecalRuns in decal_build.cpp and buildSkidTrails in skid_build.cpp). docs/3ds-port.md "M3.2".
//
// Marks. setMarks() takes the scene's mark descriptions (WorldScene::markDescs). When the set
// changes (an entity slot or generation, colour, alpha, texture) or invalidate() was called
// (craters changed the terrain), every mark is draped again on the CPU (the GL rectangle:
// axis aligned, origin + bbox, never rotating; unflipped texture for the sequels) into FlatVertex
// with the entity colour and alpha as the vertex colour, and the runs come from buildDecalRuns
// (state = decalMaterialKey: the mark's blend, depth test on, write off, polygon offset, cull
// back, fog colour from the blend). Between changes the draped vertices are kept: a frame with
// the same marks costs a comparison. At most kMaxMarkVertices are kept (the rest is dropped,
// counted).
//
// Skid trails (the sequels): rebuilt every frame by buildSkidTrails (their alpha fades with the
// age) as strips with the per-section alpha in the vertex colour, white.
//
// Drawing. Nothing is created or built inside a frame. draw() (per eye) copies the marks and the
// skid vertices into the frame arena at its first call of an arena frame and reuses them for the
// second eye, then drawFlatRuns (fx_draw_pica.h): flat shader, TEV texture x vertex colour, the
// run's MaterialKey through applyMaterialState, one C3D_DrawArrays per run. Textures are the
// caller's (the ResourceCache's, created at precache), the pass owns none.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "as3d/draw_run.h"
#include "as3d/ground_marks.h"
#include "as3d/skid_render.h"
#include "as3d/terrain_grid.h"
#include "shader_set.h"

namespace as3d::pica {

// Marks kept at most (24 B each: 400 KB of the 1.5 MB arena half in the worst case).
constexpr size_t kMaxMarkVertices = 16384;

struct MarkPicaStats {
    // The last prepared frame.
    int marks = 0;             // marks with geometry
    int marksDropped = 0;      // marks not kept (vertex cap, no terrain under them)
    int markVertices = 0;
    int skidTrails = 0;
    int skidSegments = 0;      // section pairs drawn: sum over trails of (vertices / 2 - 1)
    int skidVertices = 0;
    int textures = 0;          // distinct textures the runs use
    size_t textureBytes = 0;   // their GPU bytes
    bool rebuilt = false;      // the mark geometry was cut again this frame
    double buildMs = 0.0;      // setMarks + setSkids
    // The last draw().
    int draws = 0;
    size_t arenaBytes = 0;
    // Since reset(): mark rebuilds.
    int rebuilds = 0;
};
const MarkPicaStats& markPicaStats();

// Test hook (apps/3ds mark_test.cpp): trails drawn by the world renderer instead of the world's
// (AirStrike 3D has no skid trails; the sequels' data is not on the test card). Null: the world's.
void setTestSkidTrails(const std::vector<SkidTrail>* trails);
const std::vector<SkidTrail>* testSkidTrails();

class MarkPass {
public:
    using MarkList = std::vector<std::pair<GroundMarkDesc, std::uint64_t>>;

    void reset(); // new level
    // Per frame, outside frames: beginFrame(), setMarks(), setSkids() (in this order).
    void beginFrame();
    void setMarks(const MarkList& marks, const TerrainGridView& grid);
    void setSkids(const SkidTrail* trails, size_t count, const TerrainGridView& grid, const SkidTrailParams& params,
                  const SkidTextureResolver& resolve);
    // Craters: cut every mark again at the next setMarks().
    void invalidate() { dirty_ = true; }
    bool active() const { return !runs_.empty(); }
    // One eye. view = the CENTRAL view, projection = the eye's PICA projection. Returns the draws.
    // Leaves blend off, depth test + write on, no polygon offset, cull back; the caller restores
    // the depth mapping and the fog of its own state (drawFlatRuns sets them from the projection).
    int draw(const Mat4& view, const Mat4& projection, const Vec3& fogColour, float fogStart, float fogEnd);

private:
    void noteTextures();
    MarkList lastMarks_;
    bool dirty_ = true;
    int markBatchCount_ = 0, markDropped_ = 0;
    std::vector<FlatVertex> markVerts_;
    std::vector<DrawRun> markRuns_;
    std::vector<SkidVertex> skidSrc_;
    std::vector<FlatVertex> skidVerts_;
    std::vector<DrawRun> skidRuns_;
    std::vector<DrawRun> runs_; // marks then skids, as drawn
    const FlatVertex* arena_ = nullptr;
    unsigned arenaFrame_ = ~0u;
};

} // namespace as3d::pica
