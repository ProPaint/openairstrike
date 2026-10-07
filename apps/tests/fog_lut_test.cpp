// Tests for the PICA fog LUT builder.
#include "doctest.h"

#include "as3d/fog_lut.h"

#include <cmath>

using namespace as3d;

TEST_CASE("fog lut: depth mapping round trip") {
    for (float z : {4.0f, 10.0f, 100.0f, 1999.0f}) {
        float d = fogDepthFromEye(4.0f, 2000.0f, z);
        CHECK(d >= 0.0f);
        CHECK(d <= 1.0f);
        CHECK(fogEyeFromDepth(4.0f, 2000.0f, d) == doctest::Approx(z).epsilon(1e-3));
    }
    CHECK(fogDepthFromEye(4.0f, 2000.0f, 4.0f) == doctest::Approx(0.0f).epsilon(1e-6));
    CHECK(fogDepthFromEye(4.0f, 2000.0f, 2000.0f) == doctest::Approx(1.0f));
}

TEST_CASE("fog lut: entries equal the analytic linear fog at their depth") {
    const float n = 4, f = 2000, s = 100, e = 1500;
    float lut[128];
    buildLinearFogLut(n, f, s, e, lut);
    CHECK(lut[0] == 1.0f);
    CHECK(lut[127] == 0.0f);
    int checked = 0;
    for (int i = 2; i < 128; i += 2) {
        float z = fogEyeFromDepth(n, f, i / 128.0f);
        CHECK(std::fabs(lut[i] - fogFactorAtEyeDepth(s, e, z)) <= 1.0f / 255.0f);
        ++checked;
    }
    CHECK(checked >= 50);
    for (int i = 1; i < 128; ++i) CHECK(lut[i] <= lut[i - 1]);
}

TEST_CASE("fog lut: interpolated lookup, gentle fog range") {
    // A small far/near ratio, where 128 linear segments follow the fog ramp closely.
    const float n = 1, f = 20, s = 2, e = 18;
    float lut[128];
    buildLinearFogLut(n, f, s, e, lut);
    int checked = 0;
    for (int k = 0; k < 50; ++k) {
        float z = 1.5f + k * 0.35f; // 1.5 .. 18.65
        float pos = fogDepthFromEye(n, f, z) * 128.0f;
        int i = static_cast<int>(pos);
        float t = pos - static_cast<float>(i);
        float v = i >= 127 ? lut[127] : lut[i] + (lut[i + 1] - lut[i]) * t;
        CHECK(std::fabs(v - fogFactorAtEyeDepth(s, e, z)) <= 1.0f / 255.0f);
        ++checked;
    }
    CHECK(checked == 50);
}
