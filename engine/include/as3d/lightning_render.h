// Lightning bolts (docs/spec/render-pipeline.md 7.1): the records the `Lightning` builtin
// queues (World::lightningBolts()), each drawn as two crossed 16-unit-wide quads from the
// caller to the struck enemy with `gfx\lightning2.tga` scrolling along the bolt, additive,
// no depth test, no culling, black fog. The geometry is GL free; the renderer needs a
// current GLES 3.0 context.
#pragma once

#include <memory>
#include <string>

#include "as3d/gfx.h"
#include "as3d/math.h"

namespace as3d {

// 0x41e130: the axis of the smallest |component| of the unit vector `d`, made orthogonal to
// it and normalised.
Vec3 perpendicularVector(const Vec3& d);

struct LightningVertex {
    Vec3 pos;
    Vec2 uv; // engine convention (v = 1 - original t); s already includes the time scroll
};

// The two quads of one bolt, 4 corners each: start - 8p, end - 8p, end + 8p, start + 8p for
// p = p1 then p2, s from time*3 at the start to time*3 + len/96 at the end, t 0 to 1 across.
// Returns false (and leaves `out` untouched) for a bolt of zero length.
bool buildLightningQuads(const Vec3& start, const Vec3& end, float time, LightningVertex out[8]);

struct LightningViewParams {
    Mat4 view;
    Mat4 projection;
    float fogStart = 1.0e9f; // linear fog towards black; fogEnd <= fogStart disables it
    float fogEnd = 1.0e9f;
    float time = 0.0f;       // world time, seconds
};

class LightningRenderer {
public:
    LightningRenderer();
    ~LightningRenderer();
    LightningRenderer(const LightningRenderer&) = delete;
    LightningRenderer& operator=(const LightningRenderer&) = delete;

    bool init(std::string* error);
    // `texture` is gfx\lightning2.tga (mipmapped, repeat). Draws `count` bolts given as
    // start/end pairs; leaves blending off, depth test and culling on.
    void draw(const Vec3* starts, const Vec3* ends, size_t count, const Texture2D* texture,
              const LightningViewParams& params);
    int lastBoltCount() const { return lastBolts_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    int lastBolts_ = 0;
};

} // namespace as3d
