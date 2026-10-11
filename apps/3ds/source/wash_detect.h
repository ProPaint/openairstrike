// The water-wash detector's platform-neutral part (docs/3ds-port.md "Stereo CPU path (round 3,
// R3.2)", "Wash detector"): it judges the picture, not the GPU registers. Every wash_every frames
// ctr_frame.cpp reads back the top targets' depth buffers; for a grid of sample pixels (denser in
// the lower half of the screen, where the near terrain is) this file computes, from the eye's
// matrices, the first game's water plane (planeZ) and the terrain's height field:
//   * where the pixel's ray meets the water plane, and its eye depth (the W-buffer depth is linear
//     in the eye depth e = -z_eye: depth = (far - e) / (far - near), 1 at the near plane);
//   * whether the ray meets the terrain clearly above the water and clearly in front of the plane
//     (a coarse height-field march, refined by bisection); only those samples are judged.
// A judged sample whose GPU depth is at the water plane or behind it (the water's own depth, or the
// cleared far depth: the terrain wrote nothing there) is washed. No 3DS API: the desktop test
// (apps/tests/wash_detect_test.cpp) compiles this file.
#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "as3d/math.h"

namespace ctrapp {

struct WashView {
    as3d::Mat4 clip;   // the eye's GL clip matrix, no tilt: eyeProjection * eyeViewMatrix (x right, y up)
    as3d::Mat4 view;   // the eye's view matrix (eyeViewMatrix): eye depth e = -(view * p).z
    float nearPlane = 4.0f, farPlane = 2000.0f;
    int width = 400, height = 240; // the upright screen
};

struct WashScene {
    float planeZ = 0.0f;           // the water plane's height this frame
    float mapW = 0.0f, mapH = 0.0f; // the terrain's extent (world units); outside it nothing is judged
    float topZ = 0.0f;             // the highest terrain point (the march starts where the ray comes down to it)
    std::function<float(float, float)> height; // the terrain height at (x, y)
};

// One judged sample: the pixel (upright, y down) and the eye depths the GPU should have there
// (the terrain) and would have if the terrain's depth were missing (the water plane).
struct WashProbe {
    int x = 0, y = 0;
    float terrainDepth = 0.0f;
    float waterDepth = 0.0f;
};

// The sample pixels: columns 20..380 step 40 (10), rows 20..120 step 20 (6) and 130..230 step 10
// (11): 170 samples, two thirds of them in the lower half.
std::vector<std::pair<int, int>> washSamplePixels(int width, int height);

// The cheap half, every frame: the water plane's point under pixel (x, y) and its eye depth. False
// when the ray never comes down to the plane inside the map.
struct WashRay {
    as3d::Vec3 a, b;  // the ray's points on the near and far planes
    float tWater = 0.0f;
    float waterDepth = 0.0f;
};
// The inverse of v.clip (once per eye and frame): false when singular.
bool washInverse(const WashView& v, as3d::Mat4* inverse);
bool washRay(const WashView& v, const as3d::Mat4& inverse, const WashScene& s, int x, int y, WashRay* out);
// The expensive half: does the ray meet the terrain clearly above the water and in front of the plane?
// The terrain's eye depth in *terrainDepth.
bool washTerrainFront(const WashView& v, const WashScene& s, const WashRay& r, float* terrainDepth);

// The samples of `pixels` where the terrain must be in front of the water plane (both halves).
std::vector<WashProbe> washProbes(const WashView& v, const WashScene& s, const std::vector<std::pair<int, int>>& pixels);

// The eye depth of a D24 W-buffer value (0 = the cleared far depth, 0xFFFFFF = the near plane).
float wBufferEyeDepth(uint32_t depth24, float nearPlane, float farPlane);

// Washed: the GPU depth is at the water plane or behind it.
bool washedSample(const WashProbe& p, float gpuEyeDepth);
// The GPU depth agrees with the terrain (or something nearer, a model): the calibration count.
bool terrainSample(const WashProbe& p, float gpuEyeDepth);

struct WashResult {
    int judged = 0;      // samples where the terrain must be in front
    int washed = 0;
    int agree = 0;       // samples whose depth matched the terrain within the tolerance
    int nearer = 0;      // samples nearer than the terrain (models, effects in front)
    double terrainErr = 0.0; // sum of |gpu - terrain| over the agreeing samples (eye units)
    int rowMin = 0, rowMax = -1, colMin = 0, colMax = -1; // the washed samples' extent
    void add(const WashProbe& p, float gpuEyeDepth);
};

// Washed samples in one read-back that make it a washed frame.
constexpr int kWashMinSamples = 3;

} // namespace ctrapp
