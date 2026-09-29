// Skid marks: the fading tyre and track trails of ground vehicles (AirStrike 2 and later;
// docs/spec/as2/render-pipeline.delta.md 7.7, as2/engine-behaviour.delta.md 3.1.2, issues
// as2/210 and as2/221). The simulation keeps the trails (another package); this renderer only
// draws them, from plain data: a trail is a texture path and a list of nodes, oldest first.
// Needs a current GLES 3.0 context for SkidTrailRenderer; the geometry functions have no GL.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "as3d/game_profile.h"
#include "as3d/ground_marks.h" // DecalViewParams
#include "as3d/math.h"
#include "as3d/terrain_grid.h"

namespace as3d {

class ResourceCache;

// One cross-section of a trail.
struct SkidNode {
    // Centre of the mark at this section in world xy: the vehicle's base origin O plus
    // b·Ŷ + a·X̂ (`skid_mark a b w`), Ŷ the horizontal forward and X̂ the horizontal lateral
    // axis of the vehicle.
    Vec2 position;
    // Ŷ: the vehicle's horizontal forward direction (local Y), normalised. The section runs
    // along X̂ = (Ŷ.y, −Ŷ.x), from u = 0 at position − (w/2)·X̂ to u = 1 at position + (w/2)·X̂.
    Vec2 direction{0.0f, 1.0f};
    float width = 16.0f;   // w
    float age = 0.0f;      // seconds since the section was started
    float distance = 0.0f; // distance the vehicle had travelled when the section was written;
                           // texture v = distance / w (one repeat per w units)
};

struct SkidTrail {
    std::string texture;         // game path, e.g. "gfx/marks/jeepmark1.tga"
    std::vector<SkidNode> nodes; // oldest first
};

// Numbers of the pass, from the game's rules (GameRules::skid*).
struct SkidTrailParams {
    int maxTrails = 64;        // drawn per frame; further trails are not drawn
    int maxNodes = 23;         // sections per trail; older ones beyond this are not drawn
    float fadeStart = 5.0f;    // alpha 1 up to this age...
    float life = 10.0f;        // ...then linearly to 0 at this age
    float heightOffset = 2.0f; // above the terrain's current height
};
SkidTrailParams skidTrailParams(const GameRules& rules);

// 1 up to fadeStart, 1 − (age − fadeStart)/(life − fadeStart) after, 0 from life on.
float skidNodeAlpha(float age, const SkidTrailParams& params);

struct SkidVertex {
    float x, y, z;
    float u, v;
    float alpha;
};

// Appends the triangle-strip vertices of one trail (two per section, oldest first, the
// u = 0 point first): at most params.maxNodes newest sections; nothing for fewer than two
// sections. Heights: the grid's bilinear height at each point + heightOffset. Returns the
// number of vertices appended.
size_t buildSkidTrailStrip(const SkidTrail& trail, const TerrainGridView& grid, const SkidTrailParams& params,
                           std::vector<SkidVertex>& out);

// Pass 5 of the as2 frame (render-pipeline.delta.md 1.1): after the ground marks, before the
// shadows. One triangle strip per trail, vertex colour white with the section's alpha,
// BLEND_ALPHA with the texture's alpha, depth test on, write off, polygon offset, back faces
// culled (a trail laid while reversing faces down and is not seen, issue as2/221), fogged.
class SkidTrailRenderer {
public:
    SkidTrailRenderer();
    ~SkidTrailRenderer();
    SkidTrailRenderer(const SkidTrailRenderer&) = delete;
    SkidTrailRenderer& operator=(const SkidTrailRenderer&) = delete;

    // `cache` resolves the texture paths and must outlive the renderer. The vertex buffer is
    // sized once for maxTrails × maxNodes sections of `params`.
    bool init(ResourceCache& cache, const SkidTrailParams& params, std::string* error);
    void draw(const SkidTrail* trails, size_t count, const TerrainGridView& grid, const DecalViewParams& view);

    int lastTrailCount() const { return lastTrails_; }
    size_t lastVertexCount() const { return lastVertices_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    int lastTrails_ = 0;
    size_t lastVertices_ = 0;
};

} // namespace as3d
