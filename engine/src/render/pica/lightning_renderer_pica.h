// Lightning bolts on the PICA (3DS only; the neutral half is buildLightningBolts in
// engine/src/render/lightning_build.cpp). docs/3ds-port.md "M3.3".
//
// prepare() (outside frames, once per frame) turns the scene's bolts into 12 vertices each (two
// crossed quads, v scrolling with the time) and one additive run (no depth test or write, no
// culling, fog towards black); at most kMaxBolts are kept. draw() (per eye) copies them into the
// frame arena at the first call of an arena frame and draws the run through drawFlatRuns (white
// vertex colour, texture x colour; the texture is the ResourceCache's gfx\lightning2.tga).
#pragma once

#include <cstddef>
#include <vector>

#include "as3d/draw_run.h"
#include "as3d/math.h"
#include "as3d/texture.h"
#include "shader_set.h"

namespace as3d::pica {

constexpr int kMaxLightningBoltsPica = 64; // 288 B each in the arena

struct LightningPicaStats {
    int bolts = 0;          // built this frame
    int boltsDropped = 0;   // over the cap
    int vertices = 0;
    int draws = 0;          // the last draw()
    size_t arenaBytes = 0;
    float time = 0.0f;      // the scroll time of the last prepare
};
const LightningPicaStats& lightningPicaStats();

// Test hook (apps/3ds water_test.cpp): bolts drawn instead of the world's (mission 1 has no
// Lightning() call with the bot). Null: the world's.
struct TestBolts {
    std::vector<Vec3> starts, ends;
};
void setTestBolts(const TestBolts* bolts);
const TestBolts* testBolts();

class LightningPass {
public:
    void reset() { verts_.clear(); runs_.clear(); }
    void prepare(const Vec3* starts, const Vec3* ends, size_t count, float time, const Texture2D* texture);
    bool active() const { return !runs_.empty(); }
    // One eye; leaves blend off, depth test + write on, cull back. Returns the draw calls.
    int draw(const Mat4& view, const Mat4& projection, const Vec3& fogColour, float fogStart, float fogEnd);

private:
    std::vector<FlatVertex> verts_;
    std::vector<DrawRun> runs_;
    const FlatVertex* arena_ = nullptr;
    unsigned arenaFrame_ = ~0u;
};

} // namespace as3d::pica
