#include "as3d/fog_lut.h"

#include <algorithm>

namespace as3d {

float fogDepthFromEye(float n, float f, float z) {
    if (z <= 0.0f) return 0.0f;
    return std::clamp(f / (f - n) * (1.0f - n / z), 0.0f, 1.0f);
}

float fogEyeFromDepth(float n, float f, float d) { return n * f / (f - d * (f - n)); }

float fogFactorAtEyeDepth(float fogStart, float fogEnd, float z) {
    float range = std::max(fogEnd - fogStart, 0.0001f);
    return std::clamp((fogEnd - z) / range, 0.0f, 1.0f);
}

void buildLinearFogLut(float nearPlane, float farPlane, float fogStart, float fogEnd, float out[128]) {
    for (int i = 0; i < 128; ++i) {
        float z = fogEyeFromDepth(nearPlane, farPlane, static_cast<float>(i) / 128.0f);
        out[i] = fogFactorAtEyeDepth(fogStart, fogEnd, z);
    }
}

} // namespace as3d
