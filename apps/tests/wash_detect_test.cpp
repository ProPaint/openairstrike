// The water-wash detector's neutral part (apps/3ds/source/wash_detect.h): which samples are judged,
// the W-buffer depth conversion and the washed / terrain classification, on a synthetic scene.
#include "doctest.h"

#include <cmath>

#include "../3ds/source/wash_detect.h"

using namespace ctrapp;
using namespace as3d;

namespace {

// A camera 300 units above the water, looking down the map at 45 degrees; the terrain is a plateau
// 40 units high on x < 400, the water (z 0) elsewhere.
struct Scene {
    WashView v;
    WashScene s;
    Scene() {
        v.view = lookAt(Vec3{400, 0, 300}, Vec3{400, 300, 0}, Vec3{0, 0, 1});
        v.clip = perspective(60.0f, 400.0f / 240.0f, 4.0f, 2000.0f) * v.view;
        s.planeZ = 0.0f;
        s.mapW = 800.0f;
        s.mapH = 4000.0f;
        s.topZ = 40.0f;
        s.height = [](float x, float) { return x < 400.0f ? 40.0f : -10.0f; };
    }
};

float depth24Of(float eye, float n, float f) { return std::round((f - eye) / (f - n) * 16777215.0f); }

} // namespace

TEST_CASE("wash detector: the sample grid") {
    const auto px = washSamplePixels(400, 240);
    CHECK(px.size() == 170);
    int lower = 0;
    for (const auto& p : px) lower += p.second >= 120 ? 1 : 0;
    CHECK(lower > 100);
}

TEST_CASE("wash detector: only samples over raised terrain are judged") {
    Scene sc;
    const auto probes = washProbes(sc.v, sc.s, washSamplePixels(400, 240));
    REQUIRE(!probes.empty());
    for (const WashProbe& p : probes) {
        CHECK(p.x < 200); // the plateau is the left half of the screen
        CHECK(p.terrainDepth < p.waterDepth);
    }
    // Everything at the water level: nothing to judge.
    sc.s.height = [](float, float) { return -10.0f; };
    CHECK(washProbes(sc.v, sc.s, washSamplePixels(400, 240)).empty());
}

TEST_CASE("wash detector: W-buffer depth and classification") {
    CHECK(wBufferEyeDepth(0xFFFFFF, 4.0f, 2000.0f) == doctest::Approx(4.0f));
    CHECK(wBufferEyeDepth(0, 4.0f, 2000.0f) == doctest::Approx(2000.0f));
    Scene sc;
    const auto probes = washProbes(sc.v, sc.s, washSamplePixels(400, 240));
    REQUIRE(probes.size() >= 3);
    WashResult clean, washed, cleared;
    for (const WashProbe& p : probes) {
        const float t = wBufferEyeDepth(static_cast<uint32_t>(depth24Of(p.terrainDepth, 4, 2000)), 4, 2000);
        const float w = wBufferEyeDepth(static_cast<uint32_t>(depth24Of(p.waterDepth, 4, 2000)), 4, 2000);
        clean.add(p, t);
        washed.add(p, w);
        cleared.add(p, wBufferEyeDepth(0, 4, 2000));
    }
    CHECK(clean.washed == 0);
    CHECK(clean.agree == clean.judged);
    CHECK(washed.washed == washed.judged);
    CHECK(cleared.washed == cleared.judged);
    CHECK(washed.rowMin <= washed.rowMax);
}
