// Acceptance tests for the typed def loader (WP-1B). See docs/spec/obj.md,
// wpn.md, ps.md, levels-txt.md.
#include "doctest.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/defs.h"
#include "as3d/vfs.h"
#include "test_data.h"

using namespace as3d;

namespace {

// ---------------------------------------------------------------------
// Minimal self-contained SHA-1 (public-domain), same approach as
// apps/tests/vfs_test.cpp: hashes the canonical text as raw bytes.
// ---------------------------------------------------------------------

u32 rol(u32 x, u32 c) { return (x << c) | (x >> (32 - c)); }

std::string sha1Hex(const std::string& data) {
    u32 h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;

    std::vector<u8> msg(data.begin(), data.end());
    const std::uint64_t bitLen = static_cast<std::uint64_t>(data.size()) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<u8>((bitLen >> (i * 8)) & 0xFF));

    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        u32 w[80];
        for (int i = 0; i < 16; ++i) {
            size_t p = chunk + static_cast<size_t>(i) * 4;
            w[i] = (u32(msg[p]) << 24) | (u32(msg[p + 1]) << 16) | (u32(msg[p + 2]) << 8) |
                   u32(msg[p + 3]);
        }
        for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        u32 a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            u32 f, k;
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
            u32 temp = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
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

// ---------------------------------------------------------------------
// A tiny general-purpose JSON reader, just enough for defs_summary.json
// (objects made of strings/ints/arrays/nested objects; produced by
// tools/ref/test_defs.py via json.dump, so it is always well-formed).
// ---------------------------------------------------------------------

struct Json {
    enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
    double num = 0;
    std::string str;
    std::vector<Json> arr;
    std::map<std::string, Json> obj;

    const Json& operator[](const char* key) const {
        static const Json kNull;
        auto it = obj.find(key);
        return it == obj.end() ? kNull : it->second;
    }
    long asInt() const { return static_cast<long>(num); }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : s_(text) {}

    Json parse() {
        skipWs();
        Json v = parseValue();
        return v;
    }

private:
    const std::string& s_;
    size_t i_ = 0;

    void skipWs() {
        while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_]))) i_++;
    }

    Json parseValue() {
        skipWs();
        char c = s_[i_];
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return parseString();
        if (c == 't') {
            i_ += 4;
            Json v;
            v.kind = Json::Kind::Bool;
            v.num = 1;
            return v;
        }
        if (c == 'f') {
            i_ += 5;
            Json v;
            v.kind = Json::Kind::Bool;
            v.num = 0;
            return v;
        }
        if (c == 'n') {
            i_ += 4;
            return Json{};
        }
        return parseNumber();
    }

    Json parseObject() {
        Json v;
        v.kind = Json::Kind::Object;
        i_++; // '{'
        skipWs();
        if (s_[i_] == '}') {
            i_++;
            return v;
        }
        for (;;) {
            skipWs();
            Json key = parseString();
            skipWs();
            i_++; // ':'
            Json val = parseValue();
            v.obj[key.str] = std::move(val);
            skipWs();
            if (s_[i_] == ',') {
                i_++;
                continue;
            }
            i_++; // '}'
            break;
        }
        return v;
    }

    Json parseArray() {
        Json v;
        v.kind = Json::Kind::Array;
        i_++; // '['
        skipWs();
        if (s_[i_] == ']') {
            i_++;
            return v;
        }
        for (;;) {
            Json val = parseValue();
            v.arr.push_back(std::move(val));
            skipWs();
            if (s_[i_] == ',') {
                i_++;
                continue;
            }
            i_++; // ']'
            break;
        }
        return v;
    }

    Json parseString() {
        Json v;
        v.kind = Json::Kind::String;
        i_++; // opening quote
        std::string out;
        while (s_[i_] != '"') {
            if (s_[i_] == '\\') {
                char e = s_[i_ + 1];
                if (e == 'n') out.push_back('\n');
                else if (e == 't') out.push_back('\t');
                else if (e == 'u') {
                    // Only used for control characters in practice here;
                    // decode as a raw byte if it's ASCII-range.
                    std::string hex = s_.substr(i_ + 2, 4);
                    int code = std::stoi(hex, nullptr, 16);
                    out.push_back(static_cast<char>(code));
                    i_ += 6;
                    continue;
                } else {
                    out.push_back(e);
                }
                i_ += 2;
            } else {
                out.push_back(s_[i_]);
                i_++;
            }
        }
        i_++; // closing quote
        v.str = out;
        return v;
    }

    Json parseNumber() {
        size_t start = i_;
        while (i_ < s_.size() &&
               (std::isdigit(static_cast<unsigned char>(s_[i_])) || s_[i_] == '-' ||
                s_[i_] == '+' || s_[i_] == '.' || s_[i_] == 'e' || s_[i_] == 'E')) {
            i_++;
        }
        Json v;
        v.kind = Json::Kind::Number;
        v.num = std::stod(s_.substr(start, i_ - start));
        return v;
    }
};

std::string slurpFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

TEST_CASE("DefDatabase loads every definition file from the real paks with the documented counts") {
    AS3D_REQUIRE_DATA();

    Vfs vfs;
    REQUIRE(testdata::mountGamePaks(vfs));

    DefDatabase db;
    REQUIRE(db.load(vfs));

    // The counts are in expected.json ("definitions").
    CHECK(db.objects().size() == static_cast<size_t>(testdata::expectedInt("definitions.objects")));
    CHECK(db.weapons().size() == static_cast<size_t>(testdata::expectedInt("definitions.weapons")));
    CHECK(db.particleSystems().size() == static_cast<size_t>(testdata::expectedInt("definitions.particle_systems")));
    CHECK(db.levels().size() == static_cast<size_t>(testdata::expectedInt("definitions.levels")));

    std::set<std::string> distinctNames;
    for (const auto& o : db.objects()) distinctNames.insert(o.name);
    CHECK(distinctNames.size() == static_cast<size_t>(testdata::expectedInt("definitions.distinct_object_names")));
}

TEST_CASE("duplicate object name: first definition wins name lookup (VERIFIED-CODE)") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE(); // the first game's definitions
    AS3D_REQUIRE_FIRST_GAME("object definitions");
    Vfs vfs;
    REQUIRE(testdata::mountGamePaks(vfs));
    DefDatabase db;
    REQUIRE(db.load(vfs));

    int dupCount = 0;
    for (const auto& o : db.objects()) {
        if (o.name == "tank_dead") dupCount++;
    }
    CHECK(dupCount == 2);

    const ObjectDef* found = db.findObject("tank_dead");
    REQUIRE(found != nullptr);
    // The first tank_dead in objects/tanks.obj (line 41) has this skin; the
    // second, unreachable one (line 180) has a different, broken skin path.
    CHECK(found->skin == "models/tanks/tank_dead.tga");
}

TEST_CASE("spot check: tank_small_green") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE(); // the first game's definitions
    AS3D_REQUIRE_FIRST_GAME("object definitions");
    Vfs vfs;
    REQUIRE(testdata::mountGamePaks(vfs));
    DefDatabase db;
    REQUIRE(db.load(vfs));

    const ObjectDef* o = db.findObject("tank_small_green");
    REQUIRE(o != nullptr);
    CHECK(o->health == 380);
    CHECK(o->score == 500);
    CHECK(o->kind == ObjectKind::Enemy);
    CHECK((o->flags & FL_ONGROUND) != 0);
    CHECK((o->flags & FL_ONGROUND_NORMAL_EXTRA) != 0); // FL_ONGROUND_NORMAL sets both bits
    CHECK(o->shadow == ShadowMode::PlanarLow); // SHADOW_PLANAR_LOW remaps to this (=8)

    REQUIRE(o->attachments.size() == 4);
    CHECK(o->attachments[0].targetName == "tank_small_green_cannon");
    CHECK(o->attachments[0].tagName == "tag_turret");
    CHECK(o->attachments[0].absolute);
    CHECK_FALSE(o->attachments[0].nightOnly);

    CHECK(o->attachments[1].targetName == "svet_far");
    CHECK(o->attachments[1].tagName == "tag_fara1");
    CHECK(o->attachments[1].absolute);
    CHECK(o->attachments[1].nightOnly);

    CHECK(o->attachments[3].targetName == "PS_CAMPFIRE_ALMOSTDEAD");
    CHECK(o->attachments[3].tagName == "origin");
    CHECK(o->attachments[3].idName == "CAMPFIRE_ALMOSTDEAD");
}

TEST_CASE("spot check: mission 1's fog, sun and water") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE(); // the first game's definitions
    AS3D_REQUIRE_FIRST_GAME("mission 1 settings");
    Vfs vfs;
    REQUIRE(testdata::mountGamePaks(vfs));
    DefDatabase db;
    REQUIRE(db.load(vfs));

    REQUIRE(!db.levels().empty());
    const LevelDef& lv = db.levels()[0];
    CHECK(lv.id == "mission1");
    CHECK(lv.name == "Mission 1: Tutorial");
    CHECK(lv.hasFog);
    CHECK(lv.fogColor[0] == doctest::Approx(0.4f));
    CHECK(lv.fogColor[1] == doctest::Approx(0.4f));
    CHECK(lv.fogColor[2] == doctest::Approx(0.3f));
    CHECK(lv.fogNear == doctest::Approx(650.0f));
    CHECK(lv.fogFar == doctest::Approx(900.0f));
    CHECK(lv.sun[0] == doctest::Approx(0.9f));
    CHECK(lv.hasWater);
    CHECK(lv.waterTexture == "gfx\\water\\water_lake2.tga");
    CHECK(lv.waterLevel == doctest::Approx(-68.0f));
    CHECK(lv.waterAlpha == doctest::Approx(0.4f));
}

TEST_CASE("golden summary: hashes and unresolved references match tools/ref/defs.py") {
    AS3D_REQUIRE_DATA();
    Vfs vfs;
    REQUIRE(testdata::mountGamePaks(vfs));
    DefDatabase db;
    REQUIRE(db.load(vfs));
    db.validate(vfs);

    std::string goldenText = slurpFile(testdata::goldenDir() + "/defs_summary.json");
    REQUIRE(!goldenText.empty());
    Json golden = JsonParser(goldenText).parse();

    // Objects: compare element-by-element, in file order (names repeat, so
    // a name->hash map would hide the duplicate).
    REQUIRE(golden["objects"].arr.size() == db.objects().size());
    int objMismatches = 0;
    for (size_t i = 0; i < db.objects().size(); i++) {
        const auto& g = golden["objects"].arr[i];
        const auto& o = db.objects()[i];
        std::string actualHash = sha1Hex(canonicalObject(o));
        if (g["name"].str != o.name || g["hash"].str != actualHash) {
            objMismatches++;
            if (objMismatches <= 5) {
                INFO("object[", i, "] name=", o.name, " golden_hash=", g["hash"].str,
                     " actual_hash=", actualHash);
                CHECK(g["name"].str == o.name);
                CHECK(g["hash"].str == actualHash);
            }
        }
    }
    CHECK(objMismatches == 0);

    REQUIRE(golden["weapons"].arr.size() == db.weapons().size());
    for (size_t i = 0; i < db.weapons().size(); i++) {
        CHECK(golden["weapons"].arr[i]["hash"].str == sha1Hex(canonicalWeapon(db.weapons()[i])));
    }

    REQUIRE(golden["particle_systems"].arr.size() == db.particleSystems().size());
    for (size_t i = 0; i < db.particleSystems().size(); i++) {
        CHECK(golden["particle_systems"].arr[i]["hash"].str ==
              sha1Hex(canonicalParticleSystem(db.particleSystems()[i])));
    }

    REQUIRE(golden["levels"].arr.size() == db.levels().size());
    for (size_t i = 0; i < db.levels().size(); i++) {
        CHECK(golden["levels"].arr[i]["hash"].str == sha1Hex(canonicalLevel(db.levels()[i])));
    }

    // Unresolved references: the C++ validate() must report exactly the
    // documented set (order-independent).
    std::set<std::string> goldenUnresolved;
    for (const auto& e : golden["unresolved_references"].arr) goldenUnresolved.insert(e.str);

    std::set<std::string> actualUnresolved;
    for (const auto& w : db.warnings()) {
        if (w.rfind("object '", 0) == 0 || w.rfind("weapon '", 0) == 0 ||
            w.rfind("particle system '", 0) == 0 || w.rfind("level '", 0) == 0) {
            actualUnresolved.insert(w);
        }
    }
    CHECK(actualUnresolved == goldenUnresolved);
    // The known unresolved references of the game are in expected.json as well.
    std::set<std::string> wantUnresolved;
    for (const auto& e : testdata::expectedStrings("definitions.unresolved_references")) wantUnresolved.insert(e);
    CHECK(actualUnresolved == wantUnresolved);
    CHECK(golden["summary"]["total_objects"].asInt() == testdata::expectedInt("definitions.objects"));
    CHECK(golden["summary"]["distinct_object_names"].asInt() == testdata::expectedInt("definitions.distinct_object_names"));
}
