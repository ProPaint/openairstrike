// The simulation's own transcendental functions (docs/spec/issues/180-online-coop.md 7.2):
// glibc, Bionic and Emscripten's musl differ in the last bit of sin, cos, atan2 ... for some
// inputs, and a one-ulp difference in an angle becomes a different collision frames later, so
// online peers on different platforms would desync. These are ported from musl (MIT, FreeBSD
// msun lineage): plain IEEE double arithmetic, identical on x86-64, arm64, wasm and the 3DS once
// contraction is off (-ffp-contract=off engine-wide, 7.1).
//
// Every libm call of these functions in the simulation (engine/src/game, engine/src/script,
// engine/src/core, as3d/math.h, as3d/vec.h) goes through here; sqrt, floor, ceil, trunc and fmod
// are exact on every platform and stay std:: (apps/tests/sim_math_test.cpp enforces both).
// T2 makes them real; until then they forward to std::.
#pragma once

#include "as3d/core.h"

namespace as3d {

float simSinf(float x);
float simCosf(float x);
float simTanf(float x);
float simAtanf(float x);
float simAtan2f(float y, float x);
float simAcosf(float x);
float simAsinf(float x);
float simExpf(float x);
float simLogf(float x);
float simPowf(float x, float y);

double simSin(double x);
double simCos(double x);
double simTan(double x);
double simAtan(double x);
double simAtan2(double y, double x);
double simAcos(double x);
double simExp(double x);
double simLog(double x);

// FNV-1a 64 of the bits of every function above over the known-answer inputs of
// apps/tests/sim_math_test.cpp: one number to compare between platforms (as3d_tests
// -tc="sim_math hash" prints it, the web page's ?simmath=1 logs AS3D_SIMMATH hash=...,
// section 14 item 3). 0 until T2.
u64 simMathCheckHash();

} // namespace as3d
