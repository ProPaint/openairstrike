// The simulation's transcendental functions (as3d/sim_math.h, docs/spec/issues/180-online-coop.md
// 7.2): a port of musl 1.2.6's src/math (as shipped in Emscripten's system/lib/libc/musl), so
// that the desktop (glibc), Android (Bionic), the web (wasm) and the 3DS (newlib) compute the
// same bits. Plain IEEE double and float arithmetic, integer bit manipulation, std::sqrt and
// std::floor (both exact by IEEE 754): no other libm call, no long double, no FMA (the whole
// engine is built with -ffp-contract=off, and log()'s __FP_FAST_FMA branch is not ported, so
// arm64 takes the same path as x86-64).
//
// Deviations from musl, all deliberate and none affecting a finite non-NaN result:
//  - floating-point exceptions are not raised (FORCE_EVAL and the barriers are dropped);
//  - every NaN result is the canonical quiet NaN 0x7fc00000 / 0x7ff8000000000000: the sign and
//    payload of a NaN produced by arithmetic differ between x86, ARM and wasm;
//  - __rem_pio2_large keeps the 66 entries of 2/pi that double precision needs (musl adds more
//    for long double on some targets only; they are never read for float and double).
//
// Licences. musl is MIT:
//   Copyright (c) 2005-2020 Rich Felker, et al.
//   Permission is hereby granted, free of charge, to any person obtaining a copy of this
//   software and associated documentation files (the "Software"), to deal in the Software
//   without restriction, including without limitation the rights to use, copy, modify, merge,
//   publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
//   to whom the Software is furnished to do so, subject to the following conditions:
//   The above copyright notice and this permission notice shall be included in all copies or
//   substantial portions of the Software.
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
//   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
//   PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
//   FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
//   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//   DEALINGS IN THE SOFTWARE.
// The trigonometric and inverse trigonometric functions come from FreeBSD's msun:
//   Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
//   Developed at SunSoft, a Sun Microsystems, Inc. business.
//   Permission to use, copy, modify, and distribute this software is freely granted, provided
//   that this notice is preserved.
//   (k_tan.c: Copyright 2004 Sun Microsystems, Inc. All Rights Reserved; same permission.)
//   (The float variants: Conversion to float by Ian Lance Taylor, Cygnus Support; optimized
//   by Bruce D. Evans.)
// exp, log, expf, logf, powf and their tables are ARM's optimized-routines:
//   Copyright (c) 2017-2018, Arm Limited. SPDX-License-Identifier: MIT
#include <cmath>
#include <cstring>

#include "as3d/sim_math.h"

namespace as3d {

namespace {

// ---- libm.h -------------------------------------------------------------------------------

inline u32 asuint(float f) {
    u32 i;
    std::memcpy(&i, &f, 4);
    return i;
}
inline float asfloat(u32 i) {
    float f;
    std::memcpy(&f, &i, 4);
    return f;
}
inline u64 asuint64(double f) {
    u64 i;
    std::memcpy(&i, &f, 8);
    return i;
}
inline double asdouble(u64 i) {
    double f;
    std::memcpy(&f, &i, 8);
    return f;
}
inline u32 highWord(double d) { return static_cast<u32>(asuint64(d) >> 32); }
inline u32 lowWord(double d) { return static_cast<u32>(asuint64(d)); }
inline double withLowWord(double d, u32 lo) { return asdouble((asuint64(d) & 0xffffffff00000000ull) | lo); }

// The canonical quiet NaNs (functions, not constants: no dynamic initialisation order to mind).
inline float qnanf() { return asfloat(0x7fc00000u); }
inline double qnan() { return asdouble(0x7ff8000000000000ull); }
inline float canon(float x) { return x != x ? qnanf() : x; }
inline double canon(double x) { return x != x ? qnan() : x; }

const double kPiOver2 = 1.57079632679489661923; // M_PI_2

// scalbn.c
double scalbn(double x, int n) {
    double y = x;
    if (n > 1023) {
        y *= 0x1p1023;
        n -= 1023;
        if (n > 1023) {
            y *= 0x1p1023;
            n -= 1023;
            if (n > 1023) n = 1023;
        }
    } else if (n < -1022) {
        // make sure final n < -53 to avoid double rounding in the subnormal range
        y *= 0x1p-1022 * 0x1p53;
        n += 1022 - 53;
        if (n < -1022) {
            y *= 0x1p-1022 * 0x1p53;
            n += 1022 - 53;
            if (n < -1022) n = -1022;
        }
    }
    return y * asdouble(static_cast<u64>(0x3ff + n) << 52);
}

// ---- Tables (exp_data.c, log_data.c, exp2f_data.c, logf_data.c, powf_data.c,
// __rem_pio2_large.c) --------------------------------------------------------------------------

struct LogEntry {
    double a, b; // invc, logc (tab) or chi, clo (tab2)
};

const u64 kExpTab[] = {
    0x0ull, 0x3ff0000000000000ull,
    0x3c9b3b4f1a88bf6eull, 0x3feff63da9fb3335ull,
    0xbc7160139cd8dc5dull, 0x3fefec9a3e778061ull,
    0xbc905e7a108766d1ull, 0x3fefe315e86e7f85ull,
    0x3c8cd2523567f613ull, 0x3fefd9b0d3158574ull,
    0xbc8bce8023f98efaull, 0x3fefd06b29ddf6deull,
    0x3c60f74e61e6c861ull, 0x3fefc74518759bc8ull,
    0x3c90a3e45b33d399ull, 0x3fefbe3ecac6f383ull,
    0x3c979aa65d837b6dull, 0x3fefb5586cf9890full,
    0x3c8eb51a92fdeffcull, 0x3fefac922b7247f7ull,
    0x3c3ebe3d702f9cd1ull, 0x3fefa3ec32d3d1a2ull,
    0xbc6a033489906e0bull, 0x3fef9b66affed31bull,
    0xbc9556522a2fbd0eull, 0x3fef9301d0125b51ull,
    0xbc5080ef8c4eea55ull, 0x3fef8abdc06c31ccull,
    0xbc91c923b9d5f416ull, 0x3fef829aaea92de0ull,
    0x3c80d3e3e95c55afull, 0x3fef7a98c8a58e51ull,
    0xbc801b15eaa59348ull, 0x3fef72b83c7d517bull,
    0xbc8f1ff055de323dull, 0x3fef6af9388c8deaull,
    0x3c8b898c3f1353bfull, 0x3fef635beb6fcb75ull,
    0xbc96d99c7611eb26ull, 0x3fef5be084045cd4ull,
    0x3c9aecf73e3a2f60ull, 0x3fef54873168b9aaull,
    0xbc8fe782cb86389dull, 0x3fef4d5022fcd91dull,
    0x3c8a6f4144a6c38dull, 0x3fef463b88628cd6ull,
    0x3c807a05b0e4047dull, 0x3fef3f49917ddc96ull,
    0x3c968efde3a8a894ull, 0x3fef387a6e756238ull,
    0x3c875e18f274487dull, 0x3fef31ce4fb2a63full,
    0x3c80472b981fe7f2ull, 0x3fef2b4565e27cddull,
    0xbc96b87b3f71085eull, 0x3fef24dfe1f56381ull,
    0x3c82f7e16d09ab31ull, 0x3fef1e9df51fdee1ull,
    0xbc3d219b1a6fbffaull, 0x3fef187fd0dad990ull,
    0x3c8b3782720c0ab4ull, 0x3fef1285a6e4030bull,
    0x3c6e149289cecb8full, 0x3fef0cafa93e2f56ull,
    0x3c834d754db0abb6ull, 0x3fef06fe0a31b715ull,
    0x3c864201e2ac744cull, 0x3fef0170fc4cd831ull,
    0x3c8fdd395dd3f84aull, 0x3feefc08b26416ffull,
    0xbc86a3803b8e5b04ull, 0x3feef6c55f929ff1ull,
    0xbc924aedcc4b5068ull, 0x3feef1a7373aa9cbull,
    0xbc9907f81b512d8eull, 0x3feeecae6d05d866ull,
    0xbc71d1e83e9436d2ull, 0x3feee7db34e59ff7ull,
    0xbc991919b3ce1b15ull, 0x3feee32dc313a8e5ull,
    0x3c859f48a72a4c6dull, 0x3feedea64c123422ull,
    0xbc9312607a28698aull, 0x3feeda4504ac801cull,
    0xbc58a78f4817895bull, 0x3feed60a21f72e2aull,
    0xbc7c2c9b67499a1bull, 0x3feed1f5d950a897ull,
    0x3c4363ed60c2ac11ull, 0x3feece086061892dull,
    0x3c9666093b0664efull, 0x3feeca41ed1d0057ull,
    0x3c6ecce1daa10379ull, 0x3feec6a2b5c13cd0ull,
    0x3c93ff8e3f0f1230ull, 0x3feec32af0d7d3deull,
    0x3c7690cebb7aafb0ull, 0x3feebfdad5362a27ull,
    0x3c931dbdeb54e077ull, 0x3feebcb299fddd0dull,
    0xbc8f94340071a38eull, 0x3feeb9b2769d2ca7ull,
    0xbc87deccdc93a349ull, 0x3feeb6daa2cf6642ull,
    0xbc78dec6bd0f385full, 0x3feeb42b569d4f82ull,
    0xbc861246ec7b5cf6ull, 0x3feeb1a4ca5d920full,
    0x3c93350518fdd78eull, 0x3feeaf4736b527daull,
    0x3c7b98b72f8a9b05ull, 0x3feead12d497c7fdull,
    0x3c9063e1e21c5409ull, 0x3feeab07dd485429ull,
    0x3c34c7855019c6eaull, 0x3feea9268a5946b7ull,
    0x3c9432e62b64c035ull, 0x3feea76f15ad2148ull,
    0xbc8ce44a6199769full, 0x3feea5e1b976dc09ull,
    0xbc8c33c53bef4da8ull, 0x3feea47eb03a5585ull,
    0xbc845378892be9aeull, 0x3feea34634ccc320ull,
    0xbc93cedd78565858ull, 0x3feea23882552225ull,
    0x3c5710aa807e1964ull, 0x3feea155d44ca973ull,
    0xbc93b3efbf5e2228ull, 0x3feea09e667f3bcdull,
    0xbc6a12ad8734b982ull, 0x3feea012750bdabfull,
    0xbc6367efb86da9eeull, 0x3fee9fb23c651a2full,
    0xbc80dc3d54e08851ull, 0x3fee9f7df9519484ull,
    0xbc781f647e5a3ecfull, 0x3fee9f75e8ec5f74ull,
    0xbc86ee4ac08b7db0ull, 0x3fee9f9a48a58174ull,
    0xbc8619321e55e68aull, 0x3fee9feb564267c9ull,
    0x3c909ccb5e09d4d3ull, 0x3feea0694fde5d3full,
    0xbc7b32dcb94da51dull, 0x3feea11473eb0187ull,
    0x3c94ecfd5467c06bull, 0x3feea1ed0130c132ull,
    0x3c65ebe1abd66c55ull, 0x3feea2f336cf4e62ull,
    0xbc88a1c52fb3cf42ull, 0x3feea427543e1a12ull,
    0xbc9369b6f13b3734ull, 0x3feea589994cce13ull,
    0xbc805e843a19ff1eull, 0x3feea71a4623c7adull,
    0xbc94d450d872576eull, 0x3feea8d99b4492edull,
    0x3c90ad675b0e8a00ull, 0x3feeaac7d98a6699ull,
    0x3c8db72fc1f0eab4ull, 0x3feeace5422aa0dbull,
    0xbc65b6609cc5e7ffull, 0x3feeaf3216b5448cull,
    0x3c7bf68359f35f44ull, 0x3feeb1ae99157736ull,
    0xbc93091fa71e3d83ull, 0x3feeb45b0b91ffc6ull,
    0xbc5da9b88b6c1e29ull, 0x3feeb737b0cdc5e5ull,
    0xbc6c23f97c90b959ull, 0x3feeba44cbc8520full,
    0xbc92434322f4f9aaull, 0x3feebd829fde4e50ull,
    0xbc85ca6cd7668e4bull, 0x3feec0f170ca07baull,
    0x3c71affc2b91ce27ull, 0x3feec49182a3f090ull,
    0x3c6dd235e10a73bbull, 0x3feec86319e32323ull,
    0xbc87c50422622263ull, 0x3feecc667b5de565ull,
    0x3c8b1c86e3e231d5ull, 0x3feed09bec4a2d33ull,
    0xbc91bbd1d3bcbb15ull, 0x3feed503b23e255dull,
    0x3c90cc319cee31d2ull, 0x3feed99e1330b358ull,
    0x3c8469846e735ab3ull, 0x3feede6b5579fdbfull,
    0xbc82dfcd978e9db4ull, 0x3feee36bbfd3f37aull,
    0x3c8c1a7792cb3387ull, 0x3feee89f995ad3adull,
    0xbc907b8f4ad1d9faull, 0x3feeee07298db666ull,
    0xbc55c3d956dcaebaull, 0x3feef3a2b84f15fbull,
    0xbc90a40e3da6f640ull, 0x3feef9728de5593aull,
    0xbc68d6f438ad9334ull, 0x3feeff76f2fb5e47ull,
    0xbc91eee26b588a35ull, 0x3fef05b030a1064aull,
    0x3c74ffd70a5fddcdull, 0x3fef0c1e904bc1d2ull,
    0xbc91bdfbfa9298acull, 0x3fef12c25bd71e09ull,
    0x3c736eae30af0cb3ull, 0x3fef199bdd85529cull,
    0x3c8ee3325c9ffd94ull, 0x3fef20ab5fffd07aull,
    0x3c84e08fd10959acull, 0x3fef27f12e57d14bull,
    0x3c63cdaf384e1a67ull, 0x3fef2f6d9406e7b5ull,
    0x3c676b2c6c921968ull, 0x3fef3720dcef9069ull,
    0xbc808a1883ccb5d2ull, 0x3fef3f0b555dc3faull,
    0xbc8fad5d3ffffa6full, 0x3fef472d4a07897cull,
    0xbc900dae3875a949ull, 0x3fef4f87080d89f2ull,
    0x3c74a385a63d07a7ull, 0x3fef5818dcfba487ull,
    0xbc82919e2040220full, 0x3fef60e316c98398ull,
    0x3c8e5a50d5c192acull, 0x3fef69e603db3285ull,
    0x3c843a59ac016b4bull, 0x3fef7321f301b460ull,
    0xbc82d52107b43e1full, 0x3fef7c97337b9b5full,
    0xbc892ab93b470dc9ull, 0x3fef864614f5a129ull,
    0x3c74b604603a88d3ull, 0x3fef902ee78b3ff6ull,
    0x3c83c5ec519d7271ull, 0x3fef9a51fbc74c83ull,
    0xbc8ff7128fd391f0ull, 0x3fefa4afa2a490daull,
    0xbc8dae98e223747dull, 0x3fefaf482d8e67f1ull,
    0x3c8ec3bc41aa2008ull, 0x3fefba1bee615a27ull,
    0x3c842b94c3a9eb32ull, 0x3fefc52b376bba97ull,
    0x3c8a64a931d185eeull, 0x3fefd0765b6e4540ull,
    0xbc8e37bae43be3edull, 0x3fefdbfdad9cbe14ull,
    0x3c77893b4d91cd9dull, 0x3fefe7c1819e90d8ull,
    0x3c5305c14160cc89ull, 0x3feff3c22b8f71f1ull,
};
const LogEntry kLogTab[] = {
    {0x1.734f0c3e0de9fp+0, -0x1.7cc7f79e69000p-2},
    {0x1.713786a2ce91fp+0, -0x1.76feec20d0000p-2},
    {0x1.6f26008fab5a0p+0, -0x1.713e31351e000p-2},
    {0x1.6d1a61f138c7dp+0, -0x1.6b85b38287800p-2},
    {0x1.6b1490bc5b4d1p+0, -0x1.65d5590807800p-2},
    {0x1.69147332f0cbap+0, -0x1.602d076180000p-2},
    {0x1.6719f18224223p+0, -0x1.5a8ca86909000p-2},
    {0x1.6524f99a51ed9p+0, -0x1.54f4356035000p-2},
    {0x1.63356aa8f24c4p+0, -0x1.4f637c36b4000p-2},
    {0x1.614b36b9ddc14p+0, -0x1.49da7fda85000p-2},
    {0x1.5f66452c65c4cp+0, -0x1.445923989a800p-2},
    {0x1.5d867b5912c4fp+0, -0x1.3edf439b0b800p-2},
    {0x1.5babccb5b90dep+0, -0x1.396ce448f7000p-2},
    {0x1.59d61f2d91a78p+0, -0x1.3401e17bda000p-2},
    {0x1.5805612465687p+0, -0x1.2e9e2ef468000p-2},
    {0x1.56397cee76bd3p+0, -0x1.2941b3830e000p-2},
    {0x1.54725e2a77f93p+0, -0x1.23ec58cda8800p-2},
    {0x1.52aff42064583p+0, -0x1.1e9e129279000p-2},
    {0x1.50f22dbb2bddfp+0, -0x1.1956d2b48f800p-2},
    {0x1.4f38f4734ded7p+0, -0x1.141679ab9f800p-2},
    {0x1.4d843cfde2840p+0, -0x1.0edd094ef9800p-2},
    {0x1.4bd3ec078a3c8p+0, -0x1.09aa518db1000p-2},
    {0x1.4a27fc3e0258ap+0, -0x1.047e65263b800p-2},
    {0x1.4880524d48434p+0, -0x1.feb224586f000p-3},
    {0x1.46dce1b192d0bp+0, -0x1.f474a7517b000p-3},
    {0x1.453d9d3391854p+0, -0x1.ea4443d103000p-3},
    {0x1.43a2744b4845ap+0, -0x1.e020d44e9b000p-3},
    {0x1.420b54115f8fbp+0, -0x1.d60a22977f000p-3},
    {0x1.40782da3ef4b1p+0, -0x1.cc00104959000p-3},
    {0x1.3ee8f5d57fe8fp+0, -0x1.c202956891000p-3},
    {0x1.3d5d9a00b4ce9p+0, -0x1.b81178d811000p-3},
    {0x1.3bd60c010c12bp+0, -0x1.ae2c9ccd3d000p-3},
    {0x1.3a5242b75dab8p+0, -0x1.a45402e129000p-3},
    {0x1.38d22cd9fd002p+0, -0x1.9a877681df000p-3},
    {0x1.3755bc5847a1cp+0, -0x1.90c6d69483000p-3},
    {0x1.35dce49ad36e2p+0, -0x1.87120a645c000p-3},
    {0x1.34679984dd440p+0, -0x1.7d68fb4143000p-3},
    {0x1.32f5cceffcb24p+0, -0x1.73cb83c627000p-3},
    {0x1.3187775a10d49p+0, -0x1.6a39a9b376000p-3},
    {0x1.301c8373e3990p+0, -0x1.60b3154b7a000p-3},
    {0x1.2eb4ebb95f841p+0, -0x1.5737d76243000p-3},
    {0x1.2d50a0219a9d1p+0, -0x1.4dc7b8fc23000p-3},
    {0x1.2bef9a8b7fd2ap+0, -0x1.4462c51d20000p-3},
    {0x1.2a91c7a0c1babp+0, -0x1.3b08abc830000p-3},
    {0x1.293726014b530p+0, -0x1.31b996b490000p-3},
    {0x1.27dfa5757a1f5p+0, -0x1.2875490a44000p-3},
    {0x1.268b39b1d3bbfp+0, -0x1.1f3b9f879a000p-3},
    {0x1.2539d838ff5bdp+0, -0x1.160c8252ca000p-3},
    {0x1.23eb7aac9083bp+0, -0x1.0ce7f57f72000p-3},
    {0x1.22a012ba940b6p+0, -0x1.03cdc49fea000p-3},
    {0x1.2157996cc4132p+0, -0x1.f57bdbc4b8000p-4},
    {0x1.201201dd2fc9bp+0, -0x1.e370896404000p-4},
    {0x1.1ecf4494d480bp+0, -0x1.d17983ef94000p-4},
    {0x1.1d8f5528f6569p+0, -0x1.bf9674ed8a000p-4},
    {0x1.1c52311577e7cp+0, -0x1.adc79202f6000p-4},
    {0x1.1b17c74cb26e9p+0, -0x1.9c0c3e7288000p-4},
    {0x1.19e010c2c1ab6p+0, -0x1.8a646b372c000p-4},
    {0x1.18ab07bb670bdp+0, -0x1.78d01b3ac0000p-4},
    {0x1.1778a25efbcb6p+0, -0x1.674f145380000p-4},
    {0x1.1648d354c31dap+0, -0x1.55e0e6d878000p-4},
    {0x1.151b990275fddp+0, -0x1.4485cdea1e000p-4},
    {0x1.13f0ea432d24cp+0, -0x1.333d94d6aa000p-4},
    {0x1.12c8b7210f9dap+0, -0x1.22079f8c56000p-4},
    {0x1.11a3028ecb531p+0, -0x1.10e4698622000p-4},
    {0x1.107fbda8434afp+0, -0x1.ffa6c6ad20000p-5},
    {0x1.0f5ee0f4e6bb3p+0, -0x1.dda8d4a774000p-5},
    {0x1.0e4065d2a9fcep+0, -0x1.bbcece4850000p-5},
    {0x1.0d244632ca521p+0, -0x1.9a1894012c000p-5},
    {0x1.0c0a77ce2981ap+0, -0x1.788583302c000p-5},
    {0x1.0af2f83c636d1p+0, -0x1.5715e67d68000p-5},
    {0x1.09ddb98a01339p+0, -0x1.35c8a49658000p-5},
    {0x1.08cabaf52e7dfp+0, -0x1.149e364154000p-5},
    {0x1.07b9f2f4e28fbp+0, -0x1.e72c082eb8000p-6},
    {0x1.06ab58c358f19p+0, -0x1.a55f152528000p-6},
    {0x1.059eea5ecf92cp+0, -0x1.63d62cf818000p-6},
    {0x1.04949cdd12c90p+0, -0x1.228fb8caa0000p-6},
    {0x1.038c6c6f0ada9p+0, -0x1.c317b20f90000p-7},
    {0x1.02865137932a9p+0, -0x1.419355daa0000p-7},
    {0x1.0182427ea7348p+0, -0x1.81203c2ec0000p-8},
    {0x1.008040614b195p+0, -0x1.0040979240000p-9},
    {0x1.fe01ff726fa1ap-1, 0x1.feff384900000p-9},
    {0x1.fa11cc261ea74p-1, 0x1.7dc41353d0000p-7},
    {0x1.f6310b081992ep-1, 0x1.3cea3c4c28000p-6},
    {0x1.f25f63ceeadcdp-1, 0x1.b9fc114890000p-6},
    {0x1.ee9c8039113e7p-1, 0x1.1b0d8ce110000p-5},
    {0x1.eae8078cbb1abp-1, 0x1.58a5bd001c000p-5},
    {0x1.e741aa29d0c9bp-1, 0x1.95c8340d88000p-5},
    {0x1.e3a91830a99b5p-1, 0x1.d276aef578000p-5},
    {0x1.e01e009609a56p-1, 0x1.07598e598c000p-4},
    {0x1.dca01e577bb98p-1, 0x1.253f5e30d2000p-4},
    {0x1.d92f20b7c9103p-1, 0x1.42edd8b380000p-4},
    {0x1.d5cac66fb5ccep-1, 0x1.606598757c000p-4},
    {0x1.d272caa5ede9dp-1, 0x1.7da76356a0000p-4},
    {0x1.cf26e3e6b2ccdp-1, 0x1.9ab434e1c6000p-4},
    {0x1.cbe6da2a77902p-1, 0x1.b78c7bb0d6000p-4},
    {0x1.c8b266d37086dp-1, 0x1.d431332e72000p-4},
    {0x1.c5894bd5d5804p-1, 0x1.f0a3171de6000p-4},
    {0x1.c26b533bb9f8cp-1, 0x1.067152b914000p-3},
    {0x1.bf583eeece73fp-1, 0x1.147858292b000p-3},
    {0x1.bc4fd75db96c1p-1, 0x1.2266ecdca3000p-3},
    {0x1.b951e0c864a28p-1, 0x1.303d7a6c55000p-3},
    {0x1.b65e2c5ef3e2cp-1, 0x1.3dfc33c331000p-3},
    {0x1.b374867c9888bp-1, 0x1.4ba366b7a8000p-3},
    {0x1.b094b211d304ap-1, 0x1.5933928d1f000p-3},
    {0x1.adbe885f2ef7ep-1, 0x1.66acd2418f000p-3},
    {0x1.aaf1d31603da2p-1, 0x1.740f8ec669000p-3},
    {0x1.a82e63fd358a7p-1, 0x1.815c0f51af000p-3},
    {0x1.a5740ef09738bp-1, 0x1.8e92954f68000p-3},
    {0x1.a2c2a90ab4b27p-1, 0x1.9bb3602f84000p-3},
    {0x1.a01a01393f2d1p-1, 0x1.a8bed1c2c0000p-3},
    {0x1.9d79f24db3c1bp-1, 0x1.b5b515c01d000p-3},
    {0x1.9ae2505c7b190p-1, 0x1.c2967ccbcc000p-3},
    {0x1.9852ef297ce2fp-1, 0x1.cf635d5486000p-3},
    {0x1.95cbaeea44b75p-1, 0x1.dc1bd3446c000p-3},
    {0x1.934c69de74838p-1, 0x1.e8c01b8cfe000p-3},
    {0x1.90d4f2f6752e6p-1, 0x1.f5509c0179000p-3},
    {0x1.8e6528effd79dp-1, 0x1.00e6c121fb800p-2},
    {0x1.8bfce9fcc007cp-1, 0x1.071b80e93d000p-2},
    {0x1.899c0dabec30ep-1, 0x1.0d46b9e867000p-2},
    {0x1.87427aa2317fbp-1, 0x1.13687334bd000p-2},
    {0x1.84f00acb39a08p-1, 0x1.1980d67234800p-2},
    {0x1.82a49e8653e55p-1, 0x1.1f8ffe0cc8000p-2},
    {0x1.8060195f40260p-1, 0x1.2595fd7636800p-2},
    {0x1.7e22563e0a329p-1, 0x1.2b9300914a800p-2},
    {0x1.7beb377dcb5adp-1, 0x1.3187210436000p-2},
    {0x1.79baa679725c2p-1, 0x1.377266dec1800p-2},
    {0x1.77907f2170657p-1, 0x1.3d54ffbaf3000p-2},
    {0x1.756cadbd6130cp-1, 0x1.432eee32fe000p-2},
};
const LogEntry kLogTab2[] = {
    {0x1.61000014fb66bp-1, 0x1.e026c91425b3cp-56},
    {0x1.63000034db495p-1, 0x1.dbfea48005d41p-55},
    {0x1.650000d94d478p-1, 0x1.e7fa786d6a5b7p-55},
    {0x1.67000074e6fadp-1, 0x1.1fcea6b54254cp-57},
    {0x1.68ffffedf0faep-1, -0x1.c7e274c590efdp-56},
    {0x1.6b0000763c5bcp-1, -0x1.ac16848dcda01p-55},
    {0x1.6d0001e5cc1f6p-1, 0x1.33f1c9d499311p-55},
    {0x1.6efffeb05f63ep-1, -0x1.e80041ae22d53p-56},
    {0x1.710000e86978p-1, 0x1.bff6671097952p-56},
    {0x1.72ffffc67e912p-1, 0x1.c00e226bd8724p-55},
    {0x1.74fffdf81116ap-1, -0x1.e02916ef101d2p-57},
    {0x1.770000f679c9p-1, -0x1.7fc71cd549c74p-57},
    {0x1.78ffffa7ec835p-1, 0x1.1bec19ef50483p-55},
    {0x1.7affffe20c2e6p-1, -0x1.07e1729cc6465p-56},
    {0x1.7cfffed3fc9p-1, -0x1.08072087b8b1cp-55},
    {0x1.7efffe9261a76p-1, 0x1.dc0286d9df9aep-55},
    {0x1.81000049ca3e8p-1, 0x1.97fd251e54c33p-55},
    {0x1.8300017932c8fp-1, -0x1.afee9b630f381p-55},
    {0x1.850000633739cp-1, 0x1.9bfbf6b6535bcp-55},
    {0x1.87000204289c6p-1, -0x1.bbf65f3117b75p-55},
    {0x1.88fffebf57904p-1, -0x1.9006ea23dcb57p-55},
    {0x1.8b00022bc04dfp-1, -0x1.d00df38e04b0ap-56},
    {0x1.8cfffe50c1b8ap-1, -0x1.8007146ff9f05p-55},
    {0x1.8effffc918e43p-1, 0x1.3817bd07a7038p-55},
    {0x1.910001efa5fc7p-1, 0x1.93e9176dfb403p-55},
    {0x1.9300013467bb9p-1, 0x1.f804e4b980276p-56},
    {0x1.94fffe6ee076fp-1, -0x1.f7ef0d9ff622ep-55},
    {0x1.96fffde3c12d1p-1, -0x1.082aa962638bap-56},
    {0x1.98ffff4458a0dp-1, -0x1.7801b9164a8efp-55},
    {0x1.9afffdd982e3ep-1, -0x1.740e08a5a9337p-55},
    {0x1.9cfffed49fb66p-1, 0x1.fce08c19bep-60},
    {0x1.9f00020f19c51p-1, -0x1.a3faa27885b0ap-55},
    {0x1.a10001145b006p-1, 0x1.4ff489958da56p-56},
    {0x1.a300007bbf6fap-1, 0x1.cbeab8a2b6d18p-55},
    {0x1.a500010971d79p-1, 0x1.8fecadd78793p-55},
    {0x1.a70001df52e48p-1, -0x1.f41763dd8abdbp-55},
    {0x1.a90001c593352p-1, -0x1.ebf0284c27612p-55},
    {0x1.ab0002a4f3e4bp-1, -0x1.9fd043cff3f5fp-57},
    {0x1.acfffd7ae1ed1p-1, -0x1.23ee7129070b4p-55},
    {0x1.aefffee510478p-1, 0x1.a063ee00edea3p-57},
    {0x1.b0fffdb650d5bp-1, 0x1.a06c8381f0ab9p-58},
    {0x1.b2ffffeaaca57p-1, -0x1.9011e74233c1dp-56},
    {0x1.b4fffd995badcp-1, -0x1.9ff1068862a9fp-56},
    {0x1.b7000249e659cp-1, 0x1.aff45d0864f3ep-55},
    {0x1.b8ffff987164p-1, 0x1.cfe7796c2c3f9p-56},
    {0x1.bafffd204cb4fp-1, -0x1.3ff27eef22bc4p-57},
    {0x1.bcfffd2415c45p-1, -0x1.cffb7ee3bea21p-57},
    {0x1.beffff86309dfp-1, -0x1.14103972e0b5cp-55},
    {0x1.c0fffe1b57653p-1, 0x1.bc16494b76a19p-55},
    {0x1.c2ffff1fa57e3p-1, -0x1.4feef8d30c6edp-57},
    {0x1.c4fffdcbfe424p-1, -0x1.43f68bcec4775p-55},
    {0x1.c6fffed54b9f7p-1, 0x1.47ea3f053e0ecp-55},
    {0x1.c8fffeb998fd5p-1, 0x1.383068df992f1p-56},
    {0x1.cb0002125219ap-1, -0x1.8fd8e64180e04p-57},
    {0x1.ccfffdd94469cp-1, 0x1.e7ebe1cc7ea72p-55},
    {0x1.cefffeafdc476p-1, 0x1.ebe39ad9f88fep-55},
    {0x1.d1000169af82bp-1, 0x1.57d91a8b95a71p-56},
    {0x1.d30000d0ff71dp-1, 0x1.9c1906970c7dap-55},
    {0x1.d4fffea790fc4p-1, -0x1.80e37c558fe0cp-58},
    {0x1.d70002edc87e5p-1, -0x1.f80d64dc10f44p-56},
    {0x1.d900021dc82aap-1, -0x1.47c8f94fd5c5cp-56},
    {0x1.dafffd86b0283p-1, 0x1.c7f1dc521617ep-55},
    {0x1.dd000296c4739p-1, 0x1.8019eb2ffb153p-55},
    {0x1.defffe54490f5p-1, 0x1.e00d2c652cc89p-57},
    {0x1.e0fffcdabf694p-1, -0x1.f8340202d69d2p-56},
    {0x1.e2fffdb52c8ddp-1, 0x1.b00c1ca1b0864p-56},
    {0x1.e4ffff24216efp-1, 0x1.2ffa8b094ab51p-56},
    {0x1.e6fffe88a5e11p-1, -0x1.7f673b1efbe59p-58},
    {0x1.e9000119eff0dp-1, -0x1.4808d5e0bc801p-55},
    {0x1.eafffdfa51744p-1, 0x1.80006d54320b5p-56},
    {0x1.ed0001a127fa1p-1, -0x1.002f860565c92p-58},
    {0x1.ef00007babcc4p-1, -0x1.540445d35e611p-55},
    {0x1.f0ffff57a8d02p-1, -0x1.ffb3139ef9105p-59},
    {0x1.f30001ee58ac7p-1, 0x1.a81acf2731155p-55},
    {0x1.f4ffff5823494p-1, 0x1.a3f41d4d7c743p-55},
    {0x1.f6ffffca94c6bp-1, -0x1.202f41c987875p-57},
    {0x1.f8fffe1f9c441p-1, 0x1.77dd1f477e74bp-56},
    {0x1.fafffd2e0e37ep-1, -0x1.f01199a7ca331p-57},
    {0x1.fd0001c77e49ep-1, 0x1.181ee4bceacb1p-56},
    {0x1.feffff7e0c331p-1, -0x1.e05370170875ap-57},
    {0x1.00ffff465606ep+0, -0x1.a7ead491c0adap-55},
    {0x1.02ffff3867a58p+0, -0x1.77f69c3fcb2ep-54},
    {0x1.04ffffdfc0d17p+0, 0x1.7bffe34cb945bp-54},
    {0x1.0700003cd4d82p+0, 0x1.20083c0e456cbp-55},
    {0x1.08ffff9f2cbe8p+0, -0x1.dffdfbe37751ap-57},
    {0x1.0b000010cda65p+0, -0x1.13f7faee626ebp-54},
    {0x1.0d00001a4d338p+0, 0x1.07dfa79489ff7p-55},
    {0x1.0effffadafdfdp+0, -0x1.7040570d66bcp-56},
    {0x1.110000bbafd96p+0, 0x1.e80d4846d0b62p-55},
    {0x1.12ffffae5f45dp+0, 0x1.dbffa64fd36efp-54},
    {0x1.150000dd59ad9p+0, 0x1.a0077701250aep-54},
    {0x1.170000f21559ap+0, 0x1.dfdf9e2e3deeep-55},
    {0x1.18ffffc275426p+0, 0x1.10030dc3b7273p-54},
    {0x1.1b000123d3c59p+0, 0x1.97f7980030188p-54},
    {0x1.1cffff8299eb7p+0, -0x1.5f932ab9f8c67p-57},
    {0x1.1effff48ad4p+0, 0x1.37fbf9da75bebp-54},
    {0x1.210000c8b86a4p+0, 0x1.f806b91fd5b22p-54},
    {0x1.2300003854303p+0, 0x1.3ffc2eb9fbf33p-54},
    {0x1.24fffffbcf684p+0, 0x1.601e77e2e2e72p-56},
    {0x1.26ffff52921d9p+0, 0x1.ffcbb767f0c61p-56},
    {0x1.2900014933a3cp+0, -0x1.202ca3c02412bp-56},
    {0x1.2b00014556313p+0, -0x1.2808233f21f02p-54},
    {0x1.2cfffebfe523bp+0, -0x1.8ff7e384fdcf2p-55},
    {0x1.2f0000bb8ad96p+0, -0x1.5ff51503041c5p-55},
    {0x1.30ffffb7ae2afp+0, -0x1.10071885e289dp-55},
    {0x1.32ffffeac5f7fp+0, -0x1.1ff5d3fb7b715p-54},
    {0x1.350000ca66756p+0, 0x1.57f82228b82bdp-54},
    {0x1.3700011fbf721p+0, 0x1.000bac40dd5ccp-55},
    {0x1.38ffff9592fb9p+0, -0x1.43f9d2db2a751p-54},
    {0x1.3b00004ddd242p+0, 0x1.57f6b707638e1p-55},
    {0x1.3cffff5b2c957p+0, 0x1.a023a10bf1231p-56},
    {0x1.3efffeab0b418p+0, 0x1.87f6d66b152bp-54},
    {0x1.410001532aff4p+0, 0x1.7f8375f198524p-57},
    {0x1.4300017478b29p+0, 0x1.301e672dc5143p-55},
    {0x1.44fffe795b463p+0, 0x1.9ff69b8b2895ap-55},
    {0x1.46fffe80475ep+0, -0x1.5c0b19bc2f254p-54},
    {0x1.48fffef6fc1e7p+0, 0x1.b4009f23a2a72p-54},
    {0x1.4afffe5bea704p+0, -0x1.4ffb7bf0d7d45p-54},
    {0x1.4d000171027dep+0, -0x1.9c06471dc6a3dp-54},
    {0x1.4f0000ff03ee2p+0, 0x1.77f890b85531cp-54},
    {0x1.5100012dc4bd1p+0, 0x1.004657166a436p-57},
    {0x1.530001605277ap+0, -0x1.6bfcece233209p-54},
    {0x1.54fffecdb704cp+0, -0x1.902720505a1d7p-55},
    {0x1.56fffef5f54a9p+0, 0x1.bbfe60ec96412p-54},
    {0x1.5900017e61012p+0, 0x1.87ec581afef9p-55},
    {0x1.5b00003c93e92p+0, -0x1.f41080abf0ccp-54},
    {0x1.5d0001d4919bcp+0, -0x1.8812afb254729p-54},
    {0x1.5efffe7b87a89p+0, -0x1.47eb780ed6904p-54},
};
const double kLogPoly[] = {
    -0x1.0000000000001p-1,
    0x1.555555551305bp-2,
    -0x1.fffffffeb459p-3,
    0x1.999b324f10111p-3,
    -0x1.55575e506c89fp-3,
};
const double kLogPoly1[] = {
    -0x1p-1,
    0x1.5555555555577p-2,
    -0x1.ffffffffffdcbp-3,
    0x1.999999995dd0cp-3,
    -0x1.55555556745a7p-3,
    0x1.24924a344de3p-3,
    -0x1.fffffa4423d65p-4,
    0x1.c7184282ad6cap-4,
    -0x1.999eb43b068ffp-4,
    0x1.78182f7afd085p-4,
    -0x1.5521375d145cdp-4,
};
const double kExpPoly[] = {
    0x1.ffffffffffdbdp-2,
    0x1.555555555543cp-3,
    0x1.55555cf172b91p-5,
    0x1.1111167a4d017p-7,
};
const u64 kExp2fTab[] = {
    0x3ff0000000000000ull, 0x3fefd9b0d3158574ull, 0x3fefb5586cf9890full, 0x3fef9301d0125b51ull,
    0x3fef72b83c7d517bull, 0x3fef54873168b9aaull, 0x3fef387a6e756238ull, 0x3fef1e9df51fdee1ull,
    0x3fef06fe0a31b715ull, 0x3feef1a7373aa9cbull, 0x3feedea64c123422ull, 0x3feece086061892dull,
    0x3feebfdad5362a27ull, 0x3feeb42b569d4f82ull, 0x3feeab07dd485429ull, 0x3feea47eb03a5585ull,
    0x3feea09e667f3bcdull, 0x3fee9f75e8ec5f74ull, 0x3feea11473eb0187ull, 0x3feea589994cce13ull,
    0x3feeace5422aa0dbull, 0x3feeb737b0cdc5e5ull, 0x3feec49182a3f090ull, 0x3feed503b23e255dull,
    0x3feee89f995ad3adull, 0x3feeff76f2fb5e47ull, 0x3fef199bdd85529cull, 0x3fef3720dcef9069ull,
    0x3fef5818dcfba487ull, 0x3fef7c97337b9b5full, 0x3fefa4afa2a490daull, 0x3fefd0765b6e4540ull,
};
const LogEntry kLogfTab[] = {
    { 0x1.661ec79f8f3bep+0, -0x1.57bf7808caadep-2 },
    { 0x1.571ed4aaf883dp+0, -0x1.2bef0a7c06ddbp-2 },
    { 0x1.49539f0f010bp+0, -0x1.01eae7f513a67p-2 },
    { 0x1.3c995b0b80385p+0, -0x1.b31d8a68224e9p-3 },
    { 0x1.30d190c8864a5p+0, -0x1.6574f0ac07758p-3 },
    { 0x1.25e227b0b8eap+0, -0x1.1aa2bc79c81p-3 },
    { 0x1.1bb4a4a1a343fp+0, -0x1.a4e76ce8c0e5ep-4 },
    { 0x1.12358f08ae5bap+0, -0x1.1973c5a611cccp-4 },
    { 0x1.0953f419900a7p+0, -0x1.252f438e10c1ep-5 },
    { 0x1p+0, 0x0p+0 },
    { 0x1.e608cfd9a47acp-1, 0x1.aa5aa5df25984p-5 },
    { 0x1.ca4b31f026aap-1, 0x1.c5e53aa362eb4p-4 },
    { 0x1.b2036576afce6p-1, 0x1.526e57720db08p-3 },
    { 0x1.9c2d163a1aa2dp-1, 0x1.bc2860d22477p-3 },
    { 0x1.886e6037841edp-1, 0x1.1058bc8a07ee1p-2 },
    { 0x1.767dcf5534862p-1, 0x1.4043057b6ee09p-2 },
};
const LogEntry kPowfLog2Tab[] = {
    { 0x1.661ec79f8f3bep+0, -0x1.efec65b963019p-2 },
    { 0x1.571ed4aaf883dp+0, -0x1.b0b6832d4fca4p-2 },
    { 0x1.49539f0f010bp+0, -0x1.7418b0a1fb77bp-2 },
    { 0x1.3c995b0b80385p+0, -0x1.39de91a6dcf7bp-2 },
    { 0x1.30d190c8864a5p+0, -0x1.01d9bf3f2b631p-2 },
    { 0x1.25e227b0b8eap+0, -0x1.97c1d1b3b7afp-3 },
    { 0x1.1bb4a4a1a343fp+0, -0x1.2f9e393af3c9fp-3 },
    { 0x1.12358f08ae5bap+0, -0x1.960cbbf788d5cp-4 },
    { 0x1.0953f419900a7p+0, -0x1.a6f9db6475fcep-5 },
    { 0x1p+0, 0x0p+0 },
    { 0x1.e608cfd9a47acp-1, 0x1.338ca9f24f53dp-4 },
    { 0x1.ca4b31f026aap-1, 0x1.476a9543891bap-3 },
    { 0x1.b2036576afce6p-1, 0x1.e840b4ac4e4d2p-3 },
    { 0x1.9c2d163a1aa2dp-1, 0x1.40645f0c6651cp-2 },
    { 0x1.886e6037841edp-1, 0x1.88e9c2c1b9ff8p-2 },
    { 0x1.767dcf5534862p-1, 0x1.ce0a44eb17bccp-2 },
};
const double kPowfLog2Poly[] = {
    0x1.27616c9496e0bp-2, -0x1.71969a075c67ap-2,
    0x1.ec70a6ca7baddp-2, -0x1.7154748bef6c8p-1,
    0x1.71547652ab82bp0,
};
const i32 kIpio2[] = {
    0xA2F983, 0x6E4E44, 0x1529FC, 0x2757D1, 0xF534DD, 0xC0DB62,
    0x95993C, 0x439041, 0xFE5163, 0xABDEBB, 0xC561B7, 0x246E3A,
    0x424DD2, 0xE00649, 0x2EEA09, 0xD1921C, 0xFE1DEB, 0x1CB129,
    0xA73EE8, 0x8235F5, 0x2EBB44, 0x84E99C, 0x7026B4, 0x5F7E41,
    0x3991D6, 0x398353, 0x39F49C, 0x845F8B, 0xBDF928, 0x3B1FF8,
    0x97FFDE, 0x05980F, 0xEF2F11, 0x8B5A0A, 0x6D1F6D, 0x367ECF,
    0x27CB09, 0xB74F46, 0x3F669E, 0x5FEA2D, 0x7527BA, 0xC7EBE5,
    0xF17B3D, 0x0739F7, 0x8A5292, 0xEA6BFB, 0x5FB11F, 0x8D5D08,
    0x560330, 0x46FC7B, 0x6BABF0, 0xCFBC20, 0x9AF436, 0x1DA9E3,
    0x91615E, 0xE61B08, 0x659985, 0x5F14A0, 0x68408D, 0xFFD880,
    0x4D7327, 0x310606, 0x1556CA, 0x73A8C9, 0x60E27B, 0xC08C6B,
};

// exp2f_data.c scalars (EXP2F_TABLE_BITS 5).
constexpr int kExp2fN = 32;
const double kExp2fShiftScaled = 0x1.8p+52 / kExp2fN;
const double kExp2fPoly[] = {0x1.c6af84b912394p-5, 0x1.ebfce50fac4f3p-3, 0x1.62e42ff0c52d6p-1};
const double kExp2fShift = 0x1.8p+52;
const double kExp2fInvLn2Scaled = 0x1.71547652b82fep+0 * kExp2fN;
const double kExp2fPolyScaled[] = {0x1.c6af84b912394p-5 / kExp2fN / kExp2fN / kExp2fN,
                                   0x1.ebfce50fac4f3p-3 / kExp2fN / kExp2fN, 0x1.62e42ff0c52d6p-1 / kExp2fN};
// logf_data.c scalar.
const double kLogfLn2 = 0x1.62e42fefa39efp-1;
const double kLogfPoly[] = {-0x1.00ea348b88334p-2, 0x1.5575b0be00b6ap-2, -0x1.ffffef20a4123p-2};
// exp_data.c scalars (EXP_TABLE_BITS 7).
constexpr int kExpN = 128;
const double kExpInvLn2N = 0x1.71547652b82fep0 * kExpN;
const double kExpNegLn2hiN = -0x1.62e42fefa0000p-8;
const double kExpNegLn2loN = -0x1.cf79abc9e3b3ap-47;
const double kExpShift = 0x1.8p52;
// log_data.c scalars (LOG_TABLE_BITS 7).
constexpr int kLogN = 128;
const double kLogLn2hi = 0x1.62e42fefa3800p-1;
const double kLogLn2lo = 0x1.ef35793c76730p-45;

// ---- Double kernels: __sin.c, __cos.c, __tan.c --------------------------------------------

double kernelSin(double x, double y, int iy) {
    const double S1 = -1.66666666666666324348e-01, S2 = 8.33333333332248946124e-03,
                 S3 = -1.98412698298579493134e-04, S4 = 2.75573137070700676789e-06,
                 S5 = -2.50507602534068634195e-08, S6 = 1.58969099521155010221e-10;
    double z, r, v, w;
    z = x * x;
    w = z * z;
    r = S2 + z * (S3 + z * S4) + z * w * (S5 + z * S6);
    v = z * x;
    if (iy == 0) return x + v * (S1 + z * r);
    return x - ((z * (0.5 * y - v * r) - y) - v * S1);
}

double kernelCos(double x, double y) {
    const double C1 = 4.16666666666666019037e-02, C2 = -1.38888888888741095749e-03,
                 C3 = 2.48015872894767294178e-05, C4 = -2.75573143513906633035e-07,
                 C5 = 2.08757232129817482790e-09, C6 = -1.13596475577881948265e-11;
    double hz, z, r, w;
    z = x * x;
    w = z * z;
    r = z * (C1 + z * (C2 + z * C3)) + w * w * (C4 + z * (C5 + z * C6));
    hz = 0.5 * z;
    w = 1.0 - hz;
    return w + (((1.0 - w) - hz) + (z * r - x * y));
}

const double kTanT[] = {
    3.33333333333334091986e-01,  1.33333333333201242699e-01,  5.39682539762260521377e-02,
    2.18694882948595424599e-02,  8.86323982359930005737e-03,  3.59207910759131235356e-03,
    1.45620945432529025516e-03,  5.88041240820264096874e-04,  2.46463134818469906812e-04,
    7.81794442939557092300e-05,  7.14072491382608190305e-05,  -1.85586374855275456654e-05,
    2.59073051863633712884e-05,
};

double kernelTan(double x, double y, int odd) {
    const double* T = kTanT;
    const double pio4 = 7.85398163397448278999e-01, pio4lo = 3.06161699786838301793e-17;
    double z, r, v, w, s, a;
    double w0, a0;
    u32 hx = highWord(x);
    int sign = 0;
    const bool big = (hx & 0x7fffffff) >= 0x3FE59428; // |x| >= 0.6744
    if (big) {
        sign = static_cast<int>(hx >> 31);
        if (sign) {
            x = -x;
            y = -y;
        }
        x = (pio4 - x) + (pio4lo - y);
        y = 0.0;
    }
    z = x * x;
    w = z * z;
    r = T[1] + w * (T[3] + w * (T[5] + w * (T[7] + w * (T[9] + w * T[11]))));
    v = z * (T[2] + w * (T[4] + w * (T[6] + w * (T[8] + w * (T[10] + w * T[12])))));
    s = z * x;
    r = y + z * (s * (r + v) + y) + s * T[0];
    w = x + r;
    if (big) {
        s = 1 - 2 * odd;
        v = s - 2.0 * (x + (r - w * w / (w + s)));
        return sign ? -v : v;
    }
    if (!odd) return w;
    // -1.0/(x+r) has up to 2ulp error, so compute it accurately
    w0 = withLowWord(w, 0);
    v = r - (w0 - x); // w0+v = r+x
    a = -1.0 / w;
    a0 = withLowWord(a, 0);
    return a0 + a * (1.0 + a0 * w0 + a0 * v);
}

// ---- Argument reduction: __rem_pio2_large.c, __rem_pio2.c, __rem_pio2f.c ------------------

int remPio2Large(const double* x, double* y, int e0, int nx, int prec) {
    static const int initJk[] = {3, 4, 4, 6};
    static const double PIo2[] = {
        1.57079625129699707031e+00, 7.54978941586159635335e-08, 5.39030252995776476554e-15,
        3.28200341580791294123e-22, 1.27065575308067607349e-29, 1.22933308981111328932e-36,
        2.73370053816464559624e-44, 2.16741683877804819444e-51,
    };
    i32 jz, jx, jv, jp, jk, carry, n, iq[20], i, j, k, m, q0, ih;
    double z, fw, f[20], fq[20] = {}, q[20]; // fq zeroed: gcc cannot see every read is written first

    jk = initJk[prec];
    jp = jk;

    jx = nx - 1;
    jv = (e0 - 3) / 24;
    if (jv < 0) jv = 0;
    q0 = e0 - 24 * (jv + 1);

    j = jv - jx;
    m = jx + jk;
    for (i = 0; i <= m; i++, j++) f[i] = j < 0 ? 0.0 : static_cast<double>(kIpio2[j]);

    for (i = 0; i <= jk; i++) {
        for (j = 0, fw = 0.0; j <= jx; j++) fw += x[j] * f[jx + i - j];
        q[i] = fw;
    }

    jz = jk;
recompute:
    // distill q[] into iq[] reversingly
    for (i = 0, j = jz, z = q[jz]; j > 0; i++, j--) {
        fw = static_cast<double>(static_cast<i32>(0x1p-24 * z));
        iq[i] = static_cast<i32>(z - 0x1p24 * fw);
        z = q[j - 1] + fw;
    }

    // compute n
    z = scalbn(z, q0);             // actual value of z
    z -= 8.0 * std::floor(z * 0.125); // trim off integer >= 8
    n = static_cast<i32>(z);
    z -= static_cast<double>(n);
    ih = 0;
    if (q0 > 0) { // need iq[jz-1] to determine n
        i = iq[jz - 1] >> (24 - q0);
        n += i;
        iq[jz - 1] -= i << (24 - q0);
        ih = iq[jz - 1] >> (23 - q0);
    } else if (q0 == 0) {
        ih = iq[jz - 1] >> 23;
    } else if (z >= 0.5) {
        ih = 2;
    }

    if (ih > 0) { // q > 0.5
        n += 1;
        carry = 0;
        for (i = 0; i < jz; i++) { // compute 1-q
            j = iq[i];
            if (carry == 0) {
                if (j != 0) {
                    carry = 1;
                    iq[i] = 0x1000000 - j;
                }
            } else {
                iq[i] = 0xffffff - j;
            }
        }
        if (q0 > 0) { // rare case: chance is 1 in 12
            switch (q0) {
            case 1: iq[jz - 1] &= 0x7fffff; break;
            case 2: iq[jz - 1] &= 0x3fffff; break;
            }
        }
        if (ih == 2) {
            z = 1.0 - z;
            if (carry != 0) z -= scalbn(1.0, q0);
        }
    }

    // check if recomputation is needed
    if (z == 0.0) {
        j = 0;
        for (i = jz - 1; i >= jk; i--) j |= iq[i];
        if (j == 0) { // need recomputation
            for (k = 1; iq[jk - k] == 0; k++) {
            } // k = no. of terms needed
            for (i = jz + 1; i <= jz + k; i++) { // add q[jz+1] to q[jz+k]
                f[jx + i] = static_cast<double>(kIpio2[jv + i]);
                for (j = 0, fw = 0.0; j <= jx; j++) fw += x[j] * f[jx + i - j];
                q[i] = fw;
            }
            jz += k;
            goto recompute;
        }
    }

    // chop off zero terms
    if (z == 0.0) {
        jz -= 1;
        q0 -= 24;
        while (iq[jz] == 0) {
            jz--;
            q0 -= 24;
        }
    } else { // break z into 24-bit if necessary
        z = scalbn(z, -q0);
        if (z >= 0x1p24) {
            fw = static_cast<double>(static_cast<i32>(0x1p-24 * z));
            iq[jz] = static_cast<i32>(z - 0x1p24 * fw);
            jz += 1;
            q0 += 24;
            iq[jz] = static_cast<i32>(fw);
        } else {
            iq[jz] = static_cast<i32>(z);
        }
    }

    // convert integer "bit" chunk to floating-point value
    fw = scalbn(1.0, q0);
    for (i = jz; i >= 0; i--) {
        q[i] = fw * static_cast<double>(iq[i]);
        fw *= 0x1p-24;
    }

    // compute PIo2[0,...,jp]*q[jz,...,0]
    for (i = jz; i >= 0; i--) {
        for (fw = 0.0, k = 0; k <= jp && k <= jz - i; k++) fw += PIo2[k] * q[i + k];
        fq[jz - i] = fw;
    }

    // compress fq[] into y[] (prec 0: float, 1: double; musl's 2 and 3 are long double)
    if (prec == 0) {
        fw = 0.0;
        for (i = jz; i >= 0; i--) fw += fq[i];
        y[0] = ih == 0 ? fw : -fw;
    } else {
        fw = 0.0;
        for (i = jz; i >= 0; i--) fw += fq[i];
        y[0] = ih == 0 ? fw : -fw;
        fw = fq[0] - fw;
        for (i = 1; i <= jz; i++) fw += fq[i];
        y[1] = ih == 0 ? fw : -fw;
    }
    return n & 7;
}

// Caller must handle the case when reduction is not needed: |x| ~<= pi/4.
int remPio2(double x, double* y) {
    const double toint = 0x1.8p52, // 1.5/DBL_EPSILON
                 pio4 = 0x1.921fb54442d18p-1, invpio2 = 6.36619772367581382433e-01,
                 pio2_1 = 1.57079632673412561417e+00, pio2_1t = 6.07710050650619224932e-11,
                 pio2_2 = 6.07710050630396597660e-11, pio2_2t = 2.02226624879595063154e-21,
                 pio2_3 = 2.02226624871116645580e-21, pio2_3t = 8.47842766036889956997e-32;
    u64 ui = asuint64(x);
    double z, w, t, r, fn;
    double tx[3], ty[2];
    u32 ix;
    int sign, n, ex, ey, i;

    sign = static_cast<int>(ui >> 63);
    ix = static_cast<u32>(ui >> 32) & 0x7fffffff;
    if (ix <= 0x400f6a7a) {              // |x| ~<= 5pi/4
        if ((ix & 0xfffff) == 0x921fb) // |x| ~= pi/2 or 2pi/2
            goto medium;               // cancellation -- use medium case
        if (ix <= 0x4002d97c) {        // |x| ~<= 3pi/4
            if (!sign) {
                z = x - pio2_1; // one round good to 85 bits
                y[0] = z - pio2_1t;
                y[1] = (z - y[0]) - pio2_1t;
                return 1;
            } else {
                z = x + pio2_1;
                y[0] = z + pio2_1t;
                y[1] = (z - y[0]) + pio2_1t;
                return -1;
            }
        } else {
            if (!sign) {
                z = x - 2 * pio2_1;
                y[0] = z - 2 * pio2_1t;
                y[1] = (z - y[0]) - 2 * pio2_1t;
                return 2;
            } else {
                z = x + 2 * pio2_1;
                y[0] = z + 2 * pio2_1t;
                y[1] = (z - y[0]) + 2 * pio2_1t;
                return -2;
            }
        }
    }
    if (ix <= 0x401c463b) {     // |x| ~<= 9pi/4
        if (ix <= 0x4015fdbc) { // |x| ~<= 7pi/4
            if (ix == 0x4012d97c) // |x| ~= 3pi/2
                goto medium;
            if (!sign) {
                z = x - 3 * pio2_1;
                y[0] = z - 3 * pio2_1t;
                y[1] = (z - y[0]) - 3 * pio2_1t;
                return 3;
            } else {
                z = x + 3 * pio2_1;
                y[0] = z + 3 * pio2_1t;
                y[1] = (z - y[0]) + 3 * pio2_1t;
                return -3;
            }
        } else {
            if (ix == 0x401921fb) // |x| ~= 4pi/2
                goto medium;
            if (!sign) {
                z = x - 4 * pio2_1;
                y[0] = z - 4 * pio2_1t;
                y[1] = (z - y[0]) - 4 * pio2_1t;
                return 4;
            } else {
                z = x + 4 * pio2_1;
                y[0] = z + 4 * pio2_1t;
                y[1] = (z - y[0]) + 4 * pio2_1t;
                return -4;
            }
        }
    }
    if (ix < 0x413921fb) { // |x| ~< 2^20*(pi/2), medium size
    medium:
        // rint(x/(pi/2))
        fn = x * invpio2 + toint - toint;
        n = static_cast<i32>(fn);
        r = x - fn * pio2_1;
        w = fn * pio2_1t; // 1st round, good to 85 bits
        // Matters with directed rounding.
        if (r - w < -pio4) {
            n--;
            fn--;
            r = x - fn * pio2_1;
            w = fn * pio2_1t;
        } else if (r - w > pio4) {
            n++;
            fn++;
            r = x - fn * pio2_1;
            w = fn * pio2_1t;
        }
        y[0] = r - w;
        ey = static_cast<int>(asuint64(y[0]) >> 52 & 0x7ff);
        ex = static_cast<int>(ix >> 20);
        if (ex - ey > 16) { // 2nd round, good to 118 bits
            t = r;
            w = fn * pio2_2;
            r = t - w;
            w = fn * pio2_2t - ((t - r) - w);
            y[0] = r - w;
            ey = static_cast<int>(asuint64(y[0]) >> 52 & 0x7ff);
            if (ex - ey > 49) { // 3rd round, good to 151 bits, covers all cases
                t = r;
                w = fn * pio2_3;
                r = t - w;
                w = fn * pio2_3t - ((t - r) - w);
                y[0] = r - w;
            }
        }
        y[1] = (r - y[0]) - w;
        return n;
    }
    // all other (large) arguments
    if (ix >= 0x7ff00000) { // x is inf or NaN
        y[0] = y[1] = qnan();
        return 0;
    }
    // set z = scalbn(|x|,-ilogb(x)+23)
    ui = asuint64(x);
    ui &= static_cast<u64>(-1) >> 12;
    ui |= static_cast<u64>(0x3ff + 23) << 52;
    z = asdouble(ui);
    for (i = 0; i < 2; i++) {
        tx[i] = static_cast<double>(static_cast<i32>(z));
        z = (z - tx[i]) * 0x1p24;
    }
    tx[i] = z;
    // skip zero terms, first term is non-zero
    while (tx[i] == 0.0) i--;
    n = remPio2Large(tx, ty, static_cast<int>(ix >> 20) - (0x3ff + 23), i + 1, 1);
    if (sign) {
        y[0] = -ty[0];
        y[1] = -ty[1];
        return -n;
    }
    y[0] = ty[0];
    y[1] = ty[1];
    return n;
}

int remPio2f(float x, double* y) {
    const double toint = 0x1.8p52 /* 1.5/DBL_EPSILON */, pio4 = 0x1.921fb6p-1,
                 invpio2 = 6.36619772367581382433e-01, pio2_1 = 1.57079631090164184570e+00,
                 pio2_1t = 1.58932547735281966916e-08;
    u32 ui = asuint(x);
    double tx[1], ty[1];
    double fn;
    u32 ix;
    int n, sign, e0;

    ix = ui & 0x7fffffff;
    // 25+53 bit pi is good enough for medium size
    if (ix < 0x4dc90fdb) { // |x| ~< 2^28*(pi/2), medium size
        // Use a specialized rint() to get fn.
        fn = static_cast<double>(x) * invpio2 + toint - toint;
        n = static_cast<i32>(fn);
        *y = x - fn * pio2_1 - fn * pio2_1t;
        // Matters with directed rounding.
        if (*y < -pio4) {
            n--;
            fn--;
            *y = x - fn * pio2_1 - fn * pio2_1t;
        } else if (*y > pio4) {
            n++;
            fn++;
            *y = x - fn * pio2_1 - fn * pio2_1t;
        }
        return n;
    }
    if (ix >= 0x7f800000) { // x is inf or NaN
        *y = qnan();
        return 0;
    }
    // scale x into [2^23, 2^24-1]
    sign = static_cast<int>(ui >> 31);
    e0 = static_cast<int>(ix >> 23) - (0x7f + 23); // e0 = ilogb(|x|)-23, positive
    ui = ix - (static_cast<u32>(e0) << 23);
    tx[0] = asfloat(ui);
    n = remPio2Large(tx, ty, e0, 1, 0);
    if (sign) {
        *y = -ty[0];
        return -n;
    }
    *y = ty[0];
    return n;
}

// ---- Float kernels: __sindf.c, __cosdf.c, __tandf.c ----------------------------------------

float kernelSindf(double x) {
    // |sin(x)/x - s(x)| < 2**-37.5 (~[-4.89e-12, 4.824e-12]).
    const double S1 = -0x15555554cbac77.0p-55, S2 = 0x111110896efbb2.0p-59,
                 S3 = -0x1a00f9e2cae774.0p-65, S4 = 0x16cd878c3b46a7.0p-71;
    double r, s, w, z;
    z = x * x;
    w = z * z;
    r = S3 + z * S4;
    s = z * x;
    return static_cast<float>((x + s * (S1 + z * S2)) + s * w * r);
}

float kernelCosdf(double x) {
    // |cos(x) - c(x)| < 2**-34.1 (~[-5.37e-11, 5.295e-11]).
    const double C0 = -0x1ffffffd0c5e81.0p-54, C1 = 0x155553e1053a42.0p-57,
                 C2 = -0x16c087e80f1e27.0p-62, C3 = 0x199342e0ee5069.0p-68;
    double r, w, z;
    z = x * x;
    w = z * z;
    r = C2 + z * C3;
    return static_cast<float>(((1.0 + z * C0) + w * C1) + (w * z) * r);
}

float kernelTandf(double x, int odd) {
    // |tan(x)/x - t(x)| < 2**-25.5 (~[-2e-08, 2e-08]).
    static const double T[] = {
        0x15554d3418c99f.0p-54, 0x1112fd38999f72.0p-55, 0x1b54c91d865afe.0p-57,
        0x191df3908c33ce.0p-58, 0x185dadfcecf44e.0p-61, 0x1362b9bf971bcd.0p-59,
    };
    double z, r, w, s, t, u;
    z = x * x;
    r = T[4] + z * T[5];
    t = T[2] + z * T[3];
    w = z * z;
    s = z * x;
    u = T[0] + z * T[1];
    r = (x + s * u) + (s * w) * (t + w * r);
    return static_cast<float>(odd ? -1.0 / r : r);
}

// ---- Inverse trigonometric helpers ---------------------------------------------------------

// acos.c R(z)
double acosR(double z) {
    const double pS0 = 1.66666666666666657415e-01, pS1 = -3.25565818622400915405e-01,
                 pS2 = 2.01212532134862925881e-01, pS3 = -4.00555345006794114027e-02,
                 pS4 = 7.91534994289814532176e-04, pS5 = 3.47933107596021167570e-05,
                 qS1 = -2.40339491173441421878e+00, qS2 = 2.02094576023350569471e+00,
                 qS3 = -6.88283971605453293030e-01, qS4 = 7.70381505559019352791e-02;
    double p, q;
    p = z * (pS0 + z * (pS1 + z * (pS2 + z * (pS3 + z * (pS4 + z * pS5)))));
    q = 1.0 + z * (qS1 + z * (qS2 + z * (qS3 + z * qS4)));
    return p / q;
}

// acosf.c / asinf.c R(z)
float acosfR(float z) {
    const float pS0 = 1.6666586697e-01, pS1 = -4.2743422091e-02, pS2 = -8.6563630030e-03,
                qS1 = -7.0662963390e-01;
    float p, q;
    p = z * (pS0 + z * (pS1 + z * pS2));
    q = 1.0f + z * qS1;
    return p / q;
}

// ---- The functions (named as musl's, the public wrappers below canonicalise NaN) -----------

double muslSin(double x) {
    double y[2];
    u32 ix = highWord(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb) {        // |x| ~< pi/4
        if (ix < 0x3e500000) return x; // |x| < 2**-26
        return kernelSin(x, 0.0, 0);
    }
    if (ix >= 0x7ff00000) return qnan(); // sin(Inf or NaN) is NaN
    unsigned n = static_cast<unsigned>(remPio2(x, y));
    switch (n & 3) {
    case 0: return kernelSin(y[0], y[1], 1);
    case 1: return kernelCos(y[0], y[1]);
    case 2: return -kernelSin(y[0], y[1], 1);
    default: return -kernelCos(y[0], y[1]);
    }
}

double muslCos(double x) {
    double y[2];
    u32 ix = highWord(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb) {         // |x| ~< pi/4
        if (ix < 0x3e46a09e) return 1.0; // |x| < 2**-27 * sqrt(2)
        return kernelCos(x, 0);
    }
    if (ix >= 0x7ff00000) return qnan(); // cos(Inf or NaN) is NaN
    unsigned n = static_cast<unsigned>(remPio2(x, y));
    switch (n & 3) {
    case 0: return kernelCos(y[0], y[1]);
    case 1: return -kernelSin(y[0], y[1], 1);
    case 2: return -kernelCos(y[0], y[1]);
    default: return kernelSin(y[0], y[1], 1);
    }
}

double muslTan(double x) {
    double y[2];
    u32 ix = highWord(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb) {        // |x| ~< pi/4
        if (ix < 0x3e400000) return x; // |x| < 2**-27
        return kernelTan(x, 0.0, 0);
    }
    if (ix >= 0x7ff00000) return qnan(); // tan(Inf or NaN) is NaN
    unsigned n = static_cast<unsigned>(remPio2(x, y));
    return kernelTan(y[0], y[1], static_cast<int>(n & 1));
}

double muslAtan(double x) {
    static const double atanhi[] = {
        4.63647609000806093515e-01, 7.85398163397448278999e-01,
        9.82793723247329054082e-01, 1.57079632679489655800e+00,
    };
    static const double atanlo[] = {
        2.26987774529616870924e-17, 3.06161699786838301793e-17,
        1.39033110312309984516e-17, 6.12323399573676603587e-17,
    };
    static const double aT[] = {
        3.33333333333329318027e-01,  -1.99999999998764832476e-01, 1.42857142725034663711e-01,
        -1.11111104054623557880e-01, 9.09088713343650656196e-02,  -7.69187620504482999495e-02,
        6.66107313738753120669e-02,  -5.83357013379057348645e-02, 4.97687799461593236017e-02,
        -3.65315727442169155270e-02, 1.62858201153657823623e-02,
    };
    double w, s1, s2, z;
    u32 ix, sign;
    int id;

    ix = highWord(x);
    sign = ix >> 31;
    ix &= 0x7fffffff;
    if (ix >= 0x44100000) { // if |x| >= 2^66
        if (x != x) return x;
        z = atanhi[3] + 0x1p-120f;
        return sign ? -z : z;
    }
    if (ix < 0x3fdc0000) {     // |x| < 0.4375
        if (ix < 0x3e400000) return x; // |x| < 2^-27
        id = -1;
    } else {
        x = std::fabs(x);
        if (ix < 0x3ff30000) {     // |x| < 1.1875
            if (ix < 0x3fe60000) { //  7/16 <= |x| < 11/16
                id = 0;
                x = (2.0 * x - 1.0) / (2.0 + x);
            } else { // 11/16 <= |x| < 19/16
                id = 1;
                x = (x - 1.0) / (x + 1.0);
            }
        } else {
            if (ix < 0x40038000) { // |x| < 2.4375
                id = 2;
                x = (x - 1.5) / (1.0 + 1.5 * x);
            } else { // 2.4375 <= |x| < 2^66
                id = 3;
                x = -1.0 / x;
            }
        }
    }
    // end of argument reduction
    z = x * x;
    w = z * z;
    // break sum from i=0 to 10 aT[i]z**(i+1) into odd and even poly
    s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
    s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));
    if (id < 0) return x - x * (s1 + s2);
    z = atanhi[id] - (x * (s1 + s2) - atanlo[id] - x);
    return sign ? -z : z;
}

double muslAtan2(double y, double x) {
    const double pi = 3.1415926535897931160E+00, pi_lo = 1.2246467991473531772E-16;
    double z;
    u32 m, lx, ly, ix, iy;

    if (x != x || y != y) return qnan();
    ix = highWord(x);
    lx = lowWord(x);
    iy = highWord(y);
    ly = lowWord(y);
    if (((ix - 0x3ff00000) | lx) == 0) return muslAtan(y); // x = 1.0
    m = ((iy >> 31) & 1) | ((ix >> 30) & 2);                // 2*sign(x)+sign(y)
    ix = ix & 0x7fffffff;
    iy = iy & 0x7fffffff;

    // when y = 0
    if ((iy | ly) == 0) {
        switch (m) {
        case 0:
        case 1: return y;   // atan(+-0,+anything)=+-0
        case 2: return pi;  // atan(+0,-anything) = pi
        case 3: return -pi; // atan(-0,-anything) =-pi
        }
    }
    // when x = 0
    if ((ix | lx) == 0) return m & 1 ? -pi / 2 : pi / 2;
    // when x is INF
    if (ix == 0x7ff00000) {
        if (iy == 0x7ff00000) {
            switch (m) {
            case 0: return pi / 4;      // atan(+INF,+INF)
            case 1: return -pi / 4;     // atan(-INF,+INF)
            case 2: return 3 * pi / 4;  // atan(+INF,-INF)
            case 3: return -3 * pi / 4; // atan(-INF,-INF)
            }
        } else {
            switch (m) {
            case 0: return 0.0;  // atan(+...,+INF)
            case 1: return -0.0; // atan(-...,+INF)
            case 2: return pi;   // atan(+...,-INF)
            case 3: return -pi;  // atan(-...,-INF)
            }
        }
    }
    // |y/x| > 0x1p64
    if (ix + (64 << 20) < iy || iy == 0x7ff00000) return m & 1 ? -pi / 2 : pi / 2;

    // z = atan(|y/x|) without spurious underflow
    if ((m & 2) && iy + (64 << 20) < ix) // |y/x| < 0x1p-64, x<0
        z = 0;
    else
        z = muslAtan(std::fabs(y / x));
    switch (m) {
    case 0: return z;                  // atan(+,+)
    case 1: return -z;                 // atan(-,+)
    case 2: return pi - (z - pi_lo);   // atan(+,-)
    default: return (z - pi_lo) - pi;  // atan(-,-)
    }
}

double muslAcos(double x) {
    const double pio2_hi = 1.57079632679489655800e+00, pio2_lo = 6.12323399573676603587e-17;
    double z, w, s, c, df;
    u32 hx, ix;

    hx = highWord(x);
    ix = hx & 0x7fffffff;
    // |x| >= 1 or nan
    if (ix >= 0x3ff00000) {
        u32 lx = lowWord(x);
        if (((ix - 0x3ff00000) | lx) == 0) {
            // acos(1)=0, acos(-1)=pi
            if (hx >> 31) return 2 * pio2_hi + 0x1p-120f;
            return 0;
        }
        return qnan();
    }
    // |x| < 0.5
    if (ix < 0x3fe00000) {
        if (ix <= 0x3c600000) // |x| < 2**-57
            return pio2_hi + 0x1p-120f;
        return pio2_hi - (x - (pio2_lo - x * acosR(x * x)));
    }
    // x < -0.5
    if (hx >> 31) {
        z = (1.0 + x) * 0.5;
        s = std::sqrt(z);
        w = acosR(z) * s - pio2_lo;
        return 2 * (pio2_hi - (s + w));
    }
    // x > 0.5
    z = (1.0 - x) * 0.5;
    s = std::sqrt(z);
    df = withLowWord(s, 0);
    c = (z - df * df) / (s + df);
    w = acosR(z) * s + c;
    return 2 * (df + w);
}

// exp.c specialcase
double expSpecialCase(double tmp, u64 sbits, u64 ki) {
    double scale, y;
    if ((ki & 0x80000000) == 0) {
        // k > 0, the exponent of scale might have overflowed by <= 460.
        sbits -= 1009ull << 52;
        scale = asdouble(sbits);
        y = 0x1p1009 * (scale + scale * tmp);
        return y;
    }
    // k < 0, need special care in the subnormal range.
    sbits += 1022ull << 52;
    scale = asdouble(sbits);
    y = scale + scale * tmp;
    if (y < 1.0) {
        // Round y to the right precision before scaling it into the subnormal range to avoid
        // double rounding that can cause 0.5+E/2 ulp error.
        double hi, lo;
        lo = scale - y + scale * tmp;
        hi = 1.0 + y;
        lo = 1.0 - hi + y + lo;
        y = (hi + lo) - 1.0;
        // Avoid -0.0 with downward rounding.
        if (y == 0.0) y = 0.0;
    }
    y = 0x1p-1022 * y;
    return y;
}

inline u32 top12(double x) { return static_cast<u32>(asuint64(x) >> 52); }

double muslExp(double x) {
    u32 abstop;
    u64 ki, idx, top, sbits;
    double kd, z, r, r2, scale, tail, tmp;
    const double C2 = kExpPoly[0], C3 = kExpPoly[1], C4 = kExpPoly[2], C5 = kExpPoly[3];

    abstop = top12(x) & 0x7ff;
    if (abstop - top12(0x1p-54) >= top12(512.0) - top12(0x1p-54)) {
        if (abstop - top12(0x1p-54) >= 0x80000000)
            // Avoid spurious underflow for tiny x. Note: 0 is common input.
            return 1.0 + x;
        if (abstop >= top12(1024.0)) {
            if (asuint64(x) == 0xfff0000000000000ull) return 0.0; // -inf
            if (abstop >= 0x7ff) return 1.0 + x;                  // +inf or NaN
            if (asuint64(x) >> 63) return 0.0;                    // __math_uflow(0)
            return asdouble(0x7ff0000000000000ull);               // __math_oflow(0)
        }
        // Large x is special cased below.
        abstop = 0;
    }

    // exp(x) = 2^(k/N) * exp(r), with exp(r) in [2^(-1/2N),2^(1/2N)].
    // x = ln2/N*k + r, with int k and r in [-ln2/2N, ln2/2N].
    z = kExpInvLn2N * x;
    // z - kd is in [-1, 1] in non-nearest rounding modes.
    kd = z + kExpShift;
    ki = asuint64(kd);
    kd -= kExpShift;
    r = x + kd * kExpNegLn2hiN + kd * kExpNegLn2loN;
    // 2^(k/N) ~= scale * (1 + tail).
    idx = 2 * (ki % kExpN);
    top = ki << (52 - 7);
    tail = asdouble(kExpTab[idx]);
    // This is only a valid scale when -1023*N < k < 1024*N.
    sbits = kExpTab[idx + 1] + top;
    // exp(x) = 2^(k/N) * exp(r) ~= scale + scale * (tail + exp(r) - 1).
    r2 = r * r;
    // Without fma the worst case error is 0.25/N ulp larger.
    tmp = tail + r + r2 * (C2 + r * C3) + r2 * r2 * (C4 + r * C5);
    if (abstop == 0) return expSpecialCase(tmp, sbits, ki);
    scale = asdouble(sbits);
    return scale + scale * tmp;
}

double muslLog(double x) {
    const double* B = kLogPoly1;
    const double* A = kLogPoly;
    double w, z, r, r2, r3, y, invc, logc, kd, hi, lo;
    u64 ix, iz, tmp;
    u32 top;
    int k, i;

    ix = asuint64(x);
    top = static_cast<u32>(ix >> 48);
    const u64 LO = asuint64(1.0 - 0x1p-4);
    const u64 HI = asuint64(1.0 + 0x1.09p-4);
    if (ix - LO < HI - LO) {
        // Handle close to 1.0 inputs separately.
        if (ix == asuint64(1.0)) return 0;
        r = x - 1.0;
        r2 = r * r;
        r3 = r * r2;
        y = r3 * (B[1] + r * B[2] + r2 * B[3] +
                  r3 * (B[4] + r * B[5] + r2 * B[6] + r3 * (B[7] + r * B[8] + r2 * B[9] + r3 * B[10])));
        // Worst-case error is around 0.507 ULP.
        w = r * 0x1p27;
        double rhi = r + w - w;
        double rlo = r - rhi;
        w = rhi * rhi * B[0]; // B[0] == -0.5.
        hi = r + w;
        lo = r - hi + w;
        lo += B[0] * rlo * (rhi + r);
        y += lo;
        y += hi;
        return y;
    }
    if (top - 0x0010 >= 0x7ff0 - 0x0010) {
        // x < 0x1p-1022 or inf or nan.
        if (ix * 2 == 0) return asdouble(0xfff0000000000000ull); // __math_divzero(1)
        if (ix == 0x7ff0000000000000ull) return x;                // log(inf) == inf.
        if ((top & 0x8000) || (top & 0x7ff0) == 0x7ff0) return qnan();
        // x is subnormal, normalize it.
        ix = asuint64(x * 0x1p52);
        ix -= 52ull << 52;
    }

    // x = 2^k z; where z is in range [OFF,2*OFF) and exact. The range is split into N
    // subintervals. The ith subinterval contains z and c is near its center.
    tmp = ix - 0x3fe6000000000000ull;
    i = static_cast<int>((tmp >> (52 - 7)) % kLogN);
    k = static_cast<int>(static_cast<i64>(tmp) >> 52); // arithmetic shift
    iz = ix - (tmp & 0xfffull << 52);
    invc = kLogTab[i].a;
    logc = kLogTab[i].b;
    z = asdouble(iz);

    // log(x) = log1p(z/c-1) + log(c) + k*Ln2. r ~= z/c - 1, |r| < 1/(2*N).
    // musl's !__FP_FAST_FMA branch on every target: rounding error 0x1p-55/N + 0x1p-66.
    r = (z - kLogTab2[i].a - kLogTab2[i].b) * invc;
    kd = static_cast<double>(k);

    // hi + lo = r + log(c) + k*Ln2.
    w = kd * kLogLn2hi + logc;
    hi = w + r;
    lo = w - hi + r + kd * kLogLn2lo;

    // log(x) = lo + (log1p(r) - r) + hi.
    r2 = r * r;
    y = lo + r2 * A[0] + r * r2 * (A[1] + r * A[2] + r2 * (A[3] + r * A[4])) + hi;
    return y;
}

float muslSinf(float x) {
    // Small multiples of pi/2 rounded to double precision.
    const double s1pio2 = 1 * kPiOver2, s2pio2 = 2 * kPiOver2, s3pio2 = 3 * kPiOver2, s4pio2 = 4 * kPiOver2;
    double y;
    u32 ix = asuint(x);
    int n;
    const int sign = static_cast<int>(ix >> 31);
    ix &= 0x7fffffff;

    if (ix <= 0x3f490fda) {         // |x| ~<= pi/4
        if (ix < 0x39800000) return x; // |x| < 2**-12
        return kernelSindf(x);
    }
    if (ix <= 0x407b53d1) {     // |x| ~<= 5*pi/4
        if (ix <= 0x4016cbe3) { // |x| ~<= 3pi/4
            if (sign) return -kernelCosdf(x + s1pio2);
            return kernelCosdf(x - s1pio2);
        }
        return kernelSindf(sign ? -(x + s2pio2) : -(x - s2pio2));
    }
    if (ix <= 0x40e231d5) {     // |x| ~<= 9*pi/4
        if (ix <= 0x40afeddf) { // |x| ~<= 7*pi/4
            if (sign) return kernelCosdf(x + s3pio2);
            return -kernelCosdf(x - s3pio2);
        }
        return kernelSindf(sign ? x + s4pio2 : x - s4pio2);
    }
    if (ix >= 0x7f800000) return qnanf(); // sin(Inf or NaN) is NaN
    // general argument reduction needed
    n = remPio2f(x, &y);
    switch (n & 3) {
    case 0: return kernelSindf(y);
    case 1: return kernelCosdf(y);
    case 2: return kernelSindf(-y);
    default: return -kernelCosdf(y);
    }
}

float muslCosf(float x) {
    const double c1pio2 = 1 * kPiOver2, c2pio2 = 2 * kPiOver2, c3pio2 = 3 * kPiOver2, c4pio2 = 4 * kPiOver2;
    double y;
    u32 ix = asuint(x);
    unsigned n;
    const unsigned sign = ix >> 31;
    ix &= 0x7fffffff;

    if (ix <= 0x3f490fda) {           // |x| ~<= pi/4
        if (ix < 0x39800000) return 1.0f; // |x| < 2**-12
        return kernelCosdf(x);
    }
    if (ix <= 0x407b53d1) { // |x| ~<= 5*pi/4
        if (ix > 0x4016cbe3) // |x|  ~> 3*pi/4
            return -kernelCosdf(sign ? x + c2pio2 : x - c2pio2);
        if (sign) return kernelSindf(x + c1pio2);
        return kernelSindf(c1pio2 - x);
    }
    if (ix <= 0x40e231d5) { // |x| ~<= 9*pi/4
        if (ix > 0x40afeddf) // |x| ~> 7*pi/4
            return kernelCosdf(sign ? x + c4pio2 : x - c4pio2);
        if (sign) return kernelSindf(-x - c3pio2);
        return kernelSindf(x - c3pio2);
    }
    if (ix >= 0x7f800000) return qnanf(); // cos(Inf or NaN) is NaN
    // general argument reduction needed
    n = static_cast<unsigned>(remPio2f(x, &y));
    switch (n & 3) {
    case 0: return kernelCosdf(y);
    case 1: return kernelSindf(-y);
    case 2: return -kernelCosdf(y);
    default: return kernelSindf(y);
    }
}

float muslTanf(float x) {
    const double t1pio2 = 1 * kPiOver2, t2pio2 = 2 * kPiOver2, t3pio2 = 3 * kPiOver2, t4pio2 = 4 * kPiOver2;
    double y;
    u32 ix = asuint(x);
    unsigned n;
    const unsigned sign = ix >> 31;
    ix &= 0x7fffffff;

    if (ix <= 0x3f490fda) {         // |x| ~<= pi/4
        if (ix < 0x39800000) return x; // |x| < 2**-12
        return kernelTandf(x, 0);
    }
    if (ix <= 0x407b53d1) {     // |x| ~<= 5*pi/4
        if (ix <= 0x4016cbe3) // |x| ~<= 3pi/4
            return kernelTandf((sign ? x + t1pio2 : x - t1pio2), 1);
        return kernelTandf((sign ? x + t2pio2 : x - t2pio2), 0);
    }
    if (ix <= 0x40e231d5) {     // |x| ~<= 9*pi/4
        if (ix <= 0x40afeddf) // |x| ~<= 7*pi/4
            return kernelTandf((sign ? x + t3pio2 : x - t3pio2), 1);
        return kernelTandf((sign ? x + t4pio2 : x - t4pio2), 0);
    }
    if (ix >= 0x7f800000) return qnanf(); // tan(Inf or NaN) is NaN
    // argument reduction
    n = static_cast<unsigned>(remPio2f(x, &y));
    return kernelTandf(y, static_cast<int>(n & 1));
}

float muslAtanf(float x) {
    static const float atanhi[] = {4.6364760399e-01, 7.8539812565e-01, 9.8279368877e-01, 1.5707962513e+00};
    static const float atanlo[] = {5.0121582440e-09, 3.7748947079e-08, 3.4473217170e-08, 7.5497894159e-08};
    static const float aT[] = {3.3333328366e-01, -1.9999158382e-01, 1.4253635705e-01, -1.0648017377e-01,
                               6.1687607318e-02};
    float w, s1, s2, z;
    u32 ix, sign;
    int id;

    ix = asuint(x);
    sign = ix >> 31;
    ix &= 0x7fffffff;
    if (ix >= 0x4c800000) { // if |x| >= 2**26
        if (x != x) return x;
        z = atanhi[3] + 0x1p-120f;
        return sign ? -z : z;
    }
    if (ix < 0x3ee00000) {     // |x| < 0.4375
        if (ix < 0x39800000) return x; // |x| < 2**-12
        id = -1;
    } else {
        x = std::fabs(x);
        if (ix < 0x3f980000) {     // |x| < 1.1875
            if (ix < 0x3f300000) { //  7/16 <= |x| < 11/16
                id = 0;
                x = (2.0f * x - 1.0f) / (2.0f + x);
            } else { // 11/16 <= |x| < 19/16
                id = 1;
                x = (x - 1.0f) / (x + 1.0f);
            }
        } else {
            if (ix < 0x401c0000) { // |x| < 2.4375
                id = 2;
                x = (x - 1.5f) / (1.0f + 1.5f * x);
            } else { // 2.4375 <= |x| < 2**26
                id = 3;
                x = -1.0f / x;
            }
        }
    }
    // end of argument reduction
    z = x * x;
    w = z * z;
    // break sum from i=0 to 10 aT[i]z**(i+1) into odd and even poly
    s1 = z * (aT[0] + w * (aT[2] + w * aT[4]));
    s2 = w * (aT[1] + w * aT[3]);
    if (id < 0) return x - x * (s1 + s2);
    z = atanhi[id] - ((x * (s1 + s2) - atanlo[id]) - x);
    return sign ? -z : z;
}

float muslAtan2f(float y, float x) {
    const float pi = 3.1415927410e+00, pi_lo = -8.7422776573e-08;
    float z;
    u32 m, ix, iy;

    if (x != x || y != y) return qnanf();
    ix = asuint(x);
    iy = asuint(y);
    if (ix == 0x3f800000) return muslAtanf(y); // x=1.0
    m = ((iy >> 31) & 1) | ((ix >> 30) & 2);    // 2*sign(x)+sign(y)
    ix &= 0x7fffffff;
    iy &= 0x7fffffff;

    // when y = 0
    if (iy == 0) {
        switch (m) {
        case 0:
        case 1: return y;   // atan(+-0,+anything)=+-0
        case 2: return pi;  // atan(+0,-anything) = pi
        case 3: return -pi; // atan(-0,-anything) =-pi
        }
    }
    // when x = 0
    if (ix == 0) return m & 1 ? -pi / 2 : pi / 2;
    // when x is INF
    if (ix == 0x7f800000) {
        if (iy == 0x7f800000) {
            switch (m) {
            case 0: return pi / 4;      // atan(+INF,+INF)
            case 1: return -pi / 4;     // atan(-INF,+INF)
            case 2: return 3 * pi / 4;  // atan(+INF,-INF)
            case 3: return -3 * pi / 4; // atan(-INF,-INF)
            }
        } else {
            switch (m) {
            case 0: return 0.0f;  // atan(+...,+INF)
            case 1: return -0.0f; // atan(-...,+INF)
            case 2: return pi;    // atan(+...,-INF)
            case 3: return -pi;   // atan(-...,-INF)
            }
        }
    }
    // |y/x| > 0x1p26
    if (ix + (26 << 23) < iy || iy == 0x7f800000) return m & 1 ? -pi / 2 : pi / 2;

    // z = atan(|y/x|) with correct underflow
    if ((m & 2) && iy + (26 << 23) < ix) // |y/x| < 0x1p-26, x < 0
        z = 0.0;
    else
        z = muslAtanf(std::fabs(y / x));
    switch (m) {
    case 0: return z;                 // atan(+,+)
    case 1: return -z;                // atan(-,+)
    case 2: return pi - (z - pi_lo);  // atan(+,-)
    default: return (z - pi_lo) - pi; // atan(-,-)
    }
}

float muslAcosf(float x) {
    const float pio2_hi = 1.5707962513e+00, pio2_lo = 7.5497894159e-08;
    float z, w, s, c, df;
    u32 hx, ix;

    hx = asuint(x);
    ix = hx & 0x7fffffff;
    // |x| >= 1 or nan
    if (ix >= 0x3f800000) {
        if (ix == 0x3f800000) {
            if (hx >> 31) return 2 * pio2_hi + 0x1p-120f;
            return 0;
        }
        return qnanf();
    }
    // |x| < 0.5
    if (ix < 0x3f000000) {
        if (ix <= 0x32800000) // |x| < 2**-26
            return pio2_hi + 0x1p-120f;
        return pio2_hi - (x - (pio2_lo - x * acosfR(x * x)));
    }
    // x < -0.5
    if (hx >> 31) {
        z = (1 + x) * 0.5f;
        s = std::sqrt(z);
        w = acosfR(z) * s - pio2_lo;
        return 2 * (pio2_hi - (s + w));
    }
    // x > 0.5
    z = (1 - x) * 0.5f;
    s = std::sqrt(z);
    df = asfloat(asuint(s) & 0xfffff000);
    c = (z - df * df) / (s + df);
    w = acosfR(z) * s + c;
    return 2 * (df + w);
}

float muslAsinf(float x) {
    const double pio2 = 1.570796326794896558e+00;
    double s;
    float z;
    u32 hx, ix;

    hx = asuint(x);
    ix = hx & 0x7fffffff;
    if (ix >= 0x3f800000) {                               // |x| >= 1
        if (ix == 0x3f800000) return static_cast<float>(x * pio2 + 0x1p-120f); // asin(+-1) = +-pi/2
        return qnanf();                                     // asin(|x|>1) is NaN
    }
    if (ix < 0x3f000000) { // |x| < 0.5
        // if 0x1p-126 <= |x| < 0x1p-12, avoid raising underflow
        if (ix < 0x39800000 && ix >= 0x00800000) return x;
        return x + x * acosfR(x * x);
    }
    // 1 > |x| >= 0.5
    z = (1 - std::fabs(x)) * 0.5f;
    s = std::sqrt(static_cast<double>(z));
    x = static_cast<float>(pio2 - 2 * (s + s * acosfR(z)));
    if (hx >> 31) return -x;
    return x;
}

inline u32 top12f(float x) { return asuint(x) >> 20; }

float muslExpf(float x) {
    u32 abstop;
    u64 ki, t;
    double kd, xd, z, r, r2, y, s;
    const double* C = kExp2fPolyScaled;

    xd = static_cast<double>(x);
    abstop = top12f(x) & 0x7ff;
    if (abstop >= top12f(88.0f)) {
        // |x| >= 88 or x is nan.
        if (asuint(x) == 0xff800000u) return 0.0f;    // -inf
        if (abstop >= top12f(asfloat(0x7f800000u))) return x + x; // +inf or NaN
        if (x > 0x1.62e42ep6f) return asfloat(0x7f800000u); // x > log(0x1p128) ~= 88.72: overflow
        if (x < -0x1.9fe368p6f) return 0.0f;                 // x < log(0x1p-150) ~= -103.97: underflow
    }

    // x*N/Ln2 = k + r with r in [-1/2, 1/2] and int k.
    z = kExp2fInvLn2Scaled * xd;

    // Round and convert z to int, the result is in [-150*N, 128*N].
    kd = z + kExp2fShift;
    ki = asuint64(kd);
    kd -= kExp2fShift;
    r = z - kd;

    // exp(x) = 2^(k/N) * 2^(r/N) ~= s * (C0*r^3 + C1*r^2 + C2*r + 1)
    t = kExp2fTab[ki % kExp2fN];
    t += ki << (52 - 5);
    s = asdouble(t);
    z = C[0] * r + C[1];
    r2 = r * r;
    y = C[2] * r + 1;
    y = z * r2 + y;
    y = y * s;
    return static_cast<float>(y);
}

float muslLogf(float x) {
    double z, r, r2, y, y0, invc, logc;
    u32 ix, iz, tmp;
    int k, i;
    const double* A = kLogfPoly;

    ix = asuint(x);
    // Fix sign of zero with downward rounding when x==1.
    if (ix == 0x3f800000) return 0;
    if (ix - 0x00800000 >= 0x7f800000 - 0x00800000) {
        // x < 0x1p-126 or inf or nan.
        if (ix * 2 == 0) return asfloat(0xff800000u); // __math_divzerof(1)
        if (ix == 0x7f800000) return x;              // log(inf) == inf.
        if ((ix & 0x80000000) || ix * 2 >= 0xff000000) return qnanf();
        // x is subnormal, normalize it.
        ix = asuint(x * 0x1p23f);
        ix -= 23 << 23;
    }

    // x = 2^k z; where z is in range [OFF,2*OFF] and exact. The range is split into N
    // subintervals. The ith subinterval contains z and c is near its center.
    tmp = ix - 0x3f330000;
    i = static_cast<int>((tmp >> (23 - 4)) % 16);
    k = static_cast<i32>(tmp) >> 23; // arithmetic shift
    iz = ix - (tmp & 0xff800000u);
    invc = kLogfTab[i].a;
    logc = kLogfTab[i].b;
    z = static_cast<double>(asfloat(iz));

    // log(x) = log1p(z/c-1) + log(c) + k*Ln2
    r = z * invc - 1;
    y0 = logc + static_cast<double>(k) * kLogfLn2;

    // Pipelined polynomial evaluation to approximate log1p(r).
    r2 = r * r;
    y = A[1] * r + A[2];
    y = A[0] * r2 + y;
    y = y * r2 + (y0 + r);
    return static_cast<float>(y);
}

// powf.c helpers (POWF_SCALE_BITS 0: no TOINT_INTRINSICS)
double powfLog2Inline(u32 ix) {
    const double* A = kPowfLog2Poly;
    double z, r, r2, r4, p, q, y, y0, invc, logc;
    u32 iz, top, tmp;
    int k, i;

    // x = 2^k z; where z is in range [OFF,2*OFF] and exact.
    tmp = ix - 0x3f330000;
    i = static_cast<int>((tmp >> (23 - 4)) % 16);
    top = tmp & 0xff800000u;
    iz = ix - top;
    k = static_cast<i32>(top) >> 23; // arithmetic shift
    invc = kPowfLog2Tab[i].a;
    logc = kPowfLog2Tab[i].b;
    z = static_cast<double>(asfloat(iz));

    // log2(x) = log1p(z/c-1)/ln2 + log2(c) + k
    r = z * invc - 1;
    y0 = logc + static_cast<double>(k);

    // Pipelined polynomial evaluation to approximate log1p(r)/ln2.
    r2 = r * r;
    y = A[0] * r + A[1];
    p = A[2] * r + A[3];
    r4 = r2 * r2;
    q = A[4] * r + y0;
    q = p * r2 + q;
    y = y * r4 + q;
    return y;
}

constexpr u32 kPowfSignBias = 1u << (5 + 11);

float powfExp2Inline(double xd, u32 signBias) {
    u64 ki, ski, t;
    double kd, z, r, r2, y, s;
    const double* C = kExp2fPoly;

    // x = k/N + r with r in [-1/(2N), 1/(2N)]
    kd = xd + kExp2fShiftScaled;
    ki = asuint64(kd);
    kd -= kExp2fShiftScaled; // k/N
    r = xd - kd;

    // exp2(x) = 2^(k/N) * 2^r ~= s * (C0*r^3 + C1*r^2 + C2*r + 1)
    t = kExp2fTab[ki % kExp2fN];
    ski = ki + signBias;
    t += ski << (52 - 5);
    s = asdouble(t);
    z = C[0] * r + C[1];
    r2 = r * r;
    y = C[2] * r + 1;
    y = z * r2 + y;
    y = y * s;
    return static_cast<float>(y);
}

// Returns 0 if not int, 1 if odd int, 2 if even int. The argument is the bit representation
// of a non-zero finite floating-point value.
int powfCheckInt(u32 iy) {
    int e = iy >> 23 & 0xff;
    if (e < 0x7f) return 0;
    if (e > 0x7f + 23) return 2;
    if (iy & ((1u << (0x7f + 23 - e)) - 1)) return 0;
    if (iy & (1u << (0x7f + 23 - e))) return 1;
    return 2;
}

inline bool powfZeroInfNan(u32 ix) { return 2 * ix - 1 >= 2u * 0x7f800000 - 1; }

float muslPowf(float x, float y) {
    u32 signBias = 0;
    u32 ix, iy;
    const float inf = asfloat(0x7f800000u);

    ix = asuint(x);
    iy = asuint(y);
    if (ix - 0x00800000 >= 0x7f800000 - 0x00800000 || powfZeroInfNan(iy)) {
        // Either (x < 0x1p-126 or inf or nan) or (y is 0 or inf or nan).
        if (powfZeroInfNan(iy)) {
            if (2 * iy == 0) return 1.0f;
            if (ix == 0x3f800000) return 1.0f;
            if (2 * ix > 2u * 0x7f800000 || 2 * iy > 2u * 0x7f800000) return qnanf();
            if (2 * ix == 2 * 0x3f800000) return 1.0f;
            if ((2 * ix < 2 * 0x3f800000) == !(iy & 0x80000000))
                return 0.0f; // |x|<1 && y==inf or |x|>1 && y==-inf.
            return y * y;
        }
        if (powfZeroInfNan(ix)) {
            float x2 = x * x;
            if (ix & 0x80000000 && powfCheckInt(iy) == 1) x2 = -x2;
            return iy & 0x80000000 ? 1 / x2 : x2;
        }
        // x and y are non-zero finite.
        if (ix & 0x80000000) {
            // Finite x < 0.
            int yint = powfCheckInt(iy);
            if (yint == 0) return qnanf();
            if (yint == 1) signBias = kPowfSignBias;
            ix &= 0x7fffffff;
        }
        if (ix < 0x00800000) {
            // Normalize subnormal x so exponent becomes negative.
            ix = asuint(x * 0x1p23f);
            ix &= 0x7fffffff;
            ix -= 23 << 23;
        }
    }
    double logx = powfLog2Inline(ix);
    double ylogx = y * logx; // cannot overflow, y is single prec.
    if ((asuint64(ylogx) >> 47 & 0xffff) >= asuint64(126.0) >> 47) {
        // |y*log(x)| >= 126.
        if (ylogx > 0x1.fffffffd1d571p+6) return signBias ? -inf : inf; // __math_oflowf
        if (ylogx <= -150.0) return signBias ? -0.0f : 0.0f;            // __math_uflowf
    }
    return powfExp2Inline(ylogx, signBias);
}

} // namespace

float simSinf(float x) { return canon(muslSinf(x)); }
float simCosf(float x) { return canon(muslCosf(x)); }
float simTanf(float x) { return canon(muslTanf(x)); }
float simAtanf(float x) { return canon(muslAtanf(x)); }
float simAtan2f(float y, float x) { return canon(muslAtan2f(y, x)); }
float simAcosf(float x) { return canon(muslAcosf(x)); }
float simAsinf(float x) { return canon(muslAsinf(x)); }
float simExpf(float x) { return canon(muslExpf(x)); }
float simLogf(float x) { return canon(muslLogf(x)); }
float simPowf(float x, float y) { return canon(muslPowf(x, y)); }

double simSin(double x) { return canon(muslSin(x)); }
double simCos(double x) { return canon(muslCos(x)); }
double simTan(double x) { return canon(muslTan(x)); }
double simAtan(double x) { return canon(muslAtan(x)); }
double simAtan2(double y, double x) { return canon(muslAtan2(y, x)); }
double simAcos(double x) { return canon(muslAcos(x)); }
double simExp(double x) { return canon(muslExp(x)); }
double simLog(double x) { return canon(muslLog(x)); }

// ---- The known-answer table (simMathCheckHash, apps/tests/sim_math_test.cpp) ---------------
//
// Per function, kSimMathCheckInputs inputs: 120 evenly spread over the range the game uses,
// 50 pseudo-random ones in the same range (xorshift32, seeded per function), 30 special values
// (multiples of pi/2 and their neighbours for the trigonometric functions; zeros, ones, tiny,
// huge, infinities and NaN for all). The inputs are computed with exact or correctly rounded
// operations only, so every platform builds the same table.

namespace {

struct CheckFn {
    const char* name;
    bool isFloat;
    bool twoArgs;
    bool trig;
    double lo, hi;   // first argument
    double lo2, hi2; // second argument
};

const CheckFn kCheckFns[kSimMathCheckFunctions] = {
    {"simSinf", true, false, true, -12.6, 12.6, 0, 0},
    {"simCosf", true, false, true, -12.6, 12.6, 0, 0},
    {"simTanf", true, false, true, -12.6, 12.6, 0, 0},
    {"simAtanf", true, false, false, -50.0, 50.0, 0, 0},
    {"simAtan2f", true, true, false, -100.0, 100.0, -100.0, 100.0},
    {"simAcosf", true, false, false, -1.0, 1.0, 0, 0},
    {"simAsinf", true, false, false, -1.0, 1.0, 0, 0},
    {"simExpf", true, false, false, -20.0, 20.0, 0, 0},
    {"simLogf", true, false, false, 1e-3, 1e4, 0, 0},
    {"simPowf", true, true, false, 0.01, 10.0, -4.0, 4.0},
    {"simSin", false, false, true, -12.6, 12.6, 0, 0},
    {"simCos", false, false, true, -12.6, 12.6, 0, 0},
    {"simTan", false, false, true, -12.6, 12.6, 0, 0},
    {"simAtan", false, false, false, -50.0, 50.0, 0, 0},
    {"simAtan2", false, true, false, -100.0, 100.0, -100.0, 100.0},
    {"simAcos", false, false, false, -1.0, 1.0, 0, 0},
    {"simExp", false, false, false, -20.0, 20.0, 0, 0},
    {"simLog", false, false, false, 1e-3, 1e4, 0, 0},
};

constexpr int kCheckSpread = 120;
constexpr int kCheckRandom = 50;
constexpr int kCheckSpecial = 30;
static_assert(kCheckSpread + kCheckRandom + kCheckSpecial == kSimMathCheckInputs, "table size");

const double kCheckSpecials[kCheckSpecial] = {
    0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 2.0, -2.0, 10.0, -10.0,
    1e-30, -1e-30, 1e-6, 3e-4, 0.999, 1.001, 88.5, -103.5, 700.0, -740.0,
    1e5, -1e5, 1e9, 1e30, -1e30, 1e300, 0, 0, 0, 0, // the last four: inf, -inf, NaN, subnormal
};

double checkSpecial(int k) {
    switch (k) {
    case 26: return asdouble(0x7ff0000000000000ull);
    case 27: return asdouble(0xfff0000000000000ull);
    case 28: return qnan();
    case 29: return asdouble(0x0000000000000123ull);
    default: return kCheckSpecials[k];
    }
}

// Trigonometric specials: k*pi/2 for k in -4..4 (in the argument's precision) and the
// neighbouring values one ulp either side; then 0, -0 and huge arguments.
double checkTrigSpecial(int k, bool isFloat) {
    if (k < 27) {
        const int m = k / 3 - 4, side = k % 3; // side 0: below, 1: at, 2: above
        if (isFloat) {
            float v = static_cast<float>(m * kPiOver2);
            u32 b = asuint(v);
            if (side != 1 && m != 0) b += ((side == 2) == (v > 0.0f)) ? 1u : static_cast<u32>(-1);
            if (side != 1 && m == 0) b = side == 2 ? 1u : 0x80000001u;
            return static_cast<double>(asfloat(b));
        }
        double v = m * kPiOver2;
        u64 b = asuint64(v);
        if (side != 1 && m != 0) b += ((side == 2) == (v > 0.0)) ? 1ull : static_cast<u64>(-1);
        if (side != 1 && m == 0) b = side == 2 ? 1ull : 0x8000000000000001ull;
        return asdouble(b);
    }
    const double big[3] = {1e5, 1e9, 1e30};
    return big[k - 27];
}

// double -> float without the undefined conversion of a finite value beyond the float range.
float toFloat(double v) {
    if (v > 3.4028234663852886e38) return asfloat(0x7f800000u);
    if (v < -3.4028234663852886e38) return asfloat(0xff800000u);
    return static_cast<float>(v);
}

void checkArgs(int f, int i, double& a, double& b) {
    const CheckFn& c = kCheckFns[f];
    a = 0.0;
    b = 0.0;
    if (i < kCheckSpread) {
        const double t = static_cast<double>(i) / (kCheckSpread - 1);
        a = c.lo + (c.hi - c.lo) * t;
        // The second argument walks the other way, at another pace, so the pairs cover the plane.
        const double t2 = static_cast<double>((i * 37) % kCheckSpread) / (kCheckSpread - 1);
        b = c.lo2 + (c.hi2 - c.lo2) * t2;
    } else if (i < kCheckSpread + kCheckRandom) {
        u32 s = static_cast<u32>(f + 1) * 0x9E3779B9u;
        double u = 0.0, u2 = 0.0;
        for (int k = 0; k <= i - kCheckSpread; ++k) {
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            u = static_cast<double>(s) / 4294967296.0;
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            u2 = static_cast<double>(s) / 4294967296.0;
        }
        a = c.lo + (c.hi - c.lo) * u;
        b = c.lo2 + (c.hi2 - c.lo2) * u2;
    } else {
        const int k = i - kCheckSpread - kCheckRandom;
        a = c.trig ? checkTrigSpecial(k, c.isFloat) : checkSpecial(k);
        b = c.twoArgs ? checkSpecial((k * 7 + 3) % kCheckSpecial) : 0.0;
        // Fixed pairs where the second argument matters most: atan2(y, +-0), pow(x, integer).
        if (c.twoArgs && k < 4) b = k < 2 ? (k == 0 ? 0.0 : -0.0) : (k == 2 ? 3.0 : -2.0);
    }
    if (c.isFloat) {
        a = static_cast<double>(toFloat(a));
        b = static_cast<double>(toFloat(b));
    }
}

u64 checkBits(int f, int i) {
    double a, b;
    checkArgs(f, i, a, b);
    const float fa = toFloat(a), fb = toFloat(b);
    switch (f) {
    case 0: return asuint(simSinf(fa));
    case 1: return asuint(simCosf(fa));
    case 2: return asuint(simTanf(fa));
    case 3: return asuint(simAtanf(fa));
    case 4: return asuint(simAtan2f(fa, fb));
    case 5: return asuint(simAcosf(fa));
    case 6: return asuint(simAsinf(fa));
    case 7: return asuint(simExpf(fa));
    case 8: return asuint(simLogf(fa));
    case 9: return asuint(simPowf(fa, fb));
    case 10: return asuint64(simSin(a));
    case 11: return asuint64(simCos(a));
    case 12: return asuint64(simTan(a));
    case 13: return asuint64(simAtan(a));
    case 14: return asuint64(simAtan2(a, b));
    case 15: return asuint64(simAcos(a));
    case 16: return asuint64(simExp(a));
    default: return asuint64(simLog(a));
    }
}

constexpr u64 kFnvOffset = 0xcbf29ce484222325ull;
constexpr u64 kFnvPrime = 0x100000001b3ull;

// FNV-1a over the result's bytes, little-endian, 4 bytes for a float and 8 for a double.
u64 fnvResult(u64 h, u64 bits, bool isFloat) {
    const int n = isFloat ? 4 : 8;
    for (int k = 0; k < n; ++k) {
        h ^= (bits >> (8 * k)) & 0xff;
        h *= kFnvPrime;
    }
    return h;
}

} // namespace

const char* simMathCheckName(int f) { return f >= 0 && f < kSimMathCheckFunctions ? kCheckFns[f].name : ""; }

bool simMathCheckIsFloat(int f) { return f >= 0 && f < kSimMathCheckFunctions && kCheckFns[f].isFloat; }

bool simMathCheckTwoArgs(int f) { return f >= 0 && f < kSimMathCheckFunctions && kCheckFns[f].twoArgs; }

void simMathCheckArgs(int f, int i, double& a, double& b) {
    a = b = 0.0;
    if (f < 0 || f >= kSimMathCheckFunctions || i < 0 || i >= kSimMathCheckInputs) return;
    checkArgs(f, i, a, b);
}

u64 simMathCheckBits(int f, int i) {
    if (f < 0 || f >= kSimMathCheckFunctions || i < 0 || i >= kSimMathCheckInputs) return 0;
    return checkBits(f, i);
}

u64 simMathCheckHashOf(int f) {
    if (f < 0 || f >= kSimMathCheckFunctions) return 0;
    u64 h = kFnvOffset;
    for (int i = 0; i < kSimMathCheckInputs; ++i) h = fnvResult(h, checkBits(f, i), kCheckFns[f].isFloat);
    return h;
}

u64 simMathCheckHash() {
    u64 h = kFnvOffset;
    for (int f = 0; f < kSimMathCheckFunctions; ++f)
        for (int i = 0; i < kSimMathCheckInputs; ++i) h = fnvResult(h, checkBits(f, i), kCheckFns[f].isFloat);
    return h;
}

} // namespace as3d
