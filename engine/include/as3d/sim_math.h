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
// Any NaN result is the canonical quiet NaN (the sign and payload of an arithmetic NaN differ
// between x86, ARM and wasm). The renderer and the UI may use them too (as3d/math.h does).
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

// FNV-1a 64 of the bits of every function above over the known-answer inputs (built in
// sim_math.cpp, checked by apps/tests/sim_math_test.cpp): one number to compare between
// platforms (as3d_tests -tc="sim_math hash" prints it, the web page's ?simmath=1 logs
// AS3D_SIMMATH hash=..., section 14 item 3).
u64 simMathCheckHash();

// The known-answer table behind simMathCheckHash, for the test: function f in
// [0, kSimMathCheckFunctions) in the order of the declarations above (simSinf first, simLog
// last), input i in [0, kSimMathCheckInputs).
constexpr int kSimMathCheckFunctions = 18;
constexpr int kSimMathCheckInputs = 200;
const char* simMathCheckName(int f);
bool simMathCheckIsFloat(int f);  // float arguments and result
bool simMathCheckTwoArgs(int f);  // atan2 (y, x) and pow (x, y)
// The arguments of input i (b is 0 for one-argument functions; float values for float ones).
void simMathCheckArgs(int f, int i, double& a, double& b);
// The bits of the result (a float's in the low 32 bits).
u64 simMathCheckBits(int f, int i);
// FNV-1a 64 of one function's results (the hash above runs over all of them in order).
u64 simMathCheckHashOf(int f);

} // namespace as3d
