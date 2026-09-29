// Collision and damage tests (WP-42a): screen rectangles on the fixed 800x600 viewport
// (engine-behaviour.md 5, render-pipeline.md 9.3), the on-screen rule, the touch pass,
// the difficulty factors and the two-player push-apart.
#include "doctest.h"

#include <cmath>

#include "world_test_util.h"

using namespace worldtest;

namespace {

ScreenRect rect(float x0, float y0, float x1, float y1) {
    ScreenRect r;
    r.min[0] = x0;
    r.min[1] = y0;
    r.max[0] = x1;
    r.max[1] = y1;
    return r;
}

// Touch script: self[21] += 1; self[22] = other[34]; Damage(other, self[35]);
// optionally remove(self).
Asm toucher(bool removeSelf) {
    Asm a;
    a.entry(EntryPoint::Touch);
    a.addSelf(21, 1.0f);
    a.leaGlobal(0, "other", 34);
    a.load(1, 0);
    a.setSelfSlot(22, 1);
    a.movGlobal(0, "other");
    a.getSelf(1, 35);
    a.call("Damage", 4);
    if (removeSelf) {
        a.movGlobal(0, "self");
        a.call("remove", 4);
    }
    a.end();
    return a;
}

void giveBox(Entity& e, float h) {
    e.boundsMin = {-h, -h, -h};
    e.boundsMax = {h, h, h};
    e.radius = h * 1.8f;
}

} // namespace

TEST_CASE("collision: rectangle, point and segment tests") {
    ScreenRect a = rect(0, 0, 10, 10), b = rect(10, 5, 20, 20), c = rect(11, 0, 20, 20);
    CHECK(World::rectsOverlap(a, b)); // touching edges overlap
    CHECK_FALSE(World::rectsOverlap(a, c));
    float in[3] = {5, 5, 0}, out[3] = {15, 5, 0};
    CHECK(World::pointInRect(in, a));
    CHECK_FALSE(World::pointInRect(out, a));
    // A segment crossing the rectangle with both ends outside.
    float s0[3] = {-5, 5, 0}, s1[3] = {15, 5, 0};
    CHECK(World::segmentHitsRect(s0, s1, a));
    float m0[3] = {-5, 15, 0}, m1[3] = {15, 15, 0};
    CHECK_FALSE(World::segmentHitsRect(m0, m1, a));
    // Shorter than sqrt(0.5): the end point is tested.
    float e0[3] = {10.3f, 5, 0}, e1[3] = {9.9f, 5, 0};
    CHECK(World::segmentHitsRect(e0, e1, a));
    CHECK_FALSE(World::segmentHitsRect(e1, e0, a));
}

TEST_CASE("collision: projection with the previous frame's camera, y up") {
    Rig r;
    r.start();
    // Default camera: (640, 32, 270), pitch -45: the view centre hits z = 0 at y = 302.
    float p[3];
    REQUIRE(r.world.projectPoint({640, 302, 0}, p));
    CHECK(p[0] == doctest::Approx(400.0f).epsilon(1e-3));
    CHECK(p[1] == doctest::Approx(300.0f).epsilon(1e-3));
    float q[3];
    REQUIRE(r.world.projectPoint({640, 500, 0}, q));
    CHECK(q[1] > p[1]); // farther ahead is higher on screen (y up)
    REQUIRE(r.world.projectPoint({700, 302, 0}, q));
    CHECK(q[0] > p[0]); // +x is to the right
    CHECK(r.world.sphereInFrustum({640, 302, 0}, 1.0f));
    CHECK_FALSE(r.world.sphereInFrustum({640, -500, 0}, 1.0f));
}

TEST_CASE("collision: screen bounds and the on-screen rule") {
    Rig r;
    r.obj("t_en {\n enemy\n flag FL_TEMPORARY\n}\n"
          "t_scenery {\n flag FL_TEMPORARY\n}\n"
          "t_dot {\n type TYPE_SPRITE\n flag FL_TEMPORARY\n touch TOUCH_ENEMIES\n}\n");
    r.start();
    int en = r.create("t_en", {640, 302, 0});
    giveBox(r.e(en), 20);
    r.world.computeScreenBounds(en);
    CHECK((r.e(en).rt & RT_COLLIDABLE) != 0);
    const ScreenRect& rc = r.e(en).rect;
    CHECK(rc.min[0] < 400.0f);
    CHECK(rc.max[0] > 400.0f);
    // bbox_scale 0.7 about the model origin: the box half-width is 14 world units.
    float edge[3];
    r.world.projectPoint({640 + 14, 302, 0}, edge);
    CHECK(rc.max[0] >= edge[0] - 1.0f);
    CHECK(rc.max[0] <= edge[0] + 30.0f);

    // Partly outside the window: not collidable.
    int side = r.create("t_en", {640, 302, 0});
    giveBox(r.e(side), 20);
    float x;
    for (x = 640; x < 1100; x += 1) {
        float pp[3];
        r.world.projectPoint({x, 302, 0}, pp);
        if (pp[0] > 799.0f) break;
    }
    r.e(side).setV3(F_BASE_ORIGIN, {x, 302, 0});
    r.world.computeScreenBounds(side);
    CHECK((r.e(side).rt & RT_COLLIDABLE) == 0);

    // Scenery (class 0, no touch filter): collidable, no rectangle.
    int sc = r.create("t_scenery", {640, -900, 0});
    r.world.computeScreenBounds(sc);
    CHECK((r.e(sc).rt & RT_COLLIDABLE) != 0);

    // Point collider: min = this frame's projection, max = the previous one.
    int dot = r.create("t_dot", {640, 302, 0});
    CHECK(r.world.isPointCollider(r.e(dot)));
    float before[3];
    r.world.projectPoint({640, 302, 0}, before);
    r.e(dot).setV3(F_BASE_ORIGIN, {650, 302, 0});
    r.world.computeScreenBounds(dot);
    float now[3];
    r.world.projectPoint({650, 302, 0}, now);
    CHECK(r.e(dot).rect.min[0] == doctest::Approx(now[0]));
    CHECK(r.e(dot).rect.max[0] == doctest::Approx(before[0]));
    CHECK((r.e(dot).rt & RT_COLLIDABLE) != 0);
}

TEST_CASE("collision: TOUCH_PLAYER hits one player, sets other and the player index") {
    Rig r;
    r.script("scripts\\touch.scr", toucher(false));
    r.obj("t_ebullet {\n type TYPE_SPRITE\n flag FL_TEMPORARY\n touch TOUCH_PLAYER\n damage 10\n"
          " script \"scripts\\touch.scr\"\n}\n");
    r.config.players = 2;
    r.start(true);
    int p0 = r.world.playerEntityIndex(0), p1 = r.world.playerEntityIndex(1);
    REQUIRE(p0 >= 0);
    REQUIRE(p1 >= 0);
    for (int p : {p0, p1}) giveBox(r.e(p), 30);
    r.step();
    // Both players on the same spot; the bullet on them.
    r.e(p0).setV3(F_ORIGIN, {640, 302, 0});
    r.e(p1).setV3(F_ORIGIN, {640, 302, 0});
    int b = r.create("t_ebullet", {640, 302, 0});
    r.e(b).setF(F_DAMAGE, 10.0f);
    r.step();
    CHECK(r.e(b).f(21) == 1.0f); // one touch per frame
    CHECK(r.e(b).f(22) == 400.0f); // other = a player (health seen before the Damage)
    int hit = (r.e(p1).f(F_HEALTH) < 400.0f) ? 1 : 0;
    CHECK(r.e(b).playerIndex == hit);
    // Damage(other, self[35]) is scaled by g_damage_factor (Normal: 0.8).
    int pe = hit ? p1 : p0;
    CHECK(r.e(pe).f(F_HEALTH) == doctest::Approx(400.0f - 8.0f));
    CHECK(r.world.otherBits == 0u); // restored after the touch
}

TEST_CASE("collision: TOUCH_ENEMIES touches every overlapping enemy unless removed") {
    for (int removeSelf = 0; removeSelf < 2; ++removeSelf) {
        Rig r;
        r.script("scripts\\touch.scr", toucher(removeSelf != 0));
        r.obj("t_pbullet {\n flag FL_TEMPORARY\n touch TOUCH_ENEMIES\n script \"scripts\\touch.scr\"\n}\n"
              "t_en {\n enemy\n flag FL_TEMPORARY\n health 50\n}\n"
              "t_friend {\n flag FL_TEMPORARY\n health 50\n}\n");
        r.start();
        int e1 = r.create("t_en", {640, 302, 0});
        int e2 = r.create("t_en", {645, 302, 0});
        int frozen = r.create("t_en", {640, 302, 0});
        int fr = r.create("t_friend", {640, 302, 0});
        int b = r.create("t_pbullet", {640, 302, 0});
        for (int i : {e1, e2, frozen, fr, b}) giveBox(r.e(i), 20);
        r.e(frozen).rt |= RT_HEALTH_FROZEN;
        r.e(b).setF(F_DAMAGE, 10.0f);
        r.step();
        CHECK(r.e(b).f(21) == (removeSelf ? 1.0f : 2.0f));
        CHECK(r.e(frozen).f(F_HEALTH) == 50.0f);
        CHECK(r.e(fr).f(F_HEALTH) == 50.0f);
        float lost = (50.0f - r.e(e1).f(F_HEALTH)) + (50.0f - r.e(e2).f(F_HEALTH));
        CHECK(lost == doctest::Approx(removeSelf ? 8.0f : 16.0f));
    }
}

TEST_CASE("collision: a point projectile sweeps between frames") {
    Rig r;
    r.script("scripts\\touch.scr", toucher(false));
    r.obj("t_dot {\n type TYPE_SPRITE\n flag FL_TEMPORARY\n touch TOUCH_ENEMIES\n script \"scripts\\touch.scr\"\n}\n"
          "t_en {\n enemy\n flag FL_TEMPORARY\n health 50\n}\n");
    r.start();
    int en = r.create("t_en", {640, 400, 0});
    giveBox(r.e(en), 10);
    int d = r.create("t_dot", {640, 300, 0});
    r.step();
    CHECK(r.e(d).f(21) == 0.0f);
    r.e(d).setV3(F_ORIGIN, {640, 500, 0}); // jumps over the enemy in one frame
    r.step();
    CHECK(r.e(d).f(21) == 1.0f);
}

TEST_CASE("collision: off-screen touchers are skipped") {
    Rig r;
    r.script("scripts\\touch.scr", toucher(false));
    r.obj("t_pbullet {\n flag FL_TEMPORARY\n touch TOUCH_ENEMIES\n script \"scripts\\touch.scr\"\n}\n"
          "t_en {\n enemy\n flag FL_TEMPORARY\n health 50\n}\n");
    r.start();
    int e1 = r.create("t_en", {640, -300, 0});
    int b = r.create("t_pbullet", {640, -300, 0});
    for (int i : {e1, b}) giveBox(r.e(i), 20);
    r.step();
    CHECK(r.e(b).f(21) == 0.0f);
    CHECK(r.e(e1).f(F_HEALTH) == 50.0f);
}

TEST_CASE("damage: the five difficulty factors, enemy fire scales twice") {
    const float factor[5] = {0.5f, 0.7f, 0.8f, 1.25f, 1.4f};
    for (int d = 0; d < 5; ++d) {
        Rig r;
        // Enemy gun: on callback, Shoot(w_e, origin, self[17]).
        Asm gun;
        gun.entry(EntryPoint::Callback);
        gun.movStr(0, "w_e");
        gun.movStr(1, "origin");
        gun.leaGlobal(2, "self", 17);
        gun.call("Shoot", 4);
        gun.end();
        r.script("scripts\\gun.scr", gun);
        r.script("scripts\\touch.scr", toucher(true));
        r.weapons = "w_e {\n missile \"t_eb\"\n speed 0\n}\n";
        r.obj("t_gun {\n enemy\n flag FL_TEMPORARY\n script \"scripts\\gun.scr\"\n}\n"
              "t_eb {\n type TYPE_SPRITE\n flag FL_TEMPORARY\n touch TOUCH_PLAYER\n damage 100\n"
              " script \"scripts\\touch.scr\"\n}\n");
        r.config.difficulty = d;
        r.start(true);
        int pe = r.world.playerEntityIndex(0);
        giveBox(r.e(pe), 30);
        r.e(pe).setV3(F_ORIGIN, {640, 302, 0});
        int g = r.create("t_gun", {640, 302, 0});
        giveBox(r.e(g), 5);
        r.step(); // rectangles and on-screen bits
        REQUIRE((r.e(g).rt & RT_COLLIDABLE) != 0);
        r.e(g).setV3(17, {0, 1, 0});
        r.world.runCallback(g, 0, 0, 0);
        r.step(); // the projectile touches the player
        INFO("difficulty " << d);
        CHECK(r.e(pe).f(F_HEALTH) == doctest::Approx(400.0f - 100.0f * factor[d] * factor[d]).epsilon(1e-4));
    }
}

TEST_CASE("damage: map health factor table and player damage immunity") {
    Rig r;
    r.config.godMode = true;
    r.start(true);
    int pe = r.world.playerEntityIndex(0);
    r.world.damageEntity(pe, 100.0f, -1);
    CHECK(r.e(pe).f(F_HEALTH) == 400.0f);
}

TEST_CASE("collision: two-player push-apart") {
    Rig r;
    r.config.players = 2;
    r.start(true);
    int p0 = r.world.playerEntityIndex(0), p1 = r.world.playerEntityIndex(1);
    for (int p : {p0, p1}) giveBox(r.e(p), 30);
    r.e(p0).setV3(F_ORIGIN, {630, 302, 0});
    r.e(p1).setV3(F_ORIGIN, {650, 302, 0});
    r.step(); // rectangles
    r.e(p0).setV3(F_VELOCITY, {0, 0, 0});
    r.e(p1).setV3(F_VELOCITY, {0, 0, 0});
    r.step();
    CHECK(r.e(p0).f(F_VELOCITY) == doctest::Approx(-2000.0f / 60.0f));
    CHECK(r.e(p1).f(F_VELOCITY) == doctest::Approx(2000.0f / 60.0f));
}
