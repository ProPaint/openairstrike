// Enemy and boss health bars (engine-behaviour.md 6.5; split of the bar: spec issue 111).
#include "doctest.h"

#include "as3d/health_bar.h"
#include "as3d/defs.h"
#include "as3d/input.h"
#include "as3d/vfs.h"
#include "as3d/world.h"
#include "test_data.h"

using namespace as3d;

namespace {

Entity bigEnemy() {
    Entity e;
    e.maxHealth = 400.0f;
    e.setF(F_CLASS, kClassEnemy);
    e.setF(F_HEALTH, 100.0f);
    e.setV3(F_BASE_ORIGIN, {100, 200, 30});
    e.radius = 50.0f;
    e.boundsMin = {-10, -10, -5};
    e.boundsMax = {10, 10, 20};
    e.sinceDamage = 0.5f;
    return e;
}

} // namespace

TEST_CASE("health bar: when it is shown") {
    Entity e = bigEnemy();
    CHECK(healthBarVisible(e));
    e.sinceDamage = 1.0f; // one second after the hit: gone
    CHECK_FALSE(healthBarVisible(e));
    e = bigEnemy();
    e.maxHealth = 150.0f; // strictly more than 150
    CHECK_FALSE(healthBarVisible(e));
    e = bigEnemy();
    e.setF(F_CLASS, kClassPlayer);
    CHECK_FALSE(healthBarVisible(e));
    e = bigEnemy();
    e.setF(F_DEAD, 1.0f);
    CHECK_FALSE(healthBarVisible(e));
    e = bigEnemy();
    e.sinceDamage = 2.0f; // the value after init: never damaged
    CHECK_FALSE(healthBarVisible(e));
}

TEST_CASE("health bar: position and fill") {
    Entity e = bigEnemy();
    Vec3 p = healthBarPosition(e); // airborne: above the top of the model, ahead on the map
    CHECK(p.x == 100.0f);
    CHECK(p.y == doctest::Approx(230.0f));
    CHECK(p.z == doctest::Approx(54.0f));
    e.setF(F_FLAGS, 1.0f); // FL_ONGROUND
    p = healthBarPosition(e);
    CHECK(p.y == doctest::Approx(170.0f));
    CHECK(p.z == doctest::Approx(24.0f));
    e.setF(F_FLAGS, 3.0f); // FL_ONGROUND_NORMAL includes FL_ONGROUND
    CHECK(healthBarPosition(e).y == doctest::Approx(170.0f));

    CHECK(healthBarFraction(e) == doctest::Approx(0.25f));
    HealthBarSpriteDef empty, full;
    SpriteInstance s[2];
    REQUIRE(buildHealthBar(e, empty, full, s) == 2);
    CHECK(s[0].minX == -16.0f);
    CHECK(s[0].maxX == 16.0f);
    CHECK(s[1].minX == -16.0f);
    CHECK(s[1].maxX == doctest::Approx(-8.0f));
    CHECK(s[1].maxS == doctest::Approx(0.25f));
    CHECK(s[0].noDepthTest);
    CHECK(s[0].kind == SpriteKind::Billboard);
    e.setF(F_HEALTH, -5.0f); // health below 0 but not dead yet: an empty bar only
    CHECK(buildHealthBar(e, empty, full, s) == 1);
    e.setF(F_HEALTH, 900.0f);
    CHECK(healthBarFraction(e) == 1.0f);
}

TEST_CASE("health bar: big enemies show it during mission 1") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    AS3D_REQUIRE_FIRST_GAME("mission 1 enemies");
    Vfs vfs;
    vfs.mount(makeDirSource(testdata::extractedDir()));
    DefDatabase db;
    REQUIRE(db.load(vfs));
    const ObjectDef* he = db.findObject("hbar_empty");
    const ObjectDef* hf = db.findObject("hbar_full");
    REQUIRE(he);
    REQUIRE(hf);
    CHECK(he->hasBbox);
    CHECK((hf->rflag & RF_NODEPTHTEST) != 0);
    World world;
    WorldConfig cfg;
    cfg.godMode = true;
    world.init(vfs, db, cfg);
    std::string err;
    REQUIRE(world.loadLevel("1", &err));
    int framesWithBar = 0;
    for (u32 f = 0; f < 3600; ++f) {
        world.step(botInput(f).toPlayerInput());
        for (int i : world.listEntities()) {
            if (healthBarVisible(world.entity(i))) {
                ++framesWithBar;
                break;
            }
        }
    }
    CHECK(framesWithBar > 30);
}
