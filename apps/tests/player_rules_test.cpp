// AirStrike 2's native player rules (docs/spec/as2/engine-behaviour.delta.md 7 and 8, the
// builtins delta F): the acceleration vector, the mission loadout and the 9 weapon slots,
// power-up cycling with the skip mask, the health clamp, respawn without the lives test,
// dead shooters, field 23 from the helicopter's `speed`. Each against the first game's rule.
#include "doctest.h"

#include <cmath>
#include <functional>
#include <string>

#include "as3d/player_select.h"
#include "world_test_util.h"

using namespace worldtest;

namespace {

const GameRules& as2Rules() { return gameProfile(GameId::AirStrike2).rules; }

const char* kAs2Helis =
    "player_1 {\n player\n flag FL_TEMPORARY\n health 500\n speed 1.0\n}\n"
    "player_2 {\n player\n flag FL_TEMPORARY\n health 400\n speed 1.25\n}\n";

struct PR {
    Rig r;
    explicit PR(GameId game = GameId::AirStrike2, int players = 1, const std::function<void(Rig&)>& before = nullptr) {
        r.config.rules = &gameProfile(game).rules;
        r.config.players = players;
        r.obj(kAs2Helis);
        if (before) before(r);
        r.start(true);
    }
    PlayerRecord& p(int i = 0) { return r.world.player(i); }
    void step(u32 bits0, u32 bits1 = 0) {
        PlayerInput in;
        in.action[0] = bits0;
        in.action[1] = bits1;
        r.world.step(in);
    }
};

std::unique_ptr<LoadedLevel> flatLevel() {
    std::unique_ptr<LoadedLevel> l(new LoadedLevel());
    l->data.width = 8;
    l->data.height = 8;
    l->data.cells.resize(64);
    l->style.hmin = -100.0f;
    l->style.hmax = 100.0f;
    return l;
}

} // namespace

TEST_CASE("player rules: acceleration vector for the 8 directions and none") {
    PR t;
    const float d = 1.0f / std::sqrt(2.0f);
    struct Case {
        u32 bits;
        float x, y;
    } cases[] = {
        {0, 0, 0},
        {ACT_RIGHT, 1, 0},
        {ACT_LEFT, -1, 0},
        {ACT_FORWARD, 0, 1},
        {ACT_BACKWARD, 0, -1},
        {ACT_FORWARD | ACT_RIGHT, d, d},
        {ACT_FORWARD | ACT_LEFT, -d, d},
        {ACT_BACKWARD | ACT_RIGHT, d, -d},
        {ACT_BACKWARD | ACT_LEFT, -d, -d},
        {ACT_LEFT | ACT_RIGHT, 0, 0},                // opposite keys cancel
        {ACT_LEFT | ACT_RIGHT | ACT_FORWARD, 0, 1},  // an axis vector, not scaled
        {ACT_FIRE | ACT_FORWARD, 0, 1},
    };
    for (const Case& c : cases) {
        t.step(c.bits);
        INFO("bits " << c.bits);
        CHECK(t.p().accel[0] == doctest::Approx(c.x));
        CHECK(t.p().accel[1] == doctest::Approx(c.y));
        CHECK(t.p().accel[2] == 0.0f);
        CHECK(std::sqrt(t.p().accel[0] * t.p().accel[0] + t.p().accel[1] * t.p().accel[1]) <= 1.0f + 1e-6f);
    }
    // Actions disabled: (0, 0).
    t.p().actionsDisabled = true;
    t.step(ACT_FORWARD);
    CHECK(t.p().accel[1] == 0.0f);
    // Paused: the vector is not rebuilt.
    t.p().actionsDisabled = false;
    t.step(ACT_RIGHT);
    CHECK(t.p().accel[0] == 1.0f);
    t.r.world.setPaused(true);
    t.step(ACT_LEFT);
    CHECK(t.p().accel[0] == 1.0f);
}

TEST_CASE("player rules: the second player gets its own vector; the first game builds none") {
    PR t(GameId::AirStrike2, 2);
    t.step(ACT_FORWARD, ACT_LEFT);
    CHECK(t.p(0).accel[1] == 1.0f);
    CHECK(t.p(1).accel[0] == -1.0f);
    PR u(GameId::AirStrike3D);
    u.step(ACT_FORWARD);
    CHECK(u.p().accel[1] == 0.0f);
}

TEST_CASE("player rules: mission loadout at level start, carried over on Next") {
    PR t;
    REQUIRE(t.r.world.startLevel(flatLevel(), 1));
    CHECK(t.p().upgrades[0] == 1);
    CHECK(t.p().weapon == 0.0f);
    REQUIRE(t.r.world.startLevel(flatLevel(), 4));
    CHECK(t.p().upgrades[0] == 4);
    CHECK(t.p().upgrades[1] == 5);
    CHECK(t.p().upgrades[2] == 3);
    CHECK(t.p().weapon == 2.0f); // the highest owned slot
    REQUIRE(t.r.world.startLevel(flatLevel(), 15));
    CHECK(t.p().upgrades[0] == 0);
    CHECK(t.p().upgrades[8] == 3);
    CHECK(t.p().weapon == 8.0f);
    // A mission beyond the table uses its last row; both players get it.
    REQUIRE(t.r.world.startLevel(flatLevel(), 30));
    CHECK(t.p().upgrades[8] == 3);
    CHECK(t.p(1).upgrades[8] == 3);
    // "Next": the upgrades collected so far are kept, dying does not cost levels.
    t.p().upgrades[0] = 4;
    t.r.world.carryUpgradesToNextLevel();
    REQUIRE(t.r.world.startLevel(flatLevel(), 16));
    CHECK(t.p().upgrades[0] == 4);
    // The next level start without carrying applies the table again.
    REQUIRE(t.r.world.startLevel(flatLevel(), 16));
    CHECK(t.p().upgrades[0] == 0);
    // applyMissionLoadout directly.
    t.r.world.applyMissionLoadout(2);
    CHECK(t.p().upgrades[0] == 4);
    CHECK(t.p().upgrades[1] == 3);
    CHECK(t.p().weapon == 1.0f);
}

TEST_CASE("player rules: the first game clears the upgrades and gives the machine gun") {
    PR t(GameId::AirStrike3D);
    t.p().upgrades[5] = 3;
    t.p().weapon = 5.0f;
    t.r.world.carryUpgradesToNextLevel(); // no table: nothing to carry, the first game's rule holds
    REQUIRE(t.r.world.startLevel(flatLevel(), 4));
    CHECK(t.p().upgrades[0] == 1);
    CHECK(t.p().upgrades[5] == 0);
    CHECK(t.p().weapon == 0.0f);
    t.r.world.applyMissionLoadout(4); // no table: no change
    CHECK(t.p().upgrades[0] == 1);
}

TEST_CASE("player rules: next weapon cycles over the 9 slots") {
    PR t;
    for (int& u : t.p().upgrades) u = 0;
    t.p().upgrades[3] = 1;
    t.p().upgrades[8] = 2;
    t.p().upgrades[12] = 5; // beyond the sequels' slots: never selected
    t.p().weapon = 3.0f;
    t.step(ACT_NEXT_WEAPON);
    CHECK(t.p().weapon == 8.0f);
    t.step(0);
    t.step(ACT_NEXT_WEAPON);
    CHECK(t.p().weapon == 3.0f);
    CHECK(nextWeaponIndex(as2Rules(), t.p()) == 8);
    // The first game's 20.
    CHECK(nextWeaponIndex(gameProfile(GameId::AirStrike3D).rules, t.p().upgrades, 8) == 12);
    CHECK(nextWeaponIndex(t.p().upgrades, 8) == 12);
}

TEST_CASE("player rules: next power-up skips 6, 7 and 9 in the sequels") {
    const GameRules& as3d = gameProfile(GameId::AirStrike3D).rules;
    int pw[16] = {};
    pw[5] = 1;
    pw[6] = 2;
    pw[7] = 2;
    pw[9] = 2;
    pw[11] = 1;
    CHECK(nextPowerupSlot(as2Rules(), pw, 5) == 11);
    CHECK(nextPowerupSlot(as3d, pw, 5) == 6);
    CHECK(nextPowerupSlot(pw, 5) == 6);
    CHECK(nextPowerupSlot(as2Rules(), pw, 11) == 5);
    CHECK(nextPowerupSlot(as2Rules(), pw, -1) == 5);
    // Only skipped kinds owned: nothing found, the selection stays.
    int only[16] = {};
    only[6] = 3;
    bool found = true;
    CHECK(nextPowerupSlot(as2Rules(), only, 2, &found) == 2);
    CHECK_FALSE(found);
    // Wrapping past 15: slot 6 reached as value 22 is not skipped (as2@0x413680).
    CHECK(nextPowerupSlot(as2Rules(), only, 12, &found) == 6);
    CHECK(found);
    // Through the player frame.
    PR t;
    for (int k = 0; k < 16; ++k) t.p().powerups[k] = pw[k];
    t.p().currentPowerup = 5;
    t.step(ACT_NEXT_POWERUP);
    CHECK(t.p().currentPowerup == 11);
}

TEST_CASE("player rules: health clamped to the helicopter's maximum every frame") {
    PR t;
    int pe = t.r.world.playerEntityIndex(0);
    REQUIRE(pe >= 0);
    CHECK(t.r.e(pe).name == "player_2");
    CHECK(t.r.e(pe).f(F_HEALTH) == 400.0f); // from the definition, never scaled
    t.r.e(pe).setF(F_HEALTH, 1000.0f);        // i_armor100.scr
    t.step(0);
    CHECK(t.r.e(pe).f(F_HEALTH) == 400.0f);
    t.r.e(pe).setF(F_HEALTH, 250.0f);
    t.step(0);
    CHECK(t.r.e(pe).f(F_HEALTH) == 250.0f);
    // Not while the HUD is hidden (after EndLevel).
    t.r.world.endLevel();
    t.r.e(pe).setF(F_HEALTH, 900.0f);
    t.step(0);
    CHECK(t.r.e(pe).f(F_HEALTH) == 900.0f);
    // The first game never clamps.
    PR u(GameId::AirStrike3D);
    int ue = u.r.world.playerEntityIndex(0);
    REQUIRE(ue >= 0);
    u.r.e(ue).setF(F_HEALTH, 5000.0f);
    u.step(0);
    CHECK(u.r.e(ue).f(F_HEALTH) == 5000.0f);
}

TEST_CASE("player rules: respawn without the lives test") {
    for (GameId g : {GameId::AirStrike3D, GameId::AirStrike2}) {
        PR t(g);
        int old = t.r.world.playerEntityIndex(0);
        REQUIRE(old >= 0);
        t.p().lives = -1.0f;
        t.r.world.spawnPlayer(0);
        INFO(std::string(gameProfile(g).key));
        if (g == GameId::AirStrike3D) {
            CHECK(t.r.world.playerEntityIndex(0) == -1);
            CHECK(t.r.e(old).f(F_DEAD) == 1.0f);
        } else {
            int now = t.r.world.playerEntityIndex(0);
            CHECK(now >= 0);
            CHECK(now != old);
            CHECK((t.r.e(old).rt & RT_REMOVED) != 0);
        }
    }
}

TEST_CASE("player rules: field 23 holds the helicopter's speed in the sequels") {
    PR t;
    int pe = t.r.world.playerEntityIndex(0);
    REQUIRE(pe >= 0);
    CHECK(t.r.e(pe).f(F_WP_SPEED) == 1.25f); // player_2's `speed`
    PR u(GameId::AirStrike3D);
    int ue = u.r.world.playerEntityIndex(0);
    REQUIRE(ue >= 0);
    CHECK(u.r.e(ue).f(F_WP_SPEED) == 0.0f);
}

TEST_CASE("player rules: a dead shooter cannot fire in the sequels") {
    for (GameId g : {GameId::AirStrike3D, GameId::AirStrike2}) {
        PR t(g, 1, [](Rig& r) {
            r.weapons = "w_gun {\n missile \"t_shot\"\n speed 300\n}\n";
            Asm s;
            s.entry(EntryPoint::Callback);
            s.movStr(0, "w_gun");
            s.movStr(1, "origin");
            s.leaGlobal(2, "self", 17);
            s.call("Shoot", 4);
            s.end();
            r.script("scripts\\gun.scr", s);
            r.obj("t_shot {\n flag FL_TEMPORARY\n}\n"
                  "t_gun {\n flag FL_TEMPORARY\n script \"scripts\\gun.scr\"\n}\n");
        });
        int gun = t.r.create("t_gun");
        REQUIRE(gun >= 0);
        t.r.e(gun).rt |= RT_COLLIDABLE;
        t.r.e(gun).setV3(17, {0, 1, 0});
        t.r.e(gun).setF(F_DEAD, 1.0f);
        int before = t.r.world.listCount();
        t.r.world.runCallback(gun, 0, 0, 0);
        INFO(std::string(gameProfile(g).key));
        CHECK(t.r.world.listCount() == before + (g == GameId::AirStrike3D ? 1 : 0));
    }
}
