// World tests (WP-42a): entity lifecycle and handle reuse, deferred removal, update
// order, event dispatch, activation states, determinism, the level-1 corpus run.
#include "doctest.h"

#include "as3d/script_host.h"
#include "test_data.h"
#include "world_test_util.h"

using namespace worldtest;

namespace {

// A script whose main counts frames into field 21 and whose init stores 7 in field 20.
Asm counterScript() {
    Asm a;
    a.entry(EntryPoint::Init);
    a.setSelf(20, 7.0f);
    a.end();
    a.entry(EntryPoint::Main);
    a.addSelf(21, 1.0f);
    a.end();
    return a;
}

} // namespace

TEST_CASE("world: entity lifecycle, deferred free and handle reuse") {
    Rig r;
    r.obj("t_plain {\n flag FL_TEMPORARY\n}\n");
    r.start();
    int a = r.create("t_plain");
    REQUIRE(a >= 0);
    EntityHandle ha = r.world.handleOf(a);
    CHECK(r.world.get(ha) == &r.e(a));
    CHECK(r.world.listCount() == 1);
    // Script-visible reference: field 0 holds the base, reference = base + 0x7B.
    CHECK(r.e(a).fields[F_SELF] + kEntityRefOffset == r.world.refOf(a));
    CHECK(r.world.liveIndexFromRef(r.world.refOf(a)) == a);
    CHECK(r.world.liveIndexFromRef(r.world.refOf(a) + 4) == -1);

    r.world.removeEntity(a);
    // Deferred: still in use (and still resolvable) until the next entity pass.
    CHECK(r.e(a).inUse);
    CHECK((r.e(a).rt & RT_REMOVED) != 0);
    CHECK(r.world.get(ha) != nullptr);
    r.step();
    CHECK_FALSE(r.e(a).inUse);
    CHECK(r.world.get(ha) == nullptr);
    CHECK(r.world.listCount() == 0);

    // LIFO free list: the next entity reuses the slot with a new generation.
    int b = r.create("t_plain");
    CHECK(b == a);
    EntityHandle hb = r.world.handleOf(b);
    CHECK(hb.generation == ha.generation + 1);
    CHECK(hb != ha);
    CHECK(r.world.get(ha) == nullptr);
    CHECK(r.world.get(hb) == &r.e(b));
}

TEST_CASE("world: removed entities are skipped for the rest of the frame") {
    Rig r;
    Asm s = counterScript();
    r.script("scripts\\count.scr", s);
    r.obj("t_count {\n flag FL_TEMPORARY\n script \"scripts\\count.scr\"\n}\n");
    r.start();
    int a = r.create("t_count");
    REQUIRE(a >= 0);
    CHECK(r.e(a).f(20) == 7.0f); // init ran
    CHECK(r.e(a).f(21) == 1.0f); // one immediate think ran main
    r.world.removeEntity(a);
    float age = r.e(a).f(F_AGE);
    r.step(); // freed at the start of this pass, never thought again
    CHECK(r.e(a).f(21) == 1.0f);
    CHECK(r.e(a).f(F_AGE) == age);
}

TEST_CASE("world: stale references read as dead, ignore writes, and do nothing in builtins") {
    Rig r;
    // callback: t0 = IsValidTarget(self[20] as a stored reference); self[21] = t0;
    //           stored[34] = 5 (a write through the stale reference); self[22] = stored[4]
    Asm s;
    s.entry(EntryPoint::Callback);
    s.getSelf(0, 20);
    s.call("IsValidTarget", 1);
    s.setSelfSlot(21, 1);
    s.getSelf(2, 20);
    s.leaSlot(3, 2, 34);
    s.storeImm(3, 5.0f);
    s.leaSlot(3, 2, 4);
    s.load(4, 3);
    s.setSelfSlot(22, 4);
    s.getSelf(0, 20);
    s.call("remove", 5);
    s.end();
    r.script("scripts\\stale.scr", s);
    r.obj("t_holder {\n flag FL_TEMPORARY\n script \"scripts\\stale.scr\"\n}\n"
          "t_target {\n flag FL_TEMPORARY\n health 10\n}\n");
    r.start();
    int holder = r.create("t_holder");
    int target = r.create("t_target");
    u32 ref = r.world.refOf(target);
    r.e(holder).fields[20] = ref;
    r.world.runCallback(holder, 0, 0, 0);
    CHECK(r.e(holder).f(21) == 1.0f);      // live and healthy
    CHECK(r.e(target).f(F_HEALTH) == 5.0f); // live write went through
    CHECK(r.e(holder).f(22) == 0.0f);
    CHECK((r.e(target).rt & RT_REMOVED) != 0);
    r.step(); // freed
    int other = r.create("t_target"); // reuses the slot with a new generation
    CHECK(other == target);
    CHECK(r.world.refOf(other) != ref);
    CHECK(r.world.liveIndexFromRef(ref) == -1);
    r.world.runCallback(holder, 0, 0, 0);
    CHECK(r.e(holder).f(21) == 0.0f);        // stale: not a valid target
    CHECK(r.e(holder).f(22) == 1.0f);        // stale reads see dead = 1.0
    CHECK(r.e(other).f(F_HEALTH) == 10.0f);  // the write did not reach the new occupant
    CHECK((r.e(other).rt & RT_REMOVED) == 0); // remove(stale) did nothing
}

TEST_CASE("world: a root with AttachEntity children is freed only after they are removed") {
    Rig r;
    r.obj("t_plain {\n flag FL_TEMPORARY\n}\n");
    r.start();
    int root = r.create("t_plain");
    int child = r.create("t_plain");
    r.world.attachEntity(child, root, "origin", false);
    CHECK(r.e(root).attachRefCount == 1);
    CHECK((r.e(child).rt & RT_ATTACHED_ENTITY) != 0);
    r.world.removeEntity(root);
    r.step();
    CHECK(r.e(root).inUse); // pinned by the attached child
    r.world.removeEntity(child);
    CHECK(r.e(root).attachRefCount == 0);
    r.step();
    CHECK_FALSE(r.e(root).inUse);
    CHECK_FALSE(r.e(child).inUse);
}

TEST_CASE("world: definition attachments build a child tree with ids") {
    Rig r;
    r.obj("t_leaf {\n}\n"
          "t_mid {\n attach id \"leaf\" \"t_leaf\" \"origin\"\n}\n"
          "t_top {\n flag FL_TEMPORARY\n attach id \"mid\" \"t_mid\" \"origin\"\n attach \"t_leaf\" \"origin\"\n"
          " attach night \"t_leaf\" \"origin\"\n}\n");
    r.start();
    int top = r.create("t_top", {640, 300, 5});
    REQUIRE(top >= 0);
    const Entity& t = r.e(top);
    REQUIRE(t.children.size() == 2); // the night attachment is skipped by day
    const Entity& mid = r.e(t.children[0]);
    CHECK(mid.name == "mid");
    CHECK(mid.parent == top);
    REQUIRE(mid.children.size() == 1);
    CHECK(r.e(mid.children[0]).name == "leaf");
    CHECK(r.world.listCount() == 1); // children are not pool (list) entities
    CHECK(r.world.slotsInUse() == 4);
    // Children follow the parent through the attachment (non-abs: base + offset).
    CHECK(r.e(mid.children[0]).f(F_ORIGIN + 2) == doctest::Approx(5.0f));
    r.world.removeEntity(top);
    r.step();
    CHECK(r.world.slotsInUse() == 0);
}

TEST_CASE("world: update order is newest first; created entities think once, immediately") {
    Rig r;
    // main: p_counter1 += 1; self[20] = p_counter1
    Asm s;
    s.entry(EntryPoint::Main);
    s.movGlobal(0, "p_counter1");
    s.emit(OP_ADD, M_IMM2, 0, imm(1.0f), 0);
    s.emit(OP_MOV, 0, s.g("p_counter1"), 0);
    s.setSelfSlot(20, 0);
    s.end();
    r.script("scripts\\order.scr", s);
    r.obj("t_order {\n flag FL_TEMPORARY\n script \"scripts\\order.scr\"\n}\n");
    r.start();
    int a = r.create("t_order"); // counter 1
    int b = r.create("t_order"); // counter 2
    CHECK(r.e(a).f(20) == 1.0f);
    CHECK(r.e(b).f(20) == 2.0f);
    r.step(); // b (newest) first: 3, then a: 4
    CHECK(r.e(b).f(20) == 3.0f);
    CHECK(r.e(a).f(20) == 4.0f);

    // An entity created during the pass is not visited again in that frame.
    Asm sp;
    sp.entry(EntryPoint::Main);
    sp.getSelf(0, 22);
    int jz = sp.emit(OP_JZ, 0, 0, 0); // if self[22] == 0 -> spawn
    sp.end();
    int spawn = sp.here();
    sp.patchB(jz, spawn - jz);
    sp.setSelf(22, 1.0f);
    sp.movStr(0, "t_count");
    sp.leaGlobal(1, "self", 5);
    sp.call("create", 2);
    sp.end();
    r.script("scripts\\spawner.scr", sp);
    Asm c = counterScript();
    r.script("scripts\\count.scr", c);
    // Definitions must exist before start(): use a second rig.
    Rig r2;
    r2.script("scripts\\spawner.scr", sp);
    r2.script("scripts\\count.scr", c);
    r2.obj("t_count {\n flag FL_TEMPORARY\n script \"scripts\\count.scr\"\n}\n"
           "t_spawner {\n flag FL_TEMPORARY\n script \"scripts\\spawner.scr\"\n}\n");
    r2.start();
    int sidx = r2.create("t_spawner");
    REQUIRE(sidx >= 0);
    // The spawner's immediate think (inside create) already spawned the child.
    std::vector<int> list = r2.world.listEntities();
    REQUIRE(list.size() == 2);
    int child = list[0];
    CHECK(r2.e(child).name == "t_count");
    CHECK(r2.e(child).f(21) == 1.0f);
    r2.step();
    CHECK(r2.e(child).f(21) == 2.0f); // one think per frame
}

TEST_CASE("world: events reach scripts (init, main, damage, touch, callback)") {
    Rig r;
    Asm s;
    s.entry(EntryPoint::Init);
    s.setSelf(20, 1.0f);
    s.end();
    s.entry(EntryPoint::Main);
    s.addSelf(21, 1.0f);
    s.end();
    s.entry(EntryPoint::Damage); // self[22] = self[34] (health as seen by the handler)
    s.getSelf(0, 34);
    s.setSelfSlot(22, 0);
    s.getSelf(0, 4); // dead flag as seen by the handler
    s.setSelfSlot(26, 0);
    s.end();
    s.entry(EntryPoint::Touch); // self[23] = other[34]
    s.leaGlobal(0, "other", 34);
    s.load(1, 0);
    s.setSelfSlot(23, 1);
    s.end();
    s.entry(EntryPoint::Callback); // self[24] = cb_msg, self[25] = cb_parm1
    s.movGlobal(0, "cb_msg");
    s.setSelfSlot(24, 0);
    s.movGlobal(0, "cb_parm1");
    s.setSelfSlot(25, 0);
    s.end();
    r.script("scripts\\ev.scr", s);
    r.obj("t_ev {\n flag FL_TEMPORARY\n health 50\n script \"scripts\\ev.scr\"\n}\n"
          "t_other {\n flag FL_TEMPORARY\n health 33\n}\n");
    r.start();
    int e = r.create("t_ev");
    int o = r.create("t_other");
    CHECK(r.e(e).f(20) == 1.0f);
    CHECK(r.e(e).f(21) == 1.0f);
    r.step(3);
    CHECK(r.e(e).f(21) == 4.0f);

    r.world.damageEntity(e, 20.0f, 0);
    CHECK(r.e(e).f(22) == 30.0f);
    CHECK(r.e(e).f(F_HEALTH) == 30.0f);
    CHECK(r.e(e).f(26) == 0.0f);
    CHECK(r.e(e).f(F_DEAD) == 0.0f);
    r.world.damageEntity(e, 40.0f, 0); // lethal: handler still sees dead == 0
    CHECK(r.e(e).f(22) == -10.0f);
    CHECK(r.e(e).f(26) == 0.0f);
    CHECK(r.e(e).f(F_DEAD) == 1.0f);
    r.e(e).setF(22, 0.0f);
    r.world.damageEntity(e, 5.0f, 0); // ignored once dead
    CHECK(r.e(e).f(22) == 0.0f);

    u32 otherBefore = r.world.otherBits;
    r.world.runTouch(e, o);
    CHECK(r.e(e).f(23) == 33.0f);
    CHECK(r.world.otherBits == otherBefore); // other restored after the touch

    r.world.runCallback(e, 3.0f, 4.0f, 5.0f);
    CHECK(r.e(e).f(24) == 3.0f);
    CHECK(r.e(e).f(25) == 4.0f);
    CHECK(r.world.cbMsgBits == fb(3.0f)); // cb_* are not restored
}

TEST_CASE("world: damage respects FreezeHealth, player freeze, drops and score") {
    Rig r;
    r.obj("t_enemy {\n enemy\n health 10\n score 120\n}\n"
          "t_drop {\n flag FL_TEMPORARY\n}\n");
    r.start(true);
    int en = r.create("t_enemy");
    r.e(en).rt |= RT_HEALTH_FROZEN;
    r.world.damageEntity(en, 5.0f, 0);
    CHECK(r.e(en).f(F_HEALTH) == 10.0f);
    r.e(en).rt &= ~RT_HEALTH_FROZEN;
    r.e(en).drop = r.db.findObject("t_drop");
    int before = r.world.listCount();
    r.world.damageEntity(en, 15.0f, 0);
    CHECK(r.e(en).f(F_DEAD) == 1.0f);
    CHECK(r.e(en).drop == nullptr);
    CHECK(r.world.player(0).kills == 1);
    CHECK(r.world.player(0).scores == 120.0f);
    // Drop + three score digits.
    CHECK(r.world.listCount() == before + 4);
    int digits = 0;
    for (int i : r.world.listEntities()) {
        if (r.e(i).name == "score_num") {
            ++digits;
            float d = r.e(i).f(F_FRAME);
            CHECK((d == 1.0f || d == 2.0f || d == 0.0f));
        }
    }
    CHECK(digits == 3);

    int pe = r.world.playerEntityIndex(0);
    REQUIRE(pe >= 0);
    float h = r.e(pe).f(F_HEALTH);
    r.world.player(0).freezeCount = 1;
    r.world.damageEntity(pe, 50.0f, -1);
    CHECK(r.e(pe).f(F_HEALTH) == h);
    r.world.player(0).freezeCount = 0;
    r.world.damageEntity(pe, 50.0f, -1);
    CHECK(r.e(pe).f(F_HEALTH) == h - 50.0f);
}

TEST_CASE("world: difficulty factors") {
    const float health[5] = {0.3f, 0.5f, 0.75f, 1.5f, 2.0f};
    const float damage[5] = {0.5f, 0.7f, 0.8f, 1.25f, 1.4f};
    for (int d = 0; d < 5; ++d) {
        Rig r;
        r.config.difficulty = d;
        r.start();
        CHECK(r.world.healthFactor() == health[d]);
        CHECK(r.world.damageFactor() == damage[d]);
    }
}

TEST_CASE("world: activation states") {
    Rig r;
    r.obj("t_static {\n flag FL_ONGROUND\n}\n");
    r.start();
    // Created entities start active; a non-temporary one outside the area goes leaving,
    // then is removed once its sphere leaves the view frustum.
    int in = r.create("t_static", {640, 300, 0});
    int behind = r.create("t_static", {640, -400, 0});
    EntityHandle hb = r.world.handleOf(behind);
    r.step();
    CHECK(r.e(in).state == ES_ACTIVE);
    CHECK(r.e(behind).state == ES_LEAVING);
    r.step();
    CHECK((r.e(behind).rt & RT_REMOVED) != 0); // leaving and out of view: removed
    r.step();
    CHECK(r.world.get(hb) == nullptr); // freed at the next pass
    // Dormant entities wake up once inside the band [map_pos + 16, map_pos + 800].
    int far = r.create("t_static", {640, 1000, 0});
    r.world.entity(far).state = ES_DORMANT;
    r.step();
    CHECK(r.e(far).state == ES_DORMANT);
    r.world.setMapPos(300.0f);
    r.step();
    CHECK(r.e(far).state == ES_ACTIVE);
}

TEST_CASE("world: stall detector faults the thread, the world keeps running") {
    Rig r;
    Asm s;
    s.entry(EntryPoint::Main);
    int loop = s.emit(OP_JMP, 0, 0); // jump to itself forever
    (void)loop;
    r.script("scripts\\stall.scr", s);
    r.obj("t_stall {\n flag FL_TEMPORARY\n script \"scripts\\stall.scr\"\n}\n");
    r.start();
    int e = r.create("t_stall");
    CHECK(r.world.stats().stalls == 1);
    CHECK(r.e(e).scriptFaulted);
    r.step(2);
    CHECK(r.world.stats().stalls == 1); // not run again
}

TEST_CASE("world: EndLevel pauses the world (no main, no ageing)") {
    Rig r;
    Asm s = counterScript();
    r.script("scripts\\count.scr", s);
    r.obj("t_count {\n flag FL_TEMPORARY\n script \"scripts\\count.scr\"\n}\n");
    r.start();
    int e = r.create("t_count");
    r.step();
    float n = r.e(e).f(21), age = r.e(e).f(F_AGE), pos = r.world.mapPos();
    r.world.endLevel();
    r.step(5);
    CHECK(r.world.levelComplete());
    CHECK(r.e(e).f(21) == n);
    CHECK(r.e(e).f(F_AGE) == age);
    CHECK(r.world.mapPos() == pos);
}

TEST_CASE("world: scroll advances at 42 units/s times the camera factor") {
    Rig r;
    r.start();
    CHECK(r.world.mapPos() == 32.0f);
    r.step(60);
    CHECK(r.world.mapPos() == doctest::Approx(32.0f + 42.0f).epsilon(1e-4));
    r.world.camera().field[9] = 0.0f;
    r.step(10);
    CHECK(r.world.mapPos() == doctest::Approx(74.0f).epsilon(1e-4));
    CHECK(r.world.camera().field[7] == 0.0f);
}

TEST_CASE("world: input edges and the one-shot switch bits") {
    Rig r;
    r.start(true);
    PlayerInput in;
    in.action[0] = ACT_FIRE | ACT_LEFT;
    r.world.step(in);
    CHECK(static_cast<u32>(r.world.player(0).action) == (ACT_FIRE | ACT_LEFT));
    in.action[0] = ACT_LEFT;
    r.world.step(in);
    CHECK(static_cast<u32>(r.world.player(0).action) == ACT_LEFT);
    r.world.player(0).upgrades[3] = 1;
    in.action[0] = ACT_NEXT_WEAPON;
    r.world.step(in);
    CHECK(r.world.player(0).weapon == 3.0f);
    CHECK((static_cast<u32>(r.world.player(0).action) & ACT_NEXT_WEAPON) == 0);
}

TEST_CASE("world: determinism on synthetic scripts") {
    auto run = [] {
        Rig r;
        Asm s;
        s.entry(EntryPoint::Main);
        s.call("crandom", 0);
        s.setSelfSlot(20, 0);
        s.leaGlobal(0, "self", 20);
        s.call("move", 2);
        s.end();
        r.script("scripts\\rnd.scr", s);
        r.obj("t_rnd {\n flag FL_TEMPORARY\n script \"scripts\\rnd.scr\"\n}\n");
        r.config.seed = 1234;
        r.start();
        for (int i = 0; i < 5; ++i) r.create("t_rnd", {600.0f + 10 * i, 300, 0});
        r.step(120);
        return r.world.dumpStateJson();
    };
    std::string a = run(), b = run();
    CHECK(a.size() > 100);
    CHECK(a == b);
}

// ---------------------------------------------------------------------------------------
// Game data.
// ---------------------------------------------------------------------------------------

namespace {
struct DataRig {
    Vfs vfs;
    DefDatabase db;
    World world;
    bool load(const std::string& level, const WorldConfig& cfg) {
        vfs.mount(makeDirSource(testdata::extractedDir()));
        if (!db.load(vfs)) return false;
        // The data is the selected game's, so its rules apply (a sequel's scripts use
        // globals and builtins the first game's rules do not have).
        WorldConfig c = cfg;
        if (!c.rules) c.rules = &testdata::game().rules;
        world.init(vfs, db, c);
        std::string err;
        bool ok = world.loadLevel(level, &err);
        if (!ok) MESSAGE(err);
        return ok;
    }
};
} // namespace

TEST_CASE("world corpus: level 1 runs 3600 frames headless") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    AS3D_REQUIRE_FIRST_GAME("level 1");
    DataRig r;
    REQUIRE(r.load("1", WorldConfig()));
    CHECK(r.world.playerEntityIndex(0) >= 0);
    PlayerInput in;
    int maxList = 0;
    for (int f = 0; f < 3600; ++f) {
        r.world.step(in);
        maxList = std::max(maxList, r.world.listCount());
    }
    const WorldStats& st = r.world.stats();
    for (const std::string& e : st.firstErrors) MESSAGE(e);
    CHECK(st.scriptErrors == 0);
    CHECK(st.stalls == 0);
    CHECK(st.spawnRefused == 0);
    CHECK(maxList > 10);
    CHECK(maxList < 400);
    CHECK(st.maxSlotsInUse < 1500);
    CHECK(r.world.mapPos() == doctest::Approx(32.0f + 42.0f * 60.0f).epsilon(1e-3));
    CHECK(r.world.enemiesInLevel() > 0);
    // Scripts have run for real: builtins were called, the report counts them.
    bool sawCreate = false;
    for (const auto& row : r.world.report().rows()) {
        if (row.name == "create" && row.calls > 0) sawCreate = true;
    }
    CHECK(sawCreate);
}

TEST_CASE("world corpus: two runs with the same seed give identical state dumps") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    std::string dumps[2];
    for (int k = 0; k < 2; ++k) {
        DataRig r;
        WorldConfig cfg;
        cfg.seed = 99;
        REQUIRE(r.load("1", cfg));
        PlayerInput in;
        for (int f = 0; f < 900; ++f) {
            in.action[0] = (f / 90) % 2 ? (ACT_FIRE | ACT_LEFT) : (ACT_FIRE | ACT_RIGHT);
            r.world.step(in);
        }
        dumps[k] = r.world.dumpStateJson();
    }
    CHECK(dumps[0].size() > 1000);
    CHECK(dumps[0] == dumps[1]);
}

TEST_CASE("world corpus: map-spawned health scales with difficulty") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    // Run Very Easy until the first map enemy exists, then Nightmare for as many frames,
    // and compare the maximum health of the same map-spawned enemies (same slots).
    DataRig easy, hard;
    WorldConfig ce, ch;
    ce.difficulty = 0;
    ch.difficulty = 4;
    REQUIRE(easy.load("1", ce));
    REQUIRE(hard.load("1", ch));
    PlayerInput in;
    int frames = 0;
    while (easy.world.enemiesInLevel() == 0 && frames < 3600) {
        easy.world.step(in);
        hard.world.step(in);
        ++frames;
    }
    int compared = 0;
    for (int i : easy.world.listEntities()) {
        const Entity& a = easy.world.entity(i);
        if (a.f(F_CLASS) != kClassEnemy || a.maxHealth <= 0.0f) continue;
        const Entity& b = hard.world.entity(i);
        REQUIRE(b.name == a.name);
        CHECK(b.maxHealth / a.maxHealth == doctest::Approx(2.0f / 0.3f).epsilon(1e-3));
        ++compared;
    }
    CHECK(compared > 0);
}
