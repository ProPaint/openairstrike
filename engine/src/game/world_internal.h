// Private helpers shared by the world, host, collision and builtin sources.
#pragma once

#include <cmath>
#include <cstring>

#include "as3d/core.h"
#include "as3d/vec.h"

namespace as3d {

inline u32 fbits(float f) {
    u32 b;
    std::memcpy(&b, &f, 4);
    return b;
}
inline float bitsf(u32 b) {
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

// Float to int as the executable does (helper 0x440870): truncation toward zero; NaN,
// infinities and values outside the int32 range give INT32_MIN (rcsl-vm.md "Values").
inline i32 ftol(float v) {
    if (!(v > -2147483649.0f && v < 2147483648.0f)) return static_cast<i32>(0x80000000u);
    return static_cast<i32>(v);
}

// Round to nearest (even), used where the builtin table says the engine rounds.
inline i32 roundToInt(float v) {
    if (!(v > -2147483649.0f && v < 2147483648.0f)) return static_cast<i32>(0x80000000u);
    return static_cast<i32>(std::nearbyint(v));
}

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;

// AnglesToAxis (engine-behaviour.md 4.3, rcsl-builtins-table.md): rows forward, left, up.
inline void anglesToAxisRows(float ax, float ay, float az, float m[9]) {
    float sa = std::sin(ax * kDegToRad), ca = std::cos(ax * kDegToRad);
    float sb = std::sin(ay * kDegToRad), cb = std::cos(ay * kDegToRad);
    float sc = std::sin(az * kDegToRad), cc = std::cos(az * kDegToRad);
    m[0] = cb * cc;
    m[1] = cb * sc;
    m[2] = sb;
    m[3] = sa * sb * cc - ca * sc;
    m[4] = sa * sb * sc + ca * cc;
    m[5] = -sa * cb;
    m[6] = -sa * sc - ca * sb * cc;
    m[7] = sa * cc - ca * sb * sc;
    m[8] = ca * cb;
}

// Wraps an angle difference into (-180, 180].
inline float wrap180(float d) {
    d = std::fmod(d, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d <= -180.0f) d += 360.0f;
    return d;
}

// vec_toangles (rcsl-builtins-table.md): (-pitch, 0, yaw - 90) in degrees.
inline void vecToAngles(float x, float y, float z, float out[3]) {
    float yaw, pitch;
    if (x != 0.0f || y != 0.0f) {
        yaw = std::atan2(y, x) * kRadToDeg;
        if (yaw < 0.0f) yaw += 360.0f;
        pitch = std::atan2(z, std::sqrt(x * x + y * y)) * kRadToDeg;
        if (pitch < 0.0f) pitch += 360.0f;
    } else {
        yaw = 0.0f;
        pitch = z > 0.0f ? 90.0f : 270.0f;
    }
    out[0] = -pitch;
    out[1] = 0.0f;
    out[2] = yaw - 90.0f;
}

} // namespace as3d
