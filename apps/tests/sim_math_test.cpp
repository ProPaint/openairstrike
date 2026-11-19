// The simulation's own transcendental functions (as3d/sim_math.h, docs/spec/issues/
// 180-online-coop.md 7.2): the known-answer table every platform must reproduce bit for bit,
// agreement with the C library to one ulp, canonical NaNs, and no libm call left in the
// simulation sources.
#include "doctest.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "as3d/sim_math.h"

using namespace as3d;

namespace {

// Computed once on the desktop (x86-64, glibc, gcc) and checked bit for bit against musl
// 1.2.6's own functions on four million inputs each before committing. The web build logs the
// same overall hash for ?simmath=1 (AS3D_SIMMATH hash=...), and the final test of the online
// work compares it between the desktop, the web and Android (section 14).
constexpr u64 kExpectedHash = 0xc38a83fb2d80117bull;
constexpr u64 kExpectedHashOf[kSimMathCheckFunctions] = {
    0x7d1ee735c9dfa579ull, // simSinf
    0xffa93e7f590c9706ull, // simCosf
    0xf3aa1b3f21f550bbull, // simTanf
    0xaab6b57aac4548dcull, // simAtanf
    0x64bfc4dd85e9b4e4ull, // simAtan2f
    0x025e1eff1196f930ull, // simAcosf
    0x0e28e9a2962eeb8full, // simAsinf
    0xd75b30272ed423eeull, // simExpf
    0xbf00c456b559d44full, // simLogf
    0xdddbdb07fc08712dull, // simPowf
    0x4afc21ef5938ae43ull, // simSin
    0xda0c565a05be6f3eull, // simCos
    0x953c020a43456c9bull, // simTan
    0xc09a2a95b23ad1fbull, // simAtan
    0x50887b3ef167442bull, // simAtan2
    0x5af9f97b17dcb5afull, // simAcos
    0x8ca1881e9851c917ull, // simExp
    0x5c42f89e5e72347bull, // simLog
};

// A few results in full, so that a failure shows where it starts: (function, input, bits).
struct Spot {
    int f, i;
    u64 bits;
};
constexpr Spot kSpots[] = {
    {0, 0, 0xbd09b8b8ull}, {0, 37, 0x3f7fa655ull}, {0, 88, 0xbe7b3fb5ull}, {0, 131, 0xbf150f25ull}, {0, 175, 0x3f800000ull}, {0, 197, 0x3d126d55ull},
    {1, 0, 0x3f7fdaf2ull}, {1, 37, 0x3d5630feull}, {1, 88, 0x3f782cb2ull}, {1, 131, 0x3eb3eac5ull}, {1, 175, 0xb4f9990full}, {1, 197, 0xbf7fd61cull},
    {2, 0, 0xbd09ccabull}, {2, 37, 0x4198c67full}, {2, 88, 0xbe8195eaull}, {2, 131, 0xc0a8f644ull}, {2, 175, 0xca03487eull}, {2, 197, 0xbd12854full},
    {3, 0, 0xbfc68095ull}, {3, 37, 0xbfc24c67ull}, {3, 88, 0x3fc3b871ull}, {3, 131, 0xbfc34b06ull}, {3, 175, 0xbeed6338ull}, {3, 197, 0xbfc90fdaull},
    {4, 0, 0xc016cbe4ull}, {4, 37, 0xc00079a0ull}, {4, 88, 0x4023f117ull}, {4, 131, 0xbf1d4216ull}, {4, 175, 0xbd4ca12dull}, {4, 197, 0xbfc90fdbull},
    {5, 0, 0x40490fdaull}, {5, 37, 0x3ffab33cull}, {5, 88, 0x3f89200eull}, {5, 131, 0x3f4a7ddeull}, {5, 175, 0x40060a92ull}, {5, 197, 0x7fc00000ull},
    {6, 0, 0xbfc90fdbull}, {6, 37, 0xbec68d85ull}, {6, 88, 0x3effbf33ull}, {6, 131, 0xbf048033ull}, {6, 175, 0xbf060a92ull}, {6, 197, 0x7fc00000ull},
    {7, 0, 0x310da433ull}, {7, 37, 0x3a0821c4ull}, {7, 88, 0x466217f5ull}, {7, 131, 0x328001f8ull}, {7, 175, 0x3f1b4598ull}, {7, 197, 0x0ull},
    {8, 0, 0xc0dd0c55ull}, {8, 37, 0x4100ac96ull}, {8, 88, 0x410e8970ull}, {8, 131, 0x40fdf429ull}, {8, 175, 0x7fc00000ull}, {8, 197, 0x7fc00000ull},
    {9, 0, 0x4cbebc21ull}, {9, 37, 0x3ee58714ull}, {9, 88, 0x3b3c571eull}, {9, 131, 0x3d7cfb36ull}, {9, 175, 0x3a800000ull}, {9, 197, 0x7f800000ull},
    {10, 0, 0xbfa1370a4069cd7aull}, {10, 37, 0x3feff4caa4178b53ull}, {10, 88, 0xbfcf67f62f2ca330ull}, {10, 131, 0xbfdc0cbc80980255ull}, {10, 175, 0x3ff0000000000000ull}, {10, 197, 0x3fa24daa9c527e96ull},
    {11, 0, 0x3feffb5e3d66faeeull}, {11, 37, 0x3faac6273ceb2091ull}, {11, 88, 0x3fef05963f42988dull}, {11, 131, 0xbfe50ee79513ca23ull}, {11, 175, 0xbcd34f272993d141ull}, {11, 197, 0xbfeffac3841b3da7ull},
    {12, 0, 0xbfa1398883b0437full}, {12, 37, 0x403318ca796fc611ull}, {12, 88, 0xbfd032bd0454ab6dull}, {12, 131, 0x3f90588bbfc118c4ull}, {12, 175, 0xc30a8410087262e4ull}, {12, 197, 0xbfa250a9d503313dull},
    {13, 0, 0xbff8d0129acd6d1cull}, {13, 37, 0xbff8498ceec4f33eull}, {13, 88, 0x3ff8770e197b7658ull}, {13, 131, 0x3ff859ede2fc6a86ull}, {13, 175, 0xbfddac670561bb4full}, {13, 197, 0xbff921fb54442d18ull},
    {14, 0, 0xc002d97c7f3321d2ull}, {14, 37, 0xc0000f340b81a4f0ull}, {14, 88, 0x40047e22d5adafa1ull}, {14, 131, 0x3ff0cac3e88e37d8ull}, {14, 175, 0xbfa9942597929f27ull}, {14, 197, 0xbff921fb54442d18ull},
    {15, 0, 0x400921fb54442d18ull}, {15, 37, 0x3fff566779a17707ull}, {15, 88, 0x3ff12401c0c3e792ull}, {15, 131, 0x3ffaceb84888d3ebull}, {15, 175, 0x4000c152382d7366ull}, {15, 197, 0x7ff8000000000000ull},
    {16, 0, 0x3e21b48655f37267ull}, {16, 37, 0x3f41043845103fe6ull}, {16, 88, 0x40cc42fe5b7f19c6ull}, {16, 131, 0x40d62ce75b78e2b8ull}, {16, 175, 0x3fe368b2fc6f960aull}, {16, 197, 0x0ull},
    {17, 0, 0xc01ba18a998fffa0ull}, {17, 37, 0x40201592b86ed2e7ull}, {17, 88, 0x4021d12df44dbc33ull}, {17, 131, 0x40216e83c82038dfull}, {17, 175, 0x7ff8000000000000ull}, {17, 197, 0x7ff8000000000000ull},
};

u32 floatBits(float v) {
    u32 b;
    std::memcpy(&b, &v, 4);
    return b;
}
u64 doubleBits(double v) {
    u64 b;
    std::memcpy(&b, &v, 8);
    return b;
}

// The C library's value of function f (the order of as3d/sim_math.h).
double reference(int f, double a, double b) {
    const float fa = static_cast<float>(a), fb = static_cast<float>(b);
    switch (f) {
    case 0: return std::sin(fa);
    case 1: return std::cos(fa);
    case 2: return std::tan(fa);
    case 3: return std::atan(fa);
    case 4: return std::atan2(fa, fb);
    case 5: return std::acos(fa);
    case 6: return std::asin(fa);
    case 7: return std::exp(fa);
    case 8: return std::log(fa);
    case 9: return std::pow(fa, fb);
    case 10: return std::sin(a);
    case 11: return std::cos(a);
    case 12: return std::tan(a);
    case 13: return std::atan(a);
    case 14: return std::atan2(a, b);
    case 15: return std::acos(a);
    case 16: return std::exp(a);
    default: return std::log(a);
    }
}

double ours(int f, double a, double b) {
    const float fa = static_cast<float>(a), fb = static_cast<float>(b);
    switch (f) {
    case 0: return simSinf(fa);
    case 1: return simCosf(fa);
    case 2: return simTanf(fa);
    case 3: return simAtanf(fa);
    case 4: return simAtan2f(fa, fb);
    case 5: return simAcosf(fa);
    case 6: return simAsinf(fa);
    case 7: return simExpf(fa);
    case 8: return simLogf(fa);
    case 9: return simPowf(fa, fb);
    case 10: return simSin(a);
    case 11: return simCos(a);
    case 12: return simTan(a);
    case 13: return simAtan(a);
    case 14: return simAtan2(a, b);
    case 15: return simAcos(a);
    case 16: return simExp(a);
    default: return simLog(a);
    }
}

// Distance in ulps between two results of function f (both NaN: 0; one NaN: huge).
long long ulps(int f, double x, double y) {
    if (x != x || y != y) return (x != x && y != y) ? 0 : (1ll << 62);
    if (simMathCheckIsFloat(f)) {
        const long long a = floatBits(static_cast<float>(x)), b = floatBits(static_cast<float>(y));
        // Map the sign-magnitude bits onto a line.
        const long long la = a & 0x80000000 ? -(a & 0x7fffffff) : a, lb = b & 0x80000000 ? -(b & 0x7fffffff) : b;
        return la > lb ? la - lb : lb - la;
    }
    const u64 ua = doubleBits(x), ub = doubleBits(y);
    const long long la = ua >> 63 ? -static_cast<long long>(ua & 0x7fffffffffffffffull) : static_cast<long long>(ua);
    const long long lb = ub >> 63 ? -static_cast<long long>(ub & 0x7fffffffffffffffull) : static_cast<long long>(ub);
    return la > lb ? la - lb : lb - la;
}

} // namespace

TEST_CASE("sim_math hash: the known-answer table is the committed one") {
    char hex[32];
    std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(simMathCheckHash()));
    MESSAGE("simMathCheckHash = " << hex); // AS3D_SIMMATH hash= on the web
    for (int f = 0; f < kSimMathCheckFunctions; ++f) {
        INFO(simMathCheckName(f));
        CHECK(simMathCheckHashOf(f) == kExpectedHashOf[f]);
    }
    for (const Spot& s : kSpots) {
        double a, b;
        simMathCheckArgs(s.f, s.i, a, b);
        INFO(simMathCheckName(s.f) << " input " << s.i << " (" << a << ", " << b << ")");
        CHECK(simMathCheckBits(s.f, s.i) == s.bits);
    }
    CHECK(simMathCheckHash() == kExpectedHash);
}

TEST_CASE("sim_math: within one ulp of the C library") {
    // The known-answer inputs, then a sweep of the ranges the game uses and beyond.
    for (int f = 0; f < kSimMathCheckFunctions; ++f) {
        INFO(simMathCheckName(f));
        int worse = 0;
        for (int i = 0; i < kSimMathCheckInputs; ++i) {
            double a, b;
            simMathCheckArgs(f, i, a, b);
            if (ulps(f, ours(f, a, b), reference(f, a, b)) > 1) {
                if (worse++ < 3) MESSAGE("input " << i << ": " << a << ", " << b);
            }
        }
        u32 s = 12345u + static_cast<u32>(f);
        for (int n = 0; n < 20000; ++n) {
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            const double u = static_cast<double>(s) / 4294967296.0;
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            const double v = static_cast<double>(s) / 4294967296.0;
            double a = (u * 2.0 - 1.0) * (n % 3 == 0 ? 1.0 : n % 3 == 1 ? 50.0 : 4000.0);
            double b = (v * 2.0 - 1.0) * 8.0;
            if (f == 8 || f == 17) a = std::fabs(a) + 1e-6;   // log
            if (f == 9) a = std::fabs(a) * 0.01 + 1e-3;        // pow: x in (0, 40], y in [-8, 8]
            if (ulps(f, ours(f, a, b), reference(f, a, b)) > 1) {
                if (worse++ < 3) MESSAGE("sweep: " << a << ", " << b);
            }
        }
        CHECK(worse == 0);
    }
}

TEST_CASE("sim_math: NaN results are the canonical quiet NaN, special values as C") {
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(floatBits(simSinf(inf)) == 0x7fc00000u);
    CHECK(floatBits(simCosf(-inf)) == 0x7fc00000u);
    CHECK(floatBits(simAcosf(2.0f)) == 0x7fc00000u);
    CHECK(floatBits(simAsinf(-1.5f)) == 0x7fc00000u);
    CHECK(floatBits(simLogf(-1.0f)) == 0x7fc00000u);
    CHECK(floatBits(simPowf(-2.0f, 0.5f)) == 0x7fc00000u);
    CHECK(floatBits(simAtan2f(std::nanf(""), 1.0f)) == 0x7fc00000u);
    CHECK(doubleBits(simSin(-std::numeric_limits<double>::infinity())) == 0x7ff8000000000000ull);
    CHECK(doubleBits(simAcos(-3.0)) == 0x7ff8000000000000ull);
    CHECK(doubleBits(simLog(-0.5)) == 0x7ff8000000000000ull);
    CHECK(simSinf(0.0f) == 0.0f);
    CHECK(floatBits(simSinf(-0.0f)) == 0x80000000u);
    CHECK(simCosf(0.0f) == 1.0f);
    CHECK(simAtan2f(0.0f, -1.0f) == simAtan2f(0.0f, -1.0f));
    CHECK(simAtan2(1.0, 0.0) == std::atan2(1.0, 0.0));
    CHECK(simExpf(0.0f) == 1.0f);
    CHECK(simExpf(-inf) == 0.0f);
    CHECK(simExpf(100.0f) == inf);
    CHECK(simLogf(0.0f) == -inf);
    CHECK(simLog(1.0) == 0.0);
    CHECK(simPowf(2.0f, 10.0f) == 1024.0f);
    CHECK(simPowf(-2.0f, 3.0f) == -8.0f);
    CHECK(simPowf(0.0f, -1.0f) == inf);
    // musl's acosf(-1) is 2 * pio2_hi (0x40490fda), one ulp under the float nearest pi.
    CHECK(floatBits(simAcosf(-1.0f)) == 0x40490fdau);
}

TEST_CASE("sim_math: no libm transcendental call left in the simulation sources") {
    namespace fs = std::filesystem;
    const std::string root = AS3D_REPO_ROOT;
    std::vector<std::string> files = {root + "/engine/include/as3d/math.h", root + "/engine/include/as3d/vec.h"};
    for (const char* dir : {"/engine/src/game", "/engine/src/script", "/engine/src/core"}) {
        for (const auto& entry : fs::recursive_directory_iterator(root + dir)) {
            const std::string p = entry.path().string();
            if (entry.is_regular_file() && (p.size() > 4 && (p.substr(p.size() - 4) == ".cpp" || p.substr(p.size() - 2) == ".h")) &&
                entry.path().filename() != "sim_math.cpp")
                files.push_back(p);
        }
    }
    REQUIRE(files.size() > 20);
    // std::sin(...) and friends, and the C names with or without the f suffix (std::log, not
    // the engine's as3d::log for messages: a bare log( is the logger).
    const std::regex call(R"((std::|::|\b)(sin|cos|tan|atan|atan2|acos|asin|exp|pow|sinf|cosf|tanf|atanf|atan2f|acosf|asinf|expf|logf|powf|sinh|cosh|tanh|exp2|log2|log10|hypot|cbrt)\s*\(|std::log\s*\()");
    int found = 0;
    for (const std::string& path : files) {
        std::ifstream in(path);
        REQUIRE(in.good());
        std::string line;
        int n = 0;
        bool inBlock = false;
        while (std::getline(in, line)) {
            ++n;
            // Drop comments (block comments are rare here; a line starting one is skipped).
            if (inBlock) {
                const size_t end = line.find("*/");
                if (end == std::string::npos) continue;
                line = line.substr(end + 2);
                inBlock = false;
            }
            const size_t start = line.find("/*");
            if (start != std::string::npos && line.find("*/", start) == std::string::npos) {
                line = line.substr(0, start);
                inBlock = true;
            }
            const size_t c = line.find("//");
            if (c != std::string::npos) line = line.substr(0, c);
            std::smatch m;
            if (std::regex_search(line, m, call)) {
                // A member or a sim_math name (simSinf) is not a C library call.
                const size_t at = static_cast<size_t>(m.position(0));
                if (at > 0 && (line[at - 1] == '.' || line[at - 1] == '>')) continue;
                ++found;
                MESSAGE(path << ":" << n << ": " << line);
            }
        }
    }
    CHECK(found == 0);
}
