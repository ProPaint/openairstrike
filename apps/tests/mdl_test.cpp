// Tests for as3d::loadModel / as3d::buildRenderMesh. See docs/spec/mdl.md.
//
// Four parts:
//  - a golden-corpus check that loads every shipped .mdl and compares against
//    testdata/golden/mdl_summary.json (computed independently by tools/ref/test_mdl.py);
//  - synthetic in-memory tests for malformed input: truncation at every array boundary,
//    overflowing counts, out-of-range face indices, and fields with no NUL terminator;
//  - buildRenderMesh property tests (index validity, triangle count, per-corner data,
//    the >65535-unique-vertex failure path);
//  - a fuzz loop over mutated real files that must never crash.
#include "doctest.h"

#include "as3d/core.h"
#include "as3d/model.h"
#include "test_data.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace as3d;

// ---------------------------------------------------------------------------------
// SHA-1 (only used by this test, to check the raw array bytes against the golden
// file's sha1_arrays -- see tools/ref/test_mdl.py for the Python side).
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

// ---------------------------------------------------------------------------------
// Small generic JSON reader (object/array/string/number/bool/null), tailored to
// nothing project-specific -- unlike tga_test.cpp's fixed-shape scanner, this file's
// golden schema has a nested "counts" object and two different shapes depending on
// "status", so a little more generality earns its keep here.
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
        pos_++; // '{'
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
        pos_++; // '['
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
        pos_++; // closing quote
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
    std::string text = ss.str(); // named, so JsonParser's reference stays valid
    JsonParser p(text);
    return p.parse(out) && out.kind == JsonValue::Kind::Array;
}

double num(const JsonValue* v, double dflt = 0.0) { return v ? v->numberValue : dflt; }
std::string str(const JsonValue* v) { return v ? v->stringValue : std::string(); }

} // namespace

// ---------------------------------------------------------------------------------
// Golden corpus.
// ---------------------------------------------------------------------------------

TEST_CASE("mdl: every shipped file loads and matches the golden summary") {
    AS3D_REQUIRE_DATA();

    std::string goldenPath = testdata::goldenDir() + "/mdl_summary.json";
    JsonValue golden;
    REQUIRE_MESSAGE(loadGolden(goldenPath, golden), "missing/unparseable golden file, run "
                                                      "tools/ref/test_mdl.py first: ",
                    goldenPath);
    REQUIRE(!golden.arrayValue.empty());

    int okChecked = 0, emptyChecked = 0, brokenChecked = 0;
    for (const JsonValue& entry : golden.arrayValue) {
        std::string path = str(entry.get("path"));
        std::string status = str(entry.get("status"));
        INFO("file: ", path, " status: ", status);

        Blob blob;
        bool read = testdata::readExtracted(path, blob);
        REQUIRE(read);

        std::string error;
        ModelData model;
        bool ok = loadModel(blob.data(), blob.size(), model, &error);

        if (status == "empty") {
            CHECK(blob.empty());
            CHECK(ok == false);
            emptyChecked++;
        } else if (status == "broken") {
            // Known-corrupt shipped files: loadModel must still not fail outright --
            // it recovers whatever whole arrays fit and reports a warning.
            CHECK(ok == true);
            CHECK(!error.empty());
            brokenChecked++;
        } else {
            REQUIRE(status == "ok");
            CHECK(ok == true);
            CHECK(error.empty());

            CHECK(model.version == static_cast<u32>(num(entry.get("version"))));
            const JsonValue* smooth = entry.get("smooth_normals");
            CHECK(model.smoothNormals == (smooth && smooth->boolValue));
            CHECK(model.normalsPerFace == !model.smoothNormals);

            const JsonValue* counts = entry.get("counts");
            REQUIRE(counts != nullptr);
            CHECK(model.positions.size() == static_cast<size_t>(num(counts->get("vertices"))));
            CHECK(model.uvs.size() == static_cast<size_t>(num(counts->get("uvs"))));
            CHECK(model.faces.size() == static_cast<size_t>(num(counts->get("faces"))));
            CHECK(model.normals.size() == static_cast<size_t>(num(counts->get("normals"))));
            CHECK(model.tags.size() == static_cast<size_t>(num(counts->get("tags"))));

            const JsonValue* bbox = entry.get("bbox");
            REQUIRE(bbox != nullptr);
            REQUIRE(bbox->arrayValue.size() == 6);
            CHECK(model.boundsMin.x == doctest::Approx(bbox->arrayValue[0].numberValue));
            CHECK(model.boundsMin.y == doctest::Approx(bbox->arrayValue[1].numberValue));
            CHECK(model.boundsMin.z == doctest::Approx(bbox->arrayValue[2].numberValue));
            CHECK(model.boundsMax.x == doctest::Approx(bbox->arrayValue[3].numberValue));
            CHECK(model.boundsMax.y == doctest::Approx(bbox->arrayValue[4].numberValue));
            CHECK(model.boundsMax.z == doctest::Approx(bbox->arrayValue[5].numberValue));

            const JsonValue* tagNames = entry.get("tag_names");
            REQUIRE(tagNames != nullptr);
            REQUIRE(model.tags.size() == tagNames->arrayValue.size());
            for (size_t i = 0; i < model.tags.size(); i++) {
                CHECK(model.tags[i].name == tagNames->arrayValue[i].stringValue);
            }

            // sha1_arrays is a hash over the raw on-disk array bytes (offset 0x78 to
            // EOF), independent of anything ModelData does -- re-derive it straight
            // from the input bytes as a cheap "same file the golden data was computed
            // from" check. sha1_engine is not re-checked here: it excludes the
            // smooth-normal recompute (docs/spec/mdl.md, "Golden data"), and the
            // recompute itself is checked numerically below instead of by hash.
            const JsonValue* sha1Arrays = entry.get("sha1_arrays");
            REQUIRE(sha1Arrays != nullptr);
            REQUIRE(blob.size() >= 0x78);
            CHECK(sha1Hex(blob.data() + 0x78, blob.size() - 0x78) == sha1Arrays->stringValue);

            // Numeric check of the engine-side normal handling (see docs/spec/mdl.md,
            // "Normals"): flat normals are unit length (within the tolerance the spec
            // derives from the shipped data, generously rounded up here since this is
            // a property check, not a byte-exact one); smooth normals, after this
            // engine's normalization, are unit length too (except an all-zero
            // accumulator, which normalizes to zero by definition -- a genuinely
            // degenerate vertex, not a bug).
            for (const Vec3& n : model.normals) {
                float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                bool isZero = len < 1e-6f;
                CHECK((isZero || len == doctest::Approx(1.0f).epsilon(0.05)));
            }

            okChecked++;
        }

        // buildRenderMesh must succeed and be internally consistent for every
        // shipped file that loaded at all (well-formed or recovered-from-corrupt).
        std::vector<RenderVertex> verts;
        std::vector<u16> indices;
        bool built = buildRenderMesh(model, verts, indices);
        CHECK(built == true);
        CHECK(indices.size() == model.faces.size() * 3);
        for (u16 idx : indices) CHECK(static_cast<size_t>(idx) < verts.size());
    }

    CHECK(okChecked + emptyChecked + brokenChecked == static_cast<int>(golden.arrayValue.size()));
    CHECK(emptyChecked == 4);
    CHECK(brokenChecked == 2);
    MESSAGE("mdl: checked ", okChecked, " ok, ", emptyChecked, " empty, ", brokenChecked, " broken");
}

// ---------------------------------------------------------------------------------
// Synthetic malformed-input coverage.
// ---------------------------------------------------------------------------------

namespace {

void pushU32(std::vector<u8>& v, uint32_t x) {
    v.push_back(static_cast<u8>(x & 0xFF));
    v.push_back(static_cast<u8>((x >> 8) & 0xFF));
    v.push_back(static_cast<u8>((x >> 16) & 0xFF));
    v.push_back(static_cast<u8>((x >> 24) & 0xFF));
}
void pushU16(std::vector<u8>& v, uint16_t x) {
    v.push_back(static_cast<u8>(x & 0xFF));
    v.push_back(static_cast<u8>((x >> 8) & 0xFF));
}
void pushF32(std::vector<u8>& v, float x) {
    uint32_t bits;
    std::memcpy(&bits, &x, sizeof bits);
    pushU32(v, bits);
}
void pushStr(std::vector<u8>& v, const char* s, size_t fieldSize) {
    size_t n = std::strlen(s);
    size_t i = 0;
    for (; i < n && i < fieldSize; i++) v.push_back(static_cast<u8>(s[i]));
    for (; i < fieldSize; i++) v.push_back(0);
}

// Builds a minimal, well-formed, flat-shaded (smoothNormals=false) model with exactly
// 1 vertex... no: a triangle needs 3 distinct vertices to be a non-degenerate face, but
// this loader doesn't require non-degenerate geometry, so 1 vertex used 3 times is a
// perfectly fine "minimal" fixture: 1 vertex, 1 uv, 1 face, 1 normal, 1 tag.
std::vector<u8> minimalModel() {
    std::vector<u8> v;
    v.push_back('M'); v.push_back('D'); v.push_back('L'); v.push_back('!');
    pushU32(v, 2);  // version
    pushU32(v, 0);  // smoothNormals = false (flat)
    pushStr(v, "c:\\tex.tga", 0x40);
    REQUIRE(v.size() == 0x4C);
    pushU32(v, 1); // nverts
    pushU32(v, 1); // nuvs
    pushU32(v, 1); // nfaces
    pushU32(v, 1); // nnormals
    pushU32(v, 1); // ntags
    REQUIRE(v.size() == 0x60);
    pushF32(v, -1.0f); pushF32(v, -1.0f); pushF32(v, -1.0f); // bbox min
    pushF32(v, 1.0f); pushF32(v, 1.0f); pushF32(v, 1.0f);    // bbox max
    REQUIRE(v.size() == 0x78);
    pushF32(v, 0.0f); pushF32(v, 0.0f); pushF32(v, 0.0f);    // vertex 0
    pushF32(v, 0.5f); pushF32(v, 0.5f);                       // uv 0
    pushU16(v, 0); pushU16(v, 0); pushU16(v, 0);              // face v0,v0,v0
    pushU16(v, 0); pushU16(v, 0); pushU16(v, 0);              // face uv0,uv0,uv0
    pushF32(v, 0.0f); pushF32(v, 0.0f); pushF32(v, 1.0f);     // normal 0
    pushStr(v, "tag_test", 32);                                // tag name
    pushF32(v, 1.0f); pushF32(v, 2.0f); pushF32(v, 3.0f);      // tag position
    pushF32(v, 0.0f); pushF32(v, 0.0f); pushF32(v, 1.0f);      // tag direction
    return v;
}

} // namespace

TEST_CASE("mdl: a well-formed minimal file loads cleanly") {
    auto buf = minimalModel();
    ModelData model;
    std::string error;
    REQUIRE(loadModel(buf.data(), buf.size(), model, &error));
    CHECK(error.empty());
    CHECK(model.version == 2);
    CHECK(model.smoothNormals == false);
    CHECK(model.normalsPerFace == true);
    CHECK(model.sourceTexturePath == "c:\\tex.tga");
    REQUIRE(model.positions.size() == 1);
    REQUIRE(model.uvs.size() == 1);
    REQUIRE(model.faces.size() == 1);
    REQUIRE(model.normals.size() == 1);
    REQUIRE(model.tags.size() == 1);
    CHECK(model.uvs[0].y == doctest::Approx(0.5f)); // v=0.5 flips to itself
    CHECK(model.tags[0].name == "tag_test");
    CHECK(model.tags[0].position.x == doctest::Approx(1.0f));
    const ModelTag* found = model.findTag("TAG_TEST");
    REQUIRE(found != nullptr);
    CHECK(found == &model.tags[0]);
    CHECK(model.findTag("nope") == nullptr);
}

TEST_CASE("mdl: empty input is rejected without crashing") {
    ModelData model;
    std::string error;
    CHECK(loadModel(nullptr, 0, model, &error) == false);
    CHECK(!error.empty());
    CHECK(model.positions.empty());

    u8 dummy = 0;
    CHECK(loadModel(&dummy, 0, model, &error) == false);
}

TEST_CASE("mdl: bad magic and unsupported version are rejected") {
    auto buf = minimalModel();
    ModelData model;
    std::string error;

    SUBCASE("bad magic") {
        buf[0] = 'X';
        CHECK(loadModel(buf.data(), buf.size(), model, &error) == false);
    }
    SUBCASE("version 1") {
        buf[4] = 1;
        CHECK(loadModel(buf.data(), buf.size(), model, &error) == false);
    }
    SUBCASE("version 3 (unsupported by this loader, see docs/spec/mdl.md)") {
        buf[4] = 3;
        CHECK(loadModel(buf.data(), buf.size(), model, &error) == false);
    }
}

TEST_CASE("mdl: truncation at every array-section boundary is handled, never crashes") {
    auto full = minimalModel();
    // Truncate at every possible length from just the header up to the full file.
    // Some lengths land mid-array (must clamp that array to 0 elements and stop) and
    // some land exactly on a boundary (must clamp cleanly); neither may crash or read
    // out of bounds (run under ASan/UBSan, see tools/ci.sh).
    for (size_t len = 0; len <= full.size(); len++) {
        ModelData model;
        std::string error;
        bool ok = loadModel(full.data(), len, model, &error);
        (void)ok; // both outcomes are valid depending on len; just must not crash
        // Whatever came back must be internally consistent.
        for (const Face& f : model.faces) {
            CHECK(f.v[0] < model.positions.size());
            CHECK(f.v[1] < model.positions.size());
            CHECK(f.v[2] < model.positions.size());
            CHECK(f.uv[0] < model.uvs.size());
            CHECK(f.uv[1] < model.uvs.size());
            CHECK(f.uv[2] < model.uvs.size());
        }
        if (!model.smoothNormals) CHECK(model.normals.size() == model.faces.size());
    }
}

TEST_CASE("mdl: counts that overflow the file size are clamped, not trusted") {
    auto buf = minimalModel();
    // Claim a huge vertex count; the file itself is tiny (100 bytes follow the
    // header), so this must clamp to however many whole 12-byte vertices actually
    // fit (100 / 12 = 8) rather than attempt a multi-gigabyte allocation, read out
    // of bounds, or trust the declared count for anything downstream.
    buf[0x4C] = 0xFF;
    buf[0x4D] = 0xFF;
    buf[0x4E] = 0xFF;
    buf[0x4F] = 0x7F; // 0x7FFFFFFF vertices claimed
    ModelData model;
    std::string error;
    bool ok = loadModel(buf.data(), buf.size(), model, &error);
    CHECK(ok == true); // header itself is still fine; arrays are just clamped/empty
    CHECK(model.positions.size() == 8);
    CHECK(model.uvs.empty());   // nothing left over once the bogus vertex array "used" it
    CHECK(model.faces.empty()); // and everything after a clamp comes back empty too
    CHECK(model.tags.empty());
    CHECK(!error.empty());
}

TEST_CASE("mdl: a face with an out-of-range index is dropped, not used") {
    auto buf = minimalModel();
    // The single face's first vertex index (right after the bbox + 1 vertex + 1 uv,
    // i.e. at 0x78 + 12 + 8 = 0x8C) is overwritten to reference a vertex that doesn't
    // exist.
    size_t faceOffset = 0x78 + 12 + 8;
    buf[faceOffset] = 0xFF;
    buf[faceOffset + 1] = 0xFF;
    ModelData model;
    std::string error;
    REQUIRE(loadModel(buf.data(), buf.size(), model, &error));
    CHECK(model.faces.empty());
    CHECK(model.normals.empty()); // flat-shaded: dropped in lockstep with the face
    CHECK(!error.empty());
}

TEST_CASE("mdl: a texture path or tag name filling its whole field with no NUL is not truncated or overrun") {
    auto buf = minimalModel();
    std::string longPath(0x40, 'x');
    for (size_t i = 0; i < 0x40; i++) buf[0x0C + i] = static_cast<u8>(longPath[i]);
    size_t tagNameOffset = 0x78 + 12 + 8 + 12 + 12; // vertex + uv + face + normal, then tag name
    for (size_t i = 0; i < 32; i++) buf[tagNameOffset + i] = static_cast<u8>('a' + static_cast<int>(i % 26));

    ModelData model;
    std::string error;
    REQUIRE(loadModel(buf.data(), buf.size(), model, &error));
    CHECK(model.sourceTexturePath.size() == 0x40);
    CHECK(model.sourceTexturePath == longPath);
    REQUIRE(model.tags.size() == 1);
    CHECK(model.tags[0].name.size() == 32);
}

// ---------------------------------------------------------------------------------
// buildRenderMesh properties.
// ---------------------------------------------------------------------------------

TEST_CASE("buildRenderMesh: a quad (2 triangles sharing an edge) dedups shared corners") {
    ModelData model;
    model.smoothNormals = true;
    model.normalsPerFace = false;
    model.positions = {Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{1, 1, 0}, Vec3{0, 1, 0}};
    model.uvs = {Vec2{0, 0}, Vec2{1, 0}, Vec2{1, 1}, Vec2{0, 1}};
    model.normals = {Vec3{0, 0, 1}, Vec3{0, 0, 1}, Vec3{0, 0, 1}, Vec3{0, 0, 1}};
    Face f0;
    f0.v[0] = 0; f0.v[1] = 1; f0.v[2] = 2;
    f0.uv[0] = 0; f0.uv[1] = 1; f0.uv[2] = 2;
    Face f1;
    f1.v[0] = 0; f1.v[1] = 2; f1.v[2] = 3;
    f1.uv[0] = 0; f1.uv[1] = 2; f1.uv[2] = 3;
    model.faces = {f0, f1};

    std::vector<RenderVertex> verts;
    std::vector<u16> indices;
    REQUIRE(buildRenderMesh(model, verts, indices));
    CHECK(indices.size() == 6);
    // 4 distinct (position, uv, normal) corners across both triangles -> 4 vertices,
    // even though there are 6 corners total (vertex 0 and 2 are each used twice).
    CHECK(verts.size() == 4);
    for (u16 idx : indices) REQUIRE(idx < verts.size());

    // Every corner's data must match its source face exactly.
    auto checkCorner = [&](u16 idx, const Face& f, int corner) {
        CHECK(verts[idx].position.x == model.positions[f.v[corner]].x);
        CHECK(verts[idx].position.y == model.positions[f.v[corner]].y);
        CHECK(verts[idx].position.z == model.positions[f.v[corner]].z);
        CHECK(verts[idx].uv.x == model.uvs[f.uv[corner]].x);
        CHECK(verts[idx].uv.y == model.uvs[f.uv[corner]].y);
    };
    checkCorner(indices[0], f0, 0);
    checkCorner(indices[1], f0, 1);
    checkCorner(indices[2], f0, 2);
    checkCorner(indices[3], f1, 0);
    checkCorner(indices[4], f1, 1);
    checkCorner(indices[5], f1, 2);
}

TEST_CASE("buildRenderMesh: flat shading keeps corners from different faces distinct even "
          "with identical position/uv") {
    // Two faces that happen to reuse the same 3 positions/uvs (e.g. a degenerate but
    // structurally valid case) must NOT be merged when flat-shaded, because each face
    // has its own normal -- merging would silently pick one face's normal for both.
    ModelData model;
    model.smoothNormals = false;
    model.normalsPerFace = true;
    model.positions = {Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{0, 1, 0}};
    model.uvs = {Vec2{0, 0}, Vec2{1, 0}, Vec2{0, 1}};
    model.normals = {Vec3{0, 0, 1}, Vec3{0, 0, -1}}; // opposite-facing duplicate
    Face f0;
    f0.v[0] = 0; f0.v[1] = 1; f0.v[2] = 2;
    f0.uv[0] = 0; f0.uv[1] = 1; f0.uv[2] = 2;
    Face f1 = f0; // identical positions/uvs, different face (-> different flat normal)
    model.faces = {f0, f1};

    std::vector<RenderVertex> verts;
    std::vector<u16> indices;
    REQUIRE(buildRenderMesh(model, verts, indices));
    CHECK(indices.size() == 6);
    CHECK(verts.size() == 6); // no merging across the two faces
    CHECK(verts[indices[0]].normal.z == doctest::Approx(1.0f));
    CHECK(verts[indices[3]].normal.z == doctest::Approx(-1.0f));
}

TEST_CASE("buildRenderMesh: more than 65535 unique vertices fails cleanly") {
    ModelData model;
    model.smoothNormals = true;
    model.normalsPerFace = false;
    const int n = 65600;
    model.positions.resize(static_cast<size_t>(n));
    model.uvs.resize(static_cast<size_t>(n));
    model.normals.resize(static_cast<size_t>(n));
    for (int i = 0; i < n; i++) {
        model.positions[static_cast<size_t>(i)] = Vec3{static_cast<float>(i), 0, 0};
        model.uvs[static_cast<size_t>(i)] = Vec2{static_cast<float>(i), 0};
    }
    model.faces.reserve(static_cast<size_t>(n) / 3);
    for (int i = 0; i + 2 < n; i += 3) {
        Face f;
        f.v[0] = static_cast<u16>(i % 65536);
        // Force every corner to be a genuinely distinct (pos, uv, normal) triple by
        // using i, i+1, i+2 as both position and uv index (each appears exactly once
        // across the whole mesh, so nothing can dedup).
        f.v[0] = static_cast<u16>(i);
        f.v[1] = static_cast<u16>(i + 1);
        f.v[2] = static_cast<u16>(i + 2);
        f.uv[0] = f.v[0];
        f.uv[1] = f.v[1];
        f.uv[2] = f.v[2];
        model.faces.push_back(f);
    }

    std::vector<RenderVertex> verts;
    std::vector<u16> indices;
    bool ok = buildRenderMesh(model, verts, indices);
    CHECK(ok == false);
    CHECK(verts.empty());
    CHECK(indices.empty());
}

// ---------------------------------------------------------------------------------
// Fuzz: mutated real file must never crash or hang (run under ASan/UBSan, see
// tools/ci.sh).
// ---------------------------------------------------------------------------------

TEST_CASE("fuzz: mutated real .mdl file never crashes") {
    AS3D_REQUIRE_DATA();
    Blob seed;
    REQUIRE(testdata::readExtracted("models\\apache\\apache.mdl", seed));
    REQUIRE(!seed.empty());

    std::mt19937 rng(20020101);
    std::uniform_int_distribution<int> byteDist(0, 255);

    for (int iter = 0; iter < 3000; iter++) {
        Blob mutated = seed;
        int mutations = 1 + static_cast<int>(rng() % 8);
        for (int m = 0; m < mutations; m++) {
            if (mutated.empty()) break;
            size_t pos = rng() % mutated.size();
            int op = rng() % 3;
            if (op == 0) {
                mutated[pos] = static_cast<u8>(byteDist(rng));
            } else if (op == 1) {
                mutated.insert(mutated.begin() + static_cast<long>(pos), static_cast<u8>(byteDist(rng)));
            } else {
                mutated.erase(mutated.begin() + static_cast<long>(pos));
            }
        }
        ModelData model;
        std::string error;
        bool ok = loadModel(mutated.data(), mutated.size(), model, &error);
        if (ok) {
            std::vector<RenderVertex> verts;
            std::vector<u16> indices;
            buildRenderMesh(model, verts, indices); // must not crash either way
        }
    }

    std::uniform_int_distribution<int> lenDist(0, 2048);
    for (int iter = 0; iter < 1000; iter++) {
        Blob randomBytes(static_cast<size_t>(lenDist(rng)));
        for (auto& b : randomBytes) b = static_cast<u8>(byteDist(rng));
        ModelData model;
        std::string error;
        loadModel(randomBytes.empty() ? nullptr : randomBytes.data(), randomBytes.size(), model, &error);
    }

    CHECK(true); // reaching here means every mutation terminated
}
