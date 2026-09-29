// The reader of testdata/golden/<key>/expected.json (apps/tests/expected.h) and the files
// themselves: every game that has a golden directory has a complete, consistent one.
#include "doctest.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "as3d/game_profile.h"
#include "test_data.h"

using namespace testdata;

TEST_CASE("expected.h: JSON reader handles nesting, escapes and numbers") {
    Json j;
    REQUIRE(parseJson(R"({"a": {"b": [1, 2.5, -3], "c": "x\\y\"zA"}, "t": true, "f": false, "n": null, "e": {}})", j));
    REQUIRE(j.kind == Json::Obj);
    const Json* b = j.find("a")->find("b");
    REQUIRE(b != nullptr);
    REQUIRE(b->a.size() == 3);
    CHECK(b->a[0].asInt() == 1);
    CHECK(b->a[1].n == doctest::Approx(2.5));
    CHECK(b->a[2].asInt() == -3);
    CHECK(j.find("a")->find("c")->s == "x\\y\"zA");
    CHECK(j.find("t")->b);
    CHECK_FALSE(j.find("f")->b);
    CHECK(j.find("n")->kind == Json::Null);
    CHECK(j.find("e")->o.empty());
}

TEST_CASE("expected.h: malformed JSON is refused") {
    Json j;
    CHECK_FALSE(parseJson("", j));
    CHECK_FALSE(parseJson("{", j));
    CHECK_FALSE(parseJson("{\"a\": }", j));
    CHECK_FALSE(parseJson("[1, 2", j));
    CHECK_FALSE(parseJson("{\"a\": 1} x", j));
    CHECK_FALSE(parseJson("\"open", j));
}

TEST_CASE("expected.json: every game with goldens has a complete one for its key") {
    int checked = 0;
    for (int i = 0; i < as3d::kGameCount; ++i) {
        const as3d::GameProfile& g = as3d::gameProfile(static_cast<as3d::GameId>(i));
        const std::string path = std::string(AS3D_REPO_ROOT) + "/testdata/golden/" + g.key + "/expected.json";
        std::ifstream f(path, std::ios::binary);
        if (!f) continue; // a game whose goldens are not there yet
        std::ostringstream ss;
        ss << f.rdbuf();
        Json j;
        INFO("game ", g.key);
        REQUIRE(parseJson(ss.str(), j));
        REQUIRE(j.kind == Json::Obj);
        CHECK(j.find("game")->s == g.key);
        REQUIRE(j.find("playable") != nullptr);
        CHECK(j.find("playable")->kind == Json::Bool);
        // The code decides (as3d::gameIsPlayable); the file's statement must agree with it.
        CHECK(j.find("playable")->b == as3d::gameIsPlayable(g));
        for (const char* section : {"paks", "textures", "models", "maps", "text_blocks", "definitions", "scripts"}) {
            INFO("section ", section);
            CHECK(j.find(section) != nullptr);
        }
        // The models: ok + empty + broken = files.
        const Json* m = j.find("models");
        REQUIRE(m != nullptr);
        CHECK(m->find("ok")->asInt() + static_cast<long long>(m->find("empty")->a.size()) +
                  static_cast<long long>(m->find("broken")->o.size()) ==
              m->find("files")->asInt());
        // The maps: as many names as files.
        const Json* maps = j.find("maps");
        REQUIRE(maps != nullptr);
        CHECK(static_cast<long long>(maps->find("names")->a.size()) == maps->find("files")->asInt());
        ++checked;
    }
    CHECK(checked >= 1);
}

TEST_CASE("expected.json: the game under test is described and the first game plays") {
    CHECK(expected().find("game")->s == gameKey());
    if (gameKey() == "as3d" || gameKey() == "as2") CHECK(playable());
    if (gameKey() == "gulf") CHECK_FALSE(playable());
    CHECK(expectedInt("definitions.objects") >= expectedInt("definitions.distinct_object_names"));
}
