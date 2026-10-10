// The drawing half shared by the PICA particle and sprite renderers (3DS only; the contract and
// the counters are in fx_pica.h).
#pragma once

#include <cstddef>
#include <vector>

#include "as3d/draw_run.h"
#include "as3d/math.h"
#include "shader_set.h"

namespace as3d::pica {

// Copies `count` builder vertices (9 floats each: x y z u v r g b a, colours in 0..1) into the
// frame arena as FlatVertex (colour bytes rounded and clamped). Null when the arena is full
// (counted in FxPicaStats::arenaFailures).
FlatVertex* uploadBuilderVertices(const float* vertices9, size_t count);

struct FxEyeParams {
    Mat4 view;       // the CENTRAL view (the eye translation is in the projection)
    Mat4 projection; // the eye's PICA projection
    Vec3 fogColour;
    float fogStart = 1.0e9f;
    float fogEnd = 1.0e9f;
    // The render-state self-check (state_check_pica.h): checked right before the first run's
    // draw, and the GPU read back right after the last one (the water pass).
    const struct PassExpect* check = nullptr;
    const char* probe = nullptr;
};

// Draws `runs` of `verts` for one eye: W-buffer depth mapping and fog of the projection's
// planes, the flat shader with mvp = projection * view, then per run the material state
// (blend, depth, cull, fog colour per blend), the TEV (modulate, or clamp-add for texMode 2;
// the vertex colour alone for a run without texture), the run's texture and one
// C3D_DrawArrays. Leaves blend off and depth test + write on, as the GL renderers do. Returns
// the number of draw calls.
int drawFlatRuns(const FlatVertex* verts, const std::vector<DrawRun>& runs, const FxEyeParams& eye);

} // namespace as3d::pica
