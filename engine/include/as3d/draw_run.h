// DrawRun: one draw call of a neutral render builder (particle_build, sprite_build,
// lightning_build, decal_build, skid_build). A builder turns plain inputs into a vertex vector
// of a documented layout plus a vector of runs; a backend uploads the vertices and, per run,
// sets the state (GL: applyMaterialState(run.key) and fogColorFor(run.key.fog, ...)), binds
// the texture and draws vertices [first, first + count). Runs keep the original draw order.
#pragma once

#include <vector>

#include "as3d/material_key.h"
#include "as3d/math.h"
#include "as3d/texture.h"

namespace as3d {

// DrawRun::texMode of the particle pass (the fragment shader's uTexMode).
constexpr int kTexModulate = 1; // texture * vertex colour
constexpr int kTexClampAdd = 2; // clamp(texture + vertex colour): the Filter blend's look

struct DrawRun {
    MaterialKey key;
    const Texture2D* texture = nullptr; // may be null: draw with whatever is bound
    int first = 0;                      // first vertex
    int count = 0;                      // vertex count
    int texMode = 0;                    // particles: kTexModulate or kTexClampAdd; others 0
    bool strip = false;                 // triangle strip instead of triangle list (skid trails)
    Vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};  // decals: entity colour (rgb) and alpha multiplying the texture
};

// Appends `run`, or extends the last run when it has the same state, texture, texMode, tint and
// primitive and `run` starts where it ends. Strips never merge; empty runs are dropped.
inline void appendDrawRun(std::vector<DrawRun>& runs, const DrawRun& run) {
    if (run.count <= 0) return;
    if (!runs.empty() && !run.strip) {
        DrawRun& l = runs.back();
        if (!l.strip && l.key == run.key && l.texture == run.texture && l.texMode == run.texMode &&
            l.tint.x == run.tint.x && l.tint.y == run.tint.y && l.tint.z == run.tint.z && l.tint.w == run.tint.w &&
            l.first + l.count == run.first) {
            l.count += run.count;
            return;
        }
    }
    runs.push_back(run);
}

// The state of an unlit alpha/add/filter pass: fog colour follows the blend.
inline MaterialKey blendKey(RenderBlend blend, bool depthTest, bool depthWrite, CullMode cull,
                            bool polygonOffset = false) {
    MaterialKey k;
    k.blend = blend;
    k.depthTest = depthTest;
    k.depthWrite = depthWrite;
    k.cull = cull;
    k.polygonOffset = polygonOffset;
    k.fog = fogColorModeFor(blend);
    return k;
}

} // namespace as3d
