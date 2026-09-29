// Particles as part of the simulation (WP-49 item 1): the World owns the particle-system
// instances of its emitter holders, advances them at step 7 of the frame and applies the
// particle damage of render-pipeline.md 6.2 / engine-behaviour.md 6.2 (parameter roles:
// docs/spec/issues/110-particle-damage.md).
#include "doctest.h"

#include "as3d/defs.h"
#include "as3d/world.h"
#include "as3d/world_particles.h"
#include "test_data.h"
#include "world_test_util.h"

using namespace worldtest;

namespace {

void giveBox(Entity& e, float h) {
    e.boundsMin = {-h, -h, -h};
    e.boundsMax = {h, h, h};
    e.radius = h * 1.8f;
}

// A continuous system, 60 particles per second living 1 s, that stays where it is emitted.
std::string flameSystem(const char* name, const char* damage) {
    return std::string(name) +
           " {\n blend_mode BLEND_ADD\n texture \"gfx\\\\t.tga\" 1 1\n emit_rate 60\n life_time 1\n"
           " init_offset 0 0 0 0 0 0\n init_size 1 0\n init_color 1 1 1 1\n fade_mode FADE_LINEAR\n" +
           (damage ? std::string(" damage ") + damage + "\n" : std::string()) + "}\n";
}

void flameRig(Rig& r, const char* damage) {
    r.src->add("particles\\test.ps", flameSystem("PS_T", damage));
    r.obj("t_flame {\n flag FL_TEMPORARY\n attach PS_T \"origin\"\n}\n"
          "t_target {\n enemy\n flag FL_TEMPORARY\n health 1000\n}\n");
}

} // namespace

TEST_CASE("particle damage: the parameters of the damage statement") {
    ParticleSystemDef d;
    CHECK(ParticleDamage::fromDef(d).touch == 0);
    d.hasDamage = true;
    d.damageTouch = TouchMode::Enemies;
    d.damageAmount = 15.0f;
    d.damageParam2 = 10.0f;
    d.damageParam3 = 2.0f;
    ParticleDamage pd = ParticleDamage::fromDef(d);
    CHECK(pd.touch == 1);
    CHECK(pd.every == 15);
    CHECK(pd.amount(0.0f, 0.5f) == doctest::Approx(10.0f));
    CHECK(pd.amount(0.25f, 0.5f) == doctest::Approx(6.0f));
    CHECK(pd.amount(0.5f, 0.5f) == doctest::Approx(2.0f));
    CHECK(pd.amount(0.9f, 0.5f) == doctest::Approx(2.0f)); // clamped
    d.damageTouch = TouchMode::Player;
    d.damageAmount = 0.0f; // every particle
    CHECK(ParticleDamage::fromDef(d).touch == 2);
    CHECK(ParticleDamage::fromDef(d).every == 1);
    d.damageTouch = TouchMode::All; // not recognised by the .ps parser
    CHECK(ParticleDamage::fromDef(d).touch == 0);
}

TEST_CASE("particle damage: TOUCH_ENEMIES hurts the enemy the particles cover") {
    Rig r;
    flameRig(r, "TOUCH_ENEMIES 1 10 0");
    r.start(false);
    int t = r.create("t_target", {640, 300, 0});
    giveBox(r.e(t), 20);
    int f = r.create("t_flame", {640, 300, 0});
    REQUIRE(f >= 0);
    r.step(30);
    CHECK(r.world.particles().emitterCount() == 1);
    CHECK(r.world.particles().liveParticles() > 0);
    const float dealt = r.world.particles().damageToEnemies();
    CHECK(dealt > 0.0f);
    CHECK(r.e(t).f(F_HEALTH) == doctest::Approx(1000.0f - dealt));
    CHECK(r.world.particles().damageToPlayers() == 0.0f);
    CHECK(r.world.dumpStateJson().find("\"particles\": {\"emitters\": 1") != std::string::npos);

    // Frozen enemies are not candidates.
    r.e(t).rt |= RT_HEALTH_FROZEN;
    float before = r.e(t).f(F_HEALTH);
    r.step(10);
    CHECK(r.e(t).f(F_HEALTH) == before);
}

TEST_CASE("particle damage: every n-th particle only, and nothing far away") {
    // Same system, damage on every 60th particle: at most one of the pool of 64 counts
    // (indices 0 and 60), so far less damage than with every particle.
    float dealt[2] = {0, 0};
    const char* specs[2] = {"TOUCH_ENEMIES 1 10 10", "TOUCH_ENEMIES 60 10 10"};
    for (int k = 0; k < 2; ++k) {
        Rig r;
        flameRig(r, specs[k]);
        r.start(false);
        int t = r.create("t_target", {640, 300, 0});
        giveBox(r.e(t), 20);
        r.e(t).setF(F_HEALTH, 1.0e7f); // never dies here
        r.create("t_flame", {640, 300, 0});
        r.step(60);
        dealt[k] = r.world.particles().damageToEnemies();
    }
    CHECK(dealt[0] > 0.0f);
    CHECK(dealt[1] > 0.0f);
    CHECK(dealt[1] * 10.0f < dealt[0]);

    Rig far;
    flameRig(far, "TOUCH_ENEMIES 1 10 0");
    far.start(false);
    int t = far.create("t_target", {640, 300, 0});
    giveBox(far.e(t), 5);
    far.create("t_flame", {700, 300, 0});
    far.step(30);
    CHECK(far.e(t).f(F_HEALTH) == 1000.0f);
}

TEST_CASE("particle damage: TOUCH_PLAYER hurts the player, not enemies; god mode protects") {
    for (int god = 0; god < 2; ++god) {
        Rig r;
        flameRig(r, "TOUCH_PLAYER 1 1 1");
        r.config.godMode = god != 0;
        r.start(true);
        int pe = r.world.playerEntityIndex(0);
        REQUIRE(pe >= 0);
        giveBox(r.e(pe), 20);
        r.e(pe).setV3(F_ORIGIN, {640, 300, 0});
        int t = r.create("t_target", {640, 300, 0});
        giveBox(r.e(t), 20);
        float h0 = r.e(pe).f(F_HEALTH);
        r.create("t_flame", {640, 300, 0});
        r.step(20);
        CHECK(r.e(t).f(F_HEALTH) == 1000.0f);
        if (god) CHECK(r.e(pe).f(F_HEALTH) == h0);
        else CHECK(r.e(pe).f(F_HEALTH) < h0);
    }
}

TEST_CASE("particles freeze with the pause and restart with the level") {
    Rig r;
    flameRig(r, nullptr);
    r.start(false);
    r.create("t_flame", {640, 300, 0});
    r.step(10);
    const u32 h = r.world.particles().stateHash();
    r.world.setPaused(true);
    r.step(5);
    CHECK(r.world.particles().stateHash() == h);
    r.world.setPaused(false);
    r.step(1);
    CHECK(r.world.particles().stateHash() != h);
    r.world.startEmptyLevel(false);
    CHECK(r.world.particles().emitterCount() == 0);
}

TEST_CASE("particle damage: the shipped flamethrowers") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    AS3D_REQUIRE_FIRST_GAME("mission 1 emitters");
    Vfs vfs;
    vfs.mount(makeDirSource(testdata::extractedDir()));
    DefDatabase db;
    REQUIRE(db.load(vfs));
    int damaging = 0;
    for (const char* name : {"PS_FLAMEFROWER_HOR", "PS_FLAMEFROWER_HOR2", "PS_FLAMEFROWER_HOR3", "PS_FLAMEFROWER_DIAG"}) {
        const ParticleSystemDef* d = db.findParticleSystem(name);
        REQUIRE_MESSAGE(d, name);
        ParticleDamage pd = ParticleDamage::fromDef(*d);
        INFO(name);
        CHECK(pd.touch == (std::string(name) == "PS_FLAMEFROWER_DIAG" ? 2 : 1));
        CHECK(pd.every >= 15);
        CHECK(pd.start >= 10.0f);
        CHECK(pd.end < pd.start);
        ++damaging;
    }
    CHECK(damaging == 4);
}
