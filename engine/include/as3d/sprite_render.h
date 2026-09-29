// Sprites (docs/spec/render-pipeline.md 3.3): TYPE_SPRITE billboards, TYPE_HSPRITE flat quads
// and TYPE_VSPRITE upright quads, drawn unlit after the particles in submission order. Driven
// by plain data (a position, the object's `min`/`max` rectangle, a frame index, a colour);
// no game world. Needs a current GLES 3.0 context.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "as3d/gfx.h"
#include "as3d/ground_marks.h" // DecalBlend
#include "as3d/math.h"

namespace as3d {

enum class SpriteKind { Billboard, Horizontal, Vertical };

// The sprite list of the original holds 512 records; later ones are dropped.
constexpr size_t kMaxSprites = 512;

struct SpriteInstance {
    SpriteKind kind = SpriteKind::Billboard;
    Vec3 origin;
    float yawDegrees = 0.0f; // billboard: rotation in the view plane; H/V sprites: about world Z
    float scale = 0.0f;      // billboards only, applied when > 0.001 (entity field 32)
    Vec4 colour{1.0f, 1.0f, 1.0f, 1.0f};
    // Object `min` / `max`: x y s t (s, t in the original texture convention, t = 0 at the
    // bottom of the picture).
    float minX = -8.0f, minY = -8.0f, minS = 0.0f, minT = 0.0f;
    float maxX = 8.0f, maxY = 8.0f, maxS = 1.0f, maxT = 1.0f;
    // Object `frames cols rows`; cols = 0 disables the atlas. `frame` is entity field 33.
    int frameCols = 0, frameRows = 0;
    int frame = 0;
    const Texture2D* texture = nullptr; // null draws nothing
    DecalBlend blend = DecalBlend::Add;
    bool noDepthTest = false;  // RF_NODEPTHTEST
    bool noDepthWrite = false; // RF_NODEPTHWRITE
};

// One sprite's four corners in draw order (x0,y0), (x1,y0), (x1,y1), (x0,y1), with texture
// coordinates in the engine convention (v = 1 - t).
struct SpriteQuad {
    Vec3 pos[4];
    Vec2 uv[4];
};

// Camera right and up as the original derives them: rows 0 and 1 of the view rotation.
void spriteBillboardAxes(const Mat4& view, Vec3& right, Vec3& up);

// Texture coordinates of the quad, applying the frame grid when `frameCols` > 0:
//   du = 1/cols, dv = 1/rows, s0 = (f mod cols) du, t0 = 1 - dv - (f div cols) dv.
void spriteUvs(const SpriteInstance& sprite, Vec2 uv[4]);

// Geometry of one sprite for a camera with billboard axes `right` and `up`.
SpriteQuad buildSpriteQuad(const SpriteInstance& sprite, const Vec3& right, const Vec3& up);

struct SpriteViewParams {
    Mat4 view;
    Mat4 projection;
    Vec3 fogColour;
    float fogStart = 1.0e9f; // linear fog; set fogEnd <= fogStart to disable
    float fogEnd = 1.0e9f;
};

class SpriteRenderer {
public:
    SpriteRenderer();
    ~SpriteRenderer();
    SpriteRenderer(const SpriteRenderer&) = delete;
    SpriteRenderer& operator=(const SpriteRenderer&) = delete;

    bool init(std::string* error);

    // Draws the sprites in the given order (no sorting). Face culling stays on, like the
    // original; the corner order faces the viewer.
    void draw(const SpriteInstance* sprites, size_t count, const SpriteViewParams& params);
    int lastSpriteCount() const { return lastSprites_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    int lastSprites_ = 0;
};

} // namespace as3d
