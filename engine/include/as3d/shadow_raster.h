// The neutral (no GL) half of the silhouette shadows (docs/spec/render-pipeline.md section 5):
// the shadow geometry shared with ShadowRenderer (as3d/shadow_render.h) and a CPU rasteriser
// that reproduces ShadowRenderer::generate() without a GPU, for back ends that cannot render
// to a texture and read it back cheaply (the 3DS).
//
// The rasteriser replays generate()'s GL pipeline sample for sample on the 2W x 2H grid:
//   - the projected triangles in the model's face order, pixel centres at (i + 0.5, j + 0.5),
//     a top-left fill rule on vertices snapped to 1/256 pixel, no culling;
//   - a depth test keeping the fragment nearest the light (highest model z, ties pass, as
//     GL_LEQUAL) with depth writes: a fragment hidden by a higher surface does not blend, and a
//     transparent skin texel on a higher surface still hides what is below it, exactly as in GL;
//   - the skin's alpha sampled like the GL skin texture (trilinear, repeat, 2x2 box mipmaps,
//     the level of detail from the major axis of the triangle's texel footprint, as the GPU
//     does for anisotropic footprints), blended as dst * (1 - alpha) over
//     white in 8 bits: alpha < 1 casts a partial shadow (there is no alpha threshold);
//   - projected shadows with RenderRules::shadowEraseBelowGround: white wherever the highest
//     geometry is at or below z = 0; then + 0.6 grey (153) saturating;
//   - the 2x2 box filter to W x H with integer division, RGB (114, 114, 114), alpha = 255 - red.
// So the produced Image is the same as ShadowMap::image: RGBA8, W x H, RGB = 114 everywhere,
// alpha = darkness in 0..102 (0.4 * 255), row 0 = the top (largest projected y, Ymax), column
// 0 = Xmin. For an A8 texture take the alpha bytes.
#pragma once

#include <cstdint>
#include <vector>

#include "as3d/core.h"
#include "as3d/image.h"
#include "as3d/model.h"
#include "as3d/render_rules.h"
#include "as3d/vec.h"

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

// Peak shadow alpha: coverage 1 gives 0.4, i.e. the ground darkens by at most 40 percent.
constexpr float kShadowMaxAlpha = 0.4f;

// The projection direction: straight down for Planar, else `towardsSun` normalised with its z
// kept at least 0.05 (every shipped level has L.z > 0; this keeps the projection finite).
Vec3 shadowLightDirection(ShadowKind kind, const Vec3& towardsSun);

// Spec 5.2 step 1 for one vertex: rotated by (cosR, sinR) about Z, then projected along `L`
// onto z = 0. The result's z is the vertex's own z (its height above the ground plane).
Vec3 projectShadowVertex(const Vec3& v, float cosR, float sinR, const Vec3& L);

// Spec 5.2 step 1. `towardsSun` is the unit vector towards the sun; ignored (straight down is
// used) for Planar. Planar shadows are always generated with rotationSteps = 0.
ShadowBounds computeShadowBounds(const ModelData& model, ShadowKind kind, const Vec3& towardsSun, int rotationSteps);

// Spec 5.2 step 2: texture size W x H for a bounding rectangle (before the 2x supersampling).
// The first game's limits; with `rules`, the game's (as2 delta 5.2: height up to 256, each
// side halved after LOW/HIGH until at most 256).
void shadowTextureSize(const ShadowBounds& bounds, ShadowQuality quality, int& width, int& height);
void shadowTextureSize(const ShadowBounds& bounds, ShadowQuality quality, int& width, int& height,
                       const RenderRules& rules);

// A skin's alpha channel prepared for silhouette sampling: the mip chain glGenerateMipmap
// builds for the GL skin texture (2x2 box), sampled trilinear with repeat wrap. Build it once
// per skin and reuse it for every silhouette of that skin.
class ShadowSkin {
public:
    ShadowSkin() = default;
    explicit ShadowSkin(const Image& skin);

    bool valid() const { return !levels_.empty(); }
    // True when every texel has alpha 255: the skin then casts like no skin at all.
    bool opaque() const { return opaque_; }
    // Alpha in 0..1 at (u, v) (v = 0 is image row 0) for level of detail `lod` (log2 of the
    // texels per pixel; <= 0 is magnification).
    float sample(float u, float v, float lod) const;
    int width() const { return valid() ? levels_[0].w : 0; }
    int height() const { return valid() ? levels_[0].h : 0; }
    // FNV-1a of the size and the alpha of the base level (keys of the 3DS silhouette cache).
    std::uint64_t contentHash() const { return hash_; }

private:
    struct Level {
        int w = 0, h = 0;
        std::vector<u8> a;
    };
    std::vector<Level> levels_;
    bool opaque_ = true;
    std::uint64_t hash_ = 0;
    float bilinear(const Level& l, float u, float v) const;
};

// The CPU result of a silhouette: the fields of ShadowMap that need no GPU.
struct ShadowSilhouette {
    Image image; // as ShadowMap::image (see the file header)
    ShadowBounds bounds;
    ShadowKind kind = ShadowKind::Planar;
    int width = 0, height = 0;
};

// The silhouette ShadowRenderer::generate() renders for the same arguments, on the CPU.
// `skin` null (or opaque) casts at full strength, like generate() without a skin texture.
// `rules` are the game's (ShadowRenderer::setRules). False when the model has no faces or
// fewer than three valid corners, as generate(). `maxSide` > 0 caps W and H (the PICA's
// smaller textures: the silhouette is rasterised at the capped size, over the same bounds).
bool rasterizeShadowSilhouette(const ModelData& model, const ShadowSkin* skin, ShadowKind kind,
                               const Vec3& towardsSun, int rotationSteps, ShadowQuality quality,
                               const RenderRules& rules, ShadowSilhouette& out, int maxSide = 0);

} // namespace as3d
