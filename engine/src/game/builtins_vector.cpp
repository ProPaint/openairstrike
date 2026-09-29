// Vector and axis builtins (docs/spec/rcsl-builtins-table.md, "Vectors"). Arguments are
// pointers to 3 floats that may alias; results are computed and stored component by
// component, as specified.
#include <cmath>

#include "builtins_common.h"

namespace as3d {
namespace builtins {

namespace {

void bVecCopy(BuiltinArgs& a, void*) {
    u32 src = a.bits(0), dst = a.bits(1);
    for (int k = 0; k < 3; ++k) {
        float v;
        if (!readComponent(a, src, k, v) || !writeComponent(a, dst, k, v)) return;
    }
}

void vecBinary(BuiltinArgs& a, bool subtract) {
    u32 pa = a.bits(0), pb = a.bits(1), po = a.bits(2);
    for (int k = 0; k < 3; ++k) {
        float x, y;
        if (!readComponent(a, pa, k, x) || !readComponent(a, pb, k, y)) return;
        if (!writeComponent(a, po, k, subtract ? x - y : x + y)) return;
    }
}
void bVecAdd(BuiltinArgs& a, void*) { vecBinary(a, false); }
void bVecSub(BuiltinArgs& a, void*) { vecBinary(a, true); }

void bVecMa(BuiltinArgs& a, void*) {
    u32 pa = a.bits(0), pb = a.bits(2), po = a.bits(3);
    float s = a.f32(1);
    for (int k = 0; k < 3; ++k) {
        float x, y;
        if (!readComponent(a, pa, k, x) || !readComponent(a, pb, k, y)) return;
        float prod = y * s;
        if (!writeComponent(a, po, k, x + prod)) return;
    }
}

void bVecLength(BuiltinArgs& a, void*) {
    float v[3];
    if (!vecArg(a, 0, v)) return;
    a.setReturnFloat(std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]));
}

void bVecNorm(BuiltinArgs& a, void*) {
    float v[3];
    if (!vecArg(a, 0, v)) return;
    float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len != 0.0f) {
        for (float& c : v) c /= len;
        if (!a.writeVec3(0, v)) {
            a.fail("access to unmapped address");
            return;
        }
    }
    a.setReturnFloat(len);
}

void bVecScale(BuiltinArgs& a, void*) {
    float v[3];
    if (!vecArg(a, 0, v)) return;
    float s = a.f32(1);
    for (float& c : v) c *= s;
    if (!a.writeVec3(0, v)) a.fail("access to unmapped address");
}

void bVecSetlen(BuiltinArgs& a, void*) {
    float v[3];
    if (!vecArg(a, 0, v)) return;
    float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len == 0.0f) return;
    float k = a.f32(1) / len;
    for (float& c : v) c *= k;
    if (!a.writeVec3(0, v)) a.fail("access to unmapped address");
}

void bVecToyaw(BuiltinArgs& a, void*) {
    float v[3];
    if (!vecArg(a, 0, v)) return;
    if (v[0] == 0.0f) { // QUIRK
        a.setReturnFloat(-90.0f);
        return;
    }
    float yaw = std::atan2(v[1], v[0]) * kRadToDeg;
    if (yaw < 0.0f) yaw += 360.0f;
    a.setReturnFloat(yaw - 90.0f);
}

void bVecToangles(BuiltinArgs& a, void*) {
    float v[3], out[3];
    if (!vecArg(a, 0, v)) return;
    vecToAngles(v[0], v[1], v[2], out);
    if (!a.writeVec3(1, out)) a.fail("access to unmapped address");
}

void writeAxis(BuiltinArgs& a, u32 base, const float m[9]) {
    for (int k = 0; k < 9; ++k) {
        if (!writeComponent(a, base, k, m[k])) return;
    }
}

void bClearAxis(BuiltinArgs& a, void*) {
    const float id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    writeAxis(a, a.bits(0), id);
}

void bAnglesToAxis(BuiltinArgs& a, void*) {
    float ang[3];
    if (!vecArg(a, 0, ang)) return;
    float m[9];
    anglesToAxisRows(ang[0], ang[1], ang[2], m);
    writeAxis(a, a.bits(1), m);
}

const BuiltinDesc kTable[] = {
    {"vec_copy", 2, bVecCopy, nullptr, BuiltinStatus::Implemented},
    {"vec_add", 3, bVecAdd, nullptr, BuiltinStatus::Implemented},
    {"vec_sub", 3, bVecSub, nullptr, BuiltinStatus::Implemented},
    {"vec_ma", 4, bVecMa, nullptr, BuiltinStatus::Implemented},
    {"vec_length", 1, bVecLength, nullptr, BuiltinStatus::Implemented},
    {"vec_norm", 1, bVecNorm, nullptr, BuiltinStatus::Implemented},
    {"vec_scale", 2, bVecScale, nullptr, BuiltinStatus::Implemented},
    {"vec_setlen", 2, bVecSetlen, nullptr, BuiltinStatus::Implemented},
    {"vec_toyaw", 1, bVecToyaw, nullptr, BuiltinStatus::Implemented},
    {"vec_toangles", 2, bVecToangles, nullptr, BuiltinStatus::Implemented},
    {"ClearAxis", 1, bClearAxis, nullptr, BuiltinStatus::Implemented},
    {"AnglesToAxis", 2, bAnglesToAxis, nullptr, BuiltinStatus::Implemented},
};

} // namespace

Family vectorBuiltins() { return {kTable, sizeof kTable / sizeof kTable[0]}; }

} // namespace builtins
} // namespace as3d
