// Gulf Thunder's own rules and HUD (package F2): the mission loadout of every operation
// (docs/spec/gulf/engine-behaviour.delta.md 8.2), operation 24 played to its end under the
// test pilot, the HUD's weapon icon table, level caps and hint panel (frontend.delta.md 3.1,
// 3.15, 4.2, 4.4), and the web page's copy of the executable text table. The game is selected
// explicitly (gameProfile(GameId::GulfThunder), locateGameData); tests needing its data skip
// loudly without it.
#include "doctest.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "as3d/defs.h"
#include "as3d/game_data.h"
#include "as3d/hud_layout.h"
#include "as3d/input.h"
#include "as3d/vfs.h"
#include "as3d/world.h"
#include "test_data.h"
#include "ui_test_util.h"
#include "world_test_util.h"

using namespace as3d;
using namespace as3d::ui;

namespace {

const GameData& gulfData() {
    static const GameData d = locateGameData(testdata::root(), gameProfile(GameId::GulfThunder));
    return d;
}

#define REQUIRE_GULF_DATA()                                                                                          \
    if (!gulfData().hasExtracted) {                                                                                  \
        std::fprintf(stderr, "SKIPPED (no Gulf Thunder data in %s): %s\n", gulfData().extractedDir.c_str(), __FILE__); \
        return;                                                                                                      \
    }

// Long plays run in the default (as3d) pass and in the gulf pass of tools/ci.sh only.
#define REQUIRE_GULF_PLAY_PASS()                                                                                  \
    if (testdata::gameKey() != "as3d" && testdata::gameKey() != "gulf") {                                        \
        std::fprintf(stderr, "SKIPPED (Gulf Thunder play tests run in the as3d and gulf passes): %s\n", __FILE__); \
        return;                                                                                                   \
    }

// The loadout of 8.2, written out from the spec (not from the profile).
const int kSpecLoadout[6][9] = {
    {4, 0, 0, 0, 0, 0, 0, 0, 0}, // operation 1
    {4, 5, 0, 0, 0, 0, 0, 0, 0}, // 2
    {4, 5, 4, 0, 3, 0, 0, 0, 0}, // 3
    {0, 5, 4, 4, 4, 0, 2, 0, 0}, // 4
    {0, 0, 4, 4, 4, 3, 2, 3, 4}, // 5
    {0, 0, 7, 6, 7, 4, 4, 4, 4}, // 6 to 24
};
const int* specRow(int op) { return kSpecLoadout[std::min(op, 6) - 1]; }

struct Gulf {
    Vfs vfs;
    DefDatabase db;
    Gulf() {
        vfs.mount(makeDirSource(gulfData().extractedDir));
        REQUIRE(db.load(vfs));
    }
    void start(World& w, const std::string& level, bool god) {
        WorldConfig cfg;
        cfg.rules = &gameProfile(GameId::GulfThunder).rules;
        cfg.godMode = god;
        w.init(vfs, db, cfg);
        std::string err;
        REQUIRE_MESSAGE(w.loadLevel(level, &err), err);
    }
};

std::string readText(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

TEST_CASE("gulf rules: the profile's loadout is the 24 rows of engine-behaviour.delta.md 8.2") {
    const GameRules& r = gameProfile(GameId::GulfThunder).rules;
    REQUIRE(r.missionLoadout != nullptr);
    REQUIRE(r.missionCount == 24);
    REQUIRE(r.weaponSlots == 9);
    for (int op = 1; op <= 24; ++op) {
        INFO("operation " << op);
        for (int k = 0; k < 9; ++k) CHECK(r.missionLoadout[op - 1][k] == specRow(op)[k]);
    }
}

// On an empty flat level (the shipped maps place pick-ups that can be taken while the level
// loads: operation 5 starts with the plasma gun one level up in ours).
TEST_CASE("gulf rules: every operation starts with its loadout, the highest owned weapon selected") {
    for (int op = 1; op <= 24; ++op) {
        INFO("operation " << op);
        worldtest::Rig r;
        r.config.rules = &gameProfile(GameId::GulfThunder).rules;
        r.obj("player_1 {\n player\n flag FL_TEMPORARY\n health 500\n speed 1.15\n}\n"
              "player_2 {\n player\n flag FL_TEMPORARY\n health 400\n speed 1.3\n}\n"
              "player_3 {\n player\n flag FL_TEMPORARY\n health 300\n speed 1.5\n}\n");
        r.start();
        std::unique_ptr<LoadedLevel> l(new LoadedLevel());
        l->data.width = 16;
        l->data.height = 16;
        l->data.cells.resize(16 * 16);
        for (LevelCell& c : l->data.cells) c.height = 128;
        REQUIRE(r.world.startLevel(std::move(l), op));
        const PlayerRecord& p = r.world.player(0);
        int highest = 0;
        for (int k = 0; k < 9; ++k) {
            CHECK(p.upgrades[k] == specRow(op)[k]);
            if (specRow(op)[k] != 0) highest = k;
        }
        CHECK(p.weapon == static_cast<float>(highest));
    }
}

// Operation 24's base falls when its four minarets do; the sphere (1,000,000) is brought down
// by boss#1.scr, then the core ends the level. The test pilot used to aim at the sphere for good
// (docs/missions-status-gulf.md): it must now end the operation.
TEST_CASE("gulf rules: operation 24 ends under the test pilot in god mode") {
    REQUIRE_GULF_DATA();
    REQUIRE_GULF_PLAY_PASS();
    Gulf d;
    World w;
    d.start(w, "24", true);
    u32 end = 0;
    for (u32 f = 0; f < 45000 && end == 0; ++f) {
        w.step(botInput(w, f).toPlayerInput());
        if (w.levelComplete() || w.gameOver()) end = f + 1;
    }
    INFO("end frame " << end << ", score " << w.player(0).scores);
    CHECK(w.levelComplete());
    CHECK_FALSE(w.gameOver());
    CHECK(end > 12000u); // not before the scroll has reached the base
    CHECK(w.stats().scriptErrors == 0u);
    CHECK(w.stats().stalls == 0u);
}

TEST_CASE("gulf hud: weapon icons, level caps and the hint panel (frontend.delta.md 4.2, 4.4, 3.15)") {
    const HudLayout& G = hudLayout(GameId::GulfThunder);
    // 4.4: texels of weapons.tga (256 x 128), in slot order.
    const float texels[9][2] = {{0, 0}, {198, 35}, {0, 35}, {66, 70}, {132, 0}, {66, 35}, {132, 70}, {132, 35}, {66, 0}};
    REQUIRE(G.weapons.size() == 9u);
    for (int k = 0; k < 9; ++k) {
        INFO("slot " << k);
        const SpecUv want = texelUv(texels[k][0], texels[k][1], k == 1 ? 58.0f : 66.0f, 35, 256, 128);
        const SpecUv& uv = G.weapons[static_cast<size_t>(k)].uv;
        CHECK(uv.s0 == doctest::Approx(want.s0).epsilon(0.003));
        CHECK(uv.t0 == doctest::Approx(want.t0).epsilon(0.003));
        CHECK(uv.s1 == doctest::Approx(want.s1).epsilon(0.01));
        CHECK(uv.t1 == doctest::Approx(want.t1).epsilon(0.003));
        CHECK(G.weapons[static_cast<size_t>(k)].blend == Blend::Add);
        // Inside the atlas.
        CHECK(std::min(uv.s0, uv.s1) >= 0.0f);
        CHECK(std::max(uv.s0, uv.s1) <= 1.0f);
        CHECK(std::min(uv.t0, uv.t1) >= 0.0f);
        CHECK(std::max(uv.t0, uv.t1) <= 1.0f);
    }
    CHECK(G.weaponLevelMax == std::vector<int>{4, 5, 7, 6, 5, 5, 4, 5, 3});
    CHECK(G.levelOverrunsCap);
    CHECK(G.hint == HintStyle::SequelPanel);
    CHECK(G.panelSkin == HudPanelSkin::Gulf);
    CHECK(std::string(G.panelAtlas) == "gfx\\ui\\interface_gulf.tga");
    // The photon gun and the plasma laser use the two cells AirStrike 2's table has not.
    CHECK_FALSE(weaponIconUv(3, GameId::GulfThunder).s0 == weaponIconUv(3, GameId::AirStrike2).s0);
    CHECK(weaponIconUv(9, GameId::GulfThunder).empty());
}

TEST_CASE("gulf hud: the laser's level 7 runs past its cap of 5; the hint panel's pieces") {
    AS3D_REQUIRE_GLES();
    REQUIRE_GULF_DATA();
    Vfs vfs;
    vfs.mount(makeDirSource(gulfData().extractedDir));
    UiAssets a;
    std::string err;
    REQUIRE_MESSAGE(a.load(vfs, &err, GameId::GulfThunder), err);
    CHECK(a.missing == 0);
    REQUIRE(a.panel.valid());
    CHECK(a.panel.width() == 256);
    CHECK(a.panel.height() == 256);
    const HudLayout& G = a.hudLayout();

    // HUD with the laser (slot 4) at level 7: the empty run is 5 pips (35 px), the filled 7 (49).
    HudState st;
    st.players[0].health = 300;
    st.players[0].lives = 2;
    st.players[0].weapon = 4;
    st.players[0].upgrades[4] = 7;
    Renderer2D r;
    r.begin(800, 600);
    drawHud(r, a, st);
    bool emptyRun = false, filledRun = false;
    for (const Quad& q : r.quads()) {
        if (q.texture != &a.mainbar || q.x != G.onePlayer.levelX || q.y != G.onePlayer.levelY) continue;
        if (q.w == 35.0f) emptyRun = true;
        if (q.w == 49.0f) filledRun = true;
        CHECK(std::max(q.s0, q.s1) <= 1.0f); // the run of pips stays inside mainbar2.tga
    }
    CHECK(emptyRun);
    CHECK(filledRun);

    // The hint panel: every piece from interface_gulf.tga inside it; the title tab's left end at
    // (x + 17, Y - 57) and the 37-high Ok button centred on x 400 at y 520.
    const std::string text = "Hint text^second line";
    const HintLayout L = layoutHint(FontMetrics::original(), text, HintStyle::SequelPanel);
    CHECK(L.box.x == 220.0f);
    CHECK(L.box.y == 220.0f);
    Renderer2D h;
    h.begin(800, 600);
    drawHint(h, a, text);
    int panelQuads = 0;
    bool titleLeft = false, ok = false;
    for (const Quad& q : h.quads()) {
        if (q.texture != &a.panel) continue;
        ++panelQuads;
        CHECK(std::min(q.s0, q.s1) >= 0.0f);
        CHECK(std::max(q.s0, q.s1) <= 1.0f);
        CHECK(std::min(q.t0, q.t1) >= 0.0f);
        CHECK(std::max(q.t0, q.t1) <= 1.0f);
        if (q.x == L.box.x + 17 && q.y == L.box.y - 57 && q.w == 45 && q.h == 40) titleLeft = true;
        if (q.x == 358 && q.y == 520 && q.w == 17 && q.h == 37) ok = true;
    }
    CHECK(panelQuads > 10);
    CHECK(titleLeft);
    CHECK(ok);
}

// apps/web/site/files.js carries tools/exe_texts/gulf.json's list for the browser: the same
// keys, addresses and kinds in the same order.
TEST_CASE("gulf texts: the web page's table is tools/exe_texts/gulf.json's") {
    const std::string js = readText(std::string(AS3D_REPO_ROOT) + "/apps/web/site/files.js");
    const std::string json = readText(std::string(AS3D_REPO_ROOT) + "/tools/exe_texts/gulf.json");
    REQUIRE_FALSE(js.empty());
    REQUIRE_FALSE(json.empty());
    // files.js: ['key', 0xADDR, 't'|'m'|'u'] between the BEGIN and END lines of the gulf list.
    const size_t b = js.find("BEGIN gulf address list");
    const size_t e = js.find("END gulf address list");
    REQUIRE(b != std::string::npos);
    REQUIRE(e != std::string::npos);
    std::vector<std::string> fromJs;
    for (size_t p = js.find("['", b); p != std::string::npos && p < e; p = js.find("['", p + 1)) {
        const size_t q = js.find('\'', p + 2);
        const size_t c = js.find(']', q);
        std::string key = js.substr(p + 2, q - p - 2);
        std::string rest = js.substr(q + 1, c - q - 1); // ", 0x48CBC4, 't'"
        unsigned long addr = std::stoul(rest.substr(rest.find("0x") + 2), nullptr, 16);
        const char k = rest[rest.size() - 2];
        const int kind = k == 't' ? 0 : k == 'm' ? 1 : k == 'u' ? 2 : -1;
        fromJs.push_back(key + "|" + std::to_string(addr) + "|" + std::to_string(kind));
    }
    // gulf.json: "key": "...", "address": "0x...", "kind": "..." per entry, in file order.
    std::vector<std::string> fromJson;
    const size_t ent = json.find("\"entries\"");
    const size_t rem = json.find("\"removed\"");
    REQUIRE(ent != std::string::npos);
    for (size_t p = json.find("\"key\":", ent); p != std::string::npos && (rem == std::string::npos || p < rem);
         p = json.find("\"key\":", p + 1)) {
        auto field = [&](const char* name) {
            const size_t f = json.find(std::string("\"") + name + "\":", p);
            const size_t s = json.find('"', json.find(':', f) + 1);
            return json.substr(s + 1, json.find('"', s + 1) - s - 1);
        };
        const std::string kind = field("kind");
        const int k = kind == "text" ? 0 : kind == "text_ml" ? 1 : 2;
        fromJson.push_back(field("key") + "|" + std::to_string(std::stoul(field("address"), nullptr, 16)) + "|" +
                           std::to_string(k));
    }
    CHECK(fromJson.size() == 230u);
    CHECK(fromJs == fromJson);
}
