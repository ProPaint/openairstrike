// Math and random builtins (docs/spec/rcsl-builtins-table.md, "Math").
#include <cmath>

#include "builtins_common.h"

namespace as3d {
namespace builtins {

namespace {

constexpr double kPiOver180 = 3.14159265358979323846 / 180.0;

void bDebug(BuiltinArgs&, void*) {}

// Our engine's deliberate deviation: xorshift32 (as3d::Rng) instead of MSVC rand()
// (docs/spec/README.md). Computed exactly like the reference mock host.
void bRandom(BuiltinArgs& a, void*) { a.setReturnFloat(worldOf(a).rng().uniform()); }

void bCrandom(BuiltinArgs& a, void*) {
    float u = worldOf(a).rng().uniform();
    float v = static_cast<float>(static_cast<double>(u) * 2.0);
    a.setReturnFloat(static_cast<float>(static_cast<double>(v) - 1.0));
}

void bSin(BuiltinArgs& a, void*) { a.setReturnFloat(static_cast<float>(std::sin(a.f32(0) * kPiOver180))); }
void bCos(BuiltinArgs& a, void*) { a.setReturnFloat(static_cast<float>(std::cos(a.f32(0) * kPiOver180))); }
void bTan(BuiltinArgs& a, void*) { a.setReturnFloat(static_cast<float>(std::tan(a.f32(0) * kPiOver180))); }
// QUIRK: the input is converted as if it were degrees, the output stays in radians.
void bAtan(BuiltinArgs& a, void*) { a.setReturnFloat(static_cast<float>(std::atan(a.f32(0) * kPiOver180))); }
void bAbs(BuiltinArgs& a, void*) { a.setReturnFloat(std::fabs(a.f32(0))); }

void bMin(BuiltinArgs& a, void*) {
    float x = a.f32(0), y = a.f32(1);
    a.setReturnFloat((y <= x || std::isnan(x) || std::isnan(y)) ? y : x);
}
void bMax(BuiltinArgs& a, void*) {
    float x = a.f32(0), y = a.f32(1);
    a.setReturnFloat((y >= x || std::isnan(x) || std::isnan(y)) ? y : x);
}
void bLerp(BuiltinArgs& a, void*) {
    float t = a.f32(0), x = a.f32(1), y = a.f32(2);
    a.setReturnFloat((y - x) * t + x);
}

void bSetFlag(BuiltinArgs& a, void*) {
    a.setReturnFloat(static_cast<float>(intArg(a, 0) | intArg(a, 1)));
}
void bClearFlag(BuiltinArgs& a, void*) {
    a.setReturnFloat(static_cast<float>(intArg(a, 0) & ~intArg(a, 1)));
}

const BuiltinDesc kTable[] = {
    {"debug", 0, bDebug, nullptr, BuiltinStatus::Implemented},
    {"random", 0, bRandom, nullptr, BuiltinStatus::Implemented},
    {"crandom", 0, bCrandom, nullptr, BuiltinStatus::Implemented},
    {"sin", 1, bSin, nullptr, BuiltinStatus::Implemented},
    {"cos", 1, bCos, nullptr, BuiltinStatus::Implemented},
    {"tan", 1, bTan, nullptr, BuiltinStatus::Implemented},
    {"atan", 1, bAtan, nullptr, BuiltinStatus::Implemented},
    {"abs", 1, bAbs, nullptr, BuiltinStatus::Implemented},
    {"min", 2, bMin, nullptr, BuiltinStatus::Implemented},
    {"max", 2, bMax, nullptr, BuiltinStatus::Implemented},
    {"lerp", 3, bLerp, nullptr, BuiltinStatus::Implemented},
    {"SetFlag", 2, bSetFlag, nullptr, BuiltinStatus::Implemented},
    {"ClearFlag", 2, bClearFlag, nullptr, BuiltinStatus::Implemented},
};

} // namespace

Family mathBuiltins() { return {kTable, sizeof kTable / sizeof kTable[0]}; }

} // namespace builtins
} // namespace as3d
