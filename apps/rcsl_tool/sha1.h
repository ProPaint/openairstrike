// Minimal local SHA-1 (no third-party dependency), used only to compare a trace against
// testdata/golden/<game>/rcsl_trace_hashes.json. Same algorithm as apps/tests/tga_test.cpp's
// local implementation.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace rcsl_tool {

inline uint32_t sha1Rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

inline std::string sha1Hex(const std::string& data) {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;

    std::string msg = data;
    uint64_t bitLen = static_cast<uint64_t>(data.size()) * 8ULL;
    msg.push_back(static_cast<char>(0x80));
    while (msg.size() % 64 != 56) msg.push_back(static_cast<char>(0));
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<char>((bitLen >> (i * 8)) & 0xFF));

    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            size_t o = chunk + static_cast<size_t>(i) * 4;
            w[i] = (static_cast<uint32_t>(static_cast<uint8_t>(msg[o])) << 24) |
                   (static_cast<uint32_t>(static_cast<uint8_t>(msg[o + 1])) << 16) |
                   (static_cast<uint32_t>(static_cast<uint8_t>(msg[o + 2])) << 8) |
                   static_cast<uint32_t>(static_cast<uint8_t>(msg[o + 3]));
        }
        for (int i = 16; i < 80; ++i) {
            uint32_t v = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
            w[i] = sha1Rotl(v, 1);
        }
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t temp = sha1Rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = sha1Rotl(b, 30);
            b = a;
            a = temp;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    char buf[41];
    std::snprintf(buf, sizeof buf, "%08x%08x%08x%08x%08x", h0, h1, h2, h3, h4);
    return std::string(buf);
}

} // namespace rcsl_tool
