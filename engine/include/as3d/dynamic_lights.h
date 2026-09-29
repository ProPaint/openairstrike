// Dynamic lights (docs/spec/render-pipeline.md 2.4): the per-frame list of at most 32 lights
// that `PlaceLight`, entity `light` and `light_dir` statements queue, and the two formulas
// that turn a light into a contribution: on a model (evaluated once at the entity origin per
// signed local axis of the ambient cube) and on a terrain vertex. Plain data, no GL and no
// game world: whoever owns the frame fills a DynamicLightList and hands it to
// MeshRenderer::setDynamicLights and TerrainViewParams::lights.
#pragma once

#include <cstddef>
#include <vector>

#include "as3d/math.h"

namespace as3d {

constexpr int kMaxDynamicLights = 32;

struct DynamicLight {
    Vec3 position;
    Vec3 colour{1.0f, 1.0f, 1.0f}; // may exceed 1 (data goes up to 2.5)
    float radius = 0.0f;           // world units; the light reaches exactly this far
    bool spot = false;
    Vec3 direction{0.0f, 0.0f, -1.0f}; // world space, unit length; spot lights only
    float cosOuter = -1.0f;            // spot: cos(0.5 * angle)
    float cosInner = -1.0f;            // spot: cos(0.4 * angle)

    static DynamicLight point(const Vec3& position, const Vec3& colour, float radius);
    // `angleDegrees` is the full cone angle of the `light_dir` statement; `direction` need
    // not be normalised.
    static DynamicLight spotLight(const Vec3& position, const Vec3& colour, float radius, const Vec3& direction,
                                  float angleDegrees);
};

// The frame's light list. Lights past the 32nd are dropped, like the original's.
class DynamicLightList {
public:
    // False when the list is full (the light is dropped).
    bool add(const DynamicLight& light);
    void clear() { lights_.clear(); }
    size_t size() const { return lights_.size(); }
    const DynamicLight* data() const { return lights_.data(); }
    const std::vector<DynamicLight>& lights() const { return lights_; }

private:
    std::vector<DynamicLight> lights_;
};

// Distance and cone factor shared by both formulas: returns false when `target` is outside
// the light. On success `toLight` is the unit vector from `target` to the light (zero if they
// coincide), `att` = (radius - dist) / radius and `spot` the cone factor (1 for point lights).
bool evaluateLight(const DynamicLight& light, const Vec3& target, Vec3& toLight, float& att, float& spot);

// Adds every light's contribution at `origin` to the six colours of a model's ambient cube:
// slot i for the +axis i side, slot i + 3 for the -axis i side; `axes` are the world-space
// directions of the model's local X, Y, Z axes (unit length).
void accumulateModelLights(const DynamicLight* lights, size_t count, const Vec3& origin, const Vec3 axes[3],
                           Vec3 cube[6]);

// Factor `k` a light adds (as colour * k) to a terrain vertex at `position` with unit normal
// `normal`; 0 when the light does not reach it or faces away.
float terrainLightFactor(const DynamicLight& light, const Vec3& position, const Vec3& normal);

// The summed, clamped terrain colour: baked colour (0..1) plus every light's colour * factor,
// clamped to 1 per channel.
Vec3 lightTerrainVertex(const DynamicLight* lights, size_t count, const Vec3& position, const Vec3& normal,
                        const Vec3& baked);

// True when the light's sphere may touch the view frustum of `viewProj` (used to keep the
// 32 slots for lights that can matter).
bool lightMayBeVisible(const DynamicLight& light, const Mat4& viewProj);

} // namespace as3d
