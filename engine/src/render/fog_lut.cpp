#include "as3d/fog_lut.h"

#include <algorithm>
#include <cmath>

namespace as3d {

float fogDepthFromEye(float n, float f, float z, FogDepthMap map) {
    if (map == FogDepthMap::WBuffer) return std::clamp((f - z) / (f - n), 0.0f, 1.0f);
    if (z <= 0.0f) return 1.0f;
    return std::clamp(n * (f - z) / (z * (f - n)), 0.0f, 1.0f);
}

float fogEyeFromDepth(float n, float f, float d, FogDepthMap map) {
    if (map == FogDepthMap::WBuffer) return f - d * (f - n);
    return n * f / (d * (f - n) + n);
}

float fogFactorAtEyeDepth(float fogStart, float fogEnd, float z) {
    float range = std::max(fogEnd - fogStart, 0.0001f);
    return std::clamp((fogEnd - z) / range, 0.0f, 1.0f);
}

void buildLinearFogLut(float nearPlane, float farPlane, float fogStart, float fogEnd, float out[256],
                       FogDepthMap map) {
    float prev = 0.0f;
    for (int i = 0; i <= 128; ++i) {
        float z = fogEyeFromDepth(nearPlane, farPlane, static_cast<float>(i) / 128.0f, map);
        float v = fogFactorAtEyeDepth(fogStart, fogEnd, z);
        if (i < 128) out[i] = v;
        if (i > 0) out[127 + i] = v - prev;
        prev = v;
    }
}

float fogLutLookup(const float lut[256], float depth) {
    float index = std::clamp(depth, 0.0f, 1.0f) * 128.0f;
    int i = std::min(static_cast<int>(std::floor(index)), 127);
    float frac = index - static_cast<float>(i);
    return std::clamp(lut[i] + lut[128 + i] * frac, 0.0f, 1.0f);
}

} // namespace as3d
