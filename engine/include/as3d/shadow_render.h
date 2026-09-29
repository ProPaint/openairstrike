// Baked silhouette shadows (docs/spec/render-pipeline.md section 5). A shadow is a small
// texture holding a model's silhouette (alpha at most 0.4), generated once per model, skin and
// rotation, and draped over the terrain under the entity at draw time:
//   Projected: silhouette along the sun direction, rotation baked in 30 degree steps, static;
//   Planar:    silhouette straight down, turned by the entity yaw, always directly below the
//              entity whatever its altitude and the sun direction.
// Everything is driven by plain data (a ModelData, a skin texture, positions and yaws), with
// no dependency on the game world. Needs a current GLES 3.0 context.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "as3d/gfx.h"
#include "as3d/image.h"
#include "as3d/ground_marks.h"
#include "as3d/math.h"
#include "as3d/model.h"
#include "as3d/terrain.h"

namespace as3d {

enum class ShadowKind { Projected, Planar };
// SHADOW_*_LOW halves and SHADOW_*_HIGH doubles the texture size (spec 5.1).
enum class ShadowQuality { Normal, Low, High };

// Bounding rectangle of the projected silhouette in world units (model space, before the
// entity position), floored and ceiled to integers.
struct ShadowBounds {
    int xmin = 0, ymin = 0, xmax = 0, ymax = 0;
};

// Rotation applied about Z before projecting: `rotationSteps` * 30 degrees (spec 5.2 step 1).
constexpr float kShadowRotationStepDegrees = 30.0f;

// Spec 5.2 step 1. `towardsSun` is the unit vector towards the sun; ignored (straight down is
// used) for Planar. Planar shadows are always generated with rotationSteps = 0.
ShadowBounds computeShadowBounds(const ModelData& model, ShadowKind kind, const Vec3& towardsSun, int rotationSteps);

// Spec 5.2 step 2: texture size W x H for a bounding rectangle (before the 2x supersampling).
void shadowTextureSize(const ShadowBounds& bounds, ShadowQuality quality, int& width, int& height);

// Peak shadow alpha: coverage 1 gives 0.4, i.e. the ground darkens by at most 40 percent.
constexpr float kShadowMaxAlpha = 0.4f;

struct ShadowMap {
    Texture2D texture; // RGB (114, 114, 114), alpha = silhouette darkness in 0..102
    Image image;       // CPU copy of the texture, row 0 = the top (largest y)
    ShadowBounds bounds;
    ShadowKind kind = ShadowKind::Planar;
    int width = 0, height = 0;
    bool valid() const { return texture.valid(); }
};

// One shadow to draw: `origin` is the entity origin, `yawDegrees` its rotation about Z (used
// by Planar shadows only).
struct ShadowInstance {
    const ShadowMap* map = nullptr;
    Vec3 origin;
    float yawDegrees = 0.0f;
};

// The world rectangle a shadow instance covers.
GroundRect shadowRect(const ShadowMap& map, const Vec3& origin, float yawDegrees);

class ShadowRenderer {
public:
    ShadowRenderer();
    ~ShadowRenderer();
    ShadowRenderer(const ShadowRenderer&) = delete;
    ShadowRenderer& operator=(const ShadowRenderer&) = delete;

    bool init(std::string* error);

    // Renders the silhouette texture (spec 5.2, into an FBO of 2W x 2H, 2x2 box filtered) and
    // stores it in `out`. `skin` supplies texel alpha (null: opaque). Restores the caller's
    // framebuffer binding and viewport. False on GL failure.
    bool generate(const ModelData& model, const Texture2D* skin, ShadowKind kind, const Vec3& towardsSun,
                  int rotationSteps, ShadowQuality quality, ShadowMap& out);

    // Pass 5: depth test on, depth write off, polygon offset (-1, -1), alpha blend, black.
    void draw(const Terrain& terrain, const ShadowInstance* instances, size_t count, const DecalViewParams& params);

    // Triangles of the last draw() call.
    size_t lastTriangleCount() const { return lastTriangles_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    size_t lastTriangles_ = 0;
};

} // namespace as3d
