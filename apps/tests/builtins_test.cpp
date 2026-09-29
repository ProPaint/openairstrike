// One test per implemented builtin (WP-42a), on synthetic scripts run in a synthetic
// world. The body under test is usually the `callback` entry of a test entity, run once
// with World::runCallback (no think, frametime set directly); latent builtins use `main`.
// See docs/spec/rcsl-builtins-semantics.md for each builtin's behaviour.
#include "doctest.h"

#include <cmath>
#include <functional>

#include "as3d/script_host.h"
#include "world_test_util.h"
// GamePath is private to the game module; the path tests drive it directly.
#include "../../engine/src/game/world_path.h"

using namespace worldtest;

namespace {

constexpr float kFt = 0.5f;

// Test rig: entity `self` runs `body` as its callback; results go to self[26] (scalar).
struct BT {
    Rig r;
    int self = -1;
    Asm a;

    void setup(const std::function<void(Asm&)>& body, const std::string& extraObjects = "", bool players = false,
               const std::string& selfExtra = "") {
        a.entry(EntryPoint::Callback);
        body(a);
        a.end();
        r.script("scripts\\bt.scr", a);
        r.obj("t_bt {\n flag FL_TEMPORARY\n script \"scripts\\bt.scr\"\n" + selfExtra + "}\n" + extraObjects);
        r.start(players);
        self = r.create("t_bt");
        REQUIRE(self >= 0);
        r.world.frametimeGlobal = kFt;
    }
    void run() { r.world.runCallback(self, 0.0f, 0.0f, 0.0f); }
    Entity& e() { return r.e(self); }
    float f(int k) { return r.e(self).f(k); }
    float result() { return f(26); }
};

// Calls `name` with the arguments already placed in t0..t3 and stores the return register
// into self[26].
void callStore(Asm& a, const std::string& name) {
    a.call(name, 4);
    a.setSelfSlot(26, 4);
}

float scalar1(const std::string& name, float x) {
    BT t;
    t.setup([&](Asm& a) {
        a.movImm(0, x);
        callStore(a, name);
    });
    t.run();
    return t.result();
}

float scalar2(const std::string& name, float x, float y) {
    BT t;
    t.setup([&](Asm& a) {
        a.movImm(0, x);
        a.movImm(1, y);
        callStore(a, name);
    });
    t.run();
    return t.result();
}

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

} // namespace

// ---------------------------------------------------------------------------------------
// A. Math.
// ---------------------------------------------------------------------------------------

TEST_CASE("builtin debug: no effect") {
    BT t;
    t.setup([](Asm& a) { a.call("debug", 4); });
    t.run();
    CHECK(t.r.world.stats().scriptErrors == 0);
}

TEST_CASE("builtin random: our xorshift generator, [0, 1)") {
    BT t;
    t.setup([](Asm& a) { callStore(a, "random"); });
    t.run();
    Rng ref(1);
    CHECK(t.result() == ref.uniform());
}

TEST_CASE("builtin crandom: 2u - 1") {
    BT t;
    t.setup([](Asm& a) { callStore(a, "crandom"); });
    t.run();
    Rng ref(1);
    float u = ref.uniform();
    CHECK(t.result() == static_cast<float>(static_cast<double>(static_cast<float>(u * 2.0)) - 1.0));
}

TEST_CASE("builtin sin/cos/tan take degrees") {
    CHECK(near(scalar1("sin", 30.0f), 0.5f));
    CHECK(near(scalar1("cos", 60.0f), 0.5f));
    CHECK(near(scalar1("tan", 45.0f), 1.0f));
}

TEST_CASE("builtin atan: quirk, input scaled as degrees, output radians") {
    CHECK(near(scalar1("atan", 45.0f), std::atan(45.0f * 3.14159265f / 180.0f)));
}

TEST_CASE("builtin abs") { CHECK(scalar1("abs", -3.5f) == 3.5f); }

TEST_CASE("builtin min/max, unordered gives b") {
    CHECK(scalar2("min", 3.0f, 2.0f) == 2.0f);
    CHECK(scalar2("min", 1.0f, 2.0f) == 1.0f);
    CHECK(scalar2("max", 1.0f, 2.0f) == 2.0f);
    CHECK(scalar2("max", 3.0f, 2.0f) == 3.0f);
    CHECK(scalar2("min", std::nanf(""), 2.0f) == 2.0f);
    CHECK(scalar2("max", std::nanf(""), 2.0f) == 2.0f);
}

TEST_CASE("builtin lerp: t first") {
    BT t;
    t.setup([](Asm& a) {
        a.movImm(0, 0.25f);
        a.movImm(1, 2.0f);
        a.movImm(2, 6.0f);
        callStore(a, "lerp");
    });
    t.run();
    CHECK(t.result() == 3.0f);
}

TEST_CASE("builtin SetFlag / ClearFlag") {
    CHECK(scalar2("SetFlag", 5.0f, 2.0f) == 7.0f);
    CHECK(scalar2("ClearFlag", 7.0f, 2.9f) == 5.0f); // bit truncated to 2
}

// ---------------------------------------------------------------------------------------
// A. Vectors (pointers into self's fields).
// ---------------------------------------------------------------------------------------

namespace {
Vec3 v3(BT& t, int k) { return t.e().v3(k); }
} // namespace

TEST_CASE("builtin vec_copy") {
    BT t;
    t.setup([](Asm& a) {
        a.leaGlobal(0, "self", 17);
        a.leaGlobal(1, "self", 8);
        a.call("vec_copy", 4);
    });
    t.e().setV3(17, {1, 2, 3});
    t.run();
    CHECK(v3(t, 8).x == 1.0f);
    CHECK(v3(t, 8).z == 3.0f);
}

TEST_CASE("builtin vec_add / vec_sub (aliasing output)") {
    for (int sub = 0; sub < 2; ++sub) {
        BT t;
        t.setup([&](Asm& a) {
            a.leaGlobal(0, "self", 17);
            a.leaGlobal(1, "self", 8);
            a.leaGlobal(2, "self", 17); // out aliases a
            a.call(sub ? "vec_sub" : "vec_add", 4);
        });
        t.e().setV3(17, {1, 2, 3});
        t.e().setV3(8, {10, 20, 30});
        t.run();
        Vec3 r = v3(t, 17);
        CHECK(r.x == (sub ? -9.0f : 11.0f));
        CHECK(r.z == (sub ? -27.0f : 33.0f));
    }
}

TEST_CASE("builtin vec_ma") {
    BT t;
    t.setup([](Asm& a) {
        a.leaGlobal(0, "self", 5);
        a.movImm(1, 0.5f);
        a.leaGlobal(2, "self", 17);
        a.leaGlobal(3, "self", 20);
        a.call("vec_ma", 4);
    });
    t.e().setV3(5, {1, 1, 1});
    t.e().setV3(17, {4, 6, 8});
    t.run();
    CHECK(v3(t, 20).x == 3.0f);
    CHECK(v3(t, 20).z == 5.0f);
}

TEST_CASE("builtin vec_length / vec_norm") {
    BT t;
    t.setup([](Asm& a) {
        a.leaGlobal(0, "self", 17);
        callStore(a, "vec_length");
    });
    t.e().setV3(17, {3, 4, 0});
    t.run();
    CHECK(t.result() == 5.0f);

    BT n;
    n.setup([](Asm& a) {
        a.leaGlobal(0, "self", 17);
        callStore(a, "vec_norm");
    });
    n.e().setV3(17, {0, 0, 2});
    n.run();
    CHECK(n.result() == 2.0f);
    CHECK(v3(n, 17).z == 1.0f);
}

TEST_CASE("builtin vec_scale / vec_setlen") {
    BT t;
    t.setup([](Asm& a) {
        a.leaGlobal(0, "self", 17);
        a.movImm(1, 3.0f);
        a.call("vec_scale", 4);
        a.leaGlobal(0, "self", 20);
        a.movImm(1, 10.0f);
        a.call("vec_setlen", 4);
        a.leaGlobal(0, "self", 8); // zero vector: unchanged
        a.movImm(1, 10.0f);
        a.call("vec_setlen", 4);
    });
    t.e().setV3(17, {1, 2, 3});
    t.e().setV3(20, {3, 0, 4});
    t.run();
    CHECK(v3(t, 17).y == 6.0f);
    CHECK(near(v3(t, 20).x, 6.0f));
    CHECK(near(v3(t, 20).z, 8.0f));
    CHECK(v3(t, 8).x == 0.0f);
}

TEST_CASE("builtin vec_toyaw: -90 offset and the x == 0 quirk") {
    BT t;
    t.setup([](Asm& a) {
        a.leaGlobal(0, "self", 17);
        callStore(a, "vec_toyaw");
    });
    t.e().setV3(17, {1, 1, 0});
    t.run();
    CHECK(near(t.result(), -45.0f));
    t.e().setV3(17, {0, 5, 0});
    t.run();
    CHECK(t.result() == -90.0f);
}

TEST_CASE("builtin vec_toangles: (-pitch, 0, yaw - 90)") {
    BT t;
    t.setup([](Asm& a) {
        a.leaGlobal(0, "self", 17);
        a.leaGlobal(1, "self", 20);
        a.call("vec_toangles", 4);
    });
    t.e().setV3(17, {1, 0, 1});
    t.run();
    CHECK(near(v3(t, 20).x, -45.0f));
    CHECK(v3(t, 20).y == 0.0f);
    CHECK(near(v3(t, 20).z, -90.0f));
}

TEST_CASE("builtin ClearAxis / AnglesToAxis") {
    BT t;
    t.setup([](Asm& a) {
        a.leaGlobal(0, "self", 44);
        a.call("ClearAxis", 4);
    });
    for (int k = 0; k < 9; ++k) t.e().setF(44 + k, 7.0f);
    t.run();
    for (int k = 0; k < 9; ++k) CHECK(t.f(44 + k) == ((k % 4 == 0) ? 1.0f : 0.0f));

    BT u;
    u.setup([](Asm& a) {
        a.leaGlobal(0, "self", 14);
        a.leaGlobal(1, "self", 53); // 9 free fields
        a.call("AnglesToAxis", 4);
    });
    u.e().setV3(14, {0, 0, 90});
    u.run();
    // Row 0 (forward) = (cy cz, cy sz, sy) = (0, 1, 0) for yaw 90.
    CHECK(near(u.f(53), 0.0f));
    CHECK(near(u.f(54), 1.0f));
    CHECK(near(u.f(61), 1.0f)); // row 2 z = cx cy
}

// ---------------------------------------------------------------------------------------
// B. Entities.
// ---------------------------------------------------------------------------------------

TEST_CASE("builtin create: copies angles and player index, returns the reference") {
    BT t;
    t.setup(
        [](Asm& a) {
            a.movStr(0, "t_child");
            a.leaGlobal(1, "self", 5);
            callStore(a, "create");
        },
        "t_child {\n flag FL_TEMPORARY\n}\n");
    t.e().setV3(F_ANGLES, {1, 2, 3});
    t.e().setV3(F_ORIGIN, {600, 400, 10});
    int before = t.r.world.listCount();
    t.run();
    CHECK(t.r.world.listCount() == before + 1);
    int c = t.r.world.liveIndexFromRef(t.e().fields[26]);
    REQUIRE(c >= 0);
    CHECK(t.r.e(c).name == "t_child");
    CHECK(t.r.e(c).f(F_ANGLES + 2) == 3.0f);
    CHECK(t.r.e(c).f(F_ORIGIN + 1) == 400.0f);
    CHECK(t.r.e(c).f(F_AGE) == 0.0f); // its immediate think ran with frametime 0 (no step yet)

    BT u; // unknown name: 0.0
    u.setup([](Asm& a) {
        a.movStr(0, "no_such_object");
        a.leaGlobal(1, "self", 5);
        callStore(a, "create");
    });
    u.e().setF(26, 9.0f);
    u.run();
    CHECK(u.e().fields[26] == 0u);
}

TEST_CASE("builtin remove: deferred, script keeps running") {
    BT t;
    t.setup([](Asm& a) {
        a.movGlobal(0, "self");
        a.call("remove", 4);
        a.setSelf(27, 1.0f);
    });
    t.run();
    CHECK((t.e().rt & RT_REMOVED) != 0);
    CHECK(t.f(27) == 1.0f);
}

TEST_CASE("builtin activate / deactivate recurse into definition children") {
    BT t;
    t.setup(
        [](Asm& a) {
            a.movGlobal(0, "self");
            a.call("deactivate", 4);
        },
        "t_kid {\n}\n", false, " attach id \"kid\" \"t_kid\" \"origin\"\n");
    int kid = t.e().children.at(0);
    t.run();
    CHECK((t.e().rt & RT_ACTIVE) == 0);
    CHECK((t.r.e(kid).rt & RT_ACTIVE) == 0);

    BT u;
    u.setup([](Asm& a) {
        a.movGlobal(0, "self");
        a.call("activate", 4);
    });
    u.e().rt &= ~RT_ACTIVE;
    u.run();
    CHECK((u.e().rt & RT_ACTIVE) != 0);
}

TEST_CASE("builtin getentity: raw pointer + 0x7B") {
    BT t;
    t.setup([](Asm& a) {
        a.getSelf(0, 0); // field 0 = raw entity
        callStore(a, "getentity");
    });
    t.run();
    CHECK(t.e().fields[26] == t.r.world.refOf(t.self));
}

TEST_CASE("builtin setskin") {
    BT t;
    t.setup([](Asm& a) {
        a.movGlobal(0, "self");
        a.movStr(1, "models\\x\\skin2.tga");
        a.call("setskin", 4);
    });
    t.run();
    CHECK(t.e().skinPath == "models\\x\\skin2.tga");
}

TEST_CASE("builtin SetModel: model changes, radius kept, tags follow") {
    BT t;
    t.r.model("models\\a.mdl", {-1, -1, -1}, {1, 1, 1});
    t.r.model("models\\b.mdl", {-5, -5, -5}, {5, 5, 5}, {{"tag_x", {0, 3, 0}}});
    t.setup(
        [](Asm& a) {
            a.movStr(0, "models\\b.mdl");
            a.call("SetModel", 4);
        },
        "", false, " model \"models\\a.mdl\"\n");
    float radius = t.e().radius;
    CHECK(t.e().boundsMax.x == 1.0f);
    t.run();
    CHECK(t.e().boundsMax.x == 5.0f);
    CHECK(t.e().radius == radius);
    Vec3 tag;
    CHECK(t.r.world.tagLocal(t.self, "tag_x", tag));
    CHECK(tag.y == 3.0f);
}

TEST_CASE("builtin callback: sets cb_* and runs the target's callback") {
    BT t;
    Asm other;
    other.entry(EntryPoint::Callback);
    other.movGlobal(0, "cb_msg");
    other.setSelfSlot(20, 0);
    other.movGlobal(0, "cb_parm2");
    other.setSelfSlot(21, 0);
    other.end();
    t.r.script("scripts\\other.scr", other);
    t.setup(
        [](Asm& a) {
            a.getSelf(0, 27); // target reference stored in self[27]
            a.movImm(1, 4.0f);
            a.movImm(2, 5.0f);
            a.movImm(3, 6.0f);
            a.call("callback", 4);
        },
        "t_other {\n flag FL_TEMPORARY\n script \"scripts\\other.scr\"\n}\n");
    int o = t.r.create("t_other");
    t.e().fields[27] = t.r.world.refOf(o);
    t.run();
    CHECK(t.r.e(o).f(20) == 4.0f);
    CHECK(t.r.e(o).f(21) == 6.0f);
}

TEST_CASE("builtin AttachEntity: linked pool entity pins its root") {
    BT t;
    t.setup(
        [](Asm& a) {
            a.getSelf(0, 27);
            a.movGlobal(1, "self");
            a.movStr(2, "origin");
            a.movImm(3, 1.0f);
            a.call("AttachEntity", 4);
        },
        "t_part {\n flag FL_TEMPORARY\n}\n");
    int part = t.r.create("t_part", {0, 0, 0});
    t.e().fields[27] = t.r.world.refOf(part);
    t.e().setV3(F_ORIGIN, {600, 300, 20});
    t.run();
    CHECK(t.r.e(part).parent == t.self);
    CHECK(t.r.e(part).absAttach);
    CHECK((t.r.e(part).rt & RT_ATTACHED_ENTITY) != 0);
    CHECK(t.e().attachRefCount == 1);
    t.r.step();
    CHECK(t.r.e(part).f(F_ORIGIN) == doctest::Approx(600.0f));
    CHECK(t.r.e(part).f(F_ORIGIN + 2) == doctest::Approx(20.0f));
}

TEST_CASE("builtin AttachActivate / AttachDeactivate: first non-removed child by id") {
    BT t;
    t.setup(
        [](Asm& a) {
            a.movStr(0, "kid");
            a.call("AttachDeactivate", 4);
        },
        "t_kid {\n}\n", false, " attach id \"kid\" \"t_kid\" \"origin\"\n attach id \"kid\" \"t_kid\" \"origin\"\n");
    int k0 = t.e().children.at(0), k1 = t.e().children.at(1);
    t.run();
    CHECK((t.r.e(k0).rt & RT_ACTIVE) == 0);
    CHECK((t.r.e(k1).rt & RT_ACTIVE) != 0);
    t.r.e(k0).rt |= RT_REMOVED; // removed children are skipped
    t.run();
    CHECK((t.r.e(k1).rt & RT_ACTIVE) == 0);

    BT u;
    u.setup(
        [](Asm& a) {
            a.movStr(0, "kid");
            a.call("AttachActivate", 4);
        },
        "t_kid {\n}\n", false, " attach id \"kid\" \"t_kid\" \"origin\"\n");
    int k = u.e().children.at(0);
    u.r.e(k).rt &= ~RT_ACTIVE;
    u.run();
    CHECK((u.r.e(k).rt & RT_ACTIVE) != 0);
}

TEST_CASE("builtin AttachCallback / ParentCallback") {
    BT t;
    Asm kid;
    kid.entry(EntryPoint::Callback);
    kid.movGlobal(0, "cb_msg");
    kid.setSelfSlot(20, 0);
    kid.movImm(0, 7.0f);
    kid.movImm(1, 0.0f);
    kid.movImm(2, 0.0f);
    kid.call("ParentCallback", 4); // parent's callback is the body under test: guard below
    kid.end();
    t.r.script("scripts\\kid.scr", kid);
    t.setup(
        [](Asm& a) {
            // Runs as the parent's callback; only forwards when cb_msg == 0.
            a.movGlobal(0, "cb_msg");
            a.setSelfSlot(21, 0);
            a.movGlobal(0, "cb_msg");
            int jz = a.emit(OP_JZ, 0, 0, 0);
            a.end();
            a.patchB(jz, a.here() - jz);
            a.movStr(0, "kid");
            a.movImm(1, 3.0f);
            a.movImm(2, 0.0f);
            a.movImm(3, 0.0f);
            a.call("AttachCallback", 4);
        },
        "t_kid {\n script \"scripts\\kid.scr\"\n}\n", false, " attach id \"kid\" \"t_kid\" \"origin\"\n");
    int k = t.e().children.at(0);
    t.run();
    CHECK(t.r.e(k).f(20) == 3.0f); // AttachCallback reached the child
    CHECK(t.f(21) == 7.0f);        // ParentCallback came back to the parent
    // Inactive child: nothing.
    t.r.e(k).setF(20, 0.0f);
    t.r.e(k).rt &= ~RT_ACTIVE;
    t.run();
    CHECK(t.r.e(k).f(20) == 0.0f);
}

TEST_CASE("builtin IsValidTarget / FreezeHealth") {
    BT t;
    t.setup([](Asm& a) {
        a.getSelf(0, 27);
        callStore(a, "IsValidTarget");
        a.getSelf(0, 27);
        a.movImm(1, 1.0f);
        a.call("FreezeHealth", 4);
    }, "t_en {\n enemy\n flag FL_TEMPORARY\n health 5\n}\n");
    int en = t.r.create("t_en");
    t.e().fields[27] = t.r.world.refOf(en);
    t.run();
    CHECK(t.result() == 1.0f);
    CHECK((t.r.e(en).rt & RT_HEALTH_FROZEN) != 0);
    t.r.e(en).setF(F_HEALTH, 0.0f);
    t.run();
    CHECK(t.result() == 0.0f);
    t.e().fields[27] = 0;
    t.run();
    CHECK(t.result() == 0.0f);
}

// ---------------------------------------------------------------------------------------
// C. Movement.
// ---------------------------------------------------------------------------------------

TEST_CASE("builtin move / movex / movey / movez") {
    BT t;
    t.setup([](Asm& a) {
        a.leaGlobal(0, "self", 17);
        a.call("move", 4);
        a.movImm(0, 2.0f);
        a.call("movex", 4);
        a.movImm(0, 4.0f);
        a.call("movey", 4);
        a.movImm(0, 6.0f);
        a.call("movez", 4);
    });
    t.e().setV3(F_ORIGIN, {0, 0, 0});
    t.e().setV3(17, {10, 20, 30});
    t.run();
    CHECK(t.f(F_ORIGIN) == 6.0f);
    CHECK(t.f(F_ORIGIN + 1) == 12.0f);
    CHECK(t.f(F_ORIGIN + 2) == 18.0f);
}

TEST_CASE("builtin rotate / rotatex / rotatey / rotatez") {
    BT t;
    t.setup([](Asm& a) {
        a.leaGlobal(0, "self", 17);
        a.call("rotate", 4);
        a.movImm(0, 2.0f);
        a.call("rotatex", 4);
        a.movImm(0, 4.0f);
        a.call("rotatey", 4);
        a.movImm(0, 6.0f);
        a.call("rotatez", 4);
    });
    t.e().setV3(F_ANGLES, {0, 0, 0});
    t.e().setV3(17, {10, 20, 30});
    t.run();
    CHECK(t.f(F_ANGLES) == 6.0f);
    CHECK(t.f(F_ANGLES + 1) == 12.0f);
    CHECK(t.f(F_ANGLES + 2) == 18.0f);
}

TEST_CASE("builtin sleep: TMO + LCALL waits the timeout") {
    Rig r;
    Asm s;
    s.entry(EntryPoint::Main);
    s.tmo(0.05f);
    s.lcall("sleep");
    s.setSelf(21, 1.0f);
    s.end();
    r.script("scripts\\sl.scr", s);
    r.obj("t_sl {\n flag FL_TEMPORARY\n script \"scripts\\sl.scr\"\n}\n");
    r.start();
    int e = r.create("t_sl"); // first main at creation: frametime 0, still waiting
    r.step(3);                // 0.05 - 3/60 <= 0: completes on the third update
    CHECK(r.e(e).f(21) == 0.0f);
    r.step();
    CHECK(r.e(e).f(21) == 1.0f);
}

TEST_CASE("builtin TerrainHeight: 0 on the flat test level") {
    CHECK(scalar2("TerrainHeight", 100.0f, 200.0f) == 0.0f);
}

namespace {
// RotateTo test: self at (640, 300), facing +y (yaw 0); target to the right (+x).
struct RotRig {
    BT t;
    int target = -1;
    void setup(const std::string& fn, int axes, float mx = 0, float mn = 0) {
        t.setup(
            [&](Asm& a) {
                a.getSelf(0, 27);
                a.movImm(1, static_cast<float>(axes));
                a.movImm(2, mx);
                a.movImm(3, mn);
                a.call(fn, 4);
            },
            "t_tgt {\n flag FL_TEMPORARY\n}\n");
        target = t.r.create("t_tgt", {740, 300, 0});
        t.e().fields[27] = t.r.world.refOf(target);
        t.e().setF(F_WP_TURN_RATE, 90.0f); // 45 degrees per call at frametime 0.5
    }
};
} // namespace

TEST_CASE("builtin RotateTo / lRotateTo: steps at field 24, arrival below 0.1 degree") {
    RotRig rr;
    rr.setup("RotateTo", 1);
    rr.t.run();
    CHECK(near(rr.t.f(F_ANGLES + 2), -45.0f));
    // The axis of the last think is used: refresh it the way a think would.
    rr.t.r.world.setupTransform(rr.t.self);
    rr.t.run();
    CHECK(near(rr.t.f(F_ANGLES + 2), -90.0f, 1e-3f));

    // Latent form: done only once the error is below 0.1 at the start of a step.
    Rig r;
    Asm s;
    s.entry(EntryPoint::Main);
    s.movGlobal(0, "player"); // the arguments of a waiting LCALL are those of its first run
    s.movImm(1, 1.0f);
    s.lcall("lRotateTo");
    s.addSelf(21, 1.0f);
    s.end();
    r.script("scripts\\rot.scr", s);
    r.obj("t_rot {\n flag FL_TEMPORARY\n script \"scripts\\rot.scr\"\n}\n t_tgt {\n flag FL_TEMPORARY\n}\n");
    r.start(true);
    int pl = r.world.playerEntityIndex(0);
    REQUIRE(pl >= 0);
    r.e(pl).setV3(F_ORIGIN, {740, 300, 0});
    int e = r.create("t_rot", {640, 300, 0});
    r.e(e).setF(F_WP_TURN_RATE, 1800.0f); // 30 degrees per frame
    r.step(3);                            // -30, -60, -90 (snap, not arrived)
    CHECK(r.e(e).f(21) == 0.0f);
    r.step(2); // arrival reported, then the code after the LCALL runs
    CHECK(r.e(e).f(21) == 1.0f);
}

TEST_CASE("builtin RotateToClamp / lRotateToClamp: clamp the raw angle") {
    RotRig rr;
    rr.setup("RotateToClamp", 1, 10.0f, -20.0f);
    rr.t.run();
    CHECK(rr.t.f(F_ANGLES + 2) == -20.0f);
    RotRig rl;
    rl.setup("lRotateToClamp", 1, 10.0f, -30.0f);
    rl.t.run();
    CHECK(rl.t.f(F_ANGLES + 2) == -30.0f);
}

namespace {
Placement straightPath(bool loop) {
    Placement p;
    Waypoint a, b, c;
    a.x = 10; a.y = 5; a.inCtrl = {10, 5}; a.outCtrl = {10, 5}; a.delay = 1.5f;
    b.x = 10; b.y = 7; b.inCtrl = {10, 7}; b.outCtrl = {10, 7}; b.delay = 2.5f;
    c.x = 10; c.y = 9; c.inCtrl = {10, 9}; c.outCtrl = {10, 9}; c.delay = 3.5f;
    p.waypoints = {a, b, c};
    p.pathFlag = loop ? 1 : 0;
    return p;
}
} // namespace

TEST_CASE("builtin MoveToNextWP / lMoveToNextWP / GetWaypointDelay") {
    // Straight north path x = 420, from y = 220 to y = 380 (two 80-unit segments).
    Placement pl = straightPath(false);
    GamePath path;
    REQUIRE(path.build(pl));
    BT t;
    t.setup([](Asm& a) {
        a.movImm(0, 1.0f);
        a.call("MoveToNextWP", 4);
        a.movImm(0, 5.0f);
        callStore(a, "GetWaypointDelay");
    });
    t.e().path = &path;
    t.e().setF(F_WP_SPEED, 32.0f); // 16 units per call
    t.run();
    CHECK(near(t.f(F_ORIGIN), 420.0f, 1e-2f));
    CHECK(near(t.f(F_ORIGIN + 1), 236.0f, 0.2f));
    CHECK(near(t.f(F_ANGLES + 2), 0.0f, 1e-2f)); // heading +y = 0 in the field-16 convention
    CHECK(t.f(F_WP_WAIT) == 0.0f);
    CHECK(t.result() == 3.5f); // index 5 mod 3 = 2
    for (int i = 0; i < 4; ++i) t.run(); // reaches the second waypoint: its delay
    CHECK(t.f(F_WP_WAIT) == 2.5f);
    for (int i = 0; i < 10; ++i) t.run(); // runs off the end: finished
    CHECK(t.e().pathFinished);
    CHECK(t.f(F_WP_WAIT) == 3.5f);
    t.run(); // a finished path deactivates its entity
    CHECK((t.e().rt & RT_ACTIVE) == 0);

    // Latent form reports done at the waypoint.
    Rig r;
    Asm s;
    s.entry(EntryPoint::Main);
    s.movImm(0, 1.0f);
    s.lcall("lMoveToNextWP");
    s.addSelf(21, 1.0f);
    s.end();
    r.script("scripts\\wp.scr", s);
    r.obj("t_wp {\n flag FL_TEMPORARY\n script \"scripts\\wp.scr\"\n}\n");
    r.start();
    int e = r.create("t_wp");
    r.e(e).path = &path;
    r.e(e).setF(F_WP_SPEED, 600.0f); // 10 units per frame
    // The think inside create ran main before the path was set: "no path" is done at
    // once, so update 1 runs the counter. Then the path starts: 10 units per update, the
    // waypoint at 80 units is reached on update 9 and the counter runs on update 10.
    r.step(9);
    CHECK(r.e(e).f(21) == 1.0f);
    r.step();
    CHECK(r.e(e).f(21) == 2.0f);
}

TEST_CASE("builtin RotateToNextWP / lRotateToNextWP: turn toward the segment's end") {
    Placement pl = straightPath(true);
    GamePath path;
    REQUIRE(path.build(pl));
    BT t;
    t.setup([](Asm& a) { a.call("RotateToNextWP", 4); });
    t.e().path = &path;
    t.e().setV3(F_ORIGIN, {420, 220, 0});
    t.e().setF(F_ANGLES + 2, 60.0f);
    t.e().setF(F_WP_TURN_RATE, 40.0f); // 20 per call
    t.run();
    CHECK(near(t.f(F_ANGLES + 2), 40.0f));
    t.run();
    t.run();
    CHECK(near(t.f(F_ANGLES + 2), 0.0f, 1e-3f));

    BT l;
    l.setup([](Asm& a) { a.lcall("lRotateToNextWP", 4); });
    l.e().path = &path;
    l.e().setV3(F_ORIGIN, {420, 220, 0});
    l.e().setF(F_ANGLES + 2, 10.0f);
    l.e().setF(F_WP_TURN_RATE, 40.0f);
    l.run();
    CHECK(near(l.f(F_ANGLES + 2), 0.0f, 1e-3f)); // snapped within one step
}

// ---------------------------------------------------------------------------------------
// D. Weapons and damage.
// ---------------------------------------------------------------------------------------

namespace {
const char* kWeaponObjects =
    "t_bullet {\n flag FL_TEMPORARY\n damage 10\n model \"models\\bullet.mdl\"\n}\n"
    "t_flash {\n flag FL_TEMPORARY\n}\n";
} // namespace

TEST_CASE("builtin Shoot: projectile, angles, speed, damage scaling, flash") {
    BT t;
    t.r.weapons = "w_test {\n missile \"t_bullet\"\n flash \"t_flash\"\n speed 100\n}\n";
    t.r.model("models\\gun.mdl", {-10, -10, -10}, {10, 10, 10}, {{"tag_gun", {0, 5, 0}}});
    t.r.model("models\\bullet.mdl", {-1, -2, -1}, {1, 2, 1});
    t.setup(
        [](Asm& a) {
            a.movStr(0, "w_test");
            a.movStr(1, "tag_gun");
            a.leaGlobal(2, "self", 17);
            a.call("Shoot", 4);
        },
        kWeaponObjects, false, " model \"models\\gun.mdl\"\n");
    t.e().setV3(17, {0, 10, 0}); // fire along +y
    t.e().rt &= ~RT_COLLIDABLE;
    int before = t.r.world.listCount();
    t.run(); // off screen: nothing
    CHECK(t.r.world.listCount() == before);
    t.e().rt |= RT_COLLIDABLE;
    t.run();
    REQUIRE(t.r.world.listCount() == before + 2);
    std::vector<int> list = t.r.world.listEntities();
    const Entity& flash = t.r.e(list[0]);
    const Entity& p = t.r.e(list[1]);
    CHECK(flash.name == "t_flash");
    CHECK(flash.parent == t.self);
    CHECK(flash.absAttach);
    CHECK(t.e().attachRefCount == 1);
    CHECK(p.name == "t_bullet");
    CHECK(p.f(F_CLASS) == kClassProjectile);
    CHECK(near(p.f(F_VELOCITY + 1), 100.0f));
    CHECK(near(p.f(F_ANGLES + 2), 0.0f)); // nose (+y) along +y
    // Muzzle = self base origin (640, 300, 0) + tag (0, 5, 0); pushed forward by -min.y = 2.
    CHECK(near(p.f(F_ORIGIN + 1), 307.0f, 1e-3f));
    CHECK(near(p.f(F_DAMAGE), 10.0f * 0.8f)); // not a player: scaled by g_damage_factor
}

TEST_CASE("builtin Damage: amount times g_damage_factor, handler runs") {
    BT t;
    t.setup([](Asm& a) {
        a.getSelf(0, 27);
        a.movImm(1, 10.0f);
        a.call("Damage", 4);
    }, "t_en {\n enemy\n flag FL_TEMPORARY\n health 100\n}\n");
    int en = t.r.create("t_en");
    t.e().fields[27] = t.r.world.refOf(en);
    t.run();
    CHECK(near(t.r.e(en).f(F_HEALTH), 92.0f));
}

TEST_CASE("builtin RadialDamage: grows toward the rim, enemies only") {
    BT t;
    t.setup([](Asm& a) {
        a.leaGlobal(0, "self", 5);
        a.movImm(1, 100.0f);
        a.movImm(2, 40.0f);
        a.call("RadialDamage", 4);
    }, "t_en {\n enemy\n flag FL_TEMPORARY\n health 100\n}\n t_neutral {\n flag FL_TEMPORARY\n health 100\n}\n");
    t.e().setV3(F_ORIGIN, {600, 300, 0});
    int near50 = t.r.create("t_en", {650, 300, 0});
    int center = t.r.create("t_en", {600, 300, 0});
    int far = t.r.create("t_en", {800, 300, 0});
    int neutral = t.r.create("t_neutral", {650, 300, 0});
    t.run();
    // 0.5 s * 40 * (50 / 100) * 0.8 = 8
    CHECK(near(t.r.e(near50).f(F_HEALTH), 92.0f));
    CHECK(t.r.e(center).f(F_HEALTH) == 100.0f);
    CHECK(t.r.e(far).f(F_HEALTH) == 100.0f);
    CHECK(t.r.e(neutral).f(F_HEALTH) == 100.0f);
}

namespace {
// An on-screen model enemy with a real rectangle.
int onScreenEnemy(BT& t, Vec3 pos) {
    int en = t.r.create("t_en", pos);
    Entity& e = t.r.e(en);
    e.boundsMin = {-20, -20, -20};
    e.boundsMax = {20, 20, 20};
    e.radius = 35.0f;
    t.r.world.computeScreenBounds(en);
    return en;
}
} // namespace

TEST_CASE("builtin TraceLine / TraceLineDamage: screen-space segment") {
    BT t;
    t.setup(
        [](Asm& a) {
            a.leaGlobal(0, "self", 5);
            a.leaGlobal(1, "self", 17);
            a.movImm(2, 1.0f);
            callStore(a, "TraceLine");
            a.leaGlobal(0, "self", 5);
            a.leaGlobal(1, "self", 17);
            a.movImm(2, 7.0f);
            a.call("TraceLineDamage", 4);
        },
        "t_en {\n enemy\n flag FL_TEMPORARY\n health 100\n}\n", false, " touch TOUCH_ENEMIES\n");
    int en = onScreenEnemy(t, {640, 400, 0});
    REQUIRE((t.r.e(en).rt & RT_COLLIDABLE) != 0);
    t.e().setV3(F_ORIGIN, {640, 300, 0});
    t.e().setV3(17, {640, 500, 0}); // the segment passes through the enemy
    t.run();
    CHECK(t.e().fields[26] == t.r.world.refOf(en));
    CHECK(t.r.e(en).f(F_HEALTH) == 93.0f); // unscaled
    t.e().setV3(17, {500, 300, 0});        // misses
    t.run();
    CHECK(t.e().fields[26] == 0u);
    CHECK(t.r.e(en).f(F_HEALTH) == 93.0f);
}

TEST_CASE("builtin Lightning: on-screen living enemies within 500") {
    BT t;
    t.setup([](Asm& a) { a.call("Lightning", 4); }, "t_en {\n enemy\n flag FL_TEMPORARY\n health 100\n}\n", false,
            " damage 20\n");
    t.e().setV3(F_ORIGIN, {640, 300, 0});
    int en = onScreenEnemy(t, {640, 400, 0});
    t.run();
    CHECK(near(t.r.e(en).f(F_HEALTH), 100.0f - 20.0f * kFt * 0.8f));
}

TEST_CASE("builtin LockTarget: nearest enemy ahead of the player") {
    BT t;
    t.setup([](Asm& a) { callStore(a, "LockTarget"); }, "t_en {\n enemy\n flag FL_TEMPORARY\n health 100\n}\n", true);
    int pe = t.r.world.playerEntityIndex(0);
    REQUIRE(pe >= 0);
    t.r.e(pe).setV3(F_ORIGIN, {640, 150, 100});
    int ahead = onScreenEnemy(t, {640, 450, 0});
    int nearer = onScreenEnemy(t, {640, 350, 0});
    int behind = onScreenEnemy(t, {640, 140, 0});
    (void)ahead;
    (void)behind;
    t.run();
    CHECK(t.e().fields[26] == t.r.world.refOf(nearer));
    CHECK((t.r.e(nearer).rt & RT_LOCKED) != 0);
}

TEST_CASE("builtin PushPlayer: 4000 * frametime away from self in the ground plane") {
    BT t;
    t.setup([](Asm& a) { a.call("PushPlayer", 4); }, "", true);
    int pe = t.r.world.playerEntityIndex(0);
    REQUIRE(pe >= 0);
    t.e().setV3(F_ORIGIN, {600, 300, 0});
    t.r.e(pe).setV3(F_ORIGIN, {700, 300, 50});
    t.r.e(pe).setV3(F_VELOCITY, {0, 0, 0});
    t.run();
    CHECK(near(t.r.e(pe).f(F_VELOCITY), 2000.0f));
    CHECK(t.r.e(pe).f(F_VELOCITY + 1) == 0.0f);
}

// ---------------------------------------------------------------------------------------
// E. Effects.
// ---------------------------------------------------------------------------------------

TEST_CASE("builtin PlaceLight: per-frame queue, at most 32") {
    BT t;
    t.setup([](Asm& a) {
        for (int i = 0; i < 40; ++i) {
            a.leaGlobal(0, "self", 5);
            a.leaGlobal(1, "self", 28);
            a.movImm(2, 200.0f);
            a.call("PlaceLight", 4);
        }
    });
    t.run();
    REQUIRE(t.r.world.lights().size() == 32);
    CHECK(t.r.world.lights()[0].radius == 200.0f);
    CHECK(t.r.world.lights()[0].color.x == 1.0f);
    t.r.step();
    CHECK(t.r.world.lights().empty());
}

TEST_CASE("builtin CameraQuake") {
    BT t;
    t.setup([](Asm& a) {
        a.movImm(0, 1.4f);
        a.call("CameraQuake", 4);
    });
    t.run();
    CHECK(t.r.world.camera().quakeAmplitude == 1.4f);
    CHECK(t.r.world.camera().quakeClock == 3.0f);
    t.r.step(2);
    CHECK(t.r.world.camera().field[4] != 0.0f); // yaw shakes
}

TEST_CASE("builtin StartSound / StartLoopingSound / StopLoopingSound: sound events") {
    BT t;
    t.setup([](Asm& a) {
        a.movStr(0, "sounds/a.wav");
        a.call("StartSound", 4);
        a.movStr(0, "sounds/loop.wav");
        a.call("StartLoopingSound", 4);
        a.call("StopLoopingSound", 4);
        a.call("StopLoopingSound", 4); // nothing to stop
    });
    t.run();
    const auto& ev = t.r.world.soundEvents();
    REQUIRE(ev.size() == 3);
    CHECK(ev[0].kind == SoundEvent::Kind::Play);
    CHECK(ev[0].sample == "sounds/a.wav");
    CHECK(ev[1].kind == SoundEvent::Kind::Loop);
    CHECK(ev[2].kind == SoundEvent::Kind::StopLoop);
    CHECK(t.e().loopSound.empty());
}

// ---------------------------------------------------------------------------------------
// F. Level flow and player record.
// ---------------------------------------------------------------------------------------

TEST_CASE("builtin EndLevel: paused, level complete") {
    BT t;
    t.setup([](Asm& a) { a.call("EndLevel", 4); });
    t.run();
    CHECK(t.r.world.levelComplete());
    CHECK(t.r.world.paused());
}

TEST_CASE("builtin ShowTutorialHint: pauses until confirmed") {
    BT t;
    t.setup([](Asm& a) {
        a.movStr(0, "Line one^Line two");
        a.call("ShowTutorialHint", 4);
    });
    t.run();
    CHECK(t.r.world.hintShowing());
    CHECK(t.r.world.paused());
    CHECK(t.r.world.hintText() == "Line one^Line two");
    PlayerInput in;
    in.confirm = true;
    t.r.world.step(in);
    CHECK_FALSE(t.r.world.paused());
}

TEST_CASE("builtin G_AddMissiles / G_GetMissiles / G_UseMissile") {
    BT t;
    t.setup([](Asm& a) {
        a.movImm(0, 2.9f); // truncated to 2
        a.movImm(1, 150.0f);
        a.call("G_AddMissiles", 4);
        a.movImm(0, 4.0f);
        a.movImm(1, 1.0f);
        a.call("G_AddMissiles", 4);
        a.movImm(0, -1.0f); // unsigned range test: nothing
        a.movImm(1, 1.0f);
        a.call("G_AddMissiles", 4);
        callStore(a, "G_GetMissiles");
    }, "", true);
    t.run();
    PlayerRecord& p = t.r.world.player(0);
    CHECK(p.missiles[2] == 99);
    CHECK(p.missiles[4] == 1);
    CHECK(t.result() == 2.0f);

    // Use: consumes, and selects the next kind when the count runs out.
    Rig r;
    Asm s;
    s.entry(EntryPoint::Callback);
    s.call("G_UseMissile", 4);
    s.setSelfSlot(26, 4);
    s.end();
    r.script("scripts\\use.scr", s);
    r.obj("t_use {\n flag FL_TEMPORARY\n script \"scripts\\use.scr\"\n}\n");
    r.start(true);
    int e = r.create("t_use");
    PlayerRecord& pr = r.world.player(0);
    pr.missiles[2] = 1;
    pr.missiles[4] = 3;
    pr.currentMissile = 2;
    r.world.runCallback(e, 0, 0, 0);
    CHECK(r.e(e).f(26) == 1.0f);
    CHECK(pr.missiles[2] == 0);
    CHECK(pr.currentMissile == 4);
    pr.missiles[4] = 0;
    r.world.runCallback(e, 0, 0, 0);
    CHECK(r.e(e).f(26) == 0.0f); // empty selection: 0, no change
    CHECK(pr.currentMissile == 4);
}

TEST_CASE("builtin G_AddPowerUp / G_GetPowerUp / G_UsePowerUp") {
    BT t;
    t.setup([](Asm& a) {
        a.movImm(0, 1.0f);
        a.movImm(1, 1.0f);
        a.call("G_AddPowerUp", 4);
        a.movImm(0, 3.0f);
        a.movImm(1, 4.0f);
        a.call("G_AddPowerUp", 4);
        a.call("G_UsePowerUp", 4);
        a.setSelfSlot(27, 4);
        callStore(a, "G_GetPowerUp");
    }, "", true);
    t.run();
    PlayerRecord& p = t.r.world.player(0);
    CHECK(t.f(27) == 1.0f);   // consumed the single power-up 1
    CHECK(p.powerups[1] == 0);
    CHECK(t.result() == 3.0f); // then selected the next kind that has some
}

TEST_CASE("builtin G_GetUpgrade / G_SetUpgrade: truncated, unsigned range") {
    BT t;
    t.setup([](Asm& a) {
        a.movImm(0, 2.9f);
        a.movImm(1, 3.9f);
        a.call("G_SetUpgrade", 4);
        a.movImm(0, 25.0f);
        a.movImm(1, 1.0f);
        a.call("G_SetUpgrade", 4); // out of range: nothing
        a.movImm(0, 2.5f);
        callStore(a, "G_GetUpgrade");
        a.movImm(0, -1.0f);
        a.call("G_GetUpgrade", 4);
        a.setSelfSlot(27, 4);
    }, "", true);
    t.run();
    CHECK(t.r.world.player(0).upgrades[2] == 3);
    CHECK(t.result() == 3.0f);
    CHECK(t.f(27) == 0.0f);
}

TEST_CASE("builtin PlayerFreezeHealth / PlayerDisableAction") {
    BT t;
    t.setup([](Asm& a) {
        a.movImm(0, 1.0f);
        a.call("PlayerFreezeHealth", 4);
        a.movImm(0, 1.0f);
        a.call("PlayerFreezeHealth", 4);
        a.movImm(0, 0.0f);
        a.call("PlayerFreezeHealth", 4);
        a.movImm(0, 1.0f);
        a.call("PlayerDisableAction", 4);
    }, "", true);
    t.r.world.player(0).action = 16.0f;
    t.run();
    CHECK(t.r.world.player(0).freezeCount == 1);
    CHECK(t.r.world.player(0).actionsDisabled);
    CHECK(t.r.world.player(0).action == 0.0f);
}

TEST_CASE("builtin RespawnPlayer: new helicopter entity, old one removed") {
    Rig r;
    Asm s;
    s.entry(EntryPoint::Callback);
    s.call("RespawnPlayer", 4);
    s.end();
    r.script("scripts\\pl.scr", s);
    r.playerScript = "scripts\\pl.scr";
    r.start(true);
    int old = r.world.playerEntityIndex(0);
    REQUIRE(old >= 0);
    r.world.player(0).freezeCount = 3;
    r.world.runCallback(old, 0, 0, 0);
    int now = r.world.playerEntityIndex(0);
    CHECK(now >= 0);
    CHECK(now != old);
    CHECK((r.e(old).rt & RT_REMOVED) != 0);
    CHECK(r.world.player(0).freezeCount == 0);
    // With no lives left the old entity is only marked dead.
    r.world.player(0).lives = -1.0f;
    r.world.runCallback(now, 0, 0, 0);
    CHECK(r.e(now).f(F_DEAD) == 1.0f);
    CHECK(r.world.playerEntityIndex(0) == -1);
}

// ---------------------------------------------------------------------------------------
// Table integrity.
// ---------------------------------------------------------------------------------------

TEST_CASE("builtins: every table entry matches the documented arity") {
    for (size_t i = 0; i < gameBuiltinCount(); ++i) {
        const BuiltinDesc& d = gameBuiltinAt(i);
        const BuiltinMeta* m = findBuiltinMeta(d.name);
        INFO(d.name);
        REQUIRE(m != nullptr);
        CHECK(d.argCount == m->arity);
    }
    CHECK(gameBuiltinCount() == builtinMetaCount()); // all 85 are implemented
}
