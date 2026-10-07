// Tests for the PICA fog LUT builder (as3d/fog_lut.h): the depth convention of citro3d's
// Mtx_PerspTilt + C3D_DepthMap(true, -1, 0) and the table layout of FogLut_FromArray.
#include "doctest.h"

#include "as3d/fog_lut.h"

#include <cmath>
#include <initializer_list>

using namespace as3d;

namespace {

// citro3d's Mtx_PerspTilt clip z and w for eye distance z (camera looking down -z, so the
// eye-space z is -z): r[2].z = n/(n-f), r[2].w = n f/(n-f), r[3].z = -1.
float citroNdcZ(float n, float f, float z) {
    float clipZ = n / (n - f) * -z + n * f / (n - f);
    return clipZ / z;
}

// citro3d's FogLut_CalcZ (fog.h).
float fogLutCalcZ(float depth, float n, float f) { return f * n / (depth * (f - n) + n); }

} // namespace

TEST_CASE("fog lut: the z-buffer depth is -z_ndc of Mtx_PerspTilt, 1 at near, 0 at far") {
    const float n = 4.0f, f = 2000.0f;
    for (float z : {4.0f, 10.0f, 100.0f, 410.0f, 1999.0f}) {
        float d = fogDepthFromEye(n, f, z);
        CHECK(d == doctest::Approx(-citroNdcZ(n, f, z)).epsilon(1e-4));
        CHECK(fogEyeFromDepth(n, f, d) == doctest::Approx(z).epsilon(1e-3));
        CHECK(fogEyeFromDepth(n, f, d) == doctest::Approx(fogLutCalcZ(d, n, f)).epsilon(1e-5));
    }
    CHECK(fogDepthFromEye(n, f, n) == doctest::Approx(1.0f));
    CHECK(fogDepthFromEye(n, f, f) == doctest::Approx(0.0f).epsilon(1e-6));
}

TEST_CASE("fog lut: the w-buffer depth is linear, 1 at near, 0 at far") {
    const float n = 4.0f, f = 2000.0f;
    CHECK(fogDepthFromEye(n, f, n, FogDepthMap::WBuffer) == doctest::Approx(1.0f));
    CHECK(fogDepthFromEye(n, f, f, FogDepthMap::WBuffer) == doctest::Approx(0.0f));
    CHECK(fogDepthFromEye(n, f, 1002.0f, FogDepthMap::WBuffer) == doctest::Approx(0.5f));
    for (float z : {10.0f, 500.0f, 1500.0f})
        CHECK(fogEyeFromDepth(n, f, fogDepthFromEye(n, f, z, FogDepthMap::WBuffer), FogDepthMap::WBuffer) ==
              doctest::Approx(z).epsilon(1e-4));
}

TEST_CASE("fog lut: entries and differences in FogLut_FromArray layout") {
    const float n = 4, f = 2000, s = 100, e = 1500;
    float lut[256];
    buildLinearFogLut(n, f, s, e, lut);
    CHECK(lut[0] == 0.0f);              // depth 0 = far plane, beyond the fog end
    CHECK(lut[127] > 0.9f);             // depth 127/128: z ~ 4.1, no fog
    for (int i = 0; i < 128; ++i) {
        float z = fogEyeFromDepth(n, f, i / 128.0f);
        CHECK(lut[i] == doctest::Approx(fogFactorAtEyeDepth(s, e, z)));
        float next = i < 127 ? lut[i + 1] : fogFactorAtEyeDepth(s, e, fogEyeFromDepth(n, f, 1.0f));
        CHECK(lut[128 + i] == doctest::Approx(next - lut[i]));
    }
    for (int i = 1; i < 128; ++i) CHECK(lut[i] >= lut[i - 1]); // fog fades out towards the near plane
    // The lookup at an entry's own depth returns the entry.
    CHECK(fogLutLookup(lut, 3.0f / 128.0f) == doctest::Approx(lut[3]));
    CHECK(fogLutLookup(lut, 1.0f) == doctest::Approx(1.0f));
}

TEST_CASE("fog lut: interpolated lookup follows the linear fog (gentle z-buffer, game-size w-buffer)") {
    {
        // A small far/near ratio, where 128 segments of the z-buffer depth follow the ramp.
        const float n = 1, f = 20, s = 2, e = 12;
        float lut[256];
        buildLinearFogLut(n, f, s, e, lut);
        for (int k = 0; k < 50; ++k) {
            float z = 1.5f + k * 0.2f; // 1.5 .. 11.3
            CHECK(std::fabs(fogLutLookup(lut, fogDepthFromEye(n, f, z)) - fogFactorAtEyeDepth(s, e, z)) <=
                  1.0f / 255.0f);
        }
    }
    {
        // The game's planes (near 4, far 2000): the w-buffer table is exact for a linear ramp.
        const float n = 4, f = 2000, s = 100, e = 1500;
        float lut[256];
        buildLinearFogLut(n, f, s, e, lut, FogDepthMap::WBuffer);
        float worst = 0;
        for (int k = 0; k <= 100; ++k) {
            float z = 5.0f + k * 19.9f;
            float d = fogDepthFromEye(n, f, z, FogDepthMap::WBuffer);
            worst = std::fmax(worst, std::fabs(fogLutLookup(lut, d) - fogFactorAtEyeDepth(s, e, z)));
        }
        CHECK(worst <= 1.0f / 255.0f);
        // The z-buffer table with the same planes is coarse in the fog range (documented in
        // fog_lut.h): measure, so a change of convention shows up here.
        buildLinearFogLut(n, f, s, e, lut);
        worst = 0;
        for (int k = 0; k <= 100; ++k) {
            float z = 5.0f + k * 19.9f;
            worst = std::fmax(worst, std::fabs(fogLutLookup(lut, fogDepthFromEye(n, f, z)) - fogFactorAtEyeDepth(s, e, z)));
        }
        CHECK(worst > 0.1f);
        CHECK(worst < 0.3f);
    }
}
