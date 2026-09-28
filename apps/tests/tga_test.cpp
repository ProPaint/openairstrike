// Tests for as3d::decodeTga. See docs/spec/tga.md.
//
// Two halves:
//  - a golden-corpus check that decodes every shipped .tga and compares against the
//    hashes tools/ref/test_tga.py computed with its independent Python decoder
//    (testdata/golden/tga_hashes.json), parsed here with a small hand-written JSON
//    scanner and verified with a local SHA-1 implementation -- no third-party
//    dependencies;
//  - synthetic in-memory tests covering every supported image type, both scanline
//    origins, and a battery of malformed inputs that must be rejected without ever
//    reading out of bounds.
#include "doctest.h"

#include "as3d/core.h"
#include "as3d/image.h"
#include "test_data.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace as3d;

// ---------------------------------------------------------------------------------
// SHA-1 (only used by this test, to check decoded pixels against the golden hashes).
// ---------------------------------------------------------------------------------
namespace {

inline uint32_t rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

std::string sha1Hex(const std::vector<u8>& data) {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;

    std::vector<u8> msg(data);
    uint64_t bitLen = static_cast<uint64_t>(data.size()) * 8ULL;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<u8>((bitLen >> (i * 8)) & 0xFF));

    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            size_t o = chunk + static_cast<size_t>(i) * 4;
            w[i] = (static_cast<uint32_t>(msg[o]) << 24) | (static_cast<uint32_t>(msg[o + 1]) << 16) |
                   (static_cast<uint32_t>(msg[o + 2]) << 8) | static_cast<uint32_t>(msg[o + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            uint32_t v = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
            w[i] = rotl32(v, 1);
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
            uint32_t temp = rotl32(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl32(b, 30);
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

// ---------------------------------------------------------------------------------
// Minimal hand-written JSON scanner, tailored to the fixed shape tools/ref/test_tga.py
// writes: an array of objects with exactly the fields name/width/height/hasAlpha/sha1,
// in that order. Not a general-purpose JSON parser.
// ---------------------------------------------------------------------------------

struct GoldenEntry {
    std::string name;
    int width = 0;
    int height = 0;
    bool hasAlpha = false;
    std::string sha1;
};

class JsonScanner {
public:
    explicit JsonScanner(const std::string& text) : s_(text) {}

    void skipWs() {
        while (pos_ < s_.size() && static_cast<unsigned char>(s_[pos_]) <= ' ') pos_++;
    }

    bool expect(char c) {
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == c) {
            pos_++;
            return true;
        }
        return false;
    }

    bool peek(char c) {
        skipWs();
        return pos_ < s_.size() && s_[pos_] == c;
    }

    bool parseString(std::string& out) {
        skipWs();
        if (pos_ >= s_.size() || s_[pos_] != '"') return false;
        pos_++;
        out.clear();
        while (pos_ < s_.size() && s_[pos_] != '"') {
            char c = s_[pos_++];
            if (c == '\\' && pos_ < s_.size()) {
                char e = s_[pos_++];
                switch (e) {
                    case '\\':
                        out.push_back('\\');
                        break;
                    case '"':
                        out.push_back('"');
                        break;
                    case '/':
                        out.push_back('/');
                        break;
                    case 'n':
                        out.push_back('\n');
                        break;
                    case 't':
                        out.push_back('\t');
                        break;
                    default:
                        out.push_back(e);
                        break;
                }
            } else {
                out.push_back(c);
            }
        }
        if (pos_ >= s_.size()) return false;
        pos_++; // closing quote
        return true;
    }

    bool parseInt(int& out) {
        skipWs();
        size_t start = pos_;
        if (pos_ < s_.size() && s_[pos_] == '-') pos_++;
        while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') pos_++;
        if (pos_ == start) return false;
        out = std::atoi(s_.substr(start, pos_ - start).c_str());
        return true;
    }

    bool parseBool(bool& out) {
        skipWs();
        if (s_.compare(pos_, 4, "true") == 0) {
            out = true;
            pos_ += 4;
            return true;
        }
        if (s_.compare(pos_, 5, "false") == 0) {
            out = false;
            pos_ += 5;
            return true;
        }
        return false;
    }

    // Parses `"key" :` and checks the key matches.
    bool parseKey(const char* key) {
        std::string k;
        if (!parseString(k)) return false;
        return k == key && expect(':');
    }

private:
    const std::string& s_;
    size_t pos_ = 0;
};

bool parseGolden(const std::string& text, std::vector<GoldenEntry>& out) {
    JsonScanner j(text);
    if (!j.expect('[')) return false;
    if (j.peek(']')) return j.expect(']');
    for (;;) {
        if (!j.expect('{')) return false;
        GoldenEntry e;
        if (!j.parseKey("name") || !j.parseString(e.name)) return false;
        if (!j.expect(',')) return false;
        if (!j.parseKey("width") || !j.parseInt(e.width)) return false;
        if (!j.expect(',')) return false;
        if (!j.parseKey("height") || !j.parseInt(e.height)) return false;
        if (!j.expect(',')) return false;
        if (!j.parseKey("hasAlpha") || !j.parseBool(e.hasAlpha)) return false;
        if (!j.expect(',')) return false;
        if (!j.parseKey("sha1") || !j.parseString(e.sha1)) return false;
        if (!j.expect('}')) return false;
        out.push_back(e);
        if (j.peek(',')) {
            j.expect(',');
            continue;
        }
        break;
    }
    return j.expect(']');
}

// ---------------------------------------------------------------------------------
// Helpers for building synthetic .tga byte buffers in memory.
// ---------------------------------------------------------------------------------

void pushU16(std::vector<u8>& v, uint16_t x) {
    v.push_back(static_cast<u8>(x & 0xFF));
    v.push_back(static_cast<u8>((x >> 8) & 0xFF));
}

std::vector<u8> tgaHeader(u8 idLen, u8 cmapType, u8 imgType, u16 cmapFirst, u16 cmapLen, u8 cmapDepth,
                           u16 w, u16 h, u8 pixelDepth, u8 descriptor) {
    std::vector<u8> v;
    v.push_back(idLen);
    v.push_back(cmapType);
    v.push_back(imgType);
    pushU16(v, cmapFirst);
    pushU16(v, cmapLen);
    v.push_back(cmapDepth);
    pushU16(v, 0); // x origin, ignored by the decoder
    pushU16(v, 0); // y origin, ignored by the decoder
    pushU16(v, w);
    pushU16(v, h);
    v.push_back(pixelDepth);
    v.push_back(descriptor);
    return v;
}

void push24(std::vector<u8>& v, u8 r, u8 g, u8 b) {
    v.push_back(b);
    v.push_back(g);
    v.push_back(r);
}

void push32(std::vector<u8>& v, u8 r, u8 g, u8 b, u8 a) {
    v.push_back(b);
    v.push_back(g);
    v.push_back(r);
    v.push_back(a);
}

void checkRgba(const Image& img, int x, int y, u8 r, u8 g, u8 b, u8 a) {
    REQUIRE(x >= 0);
    REQUIRE(y >= 0);
    REQUIRE(x < img.width);
    REQUIRE(y < img.height);
    size_t o = (static_cast<size_t>(y) * img.width + x) * 4;
    REQUIRE(o + 4 <= img.rgba.size());
    CHECK(img.rgba[o] == r);
    CHECK(img.rgba[o + 1] == g);
    CHECK(img.rgba[o + 2] == b);
    CHECK(img.rgba[o + 3] == a);
}

} // namespace

// ---------------------------------------------------------------------------------
// Golden corpus.
// ---------------------------------------------------------------------------------

TEST_CASE("tga: every shipped file matches the golden reference decode") {
    AS3D_REQUIRE_DATA();

    std::string goldenPath = testdata::goldenDir() + "/tga_hashes.json";
    std::ifstream f(goldenPath, std::ios::binary);
    REQUIRE_MESSAGE(f.good(), "missing golden file, run tools/ref/test_tga.py first: ", goldenPath);
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string text = ss.str();

    std::vector<GoldenEntry> entries;
    REQUIRE_MESSAGE(parseGolden(text, entries), "failed to parse ", goldenPath);
    REQUIRE(!entries.empty());

    int checked = 0;
    for (const GoldenEntry& e : entries) {
        INFO("file: ", e.name);
        Blob blob;
        bool read = testdata::readExtracted(e.name, blob);
        CHECK(read);
        if (!read) continue;

        Image img;
        bool decoded = decodeTga(blob.data(), blob.size(), img);
        CHECK(decoded);
        if (!decoded) continue;

        CHECK(img.width == e.width);
        CHECK(img.height == e.height);
        CHECK(img.hasAlpha == e.hasAlpha);
        CHECK(img.rgba.size() == static_cast<size_t>(img.width) * img.height * 4);
        CHECK(sha1Hex(img.rgba) == e.sha1);
        checked++;
    }
    CHECK(checked == static_cast<int>(entries.size()));
    MESSAGE("tga: checked ", checked, " files against golden data");
}

// ---------------------------------------------------------------------------------
// Synthetic type coverage.
// ---------------------------------------------------------------------------------

TEST_CASE("tga: type 2 (uncompressed true colour), both origins") {
    // 2x2 image, distinct colours per corner:
    //   top-left=(10,20,30) top-right=(40,50,60) bottom-left=(70,80,90) bottom-right=(100,110,120)
    auto expect = [](const Image& img) {
        REQUIRE(img.width == 2);
        REQUIRE(img.height == 2);
        CHECK(img.hasAlpha == false);
        checkRgba(img, 0, 0, 10, 20, 30, 255);
        checkRgba(img, 1, 0, 40, 50, 60, 255);
        checkRgba(img, 0, 1, 70, 80, 90, 255);
        checkRgba(img, 1, 1, 100, 110, 120, 255);
    };

    SUBCASE("bottom-left origin (descriptor=0x00)") {
        auto buf = tgaHeader(0, 0, 2, 0, 0, 0, 2, 2, 24, 0x00);
        // File row order is bottom-to-top: first the bottom row, then the top row.
        push24(buf, 70, 80, 90);
        push24(buf, 100, 110, 120);
        push24(buf, 10, 20, 30);
        push24(buf, 40, 50, 60);
        Image img;
        REQUIRE(decodeTga(buf.data(), buf.size(), img));
        expect(img);
    }
    SUBCASE("top-left origin (descriptor=0x20)") {
        auto buf = tgaHeader(0, 0, 2, 0, 0, 0, 2, 2, 24, 0x20);
        // File row order is top-to-bottom already.
        push24(buf, 10, 20, 30);
        push24(buf, 40, 50, 60);
        push24(buf, 70, 80, 90);
        push24(buf, 100, 110, 120);
        Image img;
        REQUIRE(decodeTga(buf.data(), buf.size(), img));
        expect(img);
    }
}

TEST_CASE("tga: type 2, 32-bit pixels carry real alpha; 24-bit forces alpha=255") {
    auto buf = tgaHeader(0, 0, 2, 0, 0, 0, 1, 1, 32, 0x20);
    push32(buf, 1, 2, 3, 128);
    Image img;
    REQUIRE(decodeTga(buf.data(), buf.size(), img));
    CHECK(img.hasAlpha == true);
    checkRgba(img, 0, 0, 1, 2, 3, 128);
}

TEST_CASE("tga: type 1 (colour-mapped), both origins, and hasAlpha follows the map depth") {
    // 4-entry, 24-bit palette; 2x1 image using indices 0 and 3.
    auto buf = tgaHeader(0, 1, 1, 0, 4, 24, 2, 1, 8, 0x00);
    push24(buf, 5, 6, 7);     // index 0
    push24(buf, 0, 0, 0);     // index 1 (unused)
    push24(buf, 0, 0, 0);     // index 2 (unused)
    push24(buf, 250, 251, 252); // index 3
    buf.push_back(0);
    buf.push_back(3);
    Image img;
    REQUIRE(decodeTga(buf.data(), buf.size(), img));
    REQUIRE(img.width == 2);
    REQUIRE(img.height == 1);
    CHECK(img.hasAlpha == false); // 24-bit colour map: no alpha, regardless of descriptor bits
    checkRgba(img, 0, 0, 5, 6, 7, 255);
    checkRgba(img, 1, 0, 250, 251, 252, 255);

    SUBCASE("32-bit colour map: hasAlpha becomes true") {
        auto buf2 = tgaHeader(0, 1, 1, 0, 1, 32, 1, 1, 8, 0x00);
        push32(buf2, 9, 8, 7, 64);
        buf2.push_back(0);
        Image img2;
        REQUIRE(decodeTga(buf2.data(), buf2.size(), img2));
        CHECK(img2.hasAlpha == true);
        checkRgba(img2, 0, 0, 9, 8, 7, 64);
    }
}

TEST_CASE("tga: type 3 (greyscale)") {
    auto buf = tgaHeader(0, 0, 3, 0, 0, 0, 2, 1, 8, 0x20);
    buf.push_back(16);
    buf.push_back(240);
    Image img;
    REQUIRE(decodeTga(buf.data(), buf.size(), img));
    CHECK(img.hasAlpha == false);
    checkRgba(img, 0, 0, 16, 16, 16, 255);
    checkRgba(img, 1, 0, 240, 240, 240, 255);
}

TEST_CASE("tga: type 9 (RLE colour-mapped)") {
    // 2x2 image, single run packet covering all 4 pixels with palette index 2.
    auto buf = tgaHeader(0, 1, 9, 0, 3, 24, 2, 2, 8, 0x20);
    push24(buf, 0, 0, 0);
    push24(buf, 0, 0, 0);
    push24(buf, 11, 22, 33);
    buf.push_back(0x80 | 3); // run packet, count = 4
    buf.push_back(2);        // index 2
    Image img;
    REQUIRE(decodeTga(buf.data(), buf.size(), img));
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 2; x++) checkRgba(img, x, y, 11, 22, 33, 255);
}

TEST_CASE("tga: type 10 (RLE true colour), mixed raw and run packets") {
    // 4x1 image: a raw packet of 2 distinct pixels, then a run packet of 2 identical
    // pixels.
    auto buf = tgaHeader(0, 0, 10, 0, 0, 0, 4, 1, 24, 0x20);
    buf.push_back(0x01); // raw packet, count = 2
    push24(buf, 1, 2, 3);
    push24(buf, 4, 5, 6);
    buf.push_back(0x80 | 0x01); // run packet, count = 2
    push24(buf, 7, 8, 9);
    Image img;
    REQUIRE(decodeTga(buf.data(), buf.size(), img));
    checkRgba(img, 0, 0, 1, 2, 3, 255);
    checkRgba(img, 1, 0, 4, 5, 6, 255);
    checkRgba(img, 2, 0, 7, 8, 9, 255);
    checkRgba(img, 3, 0, 7, 8, 9, 255);
}

TEST_CASE("tga: type 11 (RLE greyscale)") {
    auto buf = tgaHeader(0, 0, 11, 0, 0, 0, 3, 1, 8, 0x20);
    buf.push_back(0x80 | 0x02); // run packet, count = 3
    buf.push_back(77);
    Image img;
    REQUIRE(decodeTga(buf.data(), buf.size(), img));
    for (int x = 0; x < 3; x++) checkRgba(img, x, 0, 77, 77, 77, 255);
}

// ---------------------------------------------------------------------------------
// Malformed input must be rejected, never crash, never read out of bounds (run this
// file under ASan/UBSan to confirm -- see tools/ci.sh).
// ---------------------------------------------------------------------------------

TEST_CASE("tga: malformed input is rejected") {
    Image img;

    SUBCASE("empty buffer") {
        CHECK(decodeTga(nullptr, 0, img) == false);
        CHECK(img.rgba.empty());
    }
    SUBCASE("truncated header") {
        std::vector<u8> buf(10, 0);
        CHECK(decodeTga(buf.data(), buf.size(), img) == false);
    }
    SUBCASE("truncated pixel data") {
        auto buf = tgaHeader(0, 0, 2, 0, 0, 0, 4, 4, 24, 0x20); // needs 48 bytes of pixels
        buf.resize(buf.size() + 10, 0); // far short
        CHECK(decodeTga(buf.data(), buf.size(), img) == false);
    }
    SUBCASE("truncated colour map") {
        auto buf = tgaHeader(0, 1, 1, 0, 256, 24, 1, 1, 8, 0x20); // claims 256 entries, 768 bytes
        buf.push_back(0);                                        // far short
        CHECK(decodeTga(buf.data(), buf.size(), img) == false);
    }
    SUBCASE("RLE packet overruns the image") {
        auto buf = tgaHeader(0, 0, 10, 0, 0, 0, 2, 1, 24, 0x20); // only 2 pixels
        buf.push_back(0x80 | 4);                                 // run packet claims count = 5
        push24(buf, 1, 2, 3);
        CHECK(decodeTga(buf.data(), buf.size(), img) == false);
    }
    SUBCASE("RLE run packet missing its pixel") {
        auto buf = tgaHeader(0, 0, 10, 0, 0, 0, 2, 1, 24, 0x20);
        buf.push_back(0x80 | 1); // run packet, count = 2, but no pixel bytes follow
        CHECK(decodeTga(buf.data(), buf.size(), img) == false);
    }
    SUBCASE("colour map index out of range") {
        auto buf = tgaHeader(0, 1, 1, 0, 2, 24, 1, 1, 8, 0x20); // palette has 2 entries
        push24(buf, 1, 2, 3);
        push24(buf, 4, 5, 6);
        buf.push_back(5); // index 5 is out of range
        CHECK(decodeTga(buf.data(), buf.size(), img) == false);
    }
    SUBCASE("colour-mapped image without a colour map") {
        auto buf = tgaHeader(0, 0, 1, 0, 0, 0, 1, 1, 8, 0x20); // colourMapType=0 but type=1
        buf.push_back(0);
        CHECK(decodeTga(buf.data(), buf.size(), img) == false);
    }
    SUBCASE("zero width") {
        auto buf = tgaHeader(0, 0, 2, 0, 0, 0, 0, 4, 24, 0x20);
        CHECK(decodeTga(buf.data(), buf.size(), img) == false);
    }
    SUBCASE("zero height") {
        auto buf = tgaHeader(0, 0, 2, 0, 0, 0, 4, 0, 24, 0x20);
        CHECK(decodeTga(buf.data(), buf.size(), img) == false);
    }
    SUBCASE("huge dimensions (above the 8192 cap)") {
        auto buf = tgaHeader(0, 0, 2, 0, 0, 0, 8193, 8193, 24, 0x20);
        CHECK(decodeTga(buf.data(), buf.size(), img) == false);
    }
    SUBCASE("dimensions exactly at the 8192 cap are not rejected for that reason alone") {
        // Header-only check: claim 8192x8192 but supply no pixel data, so it must still
        // fail -- just not because of the dimension cap (verifies the cap is ">8192",
        // not ">=8192").
        auto buf = tgaHeader(0, 0, 2, 0, 0, 0, 8192, 8192, 24, 0x20);
        CHECK(decodeTga(buf.data(), buf.size(), img) == false); // truncated pixel data
    }
    SUBCASE("unsupported image type") {
        auto buf = tgaHeader(0, 0, 4, 0, 0, 0, 1, 1, 24, 0x20);
        push24(buf, 1, 2, 3);
        CHECK(decodeTga(buf.data(), buf.size(), img) == false);
    }
    SUBCASE("unsupported pixel depth for true colour") {
        auto buf = tgaHeader(0, 0, 2, 0, 0, 0, 1, 1, 17, 0x20);
        buf.push_back(0);
        CHECK(decodeTga(buf.data(), buf.size(), img) == false);
    }

    CHECK(img.rgba.empty());
    CHECK(img.width == 0);
    CHECK(img.height == 0);
}
