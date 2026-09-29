// The on-screen bit 0x08 as docs/spec/issues/120 establishes it ("Required engine
// behaviour", test cases T1 to T8): the projected box touches the 800x600 window, not lies
// inside it. T1 to T3 use mission 1 with the Apache and the worked example's numbers; T4 is
// player_edge_test.cpp (the player fires at every limit); T5 to T8 use synthetic entities.
#include "doctest.h"

#include "as3d/defs.h"
#include "as3d/vfs.h"
#include "as3d/world.h"
#include "test_data.h"
#include "world_test_util.h"

using namespace worldtest;

namespace {

ScreenRect rectOf(float x0, float y0, float x1, float y1) {
    ScreenRect r;
    r.min[0] = x0;
    r.min[1] = y0;
    r.max[0] = x1;
    r.max[1] = y1;
    return r;
}

struct ApacheCase {
    ScreenRect rect;
    bool onScreen = false;
    float radius = 0.0f;
};

// Mission 1, player 1 flying the Apache (helicopter 0). One frame with the collision camera
// at x = camX (mode 1, pitch -45, height 270, y = g_map_pos), then the player's box at
// (x, g + 35, 100) with angles 0 is projected with that camera.
ApacheCase apacheAt(float camX, float x) {
    ApacheCase out;
    Vfs vfs;
    vfs.mount(makeDirSource(testdata::extractedDir()));
    DefDatabase db;
    REQUIRE(db.load(vfs));
    World w;
    WorldConfig cfg;
    cfg.heli[0] = 0;
    w.init(vfs, db, cfg);
    std::string err;
    REQUIRE(w.loadLevel("1", &err));
    int pi = w.playerEntityIndex(0);
    REQUIRE(pi >= 0);
    REQUIRE(w.entity(pi).name == "p_apache");
    PlayerInput in;
    for (int f = 0; f < 5; ++f) {
        w.camera().field[0] = camX;
        w.entity(pi).setV3(F_ORIGIN, {x, w.mapPos() + 35.0f, 100.0f});
        w.step(in);
    }
    REQUIRE(w.camera().field[0] == camX);
    REQUIRE(w.camera().quakeClock <= 0.0f);
    Entity& e = w.entity(pi);
    const Vec3 o{x, w.mapPos() + 35.0f, 100.0f};
    e.setV3(F_ORIGIN, o);
    e.setV3(F_BASE_ORIGIN, o);
    e.setV3(F_AXIS, {1, 0, 0});
    e.setV3(F_AXIS + 3, {0, 1, 0});
    e.setV3(F_AXIS + 6, {0, 0, 1});
    w.computeScreenBounds(pi);
    out.rect = e.rect;
    out.onScreen = (e.rt & RT_COLLIDABLE) != 0;
    out.radius = e.radius;
    return out;
}

} // namespace

TEST_CASE("on-screen bit: T1 bottom limit (Apache, 800x600)") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    AS3D_REQUIRE_FIRST_GAME("Apache and its worked examples");
    ApacheCase c = apacheAt(640.0f, 640.0f);
    INFO("rect " << c.rect.min[0] << ", " << c.rect.min[1] << " .. " << c.rect.max[0] << ", " << c.rect.max[1]);
    CHECK(c.radius == doctest::Approx(44.07f).epsilon(0.001));
    CHECK(c.rect.min[1] == doctest::Approx(-177.6f).epsilon(0.004));
    CHECK(c.rect.max[1] == doctest::Approx(62.7f).epsilon(0.01));
    CHECK(c.rect.min[0] == doctest::Approx(358.1f).epsilon(0.002));
    CHECK(c.rect.max[0] == doctest::Approx(442.2f).epsilon(0.002));
    CHECK(c.onScreen);
}

TEST_CASE("on-screen bit: T2 right limit, T3 left limit (Apache, 800x600)") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    AS3D_REQUIRE_FIRST_GAME("Apache and its worked examples");
    ApacheCase r = apacheAt(702.0f, 803.59f);
    INFO("right rect " << r.rect.min[0] << ", " << r.rect.min[1] << " .. " << r.rect.max[0] << ", " << r.rect.max[1]);
    CHECK(r.rect.min[0] == doctest::Approx(690.9f).epsilon(0.002));
    CHECK(r.rect.max[0] == doctest::Approx(887.1f).epsilon(0.002));
    CHECK(r.onScreen);
    ApacheCase l = apacheAt(578.0f, 476.41f);
    INFO("left rect " << l.rect.min[0] << ", " << l.rect.min[1] << " .. " << l.rect.max[0] << ", " << l.rect.max[1]);
    CHECK(l.rect.min[0] == doctest::Approx(-86.8f).epsilon(0.01));
    CHECK(l.rect.max[0] == doctest::Approx(109.3f).epsilon(0.005));
    CHECK(l.onScreen);
}

TEST_CASE("on-screen bit: the player's x clamp is at the worked example's limits") {
    // Rule 8: origin.x within [x_left + 10, x_right - 10] of the collision camera's frustum
    // at the origin's y and z. Holding right (left) with the camera at its clamp ends at the
    // worked example's 803.59 (476.41) at the bottom of the band.
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    AS3D_REQUIRE_FIRST_GAME("Apache and its worked examples");
    Vfs vfs;
    vfs.mount(makeDirSource(testdata::extractedDir()));
    DefDatabase db;
    REQUIRE(db.load(vfs));
    for (int side = 0; side < 2; ++side) {
        World w;
        WorldConfig cfg;
        cfg.heli[0] = 0;
        cfg.godMode = true;
        w.init(vfs, db, cfg);
        std::string err;
        REQUIRE(w.loadLevel("1", &err));
        PlayerInput in;
        in.confirm = true;
        // Hold the direction into the corner, then let the helicopter come to rest there (the
        // script moves it after the clamp, so while a key is held it is one frame's move,
        // 2.5 units, past the limit).
        for (int f = 0; f < 800; ++f) {
            in.action[0] = (f < 400 || f >= 700) ? 0u : (ACT_BACKWARD | (side ? ACT_RIGHT : ACT_LEFT));
            w.step(in);
        }
        int pi = w.playerEntityIndex(0);
        REQUIRE(pi >= 0);
        const Entity& e = w.entity(pi);
        INFO("side " << side << ": x " << e.f(F_ORIGIN) << ", y - g " << e.f(F_ORIGIN + 1) - w.mapPos() << ", camera x "
                     << w.camera().field[0]);
        CHECK(e.f(F_ORIGIN + 1) - w.mapPos() == doctest::Approx(35.0f).epsilon(0.02));
        CHECK(w.camera().field[0] == (side ? 702.0f : 578.0f));
        CHECK(e.f(F_ORIGIN) == doctest::Approx(side ? 803.59f : 476.41f).epsilon(0.0002));
        CHECK((e.rt & RT_COLLIDABLE) != 0);
    }
}

TEST_CASE("on-screen bit: T5 to T8, the overlap rule and its bounds") {
    CHECK(World::rectTouchesViewport(rectOf(780, 300, 860, 360)));       // T5
    CHECK_FALSE(World::rectTouchesViewport(rectOf(801, 300, 860, 360))); // T6
    CHECK(World::rectTouchesViewport(rectOf(-50, 300, 0, 360)));         // T7: max.x = 0
    CHECK_FALSE(World::rectTouchesViewport(rectOf(800, 300, 860, 360))); // T8: min.x = 800
    CHECK(World::rectTouchesViewport(rectOf(300, -200, 400, 0)));        // max.y = 0
    CHECK_FALSE(World::rectTouchesViewport(rectOf(300, 600, 400, 700))); // min.y = 600
    CHECK_FALSE(World::rectTouchesViewport(rectOf(-90, 300, -0.01f, 360)));
    CHECK(World::rectTouchesViewport(rectOf(-1000, -1000, 5000, 5000))); // covers the window
    const float in0[3] = {0, 0, 0}, edgeX[3] = {800, 10, 0}, edgeY[3] = {10, 600, 0}, neg[3] = {-0.01f, 10, 0},
                last[3] = {799.9f, 599.9f, 0};
    CHECK(World::pointOnViewport(in0)); // lower bound inclusive
    CHECK_FALSE(World::pointOnViewport(edgeX));
    CHECK_FALSE(World::pointOnViewport(edgeY));
    CHECK_FALSE(World::pointOnViewport(neg));
    CHECK(World::pointOnViewport(last));
}

TEST_CASE("on-screen bit: T5 and T6 on a live enemy: fires and is hit while partly off screen") {
    // A synthetic enemy with a 40-unit box and a gun script, moved right in 5-unit steps:
    // while its rectangle straddles x = 800 it is collidable and Shoot makes projectiles;
    // once the rectangle is entirely past x = 800 it is not and Shoot does nothing.
    Rig r;
    Asm gun;
    gun.entry(EntryPoint::Callback);
    gun.movStr(0, "w_e");
    gun.movStr(1, "origin");
    gun.leaGlobal(2, "self", 17);
    gun.call("Shoot", 4);
    gun.end();
    r.script("scripts\\gun.scr", gun);
    r.weapons = "w_e {\n missile \"t_eb\"\n speed 0\n}\n";
    r.obj("t_gun {\n enemy\n flag FL_TEMPORARY\n script \"scripts\\gun.scr\"\n}\n"
          "t_eb {\n type TYPE_SPRITE\n flag FL_TEMPORARY\n touch TOUCH_PLAYER\n}\n");
    r.start(false);
    int g = r.create("t_gun", {640, 400, 0});
    Entity& e = r.e(g);
    e.boundsMin = {-20, -20, -20};
    e.boundsMax = {20, 20, 20};
    e.radius = 36.0f;
    r.step();
    bool sawStraddle = false, sawOff = false;
    for (float x = 640.0f; x < 1400.0f && !(sawStraddle && sawOff); x += 5.0f) {
        e.setV3(F_ORIGIN, {x, 400, 0});
        r.step();
        const ScreenRect& rc = e.rect;
        const bool straddles = rc.min[0] < 800.0f && rc.max[0] >= 800.0f;
        const bool past = rc.min[0] >= 800.0f;
        if (!straddles && !past) continue;
        if (past && !r.world.sphereInFrustum(e.v3(F_ORIGIN), e.radius)) break; // rectangle no longer updated
        int before = r.world.listCount();
        e.setV3(17, {0, 1, 0});
        r.world.runCallback(g, 0, 0, 0);
        int made = r.world.listCount() - before;
        INFO("x " << x << " rect " << rc.min[0] << ".." << rc.max[0]);
        if (straddles) {
            CHECK((e.rt & RT_COLLIDABLE) != 0);
            CHECK(made == 1);
            sawStraddle = true;
        } else {
            CHECK((e.rt & RT_COLLIDABLE) == 0);
            CHECK(made == 0);
            sawOff = true;
        }
    }
    CHECK(sawStraddle);
}
