// AirStrike 2's builtins (docs/spec/as2/rcsl-builtins-semantics.delta.md): one test per new
// builtin and per changed one, on synthetic scripts in a synthetic world run under the as2
// rules, like builtins_test.cpp does for the first game; TerraMorph on a synthetic terrain;
// the per-game tables (85 / 24 for as3d, 101 / 28 for the sequels); with the as2 data, every
// shipped script binds without an unknown builtin or global.
#include "doctest.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "as3d/game_data.h"
#include "as3d/input.h"
#include "as3d/script_host.h"
#include "test_data.h"
#include "world_test_util.h"

using namespace worldtest;

namespace {

constexpr float kFt = 0.5f;

const GameRules& as2Rules() { return gameProfile(GameId::AirStrike2).rules; }

// The as2 helicopters the rules name (player_2 is WorldConfig's default heli index 1).
const char* kAs2Helis =
    "player_1 {\n player\n flag FL_TEMPORARY\n health 500\n}\n"
    "player_2 {\n player\n flag FL_TEMPORARY\n health 400\n}\n";

// builtins_test.cpp's rig under a chosen game: `self` runs `body` as its callback.
struct BT {
    Rig r;
    int self = -1;
    Asm a;

    explicit BT(GameId game = GameId::AirStrike2) {
        r.config.rules = &gameProfile(game).rules;
        r.obj(kAs2Helis);
    }
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

void callStore(Asm& a, const std::string& name) {
    a.call(name, 4);
    a.setSelfSlot(26, 4);
}

float scalar(const std::string& name, std::vector<float> args, GameId game = GameId::AirStrike2) {
    BT t(game);
    t.setup([&](Asm& a) {
        for (size_t k = 0; k < args.size(); ++k) a.movImm(static_cast<int>(k), args[k]);
        callStore(a, name);
    });
    t.run();
    CHECK(t.r.world.stats().scriptErrors == 0);
    return t.result();
}

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// An on-screen model entity with a real rectangle (builtins_test.cpp's onScreenEnemy).
int onScreen(BT& t, const std::string& name, Vec3 pos, float cls = -1.0f) {
    int en = t.r.create(name, pos);
    Entity& e = t.r.e(en);
    if (cls >= 0.0f) e.setF(F_CLASS, cls);
    e.boundsMin = {-20, -20, -20};
    e.boundsMax = {20, 20, 20};
    e.radius = 35.0f;
    t.r.world.computeScreenBounds(en);
    return en;
}

const char* kTargets = "t_en {\n enemy\n flag FL_TEMPORARY\n health 100\n}\n"
                       "t_civ {\n civilian\n flag FL_TEMPORARY\n health 100\n}\n";

} // namespace

// ---------------------------------------------------------------------------------------
// Tables per game.
// ---------------------------------------------------------------------------------------

TEST_CASE("sequel tables: as3d keeps 85 builtins and 24 globals at their addresses") {
    CHECK(gameBuiltinCount(GameId::AirStrike3D) == 85);
    CHECK(gameBuiltinCount() == 85);
    CHECK(scriptGlobalCount(GameId::AirStrike3D) == 24);
    const char* const v170[24] = {"self", "other", "cb_msg", "cb_parm1", "cb_parm2", "player", "p_action",
                                  "p_scores", "p_lives", "p_stars", "p_speedfactor", "p_counter1", "p_counter2",
                                  "p_counter3", "p_weapon", "l_night", "l_water", "l_waterlevel", "frametime",
                                  "time", "camera", "g_map_pos", "g_damage_factor", "g_health_factor"};
    for (int i = 0; i < 24; ++i) {
        INFO(v170[i]);
        CHECK(std::string(scriptGlobalName(GameId::AirStrike3D, i)) == v170[i]);
        CHECK(scriptGlobalAddress(GameId::AirStrike3D, v170[i]) == kGlobalAddrBase + 0x10u * static_cast<u32>(i));
        // The sequels keep every address.
        CHECK(scriptGlobalAddress(GameId::AirStrike2, v170[i]) == kGlobalAddrBase + 0x10u * static_cast<u32>(i));
    }
    CHECK(scriptGlobalAddress(GameId::AirStrike3D, "player1") == 0u);
    CHECK(scriptGlobalAddress(GameId::AirStrike3D, "p_maxHealth") == 0u);
    CHECK(findGameBuiltin(GameId::AirStrike3D, "TerraMorph") == nullptr);
    REQUIRE(findGameBuiltin(GameId::AirStrike3D, "Lightning") != nullptr);
    CHECK(findGameBuiltin(GameId::AirStrike3D, "Lightning")->argCount == 0);
}

TEST_CASE("sequel tables: as2 and gulf have 101 builtins and 28 globals, in the executable's order") {
    for (GameId g : {GameId::AirStrike2, GameId::GulfThunder}) {
        CHECK(gameBuiltinCount(g) == 101);
        CHECK(scriptGlobalCount(g) == 28);
        BuiltinMetaTable meta = builtinMetaTable(gameProfile(g).builtinSet);
        REQUIRE(meta.count == 101);
        for (size_t i = 0; i < gameBuiltinCount(g); ++i) {
            const BuiltinDesc& d = gameBuiltinAt(g, i);
            INFO(d.name);
            CHECK(std::string(d.name) == meta.rows[i].name);
            CHECK(d.argCount == meta.rows[i].arity);
            CHECK(d.status != BuiltinStatus::Stub);
        }
    }
    CHECK(findGameBuiltin(GameId::AirStrike2, "Lightning")->argCount == 1);
    const char* const as2[28] = {"self", "other", "cb_msg", "cb_parm1", "cb_parm2", "player", "player1", "player2",
                                 "p_action", "p_maxHealth", "p_scores", "p_lives", "p_stars", "p_speedfactor",
                                 "p_counter1", "p_counter2", "p_counter3", "p_weapon", "l_night", "l_water",
                                 "l_waterlevel", "frametime", "time", "camera", "cameramode", "g_map_pos",
                                 "g_damage_factor", "g_health_factor"};
    for (int i = 0; i < 28; ++i) CHECK(std::string(scriptGlobalName(GameId::AirStrike2, i)) == as2[i]);
    CHECK(scriptGlobalAddress(GameId::AirStrike2, "player1") == kGlobalAddrBase + 0x10u * 24u);
    CHECK(scriptGlobalAddress(GameId::AirStrike2, "cameramode") == kGlobalAddrBase + 0x10u * 27u);
}

TEST_CASE("sequel tables: a world takes its game from the rules") {
    Rig a;
    a.start();
    CHECK(a.world.game() == GameId::AirStrike3D);
    Rig b;
    b.config.rules = &as2Rules();
    b.start();
    CHECK(b.world.game() == GameId::AirStrike2);
    Rig c;
    c.config.game = static_cast<int>(GameId::GulfThunder);
    c.start();
    CHECK(c.world.game() == GameId::GulfThunder);
}

TEST_CASE("sequel globals: an unknown global binds to a scratch cell, reported once") {
    for (GameId g : {GameId::AirStrike3D, GameId::AirStrike2}) {
        BT t(g);
        t.setup([](Asm& a) {
            a.movImm(0, 5.0f);
            a.emit(OP_MOV, 0, a.g("no_such_global"), 0); // no_such_global = 5
            a.movGlobal(1, "no_such_global");
            a.setSelfSlot(26, 1);
        });
        t.run();
        CHECK(t.r.world.stats().scriptErrors == 0);
        CHECK(t.result() == 5.0f);
        const auto& u = t.r.world.host().unknownGlobals();
        REQUIRE(u.size() == 1);
        CHECK(u[0].name == "no_such_global");
        CHECK(u[0].binds >= 1);
    }
}

TEST_CASE("sequel globals: player1, player2, p_maxHealth, cameramode") {
    BT t;
    t.r.config.players = 2;
    t.setup([](Asm& a) {
        a.movGlobal(0, "player1");
        a.setSelfSlot(26, 0);
        a.movGlobal(0, "player2");
        a.setSelfSlot(27, 0);
        a.movGlobal(0, "cameramode");
        a.setSelfSlot(20, 0);
        a.movImm(0, 321.0f);
        a.emit(OP_MOV, 0, a.g("p_maxHealth"), 0);
        a.movImm(0, 0.0f);
        a.emit(OP_MOV, 0, a.g("cameramode"), 0);
    }, "", true);
    t.run();
    CHECK(t.e().fields[26] == t.r.world.player(0).entityRef);
    CHECK(t.e().fields[27] == t.r.world.player(1).entityRef); // the other player of index 0
    CHECK(t.f(20) == 1.0f);                                     // initial value
    CHECK(t.r.world.player(0).maxHealthGlobal == 321.0f);
    CHECK(t.r.world.cameraModeGlobal == 0.0f);
    // An entity of player index 1 sees player 1 as `player2`.
    t.e().playerIndex = 1;
    t.run();
    CHECK(t.e().fields[27] == t.r.world.player(0).entityRef);
    CHECK(t.r.world.player(1).maxHealthGlobal == 321.0f);
}

// ---------------------------------------------------------------------------------------
// A. Math.
// ---------------------------------------------------------------------------------------

TEST_CASE("sequel atan (changed): degrees of atan(x)") {
    CHECK(near(scalar("atan", {1.0f}), 45.0f));
    CHECK(near(scalar("atan", {-1.0f}), -45.0f));
    // The first game keeps its quirk.
    CHECK(near(scalar("atan", {45.0f}, GameId::AirStrike3D), std::atan(45.0f * 3.14159265f / 180.0f)));
}

TEST_CASE("sequel atan2: degrees in (-180, 180]") {
    CHECK(near(scalar("atan2", {1.0f, 1.0f}), 45.0f));
    CHECK(near(scalar("atan2", {0.0f, -1.0f}), 180.0f));
    CHECK(near(scalar("atan2", {-1.0f, 0.0f}), -90.0f));
    CHECK(scalar("atan2", {0.0f, 0.0f}) == 0.0f);
}

TEST_CASE("sequel copysign") {
    CHECK(scalar("copysign", {3.0f, -1.0f}) == -3.0f);
    CHECK(scalar("copysign", {-3.0f, 2.0f}) == 3.0f);
    CHECK(scalar("copysign", {3.0f, -0.0f}) == -3.0f);
}

TEST_CASE("sequel floor") {
    CHECK(scalar("floor", {2.7f}) == 2.0f);
    CHECK(scalar("floor", {-2.2f}) == -3.0f);
}

TEST_CASE("sequel floor2: toward zero to a multiple of the step") {
    CHECK(scalar("floor2", {7.0f, 5.0f}) == 5.0f);
    CHECK(scalar("floor2", {-7.0f, 5.0f}) == -5.0f);
    CHECK(std::isnan(scalar("floor2", {7.0f, 0.0f})));
}

TEST_CASE("sequel fmod: sign of x") {
    CHECK(scalar("fmod", {7.0f, 5.0f}) == 2.0f);
    CHECK(scalar("fmod", {-7.0f, 5.0f}) == -2.0f);
    CHECK(std::isnan(scalar("fmod", {7.0f, 0.0f})));
}

// ---------------------------------------------------------------------------------------
// B. Entities.
// ---------------------------------------------------------------------------------------

TEST_CASE("sequel create (changed): returns the new entity even after a RET in its init") {
    for (int withRet = 0; withRet < 2; ++withRet) {
        BT t;
        Asm child;
        child.entry(EntryPoint::Init);
        if (withRet) child.emit(OP_RET, M_IMM1, imm(7.0f));
        else child.end();
        t.r.script("scripts\\child.scr", child);
        t.setup(
            [](Asm& a) {
                a.movStr(0, "t_child");
                a.leaGlobal(1, "self", 5);
                callStore(a, "create");
            },
            "t_child {\n flag FL_TEMPORARY\n script \"scripts\\child.scr\"\n}\n");
        t.run();
        INFO("with RET " << withRet);
        int c = t.r.world.liveIndexFromRef(t.e().fields[26]);
        REQUIRE(c >= 0);
        CHECK(t.r.e(c).name == "t_child");
    }
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

TEST_CASE("sequel AttachEntity (changed): a null parent does nothing") {
    BT t;
    t.setup([](Asm& a) {
        a.getSelf(0, 27);
        a.movImm(1, 0.0f);
        a.movStr(2, "origin");
        a.movImm(3, 1.0f);
        a.call("AttachEntity", 4);
    }, "t_part {\n flag FL_TEMPORARY\n}\n");
    int part = t.r.create("t_part");
    t.e().fields[27] = t.r.world.refOf(part);
    t.run();
    CHECK(t.r.e(part).parent == -1);
    CHECK((t.r.e(part).rt & RT_ATTACHED_ENTITY) == 0);
    CHECK(t.r.world.stats().scriptErrors == 0);
}

TEST_CASE("sequel DetachEntity: the undo of AttachEntity") {
    BT t;
    t.setup([](Asm& a) {
        a.getSelf(0, 27);
        a.call("DetachEntity", 4);
    }, "t_part {\n flag FL_TEMPORARY\n}\n");
    int part = t.r.create("t_part", {600, 350, 0});
    t.r.world.attachEntity(part, t.self, "origin", true);
    REQUIRE(t.e().attachRefCount == 1);
    t.e().setV3(F_ANGLES, {10, 20, 30});
    t.e().fields[27] = t.r.world.refOf(part);
    t.run();
    const Entity& p = t.r.e(part);
    CHECK(p.parent == -1);
    CHECK(p.tagName.empty());
    CHECK_FALSE(p.absAttach);
    CHECK_FALSE(p.countedInRoot);
    CHECK(t.e().attachRefCount == 0);
    CHECK(p.f(F_ANGLES + 2) == 30.0f); // the parent's own angles
    CHECK((p.rt & RT_ATTACHED_ENTITY) != 0); // not cleared, as in the original
    t.run(); // twice: harmless
    CHECK(t.e().attachRefCount == 0);
    // Null reference: nothing, no error.
    t.e().fields[27] = 0;
    t.run();
    CHECK(t.r.world.stats().scriptErrors == 0);
}

// ---------------------------------------------------------------------------------------
// C. Movement and terrain.
// ---------------------------------------------------------------------------------------

TEST_CASE("sequel RotateTo / lRotateTo (changed): null target, nothing turns, done 0") {
    BT t;
    t.setup([](Asm& a) {
        a.movImm(0, 0.0f);
        a.movImm(1, 1.0f);
        a.call("RotateTo", 4);
    });
    t.e().setF(F_WP_TURN_RATE, 90.0f);
    t.e().setF(F_ANGLES + 2, 12.0f);
    t.run();
    CHECK(t.f(F_ANGLES + 2) == 12.0f);
    CHECK(t.r.world.stats().scriptErrors == 0);
    // lRotateTo on a 0 target waits until its timeout.
    Rig r;
    r.config.rules = &as2Rules();
    Asm s;
    s.entry(EntryPoint::Main);
    s.tmo(0.05f);
    s.movImm(0, 0.0f);
    s.movImm(1, 1.0f);
    s.lcall("lRotateTo", 4);
    s.addSelf(27, 1.0f);
    s.end();
    r.script("scripts\\rt.scr", s);
    r.obj("t_rt {\n flag FL_TEMPORARY\n script \"scripts\\rt.scr\"\n}\n");
    r.start();
    int e = r.create("t_rt");
    r.e(e).setF(27, 0.0f);
    r.step(1);
    CHECK(r.e(e).f(27) == 0.0f); // still waiting after one 1/60 s frame
    r.step(5);
    CHECK(r.e(e).f(27) >= 1.0f); // the timeout ended it
}

namespace {
// A synthetic terrain: W x H cells, every raw height 128, levels.txt hmin..hmax.
std::unique_ptr<LoadedLevel> flatLevel(int w, int h, float hmin, float hmax, bool water = false, float waterLevel = 0.0f) {
    std::unique_ptr<LoadedLevel> l(new LoadedLevel());
    l->data.width = static_cast<u32>(w);
    l->data.height = static_cast<u32>(h);
    l->data.cells.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (LevelCell& c : l->data.cells) c.height = 128;
    l->style.hmin = hmin;
    l->style.hmax = hmax;
    l->style.hasWater = water;
    l->style.waterLevel = waterLevel;
    return l;
}

// An uncompressed type 3 (greyscale) 8-bit TGA, bottom-up unless `topDown`; `rows` in file
// order (rows[0] is stored first).
Blob greyTga(int w, int h, const std::vector<u8>& fileOrder, bool topDown = false) {
    Blob b(18, 0);
    b[2] = 3;
    b[12] = static_cast<u8>(w);
    b[13] = static_cast<u8>(w >> 8);
    b[14] = static_cast<u8>(h);
    b[15] = static_cast<u8>(h >> 8);
    b[16] = 8;
    b[17] = topDown ? 0x28 : 0x08;
    b.insert(b.end(), fileOrder.begin(), fileOrder.end());
    return b;
}

struct MorphRig {
    Rig r;
    float hmin = -100.0f, hmax = 155.0f; // one grey level = 1 unit
    explicit MorphRig(bool water = false, float waterLevel = 0.0f, int w = 8, int h = 8,
                      const std::function<void(Rig&)>& before = nullptr) {
        r.config.rules = &as2Rules();
        if (before) before(r);
        r.start();
        REQUIRE(r.world.startLevel(flatLevel(w, h, hmin, hmax, water, waterLevel), 1));
        REQUIRE(r.world.terrain() != nullptr);
    }
    float z(int c, int row) const { return r.world.terrain()->vertexZ(c, row); }
    float base() const { return (hmax - hmin) * (128.0f / 255.0f) + hmin; }
};
} // namespace

TEST_CASE("sequel TerraMorph: additive heights, 128 neutral, placement by truncation, file order") {
    MorphRig m;
    // A 3x2 stamp, file rows: [128, 138, 118] then [128, 128, 228].
    m.r.src->add("morphmaps/t.tga", greyTga(3, 2, {128, 138, 118, 128, 128, 228}));
    const float z0 = m.base();
    CHECK(m.z(3, 3) == z0);
    const u32 rev0 = m.r.world.terrainRevision();
    // pos (4.5 * 40, 3.9 * 40): c0 = trunc(4.5 - 1) = 3, r0 = trunc(3.9 - 1) = 2.
    REQUIRE(m.r.world.terraMorph(180.0f, 156.0f, "morphmaps/t.tga"));
    CHECK(m.z(3, 2) == z0);          // pixel (0, 0) = 128: neutral
    CHECK(near(m.z(4, 2), z0 + 10.0f)); // pixel (1, 0) = 138
    CHECK(near(m.z(5, 2), z0 - 10.0f)); // pixel (2, 0) = 118
    CHECK(near(m.z(5, 3), z0 + 100.0f)); // the second stored row is the next y
    CHECK(m.z(6, 2) == z0);
    // The height query sees the new ground at once.
    CHECK(near(m.r.world.terrainHeight(160.0f, 80.0f), z0 + 10.0f));
    // Dirty rectangle: the vertices written.
    std::vector<TerrainChange> ch;
    CHECK(m.r.world.terrainRevision() == rev0 + 1);
    REQUIRE(m.r.world.terrainChangesSince(rev0, ch));
    REQUIRE(ch.size() == 1);
    CHECK(ch[0].c0 == 3);
    CHECK(ch[0].r0 == 2);
    CHECK(ch[0].c1 == 5);
    CHECK(ch[0].r1 == 3);
    // Reading changes nothing; a reader up to date gets nothing.
    REQUIRE(m.r.world.terrainChangesSince(rev0, ch));
    CHECK(ch.size() == 1);
    REQUIRE(m.r.world.terrainChangesSince(rev0 + 1, ch));
    CHECK(ch.empty());
    // Accumulation, and the stamp is loaded once.
    REQUIRE(m.r.world.terraMorph(180.0f, 156.0f, "morphmaps/t.tga"));
    CHECK(near(m.z(4, 2), z0 + 20.0f));
    CHECK(m.r.world.terraMorphStampCount() == 1);
    // Normals are not recomputed.
    CHECK(m.r.world.terrain()->normals()[static_cast<size_t>(2 * 9 + 4)].z == doctest::Approx(1.0f));
}

TEST_CASE("sequel TerraMorph: a top-down stamp is still applied in file order") {
    MorphRig m;
    m.r.src->add("morphmaps/td.tga", greyTga(1, 2, {200, 100}, true));
    REQUIRE(m.r.world.terraMorph(40.0f, 80.0f, "morphmaps/td.tga")); // c0 = 1, r0 = trunc(2 - 1) = 1
    CHECK(near(m.z(1, 1), m.base() + 72.0f));
    CHECK(near(m.z(1, 2), m.base() - 28.0f));
}

TEST_CASE("sequel TerraMorph: edges, a stamp bigger than the terrain, truncation toward zero") {
    MorphRig m(false, 0.0f, 2, 2); // 3 x 3 vertices
    std::vector<u8> big(16 * 16, 129);
    m.r.src->add("morphmaps/big.tga", greyTga(16, 16, big));
    REQUIRE(m.r.world.terraMorph(40.0f, 40.0f, "morphmaps/big.tga"));
    for (int r = 0; r <= 2; ++r) {
        for (int c = 0; c <= 2; ++c) CHECK(near(m.z(c, r), m.base() + 1.0f));
    }
    // Negative positions: trunc(-0.5 - 0) = 0 (toward zero, not -1).
    MorphRig n;
    n.r.src->add("morphmaps/one.tga", greyTga(1, 1, {130}));
    REQUIRE(n.r.world.terraMorph(-20.0f, -20.0f, "morphmaps/one.tga"));
    CHECK(near(n.z(0, 0), n.base() + 2.0f));
    // Far outside: nothing written, no change recorded.
    const u32 rev = n.r.world.terrainRevision();
    CHECK(n.r.world.terraMorph(10000.0f, 10000.0f, "morphmaps/one.tga"));
    CHECK(n.r.world.terrainRevision() == rev);
}

TEST_CASE("sequel TerraMorph: underwater vertices never change") {
    MorphRig m(true, 0.0f); // base height ~ 28.5 above water; lower one vertex below it
    m.r.src->add("morphmaps/d.tga", greyTga(2, 1, {28, 28}));
    REQUIRE(m.r.world.terraMorph(40.0f, 40.0f, "morphmaps/d.tga")); // c0 = 0, r0 = 1
    const float after = m.z(0, 1);
    CHECK(after < 0.0f); // 100 levels down: under the water level now
    REQUIRE(m.r.world.terraMorph(40.0f, 40.0f, "morphmaps/d.tga"));
    CHECK(m.z(0, 1) == after); // skipped
}

TEST_CASE("sequel TerraMorph: missing, non-8-bit and too many stamps change nothing") {
    MorphRig m;
    CHECK_FALSE(m.r.world.terraMorph(100.0f, 100.0f, "morphmaps/none.tga"));
    CHECK(m.r.world.terraMorphStampCount() == 0);
    Blob b = greyTga(1, 1, {200});
    b[16] = 24;
    m.r.src->add("morphmaps/rgb.tga", b);
    CHECK_FALSE(m.r.world.terraMorph(100.0f, 100.0f, "morphmaps/rgb.tga"));
    for (int i = 0; i < kMaxTerraMorphStamps; ++i) {
        std::string name = "morphmaps/s" + std::to_string(i) + ".tga";
        m.r.src->add(name, greyTga(1, 1, {128}));
        CHECK(m.r.world.terraMorph(100.0f, 100.0f, name.c_str()));
    }
    m.r.src->add("morphmaps/extra.tga", greyTga(1, 1, {255}));
    CHECK_FALSE(m.r.world.terraMorph(100.0f, 100.0f, "morphmaps/extra.tga"));
    CHECK(m.z(2, 2) == m.base());
    // The level loader empties the table.
    REQUIRE(m.r.world.startLevel(flatLevel(8, 8, m.hmin, m.hmax), 1));
    CHECK(m.r.world.terraMorphStampCount() == 0);
    CHECK(m.r.world.terraMorph(100.0f, 100.0f, "morphmaps/extra.tga"));
}

TEST_CASE("sequel TerraMorph builtin: through a script, ground entities follow") {
    MorphRig m(false, 0.0f, 8, 8, [](Rig& r) {
        r.src->add("morphmaps/map2.tga", greyTga(1, 1, {178}));
        Asm s;
        s.entry(EntryPoint::Callback);
        s.leaGlobal(0, "self", 5);
        s.movStr(1, "morphmaps/map2.tga");
        s.call("TerraMorph", 4);
        s.end();
        r.script("scripts\\tm.scr", s);
        r.obj("t_tm {\n flag FL_TEMPORARY\n script \"scripts\\tm.scr\"\n}\n"
              "t_ground {\n flag FL_TEMPORARY\n flag FL_ONGROUND\n}\n");
    });
    int e = m.r.create("t_tm", {120.0f, 120.0f, 0.0f});
    int g = m.r.create("t_ground", {120.0f, 120.0f, 0.0f});
    REQUIRE(e >= 0);
    REQUIRE(g >= 0);
    m.r.world.runCallback(e, 0, 0, 0);
    CHECK(near(m.z(3, 3), m.base() + 50.0f));
    m.r.world.setupTransform(g);
    CHECK(near(m.r.e(g).f(F_ORIGIN + 2), m.base() + 50.0f));
}

TEST_CASE("sequel WaterHeight: terrain without water, water level over flooded vertices") {
    MorphRig dry;
    CHECK(near(dry.r.world.waterHeight(100.0f, 100.0f), dry.base()));
    MorphRig wet(true, 50.0f); // base ~28.5 is under 50
    CHECK(near(wet.r.world.waterHeight(100.0f, 100.0f), 50.0f));
    // The builtin.
    BT t;
    t.setup([](Asm& a) {
        a.movImm(0, 100.0f);
        a.movImm(1, 100.0f);
        callStore(a, "WaterHeight");
    });
    t.run();
    CHECK(t.result() == 0.0f); // the empty test level: flat ground at 0, no water
}

// ---------------------------------------------------------------------------------------
// D. Weapons and damage.
// ---------------------------------------------------------------------------------------

TEST_CASE("sequel Shoot (changed): a dead shooter does not fire") {
    for (GameId g : {GameId::AirStrike3D, GameId::AirStrike2}) {
        BT t(g);
        t.r.weapons = "w_test {\n missile \"t_bullet\"\n speed 100\n}\n";
        t.setup([](Asm& a) {
            a.movStr(0, "w_test");
            a.movStr(1, "origin");
            a.leaGlobal(2, "self", 17);
            a.call("Shoot", 4);
        }, "t_bullet {\n flag FL_TEMPORARY\n}\n");
        t.e().setV3(17, {0, 10, 0});
        t.e().rt |= RT_COLLIDABLE;
        t.e().setF(F_DEAD, 1.0f);
        int before = t.r.world.listCount();
        t.run();
        INFO(std::string(gameProfile(g).key));
        CHECK(t.r.world.listCount() == before + (g == GameId::AirStrike3D ? 1 : 0));
        t.e().setF(F_DEAD, 0.0f);
        t.run();
        CHECK(t.r.world.listCount() == before + (g == GameId::AirStrike3D ? 2 : 1));
    }
}

TEST_CASE("sequel RadialDamage (changed): civilians are hit too") {
    for (GameId g : {GameId::AirStrike3D, GameId::AirStrike2}) {
        BT t(g);
        t.setup([](Asm& a) {
            a.leaGlobal(0, "self", 5);
            a.movImm(1, 100.0f);
            a.movImm(2, 40.0f);
            a.call("RadialDamage", 4);
        }, kTargets);
        t.e().setV3(F_ORIGIN, {600, 300, 0});
        int en = t.r.create("t_en", {650, 300, 0});
        int civ = t.r.create("t_civ", {650, 300, 0});
        REQUIRE(t.r.e(civ).f(F_CLASS) == kClassCivilian); // `civilian` gives class 5
        t.run();
        INFO(std::string(gameProfile(g).key));
        CHECK(near(t.r.e(en).f(F_HEALTH), 92.0f));
        CHECK(near(t.r.e(civ).f(F_HEALTH), g == GameId::AirStrike3D ? 100.0f : 92.0f));
    }
}

TEST_CASE("sequel RadialDamagePlayer: the players, growing toward the rim") {
    BT t;
    t.setup([](Asm& a) {
        a.leaGlobal(0, "self", 5);
        a.movImm(1, 100.0f);
        a.movImm(2, 40.0f);
        a.call("RadialDamagePlayer", 4);
    }, kTargets, true);
    int pe = t.r.world.playerEntityIndex(0);
    REQUIRE(pe >= 0);
    t.e().setV3(F_ORIGIN, {600, 300, 0});
    t.r.e(pe).setV3(F_ORIGIN, {650, 300, 0});
    float h0 = t.r.e(pe).f(F_HEALTH);
    int en = t.r.create("t_en", {650, 300, 0});
    t.run();
    CHECK(near(t.r.e(pe).f(F_HEALTH), h0 - 8.0f)); // 0.5 * 40 * 0.5 * 0.8
    CHECK(t.r.e(en).f(F_HEALTH) == 100.0f);        // enemies are not considered
    t.r.e(pe).setV3(F_ORIGIN, {800, 300, 0});      // out of range
    t.run();
    CHECK(near(t.r.e(pe).f(F_HEALTH), h0 - 8.0f));
    t.r.world.player(0).freezeCount = 1; // invulnerable: nothing
    t.r.e(pe).setV3(F_ORIGIN, {650, 300, 0});
    t.run();
    CHECK(near(t.r.e(pe).f(F_HEALTH), h0 - 8.0f));
}

TEST_CASE("sequel TraceLine (changed): mask bit 4 civilians, mask 2 players only") {
    BT t;
    t.setup([](Asm& a) {
        a.leaGlobal(0, "self", 5);
        a.leaGlobal(1, "self", 17);
        a.getSelf(2, 27);
        callStore(a, "TraceLine");
    }, kTargets);
    int civ = onScreen(t, "t_civ", {640, 400, 0}, kClassCivilian);
    REQUIRE((t.r.e(civ).rt & RT_COLLIDABLE) != 0);
    t.e().setV3(F_ORIGIN, {640, 300, 0});
    t.e().setV3(17, {640, 500, 0});
    t.e().setF(27, 1.0f); // enemies only
    t.run();
    CHECK(t.e().fields[26] == 0u);
    t.e().setF(27, 4.0f);
    t.run();
    CHECK(t.e().fields[26] == t.r.world.refOf(civ));
    t.e().setF(27, 2.0f);
    t.run();
    CHECK(t.e().fields[26] == 0u);
    t.e().setF(27, 0.0f);
    t.run();
    CHECK(t.e().fields[26] == 0u);
}

TEST_CASE("sequel TraceLineDamage (changed): self's touch mode as a bit set") {
    BT t;
    t.setup([](Asm& a) {
        a.leaGlobal(0, "self", 5);
        a.leaGlobal(1, "self", 17);
        a.movImm(2, 7.0f);
        a.call("TraceLineDamage", 4);
    }, kTargets, true);
    int en = onScreen(t, "t_en", {640, 400, 0});
    int civ = onScreen(t, "t_civ", {630, 400, 0}, kClassCivilian);
    t.e().setV3(F_ORIGIN, {640, 300, 0});
    t.e().setV3(17, {640, 500, 0});
    t.e().touchMode = 4; // civilians only
    t.run();
    CHECK(t.r.e(en).f(F_HEALTH) == 100.0f);
    CHECK(t.r.e(civ).f(F_HEALTH) == 93.0f);
    t.e().touchMode = 5; // enemies and civilians
    t.run();
    CHECK(t.r.e(en).f(F_HEALTH) == 93.0f);
    CHECK(t.r.e(civ).f(F_HEALTH) == 86.0f);
    t.e().touchMode = 3; // v1.70 hit nothing for mode 3; as2 hits enemies (and players)
    t.run();
    CHECK(t.r.e(en).f(F_HEALTH) == 86.0f);
    t.r.e(en).setF(F_DEAD, 1.0f); // dead: skipped
    t.run();
    CHECK(t.r.e(en).f(F_HEALTH) == 86.0f);
}

TEST_CASE("sequel Lightning (changed): radius argument, horizontal distance, hit effect every 0.2 s") {
    BT t;
    t.setup([](Asm& a) {
        a.movImm(0, 200.0f);
        a.call("Lightning", 4);
    }, std::string(kTargets) + "wavegun_hit {\n flag FL_TEMPORARY\n}\n", false, " damage 20\n");
    t.e().setV3(F_ORIGIN, {640, 300, 500}); // high above: only the horizontal distance counts
    int inRange = onScreen(t, "t_en", {640, 450, 0});
    int outOfRange = onScreen(t, "t_en", {640, 520, 0});
    int civ = onScreen(t, "t_civ", {620, 400, 0}, kClassCivilian);
    REQUIRE((t.r.e(inRange).rt & RT_COLLIDABLE) != 0);
    REQUIRE((t.r.e(outOfRange).rt & RT_COLLIDABLE) != 0);
    REQUIRE((t.r.e(civ).rt & RT_COLLIDABLE) != 0);
    t.r.e(inRange).lightningTimer = 0.1f; // spawned less than 0.2 s ago: no effect
    int before = t.r.world.listCount();
    t.run();
    CHECK(near(t.r.e(inRange).f(F_HEALTH), 100.0f - 20.0f * kFt * 0.8f));
    CHECK(t.r.e(outOfRange).f(F_HEALTH) == 100.0f);
    CHECK(t.r.e(civ).f(F_HEALTH) == 100.0f); // civilians are not struck
    CHECK(t.r.world.lightningBolts().size() == 1);
    CHECK(t.r.world.listCount() == before);
    t.r.e(inRange).lightningTimer = 0.25f;
    t.run();
    REQUIRE(t.r.world.listCount() == before + 1);
    const Entity& fx = t.r.e(t.r.world.listEntities()[0]);
    CHECK(fx.name == "wavegun_hit");
    CHECK(fx.f(F_ORIGIN + 1) == 450.0f);
    CHECK(t.r.e(inRange).lightningTimer == 0.0f);
    t.run(); // timer 0 now: no second effect
    CHECK(t.r.world.listCount() == before + 1);
}

TEST_CASE("sequel Lightning timer: grows by frametime up to the rules' cap; not in the first game") {
    for (GameId g : {GameId::AirStrike3D, GameId::AirStrike2}) {
        Rig r;
        r.config.rules = &gameProfile(g).rules;
        r.obj("t_x {\n flag FL_TEMPORARY\n}\n");
        r.start();
        int e = r.create("t_x");
        r.step(60 * 6);
        INFO(std::string(gameProfile(g).key));
        if (g == GameId::AirStrike3D) CHECK(r.e(e).lightningTimer == 0.0f);
        else CHECK(r.e(e).lightningTimer == doctest::Approx(5.0f).epsilon(0.01));
    }
}

// ---------------------------------------------------------------------------------------
// E. Camera.
// ---------------------------------------------------------------------------------------

TEST_CASE("sequel GetMapPosOfs: y offset of the current camera mode") {
    for (int mode = 0; mode < 4; ++mode) {
        BT t;
        t.r.config.cameraMode = mode;
        t.setup([](Asm& a) { callStore(a, "GetMapPosOfs"); });
        t.run();
        CHECK(t.result() == as2Rules().cameraModes[mode].yOffset);
    }
}

// ---------------------------------------------------------------------------------------
// F. Level flow and players.
// ---------------------------------------------------------------------------------------

TEST_CASE("sequel EndLevel (changed): the checkpoint statistics, then the level ends") {
    BT t;
    t.setup([](Asm& a) { a.call("EndLevel", 4); }, "", true);
    PlayerRecord& p = t.r.world.player(0);
    p.lives = 1.7f;
    p.banked = 1000;
    p.scores = 250.5f;
    p.rankAccumulator = 2.0f;
    t.run();
    CHECK(t.r.world.levelComplete());
    CHECK(t.r.world.paused());
    CHECK(t.r.world.checkpointMission() == 0); // the empty level is mission 0
    CHECK(p.checkpointLives == 1);
    CHECK(p.checkpointScore == 1250);
    CHECK(p.checkpointRank == 2.0f); // no stars, no scored objects: those terms count 0
    // The first game stores nothing.
    BT u(GameId::AirStrike3D);
    u.setup([](Asm& a) { a.call("EndLevel", 4); }, "", true);
    u.r.world.player(0).lives = 1.0f;
    u.run();
    CHECK(u.r.world.levelComplete());
    CHECK(u.r.world.player(0).checkpointLives == 0);
    CHECK(u.r.world.checkpointMission() == -1);
}

TEST_CASE("sequel GameOver: game over, world stopped") {
    BT t;
    t.setup([](Asm& a) { a.call("GameOver", 4); });
    t.run();
    CHECK(t.r.world.gameOver());
    CHECK(t.r.world.paused());
    CHECK_FALSE(t.r.world.levelComplete());
}

TEST_CASE("sequel RespawnPlayer (changed): always spawns, whatever p_lives") {
    Rig r;
    r.config.rules = &as2Rules();
    r.obj(kAs2Helis);
    Asm s;
    s.entry(EntryPoint::Callback);
    s.call("RespawnPlayer", 4);
    s.end();
    r.script("scripts\\pl.scr", s);
    r.obj("t_dummy {\n flag FL_TEMPORARY\n}\n");
    r.start(true);
    int old = r.world.playerEntityIndex(0);
    REQUIRE(old >= 0);
    // Give the helicopter the script (the rig's player definitions have none).
    r.world.player(0).lives = -1.0f;
    r.e(old).program = r.world.host().program("scripts\\pl.scr");
    r.e(old).thread.reset(new ScriptThread(*r.e(old).program, r.world.host()));
    r.world.runCallback(old, 0, 0, 0);
    int now = r.world.playerEntityIndex(0);
    CHECK(now >= 0);
    CHECK(now != old);
    CHECK((r.e(old).rt & RT_REMOVED) != 0);
    CHECK(r.e(now).name == "player_2");
}

TEST_CASE("sequel G_UsePowerUp (changed): the next selection skips kinds 6, 7 and 9") {
    BT t;
    t.setup([](Asm& a) { callStore(a, "G_UsePowerUp"); }, "", true);
    PlayerRecord& p = t.r.world.player(0);
    p.powerups[5] = 1;
    p.powerups[6] = 3;
    p.powerups[7] = 3;
    p.powerups[9] = 3;
    p.powerups[10] = 2;
    p.currentPowerup = 5;
    t.run();
    CHECK(t.result() == 1.0f);
    CHECK(p.currentPowerup == 10);
    // Only skipped kinds left: none selected.
    p.powerups[10] = 0;
    p.powerups[4] = 1;
    p.currentPowerup = 4;
    t.run();
    CHECK(p.currentPowerup == -1);
    // A value above 15 is not skipped even when it wraps onto 6 (sel 12, candidate 22).
    p.powerups[12] = 1;
    p.currentPowerup = 12;
    t.run();
    CHECK(p.currentPowerup == 6);
    // The first game does not skip.
    BT u(GameId::AirStrike3D);
    u.setup([](Asm& a) { callStore(a, "G_UsePowerUp"); }, "", true);
    PlayerRecord& q = u.r.world.player(0);
    q.powerups[5] = 1;
    q.powerups[6] = 1;
    q.currentPowerup = 5;
    u.run();
    CHECK(q.currentPowerup == 6);
}

TEST_CASE("sequel G_SetPowerUpCount: writes the count, 0 if negative, selection kept") {
    BT t;
    t.setup([](Asm& a) {
        a.movImm(0, 9.0f);
        a.movImm(1, 12.7f);
        a.call("G_SetPowerUpCount", 4);
        a.movImm(0, 6.0f);
        a.movImm(1, -3.0f);
        a.call("G_SetPowerUpCount", 4);
        a.movImm(0, 40.0f); // outside 0..15: ignored
        a.movImm(1, 5.0f);
        a.call("G_SetPowerUpCount", 4);
    }, "", true);
    PlayerRecord& p = t.r.world.player(0);
    p.powerups[6] = 4;
    p.currentPowerup = 2;
    t.run();
    CHECK(p.powerups[9] == 12);
    CHECK(p.powerups[6] == 0);
    CHECK(p.currentPowerup == 2);
    CHECK(t.r.world.stats().scriptErrors == 0);
}

TEST_CASE("sequel G_GetUpgrade / G_SetUpgrade (changed): 9 slots") {
    BT t;
    t.setup([](Asm& a) {
        a.movImm(0, 8.0f);
        a.movImm(1, 3.0f);
        a.call("G_SetUpgrade", 4);
        a.movImm(0, 9.0f);
        a.movImm(1, 4.0f);
        a.call("G_SetUpgrade", 4); // slot 9: nothing
        a.movImm(0, 8.0f);
        callStore(a, "G_GetUpgrade");
        a.movImm(0, 9.0f);
        a.call("G_GetUpgrade", 4);
        a.setSelfSlot(27, 4);
    }, "", true);
    t.r.world.player(0).upgrades[9] = 5;
    t.run();
    CHECK(t.r.world.player(0).upgrades[8] == 3);
    CHECK(t.r.world.player(0).upgrades[9] == 5);
    CHECK(t.result() == 3.0f);
    CHECK(t.f(27) == 0.0f);
}

TEST_CASE("sequel GetPlayerAccel: the player's vector, nothing for other entities") {
    Rig r;
    r.config.rules = &as2Rules();
    r.obj(kAs2Helis);
    Asm s;
    s.entry(EntryPoint::Callback);
    s.leaGlobal(0, "self", 20);
    s.call("GetPlayerAccel", 4);
    s.end();
    r.script("scripts\\acc.scr", s);
    r.obj("t_acc {\n flag FL_TEMPORARY\n script \"scripts\\acc.scr\"\n}\n");
    r.start(true);
    int pe = r.world.playerEntityIndex(0);
    REQUIRE(pe >= 0);
    r.e(pe).program = r.world.host().program("scripts\\acc.scr");
    r.e(pe).thread.reset(new ScriptThread(*r.e(pe).program, r.world.host()));
    r.world.player(0).accel[0] = 0.6f;
    r.world.player(0).accel[1] = -0.8f;
    r.world.runCallback(pe, 0, 0, 0);
    CHECK(r.e(pe).f(20) == 0.6f);
    CHECK(r.e(pe).f(21) == -0.8f);
    CHECK(r.e(pe).f(22) == 0.0f);
    int other = r.create("t_acc");
    r.e(other).setV3(20, {9, 9, 9});
    r.world.runCallback(other, 0, 0, 0);
    CHECK(r.e(other).f(20) == 9.0f); // untouched
    CHECK(r.world.stats().scriptErrors == 0);
}

TEST_CASE("sequel GetPlayersDistance / IsMultiplayer / IsPlayerInGame") {
    for (int players = 1; players <= 2; ++players) {
        Rig r;
        r.config.rules = &as2Rules();
        r.config.players = players;
        r.obj(kAs2Helis);
        Asm s;
        s.entry(EntryPoint::Callback);
        callStore(s, "GetPlayersDistance");
        s.call("IsMultiplayer", 4);
        s.setSelfSlot(27, 4);
        s.movImm(0, 1.0f);
        s.call("IsPlayerInGame", 4);
        s.setSelfSlot(28, 4);
        s.movImm(0, 0.0f);
        s.call("IsPlayerInGame", 4);
        s.setSelfSlot(29, 4);
        s.movImm(0, 7.0f);
        s.call("IsPlayerInGame", 4);
        s.setSelfSlot(30, 4);
        s.end();
        r.script("scripts\\pd.scr", s);
        r.start(true);
        int p0 = r.world.playerEntityIndex(0);
        REQUIRE(p0 >= 0);
        r.e(p0).program = r.world.host().program("scripts\\pd.scr");
        r.e(p0).thread.reset(new ScriptThread(*r.e(p0).program, r.world.host()));
        r.e(p0).setF(F_ORIGIN + 1, 500.0f);
        if (players == 2) r.e(r.world.playerEntityIndex(1)).setF(F_ORIGIN + 1, 380.0f);
        r.world.runCallback(p0, 0, 0, 0);
        INFO(players << " players");
        CHECK(r.e(p0).f(26) == (players == 2 ? 120.0f : -1.0f));
        CHECK(r.e(p0).f(27) == (players == 2 ? 1.0f : 0.0f));
        CHECK(r.e(p0).f(28) == (players == 2 ? 1.0f : 0.0f));
        CHECK(r.e(p0).f(29) == 1.0f);
        CHECK(r.e(p0).f(30) == 0.0f);
        if (players == 2) {
            r.world.player(1).lives = -1.0f; // out of lives: -1
            r.world.runCallback(p0, 0, 0, 0);
            CHECK(r.e(p0).f(26) == -1.0f);
        }
    }
}

// ---------------------------------------------------------------------------------------
// Touch pass and kill counter (rcsl-vm.delta.md, engine-behaviour.delta.md 5.2 and 6.1).
// ---------------------------------------------------------------------------------------

TEST_CASE("sequel touch pass: modes as bit sets, civilians for bit 4, dead candidates skipped") {
    for (GameId g : {GameId::AirStrike3D, GameId::AirStrike2}) {
        BT t(g);
        Asm tch;
        tch.entry(EntryPoint::Touch);
        tch.addSelf(26, 1.0f);
        tch.end();
        t.r.script("scripts\\touch.scr", tch);
        t.setup([](Asm&) {}, std::string(kTargets) + "t_toucher {\n flag FL_TEMPORARY\n script \"scripts\\touch.scr\"\n}\n");
        int toucher = onScreen(t, "t_toucher", {640, 400, 0});
        int en = onScreen(t, "t_en", {640, 400, 0});
        int civ = onScreen(t, "t_civ", {640, 400, 0}, kClassCivilian);
        Entity& te = t.r.e(toucher);
        INFO(std::string(gameProfile(g).key));
        te.touchMode = 1;
        t.r.world.computeScreenBounds(toucher); // a class 0 entity gets a rectangle once it touches
        REQUIRE((te.rt & RT_COLLIDABLE) != 0);
        te.setF(26, 0.0f);
        t.r.world.touchEntity(toucher);
        CHECK(te.f(26) == 1.0f); // the enemy
        te.touchMode = 5;
        te.setF(26, 0.0f);
        t.r.world.touchEntity(toucher);
        CHECK(te.f(26) == (g == GameId::AirStrike3D ? 1.0f : 2.0f)); // + the civilian in as2
        te.touchMode = 4;
        te.setF(26, 0.0f);
        t.r.world.touchEntity(toucher);
        CHECK(te.f(26) == (g == GameId::AirStrike3D ? 0.0f : 1.0f));
        t.r.e(en).setF(F_DEAD, 1.0f);
        te.touchMode = 1;
        te.setF(26, 0.0f);
        t.r.world.touchEntity(toucher);
        CHECK(te.f(26) == (g == GameId::AirStrike3D ? 1.0f : 0.0f)); // a wreck absorbs nothing in as2
        (void)civ;
    }
}

TEST_CASE("sequel kill counter: capped at the level's enemy total") {
    for (GameId g : {GameId::AirStrike3D, GameId::AirStrike2}) {
        BT t(g);
        t.setup([](Asm&) {}, kTargets, true);
        int base = t.r.world.enemiesInLevel(); // the `create`d enemies count
        std::vector<int> ens;
        for (int i = 0; i < 3; ++i) ens.push_back(t.r.create("t_en"));
        REQUIRE(t.r.world.enemiesInLevel() == base + 3);
        t.r.world.player(0).kills = base + 2;
        for (int e : ens) t.r.world.damageEntity(e, 1000.0f, 0);
        INFO(std::string(gameProfile(g).key));
        CHECK(t.r.world.player(0).kills == (g == GameId::AirStrike3D ? base + 5 : base + 3));
    }
}

// ---------------------------------------------------------------------------------------
// With the game data.
// ---------------------------------------------------------------------------------------

namespace {
// AirStrike 2's extracted files, whatever game the suite runs for (AS3D_GAME).
const GameData& as2Data() {
    static const GameData d = locateGameData(testdata::root(), gameProfile(GameId::AirStrike2));
    return d;
}
std::string as2Dir() { return as2Data().extractedDir; }
bool as2Available() { return as2Data().hasExtracted; }
} // namespace

TEST_CASE("sequel data: every AirStrike 2 script binds with no unknown builtin or global") {
    if (!as2Available()) {
        std::fprintf(stderr, "SKIPPED (no AirStrike 2 data in %s): %s\n", as2Dir().c_str(), __FILE__);
        return;
    }
    std::unique_ptr<IFileSource> src = makeDirSource(as2Dir());
    REQUIRE(src);
    std::vector<std::string> files;
    src->list(files);
    int scripts = 0;
    std::vector<std::string> unknownBuiltins, unknownGlobals;
    for (const std::string& path : files) {
        if (path.size() < 4) continue;
        std::string ext = path.substr(path.size() - 4);
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".scr" && ext.substr(1) != ".sc") continue;
        Blob blob;
        if (!src->read(path, blob)) continue;
        ScriptProgram prog;
        std::string err;
        if (!ScriptProgram::load(blob.data(), blob.size(), prog, &err)) continue; // not ours to judge here
        ++scripts;
        for (const std::string& f : prog.funcs()) {
            if (!findGameBuiltin(GameId::AirStrike2, f.c_str())) unknownBuiltins.push_back(path + ": " + f);
        }
        for (const std::string& d : prog.defs()) {
            if (scriptGlobalIndex(GameId::AirStrike2, d.c_str()) < 0) unknownGlobals.push_back(path + ": " + d);
        }
    }
    MESSAGE("as2 scripts checked: " << scripts);
    CHECK(scripts > 600);
    for (const std::string& u : unknownBuiltins) FAIL_CHECK("unknown builtin " << u);
    for (const std::string& u : unknownGlobals) FAIL_CHECK("unknown global " << u);
}

TEST_CASE("sequel data: AirStrike 2 mission 1 under the bot, no script error, the player moves and scores") {
    if (!as2Available()) {
        std::fprintf(stderr, "SKIPPED (no AirStrike 2 data in %s): %s\n", as2Dir().c_str(), __FILE__);
        return;
    }
    Vfs vfs;
    vfs.mount(makeDirSource(as2Dir()));
    DefDatabase db;
    REQUIRE(db.load(vfs));
    World w;
    WorldConfig cfg;
    cfg.rules = &as2Rules();
    w.init(vfs, db, cfg);
    std::string err;
    REQUIRE(w.loadLevel("1", &err));
    int pe = w.playerEntityIndex(0);
    REQUIRE(pe >= 0);
    CHECK(w.player(0).upgrades[0] == 1); // mission 1's loadout
    float minX = 1e9f, maxX = -1e9f;
    for (u32 f = 0; f < 1800; ++f) {
        w.step(botInput(f).toPlayerInput());
        int p = w.playerEntityIndex(0);
        if (p >= 0) {
            minX = std::min(minX, w.entity(p).f(F_ORIGIN));
            maxX = std::max(maxX, w.entity(p).f(F_ORIGIN));
        }
    }
    MESSAGE("as2 mission 1, 1800 frames: score " << w.player(0).scores << ", kills " << w.player(0).kills
                                                  << ", x range " << minX << ".." << maxX);
    CHECK(w.stats().scriptErrors == 0);
    CHECK(w.stats().stalls == 0);
    CHECK(maxX - minX > 50.0f);
    CHECK(w.player(0).scores > 0.0f);
    for (const script::BuiltinReport::Row& row : w.report().rows()) {
        INFO(row.name);
        CHECK(row.status != BuiltinStatus::Stub);
    }
    CHECK(w.host().unknownGlobals().empty());
}

TEST_CASE("sequel definitions: civilian class 5, TOUCH_CIVILIAN and TOUCH_ALL as bits, speed in field 23") {
    Rig r;
    r.config.rules = &as2Rules();
    r.obj("t_c {\n civilian\n flag FL_TEMPORARY\n}\n"
          "t_t5 {\n flag FL_TEMPORARY\n touch TOUCH_ENEMIES\n touch TOUCH_CIVILIAN\n}\n"
          "t_t6 {\n flag FL_TEMPORARY\n touch TOUCH_CIVILIAN\n touch TOUCH_PLAYER\n}\n"
          "t_all {\n flag FL_TEMPORARY\n touch TOUCH_ALL\n}\n"
          "t_w {\n flag FL_TEMPORARY\n speed 2.5\n}\n"
          "t_n {\n flag FL_TEMPORARY\n}\n");
    r.start();
    CHECK(r.e(r.create("t_c")).f(F_CLASS) == kClassCivilian);
    CHECK(r.e(r.create("t_t5")).touchMode == 5);
    CHECK(r.e(r.create("t_t6")).touchMode == 6);
    CHECK(r.e(r.create("t_all")).touchMode == 0xF);
    CHECK(r.e(r.create("t_w")).f(F_WP_SPEED) == 2.5f);
    CHECK(r.e(r.create("t_n")).f(F_WP_SPEED) == 0.0f);
    // The first game: TOUCH_ALL stays 3 and `speed` does not reach field 23.
    Rig a;
    a.obj("t_all {\n flag FL_TEMPORARY\n touch TOUCH_ALL\n}\n"
          "t_w {\n flag FL_TEMPORARY\n speed 2.5\n}\n");
    a.start();
    CHECK(a.e(a.create("t_all")).touchMode == 3);
    CHECK(a.e(a.create("t_w")).f(F_WP_SPEED) == 0.0f);
}
