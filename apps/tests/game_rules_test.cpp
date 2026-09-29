// GameRules read by the simulation (docs/spec/README.md "Games and which spec applies"):
// the default rules reproduce the first game exactly, and a changed field changes the world.
#include "doctest.h"

#include <string>

#include "as3d/defs.h"
#include "as3d/game_profile.h"
#include "as3d/input.h"
#include "as3d/vfs.h"
#include "as3d/world.h"
#include "test_data.h"

using namespace as3d;

namespace {

struct Fixture {
    Vfs vfs;
    DefDatabase db;
    Fixture() {
        vfs.mount(makeDirSource(testdata::extractedDir()));
        REQUIRE(db.load(vfs));
    }
};

void stepBot(World& w, int frames) {
    for (int f = 0; f < frames; ++f) w.step(botInput(static_cast<u32>(f)).toPlayerInput());
}

std::string fly(Fixture& fx, const GameRules* rules, int frames) {
    World w;
    WorldConfig cfg;
    cfg.rules = rules;
    w.init(fx.vfs, fx.db, cfg);
    std::string err;
    REQUIRE(w.loadLevel("1", &err));
    stepBot(w, frames);
    return w.dumpStateJson();
}

const char* const kOtherHelis[] = {"p_apache", "p_apache_blue"};

} // namespace

TEST_CASE("game rules: nullptr and the default rules give identical state dumps") {
    AS3D_REQUIRE_DATA();
    Fixture fx;
    std::string a = fly(fx, nullptr, 600), b = fly(fx, &defaultGameRules(), 600);
    CHECK(a == b);
}

TEST_CASE("game rules: a world resolves nullptr to the default rules") {
    AS3D_REQUIRE_DATA();
    Fixture fx;
    World w;
    w.init(fx.vfs, fx.db, WorldConfig());
    CHECK(&w.rules() == &defaultGameRules());
}

TEST_CASE("game rules: changed scroll speed, start position and helicopter are read") {
    AS3D_REQUIRE_DATA();
    Fixture fx;
    GameRules r = defaultGameRules();
    r.scrollSpeed = 21.0f;
    r.startMapPos = 100.0f;
    r.startCameraX = 600.0f;
    r.heliObjects = kOtherHelis;
    r.helicopterCount = 2;

    World w;
    WorldConfig cfg;
    cfg.rules = &r;
    cfg.heli[0] = 1;
    w.init(fx.vfs, fx.db, cfg);
    std::string err;
    REQUIRE(w.loadLevel("1", &err));
    CHECK(w.mapPos() == doctest::Approx(100.0f));
    CHECK(w.camera().field[0] == doctest::Approx(600.0f));
    const int frames = 60;
    stepBot(w, frames);
    float scrolled = w.mapPos() - 100.0f;
    CHECK(scrolled > 0.0f);
    CHECK(scrolled <= 21.0f * frames * cfg.dt + 0.01f);
    int pi = w.playerEntityIndex(0);
    REQUIRE(pi >= 0);
    REQUIRE(w.entity(pi).def != nullptr);
    CHECK(w.entity(pi).def->name == "p_apache_blue");

    World d;
    d.init(fx.vfs, fx.db, WorldConfig());
    REQUIRE(d.loadLevel("1", &err));
    CHECK(d.mapPos() == doctest::Approx(32.0f));
    stepBot(d, frames);
    CHECK(d.mapPos() - 32.0f > 1.5f * scrolled);
    int di = d.playerEntityIndex(0);
    REQUIRE(di >= 0);
    CHECK(d.entity(di).def->name == "p_comanche");
}

TEST_CASE("game rules: lives and camera limits are read") {
    AS3D_REQUIRE_DATA();
    Fixture fx;
    GameRules r = defaultGameRules();
    r.startLives = 5;
    r.cameraMinX = 600.0f;
    r.cameraMaxX = 610.0f;
    World w;
    WorldConfig cfg;
    cfg.rules = &r;
    w.init(fx.vfs, fx.db, cfg);
    std::string err;
    REQUIRE(w.loadLevel("1", &err));
    CHECK(w.player(0).lives == doctest::Approx(5.0f));
    stepBot(w, 30);
    CHECK(w.camera().field[0] >= 600.0f);
    CHECK(w.camera().field[0] <= 610.0f);
}

TEST_CASE("game rules: a game without the named objects skips the features") {
    AS3D_REQUIRE_DATA();
    Fixture fx;
    GameRules r = defaultGameRules();
    r.scoreDigitObject = nullptr;
    r.starItemObject = nullptr;
    r.healthBarEmptyObject = nullptr;
    r.healthBarFullObject = nullptr;
    World w;
    WorldConfig cfg;
    cfg.rules = &r;
    w.init(fx.vfs, fx.db, cfg);
    std::string err;
    REQUIRE(w.loadLevel("1", &err));
    CHECK(w.starTotal() == 0);
    stepBot(w, 600);
    // Scoring an enemy shows no floating digits and does not crash.
    int enemy = -1;
    for (int i = 0; i < kMaxEntitySlots && enemy < 0; ++i) {
        if (w.validIndex(i) && w.entity(i).f(F_CLASS) == kClassEnemy) enemy = i;
    }
    REQUIRE(enemy >= 0);
    w.entity(enemy).setF(F_SCORE, 100.0f);
    int before = w.listCount();
    w.awardScore(0, enemy);
    CHECK(w.listCount() == before);
    CHECK(w.player(0).scores >= 100.0f);
}
