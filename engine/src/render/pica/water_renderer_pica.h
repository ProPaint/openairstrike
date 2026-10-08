// The first game's water on the PICA (3DS only): the flat scrolling plane of the GL WaterRenderer
// (engine/src/render/gles/water_renderer.cpp, docs/spec/hmap.md "Water"). docs/3ds-port.md "M3.5".
//
// The plane is one quad per 320 world units of map length (x 0..width*40), texture coordinates
// (0,0)-(width/160, 2) per quad, at z = planeZ(time) = level + sin(t), with the uv offset
// (0.4 sin t, 0.4 sin t/2) and the colour sun*sun_dir.z + ambient (computeWaterParams), alpha =
// the style's water alpha, alpha blend, depth test and write on, no culling, fog towards the
// level colour. The GL shader adds the plane z and the uv offset per frame as uniforms; the PICA
// flat shader has neither, so the few quads (about 13 in mission 1: 78 vertices, 1.9 KB) are
// written into the frame arena by the first eye of an arena frame with z and uv already moved,
// the colour and alpha in the vertex colour (TEV: texture x vertex colour; the texture is opaque
// RGB565, so the alpha is the style's, as the GL's which ignores the texture alpha). Each eye
// culls the quads against its own matrix as the GL does (conservative clip-space test of the 4
// corners) and draws the visible ones, adjacent quads as one draw call.
//
// The texture is created at beginLevel (after the terrain's, so it goes to VRAM while the 4.2 MB
// budget has room, else linear memory: waterPicaStats().textureVram). Sequel water (the wave grid,
// two textures) is M5.1: build() logs a TODO and leaves the pass inactive for it.
#pragma once

#include <cstddef>
#include <string>

#include "as3d/math.h"
#include "as3d/terrain.h"
#include "as3d/texture.h"
#include "as3d/vfs.h"
#include "as3d/water_build.h"

namespace as3d::pica {

struct WaterPicaStats {
    bool active = false;
    int quads = 0;              // the plane's quads (all)
    int textureWidth = 0, textureHeight = 0;
    size_t textureBytes = 0;
    bool textureVram = false;
    int missingTextures = 0;
    // The last prepared frame.
    float planeZ = 0.0f;
    Vec2 uvOffset;              // (0.4 sin t, 0.4 sin t/2), the scroll
    // The last draw() (one eye).
    int quadsDrawn = 0;
    int draws = 0;
    size_t arenaBytes = 0;      // vertex bytes written to the frame arena this frame
};
const WaterPicaStats& waterPicaStats();

class WaterPass {
public:
    // Level start (outside frames). `sequelGrid`: the wave-grid water of the sequels (M5.1, not
    // drawn yet). Returns false only on a hard error.
    bool build(const Terrain& terrain, Vfs& vfs, bool sequelGrid, std::string* error);
    void reset();
    bool active() const { return active_; }
    // Per frame, outside frames: the plane's values at world time `timeSeconds`.
    void prepare(float timeSeconds);
    // One eye; viewProj = projection * view as the other passes. Returns the draw calls; leaves
    // blend off, depth test + write on, cull back.
    int draw(const Mat4& view, const Mat4& projection, const Vec3& fogColour, float fogStart, float fogEnd);

private:
    bool active_ = false;
    Texture2D texture_;
    int quads_ = 0;
    float width_ = 0.0f;
    float level_ = 0.0f;
    TerrainStyle style_;
    WaterParams params_;
    float time_ = 0.0f;
};

} // namespace as3d::pica
