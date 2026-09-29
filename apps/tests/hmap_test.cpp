// Tests for as3d::loadLevel. See docs/spec/hmap.md.
//
// Three parts:
//  - a golden-corpus check that loads every shipped .hsc and compares against
//    testdata/golden/hmap_summary.json (computed independently by tools/ref/test_hmap.py
//    / tools/ref/hmap.py), including a byte-exact reproduction of the reference's
//    canonical placement sha1 (which needs a C++ replica of Python's float.hex());
//  - synthetic in-memory tests for malformed input: truncation at every structure
//    boundary of a small synthetic level, absurd counts, and string lengths running
//    past the end;
//  - a fuzz loop over mutations of a real level file that must never crash.
#include "doctest.h"

#include "as3d/core.h"
#include "as3d/level.h"
#include "test_data.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace as3d;

// ---------------------------------------------------------------------------------
// SHA-1 (duplicated from apps/tests/mdl_test.cpp; see that file's comment on why this
// project duplicates a tiny hash routine per test file rather than sharing one).
// ---------------------------------------------------------------------------------
namespace {

inline uint32_t rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

std::string sha1Hex(const u8* data, size_t size) {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;

    std::vector<u8> msg(data, data + size);
    uint64_t bitLen = static_cast<uint64_t>(size) * 8ULL;
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
        for (int i = 16; i < 80; ++i) w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
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

std::string sha1Hex(const std::string& s) {
    return sha1Hex(reinterpret_cast<const u8*>(s.data()), s.size());
}

// ---------------------------------------------------------------------------------
// A tiny generic JSON reader, duplicated (with the same shape) from mdl_test.cpp: this
// file's golden schema also needs nested objects/arrays, so a fixed-shape scanner isn't
// enough.
// ---------------------------------------------------------------------------------

struct JsonValue {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    std::vector<JsonValue> arrayValue;
    std::vector<std::pair<std::string, JsonValue>> objectValue;

    const JsonValue* get(const std::string& key) const {
        for (const auto& kv : objectValue) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& s) : s_(s) {}

    bool parse(JsonValue& out) {
        skipWs();
        if (!parseValue(out)) return false;
        skipWs();
        return true;
    }

private:
    void skipWs() {
        while (pos_ < s_.size() && static_cast<unsigned char>(s_[pos_]) <= ' ') pos_++;
    }

    bool parseValue(JsonValue& v) {
        skipWs();
        if (pos_ >= s_.size()) return false;
        char c = s_[pos_];
        if (c == '{') return parseObject(v);
        if (c == '[') return parseArray(v);
        if (c == '"') return parseString(v);
        if (c == 't' || c == 'f') return parseBool(v);
        if (c == 'n') return parseNull(v);
        return parseNumber(v);
    }

    bool parseObject(JsonValue& v) {
        v.kind = JsonValue::Kind::Object;
        pos_++;
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == '}') {
            pos_++;
            return true;
        }
        for (;;) {
            JsonValue key;
            skipWs();
            if (!parseString(key)) return false;
            skipWs();
            if (pos_ >= s_.size() || s_[pos_] != ':') return false;
            pos_++;
            JsonValue val;
            if (!parseValue(val)) return false;
            v.objectValue.emplace_back(key.stringValue, std::move(val));
            skipWs();
            if (pos_ < s_.size() && s_[pos_] == ',') {
                pos_++;
                continue;
            }
            break;
        }
        skipWs();
        if (pos_ >= s_.size() || s_[pos_] != '}') return false;
        pos_++;
        return true;
    }

    bool parseArray(JsonValue& v) {
        v.kind = JsonValue::Kind::Array;
        pos_++;
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == ']') {
            pos_++;
            return true;
        }
        for (;;) {
            JsonValue val;
            if (!parseValue(val)) return false;
            v.arrayValue.push_back(std::move(val));
            skipWs();
            if (pos_ < s_.size() && s_[pos_] == ',') {
                pos_++;
                continue;
            }
            break;
        }
        skipWs();
        if (pos_ >= s_.size() || s_[pos_] != ']') return false;
        pos_++;
        return true;
    }

    bool parseString(JsonValue& v) {
        if (pos_ >= s_.size() || s_[pos_] != '"') return false;
        pos_++;
        v.kind = JsonValue::Kind::String;
        v.stringValue.clear();
        while (pos_ < s_.size() && s_[pos_] != '"') {
            char c = s_[pos_++];
            if (c == '\\' && pos_ < s_.size()) {
                char e = s_[pos_++];
                switch (e) {
                    case '\\': v.stringValue.push_back('\\'); break;
                    case '"': v.stringValue.push_back('"'); break;
                    case '/': v.stringValue.push_back('/'); break;
                    case 'n': v.stringValue.push_back('\n'); break;
                    case 't': v.stringValue.push_back('\t'); break;
                    default: v.stringValue.push_back(e); break;
                }
            } else {
                v.stringValue.push_back(c);
            }
        }
        if (pos_ >= s_.size()) return false;
        pos_++;
        return true;
    }

    bool parseBool(JsonValue& v) {
        if (s_.compare(pos_, 4, "true") == 0) {
            v.kind = JsonValue::Kind::Bool;
            v.boolValue = true;
            pos_ += 4;
            return true;
        }
        if (s_.compare(pos_, 5, "false") == 0) {
            v.kind = JsonValue::Kind::Bool;
            v.boolValue = false;
            pos_ += 5;
            return true;
        }
        return false;
    }

    bool parseNull(JsonValue& v) {
        if (s_.compare(pos_, 4, "null") == 0) {
            v.kind = JsonValue::Kind::Null;
            pos_ += 4;
            return true;
        }
        return false;
    }

    bool parseNumber(JsonValue& v) {
        size_t start = pos_;
        if (pos_ < s_.size() && (s_[pos_] == '-' || s_[pos_] == '+')) pos_++;
        while (pos_ < s_.size() &&
               (std::isdigit(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '.' || s_[pos_] == 'e' ||
                s_[pos_] == 'E' || s_[pos_] == '+' || s_[pos_] == '-')) {
            pos_++;
        }
        if (pos_ == start) return false;
        v.kind = JsonValue::Kind::Number;
        v.numberValue = std::atof(s_.substr(start, pos_ - start).c_str());
        return true;
    }

    const std::string& s_;
    size_t pos_ = 0;
};

bool loadGolden(const std::string& path, JsonValue& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f.good()) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string text = ss.str();
    JsonParser p(text);
    return p.parse(out) && out.kind == JsonValue::Kind::Array;
}

double num(const JsonValue* v, double dflt = 0.0) { return v ? v->numberValue : dflt; }
std::string str(const JsonValue* v) { return v ? v->stringValue : std::string(); }

// ---------------------------------------------------------------------------------
// A C++ replica of Python's float.hex(), needed to reproduce tools/ref/hmap.py's
// placements_sha1 exactly: the canonical placement encoding formats every waypoint
// float with Python's `float(x).hex()` before hashing. Waypoint floats are read as
// IEEE-754 f32 in both implementations; Python's struct.unpack promotes to a double
// holding the *exact* value of that f32 (never rounded), same as C++'s implicit
// float->double conversion, so operating on `double(f)` here reproduces the same
// bit pattern Python hexes. Only the zero and "normal double" cases are implemented:
// a double produced by widening a finite float32 is never a subnormal double (float32's
// subnormal range is well within double's normal range), so that branch of CPython's
// algorithm can never be exercised by this data; NaN/Inf are not expected in level data
// (test_hmap.py's corpus check already asserts every waypoint float is finite).
std::string pyFloatHex(double d) {
    uint64_t bits;
    std::memcpy(&bits, &d, 8);
    bool sign = (bits >> 63) != 0;
    if (d == 0.0) return sign ? "-0x0.0p+0" : "0x0.0p+0";
    if (std::isnan(d)) return "nan";
    if (std::isinf(d)) return sign ? "-inf" : "inf";
    uint64_t expBits = (bits >> 52) & 0x7FFull;
    uint64_t mantissa = bits & 0xFFFFFFFFFFFFFull; // 52 bits
    int unbiasedExp;
    int leadDigit;
    if (expBits == 0) { // subnormal double; not expected, handled defensively
        unbiasedExp = -1022;
        leadDigit = 0;
    } else {
        unbiasedExp = static_cast<int>(expBits) - 1023;
        leadDigit = 1;
    }
    char hexdigits[14];
    for (int i = 0; i < 13; i++) {
        int shift = 48 - 4 * i;
        int nibble = static_cast<int>((mantissa >> shift) & 0xFull);
        hexdigits[i] = "0123456789abcdef"[nibble];
    }
    hexdigits[13] = '\0';
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s0x%d.%sp%+d", sign ? "-" : "", leadDigit, hexdigits, unbiasedExp);
    return std::string(buf);
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

std::string jsonStr(const std::string& s) { return "\"" + jsonEscape(s) + "\""; }

// Reproduces tools/ref/hmap.py's placement_canonical() + placements_sha1() exactly
// (key order after sort_keys=True is alphabetical: flag,item,rot,script,type,wp,x,y;
// separators=(",", ":") means no extra whitespace).
std::string canonicalPlacement(const Placement& p) {
    std::string out = "{";
    out += "\"flag\":" + std::to_string(static_cast<int>(p.pathFlag));
    out += ",\"item\":" + std::to_string(static_cast<int>(p.dropItem));
    out += ",\"rot\":" + std::to_string(static_cast<int>(p.rotationSteps));
    out += ",\"script\":";
    if (p.scriptOverride.empty()) {
        // Ambiguous with "script explicitly empty" is impossible: the file format's
        // script length byte is 0 exactly when there is no override (docs/spec/hmap.md).
        out += "null";
    } else {
        out += jsonStr(p.scriptOverride);
    }
    out += ",\"type\":" + std::to_string(static_cast<int>(p.typeIndex));
    out += ",\"wp\":[";
    for (size_t i = 0; i < p.waypoints.size(); i++) {
        if (i) out += ",";
        const Waypoint& w = p.waypoints[i];
        out += "[";
        out += std::to_string(w.x) + ",";
        out += std::to_string(w.y) + ",";
        out += std::to_string(w.unknown8) + ",";
        out += jsonStr(pyFloatHex(static_cast<double>(w.inCtrl.x))) + ",";
        out += jsonStr(pyFloatHex(static_cast<double>(w.inCtrl.y))) + ",";
        out += jsonStr(pyFloatHex(static_cast<double>(w.outCtrl.x))) + ",";
        out += jsonStr(pyFloatHex(static_cast<double>(w.outCtrl.y))) + ",";
        out += jsonStr(pyFloatHex(static_cast<double>(w.delay)));
        out += "]";
    }
    out += "]";
    out += ",\"x\":" + std::to_string(static_cast<int>(p.x));
    out += ",\"y\":" + std::to_string(static_cast<int>(p.y));
    out += "}";
    return out;
}

std::string placementsSha1(const LevelData& level) {
    std::string blob = "[";
    for (size_t i = 0; i < level.placements.size(); i++) {
        if (i) blob += ",";
        blob += canonicalPlacement(level.placements[i]);
    }
    blob += "]";
    return sha1Hex(blob);
}

} // namespace

// ---------------------------------------------------------------------------------
// Golden corpus.
// ---------------------------------------------------------------------------------

TEST_CASE("hmap: every shipped level loads and matches the golden summary") {
    AS3D_REQUIRE_DATA();

    std::string goldenPath = testdata::goldenDir() + "/hmap_summary.json";
    JsonValue golden;
    REQUIRE_MESSAGE(loadGolden(goldenPath, golden), "missing/unparseable golden file: ", goldenPath);
    REQUIRE(golden.arrayValue.size() == 24);

    int checked = 0;
    for (const JsonValue& entry : golden.arrayValue) {
        std::string path = str(entry.get("file"));
        INFO("file: ", path);

        Blob blob;
        REQUIRE(testdata::readExtracted(path, blob));
        CHECK(blob.size() == static_cast<size_t>(num(entry.get("size"))));

        LevelData level;
        std::string error;
        bool ok = loadLevel(blob.data(), blob.size(), level, &error);
        REQUIRE_MESSAGE(ok, "loadLevel failed for ", path, ": ", error);
        CHECK(error.empty());

        CHECK(num(entry.get("version")) == 2);
        CHECK(level.width == static_cast<u32>(num(entry.get("width"))));
        CHECK(level.height == static_cast<u32>(num(entry.get("height"))));
        CHECK(level.placements.size() == static_cast<size_t>(num(entry.get("placement_count"))));

        const JsonValue* typeNames = entry.get("type_names");
        REQUIRE(typeNames != nullptr);
        REQUIRE(level.typeNames.size() == typeNames->arrayValue.size());
        for (size_t i = 0; i < level.typeNames.size(); i++) {
            CHECK(level.typeNames[i] == typeNames->arrayValue[i].stringValue);
        }
        const JsonValue* itemNames = entry.get("item_names");
        REQUIRE(itemNames != nullptr);
        REQUIRE(level.itemNames.size() == itemNames->arrayValue.size());
        for (size_t i = 0; i < level.itemNames.size(); i++) {
            CHECK(level.itemNames[i] == itemNames->arrayValue[i].stringValue);
        }

        // Per-layer stats: byte k of every cell, k=0..3 (height, tile set, tile index,
        // tile rotation), matching tools/ref/hmap.py's HmapFile.layer(k).
        const JsonValue* layers = entry.get("layers");
        REQUIRE(layers != nullptr);
        REQUIRE(layers->arrayValue.size() == 4);
        for (int k = 0; k < 4; k++) {
            std::vector<u8> plane(level.cells.size());
            for (size_t i = 0; i < level.cells.size(); i++) {
                const LevelCell& c = level.cells[i];
                plane[i] = (k == 0) ? c.height : (k == 1) ? c.tileSet : (k == 2) ? c.tileIndex : c.tileRotation;
            }
            u8 mn = plane.empty() ? 0 : plane[0], mx = plane.empty() ? 0 : plane[0];
            int nonzero = 0;
            for (u8 b : plane) {
                mn = std::min(mn, b);
                mx = std::max(mx, b);
                if (b) nonzero++;
            }
            const JsonValue& layer = layers->arrayValue[static_cast<size_t>(k)];
            CHECK(static_cast<int>(mn) == static_cast<int>(num(layer.get("min"))));
            CHECK(static_cast<int>(mx) == static_cast<int>(num(layer.get("max"))));
            CHECK(nonzero == static_cast<int>(num(layer.get("nonzero"))));
            CHECK(sha1Hex(plane.data(), plane.size()) == str(layer.get("sha1")));
        }

        // Placement counts per type name.
        std::map<std::string, int> perType;
        int withPath = 0;
        int totalWaypoints = 0;
        for (const Placement& p : level.placements) {
            perType[level.typeName(p)]++;
            if (!p.waypoints.empty()) withPath++;
            totalWaypoints += static_cast<int>(p.waypoints.size());
        }
        const JsonValue* perTypeGolden = entry.get("placements_per_type");
        REQUIRE(perTypeGolden != nullptr);
        REQUIRE(perTypeGolden->objectValue.size() == perType.size());
        for (const auto& kv : perTypeGolden->objectValue) {
            auto it = perType.find(kv.first);
            REQUIRE(it != perType.end());
            CHECK(it->second == static_cast<int>(kv.second.numberValue));
        }
        CHECK(withPath == static_cast<int>(num(entry.get("placements_with_path"))));
        CHECK(totalWaypoints == static_cast<int>(num(entry.get("waypoints"))));

        // The canonical placement hash: byte-exact reproduction of the reference's
        // placements_sha1 (file order, not spawn order).
        CHECK(placementsSha1(level) == str(entry.get("placements_sha1")));

        checked++;
    }
    CHECK(checked == 24);
    MESSAGE("hmap: checked ", checked, " levels against the golden summary");
}

// ---------------------------------------------------------------------------------
// Synthetic malformed-input coverage.
// ---------------------------------------------------------------------------------

namespace {

void pushU8(std::vector<u8>& v, uint8_t x) { v.push_back(x); }
void pushU16(std::vector<u8>& v, uint16_t x) {
    v.push_back(static_cast<u8>(x & 0xFF));
    v.push_back(static_cast<u8>((x >> 8) & 0xFF));
}
void pushU32(std::vector<u8>& v, uint32_t x) {
    v.push_back(static_cast<u8>(x & 0xFF));
    v.push_back(static_cast<u8>((x >> 8) & 0xFF));
    v.push_back(static_cast<u8>((x >> 16) & 0xFF));
    v.push_back(static_cast<u8>((x >> 24) & 0xFF));
}
void pushI32(std::vector<u8>& v, int32_t x) { pushU32(v, static_cast<uint32_t>(x)); }
void pushF32(std::vector<u8>& v, float x) {
    uint32_t bits;
    std::memcpy(&bits, &x, sizeof bits);
    pushU32(v, bits);
}
void pushName(std::vector<u8>& v, const char* s) {
    size_t n = std::strlen(s) + 1; // +1 for the NUL
    REQUIRE(n <= 255);
    v.push_back(static_cast<u8>(n));
    for (size_t i = 0; i < std::strlen(s); i++) v.push_back(static_cast<u8>(s[i]));
    v.push_back(0);
}

// A minimal, well-formed level: 2x2 grid, 1 type, 1 item, 1 placement carrying one
// waypoint (the loader does not require K >= 2; that requirement belongs to the
// terrain/waypoint-path builder, not the file parser).
std::vector<u8> minimalLevel() {
    std::vector<u8> v;
    v.push_back('H'); v.push_back('M'); v.push_back('A'); v.push_back('P');
    pushU32(v, 2);  // version
    pushU32(v, 2);  // width
    pushU32(v, 2);  // height
    pushU32(v, 1);  // placement count
    pushU32(v, 1);  // type count
    pushU32(v, 1);  // item count
    REQUIRE(v.size() == 28);
    pushName(v, "unit_a"); // type table
    pushName(v, "star");   // item table
    REQUIRE(v.size() == 28 + 8 + 6);
    size_t gridStart = v.size();
    for (int i = 0; i < 2 * 2; i++) {
        pushU8(v, static_cast<u8>(10 + i)); // height
        pushU8(v, 0);                        // tile set
        pushU8(v, 0);                        // tile index
        pushU8(v, 0);                        // tile rotation
    }
    REQUIRE(v.size() == gridStart + 16);
    size_t placementStart = v.size();
    pushU16(v, 0);  // type index
    pushU16(v, 1);  // x
    pushU16(v, 1);  // y (1-based, row 0)
    pushU8(v, 3);   // rotation
    pushU8(v, 1);   // drop item (1-based -> "star")
    pushU16(v, 1);  // K = 1 waypoint
    pushU8(v, 0);   // S = 0, no script
    pushU16(v, 0);  // path flag
    // one waypoint
    pushI32(v, 1); pushI32(v, 0); pushI32(v, 0);           // x, y, unknown8
    pushF32(v, 0.0f); pushF32(v, 0.0f);                     // in ctrl
    pushF32(v, 1.0f); pushF32(v, 1.0f);                     // out ctrl
    pushF32(v, 5.0f);                                        // delay
    REQUIRE(v.size() == placementStart + 10 + 1 + 2 + 32);
    return v;
}

} // namespace

TEST_CASE("hmap: a well-formed minimal file loads cleanly") {
    auto buf = minimalLevel();
    LevelData level;
    std::string error;
    REQUIRE(loadLevel(buf.data(), buf.size(), level, &error));
    CHECK(error.empty());
    CHECK(level.width == 2);
    CHECK(level.height == 2);
    REQUIRE(level.typeNames.size() == 1);
    CHECK(level.typeNames[0] == "unit_a");
    REQUIRE(level.itemNames.size() == 1);
    CHECK(level.itemNames[0] == "star");
    REQUIRE(level.cells.size() == 4);
    CHECK(level.cells[0].height == 10);
    REQUIRE(level.placements.size() == 1);
    const Placement& p = level.placements[0];
    CHECK(p.typeIndex == 0);
    CHECK(p.x == 1);
    CHECK(p.y == 1);
    CHECK(p.row() == 0);
    CHECK(p.rotationSteps == 3);
    CHECK(p.yawDegrees() == doctest::Approx(90.0f));
    CHECK(p.dropItem == 1);
    CHECK(level.itemName(p) != nullptr);
    CHECK(*level.itemName(p) == "star");
    CHECK(level.typeName(p) == "unit_a");
    CHECK(p.scriptOverride.empty());
    REQUIRE(p.waypoints.size() == 1);
    CHECK(p.waypoints[0].x == 1);
    CHECK(p.waypoints[0].delay == doctest::Approx(5.0f));
    CHECK(p.loopingPath() == false); // K=1, no meaningful loop, and pathFlag == 0 anyway
    Vec3 spawn = p.spawnPosition();
    CHECK(spawn.x == doctest::Approx(1 * 40.0f + 20.0f));
    CHECK(spawn.y == doctest::Approx(1 * 40.0f + 20.0f - 40.0f));
}

TEST_CASE("hmap: bad magic, version and header limits are rejected") {
    auto buf = minimalLevel();
    LevelData level;
    std::string error;

    SUBCASE("bad magic") {
        buf[0] = 'X';
        CHECK(loadLevel(buf.data(), buf.size(), level, &error) == false);
    }
    SUBCASE("version 1") {
        buf[4] = 1;
        CHECK(loadLevel(buf.data(), buf.size(), level, &error) == false);
    }
    SUBCASE("version 3") {
        buf[4] = 3;
        CHECK(loadLevel(buf.data(), buf.size(), level, &error) == false);
    }
    SUBCASE("width 0") {
        buf[8] = 0; buf[9] = 0; buf[10] = 0; buf[11] = 0;
        CHECK(loadLevel(buf.data(), buf.size(), level, &error) == false);
    }
    SUBCASE("width 1 (still degenerate)") {
        buf[8] = 1; buf[9] = 0; buf[10] = 0; buf[11] = 0;
        CHECK(loadLevel(buf.data(), buf.size(), level, &error) == false);
    }
    SUBCASE("placement count above the engine limit (0x4000)") {
        buf[16] = 0x01; buf[17] = 0x40; buf[18] = 0; buf[19] = 0; // 0x4001
        CHECK(loadLevel(buf.data(), buf.size(), level, &error) == false);
    }
    SUBCASE("type count above the engine limit (1024)") {
        buf[20] = 0x01; buf[21] = 0x04; buf[22] = 0; buf[23] = 0; // 1025
        CHECK(loadLevel(buf.data(), buf.size(), level, &error) == false);
    }
    SUBCASE("item count above the engine limit (255)") {
        buf[24] = 0; buf[25] = 0x01; buf[26] = 0; buf[27] = 0; // 256
        CHECK(loadLevel(buf.data(), buf.size(), level, &error) == false);
    }
    SUBCASE("absurd width*height overflow guard") {
        buf[8] = 0xFF; buf[9] = 0xFF; buf[10] = 0xFF; buf[11] = 0x7F; // width ~2^31
        buf[12] = 0xFF; buf[13] = 0xFF; buf[14] = 0xFF; buf[15] = 0x7F; // height ~2^31
        CHECK(loadLevel(buf.data(), buf.size(), level, &error) == false);
    }
}

TEST_CASE("hmap: truncation at every structure boundary is handled, never crashes") {
    auto full = minimalLevel();
    for (size_t len = 0; len <= full.size(); len++) {
        LevelData level;
        std::string error;
        bool ok = loadLevel(full.data(), len, level, &error);
        if (len == full.size()) {
            CHECK(ok == true);
        } else {
            CHECK(ok == false);
            // On failure the loader must leave a default (empty, safe-to-use) LevelData.
            CHECK(level.placements.empty());
            CHECK(level.cells.empty());
        }
    }
}

TEST_CASE("hmap: a string length that runs past the end of the file is rejected") {
    auto buf = minimalLevel();
    // The type table's length-prefix byte sits right after the 28-byte header.
    buf[28] = 0xFF; // claims a 255-byte name; the file is nowhere near that long
    LevelData level;
    std::string error;
    CHECK(loadLevel(buf.data(), buf.size(), level, &error) == false);
    CHECK(!error.empty());
}

TEST_CASE("hmap: a placement's waypoint count claiming more data than exists is rejected") {
    auto buf = minimalLevel();
    // K (waypoint count) is a u16 at offset placementStart + 8; bump it hugely so the
    // implied 32*K byte block cannot possibly fit.
    size_t placementStart = 28 + 8 + 6 + 16;
    buf[placementStart + 8] = 0xFF;
    buf[placementStart + 9] = 0xFF; // K = 0xFFFF
    LevelData level;
    std::string error;
    CHECK(loadLevel(buf.data(), buf.size(), level, &error) == false);
}

TEST_CASE("hmap: a script length claiming more data than exists is rejected") {
    auto buf = minimalLevel();
    // S sits right after the fixed 10-byte prefix.
    size_t placementStart = 28 + 8 + 6 + 16;
    buf[placementStart + 10] = 0xFF; // S = 255, file has nowhere near that much left
    LevelData level;
    std::string error;
    CHECK(loadLevel(buf.data(), buf.size(), level, &error) == false);
}

TEST_CASE("hmap: out-of-range type/item indices do not crash the accessors") {
    auto buf = minimalLevel();
    size_t placementStart = 28 + 8 + 6 + 16;
    buf[placementStart + 0] = 0xFF; buf[placementStart + 1] = 0xFF; // type index 65535
    buf[placementStart + 7] = 0xFF;                                  // drop item 255
    LevelData level;
    std::string error;
    REQUIRE(loadLevel(buf.data(), buf.size(), level, &error)); // the parser itself does
                                                                // not validate these
    REQUIRE(level.placements.size() == 1);
    const Placement& p = level.placements[0];
    CHECK(level.typeName(p).empty());     // safe fallback, not a crash
    CHECK(level.itemName(p) == nullptr);  // out of range -> null, not a crash
}

// ---------------------------------------------------------------------------------
// Fuzz: mutated real .hsc file never crashes or hangs (run under ASan/UBSan, see
// tools/ci.sh).
// ---------------------------------------------------------------------------------

TEST_CASE("fuzz: mutated real .hsc file never crashes") {
    AS3D_REQUIRE_DATA();
    Blob seed;
    REQUIRE(testdata::readExtracted("maps\\level1.hsc", seed));
    REQUIRE(!seed.empty());

    std::mt19937 rng(20020101);
    std::uniform_int_distribution<int> byteDist(0, 255);

    for (int iter = 0; iter < 4000; iter++) {
        Blob mutated = seed;
        int mutations = 1 + static_cast<int>(rng() % 8);
        for (int m = 0; m < mutations; m++) {
            if (mutated.empty()) break;
            size_t pos = rng() % mutated.size();
            int op = static_cast<int>(rng() % 3);
            if (op == 0) {
                mutated[pos] = static_cast<u8>(byteDist(rng));
            } else if (op == 1) {
                mutated.insert(mutated.begin() + static_cast<long>(pos), static_cast<u8>(byteDist(rng)));
            } else {
                mutated.erase(mutated.begin() + static_cast<long>(pos));
            }
        }
        LevelData level;
        std::string error;
        bool ok = loadLevel(mutated.data(), mutated.size(), level, &error);
        if (ok) {
            // Whatever came back must be internally consistent (no accessor should be
            // able to read out of bounds even though the data is now fuzzed).
            for (const Placement& p : level.placements) {
                (void)level.typeName(p);
                (void)level.itemName(p);
            }
            for (u32 r = 0; r < level.height; r++)
                for (u32 c = 0; c < level.width; c++) (void)level.cellAt(static_cast<int>(c), static_cast<int>(r));
        }
    }

    std::uniform_int_distribution<int> lenDist(0, 2048);
    for (int iter = 0; iter < 1000; iter++) {
        Blob randomBytes(static_cast<size_t>(lenDist(rng)));
        for (auto& b : randomBytes) b = static_cast<u8>(byteDist(rng));
        LevelData level;
        std::string error;
        loadLevel(randomBytes.empty() ? nullptr : randomBytes.data(), randomBytes.size(), level, &error);
    }

    CHECK(true); // reaching here means every mutation terminated
}
