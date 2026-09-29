// AirStrike 2's remaining game rules (package C6; docs/spec/as2/engine-behaviour.delta.md):
// skid trails (3.1.2, issue as2/210), entities on the animated water (4.2), civilians in the
// touch pass and the statistics (3.1.1, 5.2), the level start's load-time spawn and its
// statistics quirk (2, 10.2, issue as2/211), the loadout on a new game, Restart and Next and
// the checkpoint in the mission report (8.2, 10.3, issue as2/271), mouse control (7.2, issue
// as2/272). Synthetic worlds need no data; the tests on the shipped levels select the sequel
// explicitly (gameProfile(GameId::AirStrike2), locateGameData) and skip loudly without it.
#include "doctest.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>

#include "as3d/frontend.h"
#include "as3d/game_data.h"
#include "as3d/input.h"
#include "as3d/water.h"
#include "../game/game_flow.h"
#include "../game/game_session.h"
#include "test_data.h"
#include "world_test_util.h"

using namespace worldtest;

namespace {

const GameRules& as2Rules() { return gameProfile(GameId::AirStrike2).rules; }

const char* kAs2Helis =
    "player_1 {\n player\n flag FL_TEMPORARY\n health 500\n speed 1.0\n}\n"
    "player_2 {\n player\n flag FL_TEMPORARY\n health 400\n speed 1.25\n}\n";

// A jeep with the shipped left and right marks (jeeps.obj: 17 21 16 and -17 21 16).
const char* kJeep = "t_jeep {\n flag FL_TEMPORARY\n skid_mark 17 21 16 \"gfx/marks/jeepmark1.tga\"\n"
                    " skid_mark -17 21 16 \"gfx/marks/jeepmark1.tga\"\n}\n"
                    "t_onemark {\n flag FL_TEMPORARY\n skid_mark 0 0 8 \"gfx/marks/tankmark1.tga\"\n}\n";

// W x H cells of raw height 128 (hmin..hmax), optionally water at `waterLevel`, and
// placements of `types` at the given 1-based rows (column 16).
std::unique_ptr<LoadedLevel> flatLevel(int w, int h, bool water = false, float waterLevel = 0.0f) {
    std::unique_ptr<LoadedLevel> l(new LoadedLevel());
    l->data.width = static_cast<u32>(w);
    l->data.height = static_cast<u32>(h);
    l->data.cells.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (LevelCell& c : l->data.cells) c.height = 128;
    l->style.hmin = -100.0f;
    l->style.hmax = 155.0f; // one grey level = 1 unit; raw 128 is z = 28
    l->style.hasWater = water;
    l->style.waterLevel = waterLevel;
    return l;
}

void place(LoadedLevel& l, const std::string& type, u16 row1, u16 col = 16) {
    u16 t = 0;
    while (t < l.data.typeNames.size() && l.data.typeNames[t] != type) ++t;
    if (t == l.data.typeNames.size()) l.data.typeNames.push_back(type);
    Placement p;
    p.typeIndex = t;
    p.x = col;
    p.y = row1;
    l.data.placements.push_back(p);
}

struct R {
    Rig r;
    explicit R(GameId game = GameId::AirStrike2, const std::string& objects = "") {
        r.config.rules = &gameProfile(game).rules;
        r.obj(kAs2Helis);
        r.obj(kJeep);
        r.obj(objects);
        r.start();
    }
    World& w() { return r.world; }
    void step(int n = 1) { r.step(n); }
};

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

Vec2 flatAxisOf(const Entity& e, int k) {
    Vec2 v{e.f(F_AXIS + 3 * k), e.f(F_AXIS + 3 * k + 1)};
    const float len = std::sqrt(v.x * v.x + v.y * v.y);
    return len > 0.0f ? Vec2{v.x / len, v.y / len} : v;
}

} // namespace

// ---------------------------------------------------------------------------------------
// Skid trails (3.1.2).
// ---------------------------------------------------------------------------------------

TEST_CASE("as2 skid trails: one per skid_mark record, nodes fixed every 5/12 s, the head follows") {
    R t;
    const GameRules& rl = as2Rules();
    CHECK(t.w().freeSkidTrailCount() == rl.skidTrailPool);
    int jeep = t.r.create("t_jeep", {640.0f, 300.0f, 0.0f});
    REQUIRE(jeep >= 0);
    CHECK(t.r.e(jeep).skidTrailCount == 2);
    REQUIRE(t.w().liveSkidTrailCount() == 2);
    CHECK(t.w().freeSkidTrailCount() == rl.skidTrailPool - 2);
    // Driving north at 1 unit per frame at yaw 0: vehicles face their local Y (axis row 1,
    // render-pipeline.delta.md 7.7), row 0 is lateral.
    t.r.e(jeep).setF(F_ANGLES + 2, 0.0f);
    for (int f = 0; f < 60; ++f) {
        Entity& e = t.r.e(jeep);
        e.setV3(F_ORIGIN, {640.0f, 300.0f + static_cast<float>(f), 0.0f});
        t.step();
    }
    // 1 s: node 0 at the start, nodes fixed at 5/12 and 10/12 s.
    for (size_t i = 0; i < 2; ++i) {
        const WorldSkidTrail& tr = t.w().liveSkidTrail(i);
        CHECK(tr.owner == jeep);
        CHECK(tr.nodeCount == 3);
        CHECK(tr.width == 16.0f);
        CHECK(tr.nodes[0].age > tr.nodes[1].age);
        CHECK(tr.nodes[2].age < rl.skidNodeInterval);
        // The head sits at B + y·Ŷ + x·X̂ (axis rows 1 and 0 flattened).
        const Entity& e = t.r.e(jeep);
        const Vec2 fx = flatAxisOf(e, 0), fy = flatAxisOf(e, 1);
        const Vec3 b = e.v3(F_BASE_ORIGIN);
        const WorldSkidNode& head = tr.nodes[static_cast<size_t>(tr.nodeCount - 1)];
        CHECK(near(head.position.x, b.x + tr.y * fy.x + tr.x * fx.x));
        CHECK(near(head.position.y, b.y + tr.y * fy.y + tr.x * fx.y));
        CHECK(near(head.direction.x, -fx.y));
        CHECK(near(head.direction.y, fx.x));
        // The length is the distance the base origin travelled (59 units after the first frame).
        CHECK(near(tr.length, 59.0f, 1e-2f));
        CHECK(near(head.distance, tr.length));
        // The section runs across the direction of travel.
        CHECK(std::fabs(head.direction.y) > 0.99f);
    }
    // The two marks are 34 units apart across the jeep.
    const Vec2 a = t.w().liveSkidTrail(0).nodes[0].position, c = t.w().liveSkidTrail(1).nodes[0].position;
    CHECK(near(std::hypot(a.x - c.x, a.y - c.y), 34.0f, 1e-2f));
}

TEST_CASE("as2 skid trails: at most 23 nodes, ageing also while paused, fade out and release") {
    R t;
    const GameRules& rl = as2Rules();
    int jeep = t.r.create("t_jeep", {640.0f, 300.0f, 0.0f});
    for (int f = 0; f < 60 * 20; ++f) { // 20 s standing: nodes pile on one spot
        t.step();
        for (size_t i = 0; i < t.w().liveSkidTrailCount(); ++i) CHECK(t.w().liveSkidTrail(i).nodeCount <= rl.skidMaxNodes);
    }
    const WorldSkidTrail& full = t.w().liveSkidTrail(0);
    CHECK(full.nodeCount == rl.skidMaxNodes);
    CHECK(full.nodes[0].age < rl.skidLife); // the cap drops the oldest before it expires
    CHECK(full.nodes[0].age > rl.skidNodeInterval * static_cast<float>(rl.skidMaxNodes - 2));
    // The owner goes: the trails stay until their last node has faded.
    t.r.world.removeEntity(jeep);
    t.step();
    REQUIRE(t.w().liveSkidTrailCount() == 2);
    CHECK(t.w().liveSkidTrail(0).owner == -1);
    const float before = t.w().liveSkidTrail(0).nodes[0].age;
    const int countBefore = t.w().liveSkidTrail(0).nodeCount;
    // Paused: the ages keep growing (G_UpdateSkidTrails runs every frame, issue as2/210 §2).
    t.w().setPaused(true);
    t.step(30);
    const bool aged = near(t.w().liveSkidTrail(0).nodes[0].age, before + 30.0f / 60.0f, 1e-3f) ||
                      t.w().liveSkidTrail(0).nodeCount < countBefore; // or the oldest expired meanwhile
    CHECK(aged);
    t.w().setPaused(false);
    t.step(60 * 11);
    CHECK(t.w().liveSkidTrailCount() == 0);
    CHECK(t.w().freeSkidTrailCount() == rl.skidTrailPool);
}

TEST_CASE("as2 skid trails: the pool of 64, entities beyond it lay no mark; the first game has none") {
    R t;
    std::vector<int> jeeps;
    for (int i = 0; i < 40; ++i) jeeps.push_back(t.r.create("t_jeep", {100.0f + 10.0f * i, 300.0f, 0.0f}));
    CHECK(t.w().liveSkidTrailCount() == 64);
    CHECK(t.w().freeSkidTrailCount() == 0);
    CHECK(t.r.e(jeeps[31]).skidTrails[1] >= 0);
    CHECK(t.r.e(jeeps[32]).skidTrailCount == 2);
    CHECK(t.r.e(jeeps[32]).skidTrails[0] == -1); // the pool was empty
    t.step(5);
    CHECK(t.w().dumpStateJson().find("\"skid_trails\"") != std::string::npos);
    // A level start rebuilds the pool.
    t.w().startEmptyLevel(false);
    CHECK(t.w().liveSkidTrailCount() == 0);
    CHECK(t.w().freeSkidTrailCount() == 64);
    // The first game: no pool, no trail, nothing in the dump.
    R a(GameId::AirStrike3D);
    int j = a.r.create("t_jeep");
    a.step(30);
    CHECK(a.r.e(j).skidTrailCount == 0);
    CHECK(a.w().liveSkidTrailCount() == 0);
    CHECK(a.w().dumpStateJson().find("skid_trails") == std::string::npos);
}

// ---------------------------------------------------------------------------------------
// Craters for the renderer: the revision-numbered change log (issue as2/230 §7).
// ---------------------------------------------------------------------------------------

TEST_CASE("as2 terrain changes: readers far behind refresh everything, reading changes nothing") {
    R t;
    // A 1x1 8-bit grey stamp (+2 levels).
    Blob tga(18, 0);
    tga[2] = 3;
    tga[12] = 1;
    tga[14] = 1;
    tga[16] = 8;
    tga[17] = 0x08;
    tga.push_back(130);
    t.r.src->add("morphmaps/one.tga", tga);
    REQUIRE(t.w().startLevel(flatLevel(16, 16), 1));
    const u32 rev0 = t.w().terrainRevision();
    for (int i = 0; i < 70; ++i) REQUIRE(t.w().terraMorph(40.0f * (i % 16), 40.0f * (i / 16), "morphmaps/one.tga"));
    CHECK(t.w().terrainRevision() == rev0 + 70);
    std::vector<TerrainChange> ch;
    CHECK_FALSE(t.w().terrainChangesSince(rev0, ch)); // 70 > 64 kept: the whole grid
    REQUIRE(ch.size() == 1);
    CHECK(ch[0].c0 == 0);
    CHECK(ch[0].r0 == 0);
    CHECK(ch[0].c1 == 16);
    CHECK(ch[0].r1 == 16);
    REQUIRE(t.w().terrainChangesSince(rev0 + 10, ch));
    REQUIRE(ch.size() == 60);
    CHECK(ch.front().c0 == 10); // change number 10: column 10, row 0
    CHECK(ch.back().c0 == 69 % 16);
    CHECK(ch.back().r0 == 69 / 16);
    // The simulation does not depend on anyone reading: the dump is unchanged by reads.
    const std::string before = t.w().dumpStateJson();
    t.w().terrainChangesSince(rev0 + 20, ch);
    CHECK(t.w().dumpStateJson() == before);
}

// ---------------------------------------------------------------------------------------
// Water (4.2).
// ---------------------------------------------------------------------------------------

TEST_CASE("as2 water: FL_ONWATER follows waterHeightAt, _NORMAL tilts with it, _FLAT stays at the level") {
    R t(GameId::AirStrike2, "t_boat {\n flag FL_TEMPORARY\n flag FL_ONWATER\n}\n"
                            "t_tilt {\n flag FL_TEMPORARY\n flag FL_ONWATER_NORMAL\n}\n"
                            "t_ship {\n flag FL_TEMPORARY\n flag FL_ONWATER_FLAT\n}\n");
    REQUIRE(t.w().startLevel(flatLevel(16, 16, true, 50.0f), 1)); // every vertex under 50
    int boat = t.r.create("t_boat", {200.0f, 220.0f, 0.0f});
    int tilt = t.r.create("t_tilt", {300.0f, 260.0f, 0.0f});
    int ship = t.r.create("t_ship", {250.0f, 300.0f, 0.0f});
    CHECK((t.r.e(tilt).flagBits() & ~FL_TEMPORARY) == 0xC); // the executable's flag values
    CHECK((t.r.e(ship).flagBits() & ~FL_TEMPORARY) == 0x204);
    // Placed at the still level at spawn; the next think moves them.
    t.r.e(boat).setF(F_ANGLES + 2, 40.0f);
    t.r.e(tilt).setF(F_ANGLES + 2, 40.0f);
    for (int f = 0; f < 90; ++f) {
        t.step();
        const float time = t.w().time();
        const WaterSurface& s = t.w().waterSurface();
        const Terrain& ter = *t.w().terrain();
        const WaterSample b = waterHeightAt(s, ter, 200.0f, 220.0f, time);
        CHECK(t.r.e(boat).f(F_ORIGIN + 2) == b.height);
        CHECK(t.r.e(boat).f(F_AXIS + 8) == doctest::Approx(1.0f)); // FL_ONWATER keeps its angles
        const WaterSample n = waterHeightAt(s, ter, 300.0f, 260.0f, time);
        CHECK(t.r.e(tilt).f(F_ORIGIN + 2) == n.height);
        const Vec3 up = t.r.e(tilt).v3(F_AXIS + 6);
        CHECK(up.x == doctest::Approx(n.normal.x).epsilon(1e-4));
        CHECK(up.y == doctest::Approx(n.normal.y).epsilon(1e-4));
        CHECK(up.z == doctest::Approx(n.normal.z).epsilon(1e-4));
        // The yaw is kept: axis row 0 is the 40 degree heading made orthogonal to the normal.
        const Vec2 f0 = flatAxisOf(t.r.e(tilt), 0);
        CHECK(std::fabs(std::atan2(f0.y, f0.x) * 180.0f / 3.14159265f - 40.0f) < 5.0f);
        const Vec3 fw = t.r.e(tilt).v3(F_AXIS);
        CHECK(std::fabs(fw.x * n.normal.x + fw.y * n.normal.y + fw.z * n.normal.z) < 1e-4f);
        CHECK(t.r.e(ship).f(F_ORIGIN + 2) == 50.0f);
        // WaterHeight is the same surface.
        CHECK(t.w().waterHeight(200.0f, 220.0f) == b.height);
    }
    // The waves move the boat (up to 16 units about the level).
    CHECK(std::fabs(t.r.e(boat).f(F_ORIGIN + 2) - 50.0f) <= 16.0f);
    // The first game: the flat level whatever the time.
    R a(GameId::AirStrike3D, "t_boat {\n flag FL_TEMPORARY\n flag FL_ONWATER\n}\n");
    REQUIRE(a.w().startLevel(flatLevel(16, 16, true, 50.0f), 1));
    int ab = a.r.create("t_boat", {200.0f, 220.0f, 0.0f});
    a.step(30);
    CHECK(a.r.e(ab).f(F_ORIGIN + 2) == 50.0f);
}

TEST_CASE("as2 water: without water FL_ONWATER follows the terrain; the clock pauses with the game") {
    R t(GameId::AirStrike2, "t_boat {\n flag FL_TEMPORARY\n flag FL_ONWATER\n}\n");
    REQUIRE(t.w().startLevel(flatLevel(16, 16, false), 1));
    int boat = t.r.create("t_boat", {200.0f, 220.0f, 0.0f});
    t.step();
    CHECK(near(t.r.e(boat).f(F_ORIGIN + 2), t.w().terrainHeight(200.0f, 220.0f)));
    // With water: while paused, time() stands still and so does the surface.
    R w(GameId::AirStrike2, "t_boat {\n flag FL_TEMPORARY\n flag FL_ONWATER\n}\n");
    REQUIRE(w.w().startLevel(flatLevel(16, 16, true, 50.0f), 1));
    int b2 = w.r.create("t_boat", {200.0f, 220.0f, 0.0f});
    w.step(10);
    const float z = w.r.e(b2).f(F_ORIGIN + 2), time = w.w().time();
    w.w().setPaused(true);
    w.step(10);
    CHECK(w.w().time() == time);
    CHECK(w.r.e(b2).f(F_ORIGIN + 2) == z);
}

// ---------------------------------------------------------------------------------------
// Civilians (3.1.1, 5.2).
// ---------------------------------------------------------------------------------------

namespace {
// The touch script counts its touches in field 26 and keeps the last `other`'s class in 27.
Asm counter() {
    Asm a;
    a.entry(EntryPoint::Touch);
    a.addSelf(26, 1.0f);
    a.leaGlobal(0, "other", F_CLASS);
    a.load(1, 0);
    a.setSelfSlot(27, 1);
    a.end();
    return a;
}
void box(Entity& e, float h) {
    e.boundsMin = {-h, -h, -h};
    e.boundsMax = {h, h, h};
    e.radius = h * 1.8f;
}
} // namespace

TEST_CASE("as2 civilians: touched only through bit 0x4, after a player for mode 6") {
    Rig r;
    r.config.rules = &as2Rules();
    r.script("scripts\\count.scr", counter());
    r.obj(kAs2Helis);
    r.obj("t_touch {\n flag FL_TEMPORARY\n script \"scripts\\count.scr\"\n}\n"
          "t_en {\n enemy\n flag FL_TEMPORARY\n health 100\n}\n"
          "t_civ {\n civilian\n flag FL_TEMPORARY\n health 100\n score 200\n}\n");
    r.start(true);
    int pl = r.world.playerEntityIndex(0);
    REQUIRE(pl >= 0);
    int to = r.create("t_touch", {640, 302, 0});
    int en = r.create("t_en", {640, 302, 0});
    int civ = r.create("t_civ", {640, 302, 0});
    for (int i : {pl, to, en, civ}) box(r.e(i), 20);
    r.e(pl).setV3(F_ORIGIN, {640, 302, 0});
    r.world.setupTransform(pl);
    for (int i : {pl, to, en, civ}) r.world.computeScreenBounds(i);
    struct Case {
        int mode;
        float touches;
    } cases[] = {{1, 1}, {2, 1}, {4, 1}, {5, 2}, {6, 1}, {0xF, 1}};
    for (const Case& c : cases) {
        INFO("mode " << c.mode);
        Entity& t = r.e(to);
        t.touchMode = c.mode;
        r.world.computeScreenBounds(to);
        REQUIRE((t.rt & RT_COLLIDABLE) != 0);
        t.setF(26, 0.0f);
        t.setF(27, -1.0f);
        r.world.touchEntity(to);
        CHECK(t.f(26) == c.touches);
        if (c.mode == 4) CHECK(t.f(27) == kClassCivilian);
        if (c.mode == 2 || c.mode == 6 || c.mode == 0xF) CHECK(t.f(27) == kClassPlayer); // the player first, then stop
    }
    // Mode 6 without a player on it: the civilian.
    r.e(pl).setV3(F_ORIGIN, {500, 302, 0}); // on screen, clear of the others
    r.world.setupTransform(pl);
    r.world.computeScreenBounds(pl);
    Entity& t = r.e(to);
    t.touchMode = 6;
    t.setF(26, 0.0f);
    r.world.touchEntity(to);
    CHECK(t.f(26) == 1.0f);
    CHECK(t.f(27) == kClassCivilian);
}

TEST_CASE("as2 civilians: no enemy total, no kill credit, no lock-on; score only from the definition") {
    Rig r;
    r.config.rules = &as2Rules();
    r.obj(kAs2Helis);
    r.obj("t_civ {\n civilian\n flag FL_TEMPORARY\n health 100\n score 200\n}\n"
          "t_tree {\n civilian\n flag FL_TEMPORARY\n health 100\n}\n");
    r.start(true);
    const int total = r.world.enemiesInLevel();
    int civ = r.create("t_civ");
    int tree = r.create("t_tree");
    CHECK(r.world.enemiesInLevel() == total);
    r.world.damageEntity(civ, 1000.0f, 0);
    r.world.damageEntity(tree, 1000.0f, 0);
    CHECK(r.e(civ).f(F_DEAD) == 1.0f);
    CHECK(r.world.player(0).kills == 0);
    CHECK(r.world.player(0).scores == 200.0f);
}

TEST_CASE("as2 definitions: several touch lines are ORed; the first game keeps the last") {
    const std::string objs = "t_both {\n flag FL_TEMPORARY\n touch TOUCH_ENEMIES\n touch TOUCH_PLAYER\n}\n"
                             "t_allciv {\n flag FL_TEMPORARY\n touch TOUCH_ALL\n touch TOUCH_CIVILIAN\n}\n"
                             "t_civen {\n flag FL_TEMPORARY\n touch TOUCH_CIVILIAN\n touch TOUCH_ENEMIES\n}\n";
    R t(GameId::AirStrike2, objs);
    CHECK(t.r.e(t.r.create("t_both")).touchMode == 3);
    CHECK(t.r.e(t.r.create("t_allciv")).touchMode == 0xF);
    CHECK(t.r.e(t.r.create("t_civen")).touchMode == 5);
    R a(GameId::AirStrike3D, objs);
    CHECK(a.r.e(a.r.create("t_both")).touchMode == 2);
    CHECK(a.r.e(a.r.create("t_allciv")).touchMode == 3);
}

// ---------------------------------------------------------------------------------------
// Level start (2, 3.4, 10.2) and the statistics.
// ---------------------------------------------------------------------------------------

TEST_CASE("as2 level start: rows 0..20 spawn during the load and are left out of the statistics") {
    // t_stay does not leave the play-field (FL_TEMPORARY), so it can be shot later.
    const std::string objs = "t_en {\n enemy\n health 100\n score 100\n}\n"
                             "t_stay {\n enemy\n flag FL_TEMPORARY\n health 100\n score 100\n}\n";
    auto level = [] {
        std::unique_ptr<LoadedLevel> l = flatLevel(32, 64);
        place(*l, "t_en", 5);
        place(*l, "t_stay", 12);
        place(*l, "t_en", 30);
        place(*l, "t_en", 32);
        return l;
    };
    R t(GameId::AirStrike2, objs);
    REQUIRE(t.w().startLevel(level(), 2));
    // Loaded and thought once; not counted.
    int early = -1, stay = -1;
    for (int i : t.w().listEntities()) {
        if (t.r.e(i).name == "t_en") early = i;
        if (t.r.e(i).name == "t_stay") stay = i;
    }
    REQUIRE(early >= 0);
    REQUIRE(stay >= 0);
    CHECK((t.r.e(early).rt & RT_THOUGHT) != 0);
    CHECK(t.w().enemiesInLevel() == 0);
    CHECK(t.w().maxLevelScore() == 0.0f);
    // A kill before any counted enemy: no credit (the cap is the total, 0).
    t.w().damageEntity(early, 1000.0f, 0);
    CHECK(t.r.e(early).f(F_DEAD) == 1.0f);
    CHECK(t.w().player(0).kills == 0);
    CHECK(t.w().player(0).scores == 100.0f); // the score is awarded all the same
    // Rows 30 and 32 come with the scroll (front edge + 1000) and count.
    t.step(60 * 7);
    CHECK(t.w().enemiesInLevel() == 2);
    CHECK(t.w().maxLevelScore() == 200.0f);
    for (int i : t.w().listEntities()) {
        if (t.r.e(i).name == "t_en" && t.r.e(i).f(F_DEAD) == 0.0f) t.w().damageEntity(i, 1000.0f, 0);
    }
    CHECK(t.w().player(0).kills == 2);
    REQUIRE(t.w().validIndex(stay));
    t.w().damageEntity(stay, 1000.0f, 0);
    CHECK(t.w().player(0).kills == 2); // capped at the enemy total

    // The first game spawns nothing during the load and counts everything.
    R a(GameId::AirStrike3D, objs);
    REQUIRE(a.w().startLevel(level(), 2));
    int n = 0;
    for (int i : a.w().listEntities()) n += a.r.e(i).name == "t_en" ? 1 : 0;
    CHECK(n == 0);
    a.step(60 * 7);
    CHECK(a.w().enemiesInLevel() == 4);
}

TEST_CASE("as2 level start: an intermission level spawns every placement at the load") {
    const std::string objs = "t_prop {\n flag FL_TEMPORARY\n}\n";
    auto level = [] {
        std::unique_ptr<LoadedLevel> l = flatLevel(32, 64);
        l->style.hasIntermission = true;
        const float cam[6] = {150, 256, 200, -60, 0, 30};
        for (int k = 0; k < 6; ++k) l->style.intermission[k] = cam[k];
        for (u16 row : {2, 25, 40, 60}) place(*l, "t_prop", row);
        return l;
    };
    R t(GameId::AirStrike2, objs);
    REQUIRE(t.w().startLevel(level(), 0));
    CHECK(t.w().intermission());
    CHECK(t.w().listCount() == 4); // no players on an intermission level
    R a(GameId::AirStrike3D, objs);
    REQUIRE(a.w().startLevel(level(), 0));
    a.step();
    CHECK(a.w().listCount() == 2); // the window: rows up to (32 + 1000) / 40
}

// ---------------------------------------------------------------------------------------
// Loadout, Next and the checkpoint (8.2, 10.3).
// ---------------------------------------------------------------------------------------

TEST_CASE("as2 checkpoint: EndLevel's values reach the mission report") {
    R t;
    REQUIRE(t.w().startLevel(flatLevel(16, 16), 3));
    PlayerRecord& p = t.w().player(0);
    p.lives = 4.0f;
    p.scores = 1234.0f;
    p.banked = 1000;
    p.rankAccumulator = 2.0f;
    ui::MissionReport before;
    as3d_game::fillCheckpoint(t.w(), before);
    CHECK_FALSE(before.hasCheckpoint); // not before EndLevel
    t.w().endLevel();
    ui::MissionReport r;
    as3d_game::fillCheckpoint(t.w(), r);
    REQUIRE(r.hasCheckpoint);
    CHECK(r.checkpointMission == 3); // 0-based: mission 4 resumes it
    CHECK(r.checkpointLives[0] == 4);
    CHECK(r.checkpointScore[0] == 2234);
    CHECK(r.checkpointRank[0] == doctest::Approx(2.0f)); // no scored object, no star: terms 0
    // The first game has no checkpoint.
    R a(GameId::AirStrike3D);
    REQUIRE(a.w().startLevel(flatLevel(16, 16), 3));
    a.w().endLevel();
    ui::MissionReport ra;
    as3d_game::fillCheckpoint(a.w(), ra);
    CHECK_FALSE(ra.hasCheckpoint);
}

namespace {
const GameData& as2Data() {
    static const GameData d = locateGameData(testdata::root(), gameProfile(GameId::AirStrike2));
    return d;
}
#define REQUIRE_AS2_DATA()                                                                                  \
    if (!as2Data().hasExtracted) {                                                                          \
        std::fprintf(stderr, "SKIPPED (no AirStrike 2 data in %s): %s\n", as2Data().extractedDir.c_str(), __FILE__); \
        return;                                                                                             \
    }

bool sameUpgrades(const PlayerRecord& p, const int* row, int slots) {
    for (int k = 0; k < slots; ++k)
        if (p.upgrades[k] != row[k]) return false;
    return true;
}
} // namespace

TEST_CASE("as2 session: the loadout on a new game and on Restart, the upgrades carried on Next") {
    REQUIRE_AS2_DATA();
    const GameRules& rl = as2Rules();
    as3d_game::GameOptions o;
    o.dataRoot = testdata::root();
    o.game = &gameProfile(GameId::AirStrike2);
    o.startLevel = false;
    o.levelFlow = true;
    o.flowDelayFrames = 1;
    as3d_game::GameSession s;
    std::string err;
    REQUIRE_MESSAGE(s.init(o, &err), err);
    // New game on mission 5: its row (impulse 5, plasma 7), weapon the highest owned slot.
    as3d_game::LevelSetup setup;
    setup.mission = 5;
    REQUIRE_MESSAGE(s.startMission(setup, &err), err);
    CHECK(sameUpgrades(s.world().player(0), rl.missionLoadout[4], rl.weaponSlots));
    CHECK(s.world().player(0).weapon == 2.0f);
    // Next with carried upgrades: those, not mission 6's row.
    as3d_game::LevelSetup next = setup;
    next.mission = 6;
    next.carryUpgrades = true;
    next.upgrades[0][0] = 4;
    next.upgrades[0][8] = 2;
    next.weapon[0] = 8;
    REQUIRE_MESSAGE(s.startMission(next, &err), err);
    CHECK(s.world().player(0).upgrades[0] == 4);
    CHECK(s.world().player(0).upgrades[8] == 2);
    CHECK(s.world().player(0).upgrades[3] == 0);
    CHECK(s.world().player(0).weapon == 8.0f);
    // Restart of mission 6: its row again.
    as3d_game::LevelSetup restart = setup;
    restart.mission = 6;
    REQUIRE_MESSAGE(s.startMission(restart, &err), err);
    CHECK(sameUpgrades(s.world().player(0), rl.missionLoadout[5], rl.weaponSlots));

    // The direct flow: a completed mission goes on to the next with the upgrades collected...
    s.world().player(0).upgrades[8] = 3;
    s.world().endLevel();
    for (int f = 0; f < 3 && s.mission() == 6; ++f) s.step(FrameInput());
    REQUIRE(s.mission() == 7);
    CHECK(s.world().player(0).upgrades[8] == 3);
    CHECK(s.world().player(0).upgrades[3] == 3); // mission 6's, kept (row 7 would give 3 too)
    CHECK(s.world().player(0).upgrades[1] == 5); // row 7 has no impulse gun: carried
    // ...and a game over restarts it with its loadout.
    s.world().setGameOver();
    for (int f = 0; f < 3 && s.world().gameOver(); ++f) s.step(FrameInput());
    CHECK(s.mission() == 7);
    CHECK(sameUpgrades(s.world().player(0), rl.missionLoadout[6], rl.weaponSlots));
}

// ---------------------------------------------------------------------------------------
// Mouse control (7.2 step 2).
// ---------------------------------------------------------------------------------------

TEST_CASE("as2 mouse control: relative motion scaled to 2.0 when the keys are idle, player 1 only") {
    Rig r;
    r.config.rules = &as2Rules();
    r.obj(kAs2Helis);
    r.start(true);
    PlayerInput in;
    in.mouseSteer = true;
    in.mouse[0] = 3.0f;
    in.mouse[1] = 4.0f;
    r.world.step(in);
    CHECK(r.world.player(0).accel[0] == doctest::Approx(1.2f));
    CHECK(r.world.player(0).accel[1] == doctest::Approx(1.6f));
    // A key wins.
    in.action[0] = ACT_LEFT;
    r.world.step(in);
    CHECK(r.world.player(0).accel[0] == -1.0f);
    CHECK(r.world.player(0).accel[1] == 0.0f);
    // No motion, or the setting off: nothing.
    in.action[0] = 0;
    in.mouse[0] = in.mouse[1] = 0.0f;
    r.world.step(in);
    CHECK(r.world.player(0).accel[0] == 0.0f);
    in.mouse[0] = 5.0f;
    in.mouseSteer = false;
    r.world.step(in);
    CHECK(r.world.player(0).accel[0] == 0.0f);
    // Through FrameInput.
    FrameInput fi;
    fi.mouseSteer = true;
    fi.mouseDx = -7.0f;
    r.world.step(fi.toPlayerInput());
    CHECK(r.world.player(0).accel[0] == doctest::Approx(-2.0f));
    // The first game has no acceleration vector at all.
    Rig a;
    a.start(true);
    a.world.step(fi.toPlayerInput());
    CHECK(a.world.player(0).accel[0] == 0.0f);
}
