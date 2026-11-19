// The simulation's transcendental functions (as3d/sim_math.h, docs/spec/issues/180-online-coop.md
// 7.2). T0 stub: forwards to the C library, so nothing changes yet. T2 replaces the bodies with
// the musl port (MIT notice here) and nothing else in this file then uses <cmath>.
#include <cmath>

#include "as3d/sim_math.h"

namespace as3d {

float simSinf(float x) { return std::sin(x); }
float simCosf(float x) { return std::cos(x); }
float simTanf(float x) { return std::tan(x); }
float simAtanf(float x) { return std::atan(x); }
float simAtan2f(float y, float x) { return std::atan2(y, x); }
float simAcosf(float x) { return std::acos(x); }
float simAsinf(float x) { return std::asin(x); }
float simExpf(float x) { return std::exp(x); }
float simLogf(float x) { return std::log(x); }
float simPowf(float x, float y) { return std::pow(x, y); }

double simSin(double x) { return std::sin(x); }
double simCos(double x) { return std::cos(x); }
double simTan(double x) { return std::tan(x); }
double simAtan(double x) { return std::atan(x); }
double simAtan2(double y, double x) { return std::atan2(y, x); }
double simAcos(double x) { return std::acos(x); }
double simExp(double x) { return std::exp(x); }
double simLog(double x) { return std::log(x); }

u64 simMathCheckHash() { return 0; }

} // namespace as3d
