// Gulf Thunder on its shipped levels (package F2): every operation and both attract levels run
// 1500 frames under the world-aware pilot in god mode with no script error, stall, refused
// spawn or stub builtin (the complete runs are in docs/missions-status-gulf.md), and operation
// 1 played to its end with the pilot in god mode, seed 1, ends at the frame, with the score
// and the lives of that run (a regression guard like bot_regression_test.cpp's). The game is
// selected explicitly (gameProfile(GameId::GulfThunder), locateGameData); these run in the
// default (as3d) pass and in the gulf pass of tools/ci.sh only, and skip loudly without its
// data.
#include "doctest.h"

#include <cstdio>
#include <string>

#include "as3d/defs.h"
#include "as3d/game_data.h"
#include "as3d/input.h"
#include "as3d/script_host.h"
#include "as3d/vfs.h"
#include "as3d/world.h"
#include "test_data.h"

using namespace as3d;

namespace {

const GameData& gulfData() {
    static const GameData d = locateGameData(testdata::root(), gameProfile(GameId::GulfThunder));
    return d;
}

#define REQUIRE_GULF_MISSIONS()                                                                                      \
    if (!gulfData().hasExtracted) {                                                                                  \
        std::fprintf(stderr, "SKIPPED (no Gulf Thunder data in %s): %s\n", gulfData().extractedDir.c_str(), __FILE__); \
        return;                                                                                                      \
    }                                                                                                                \
    if (testdata::gameKey() != "as3d" && testdata::gameKey() != "gulf") {                                           \
        std::fprintf(stderr, "SKIPPED (Gulf Thunder play tests run in the as3d and gulf passes): %s\n", __FILE__);   \
        return;                                                                                                      \
    }

struct Gulf {
    Vfs vfs;
    DefDatabase db;
    Gulf() {
        vfs.mount(makeDirSource(gulfData().extractedDir));
        REQUIRE(db.load(vfs));
    }
    void start(World& w, const std::string& level) {
        WorldConfig cfg;
        cfg.rules = &gameProfile(GameId::GulfThunder).rules;
        cfg.godMode = true;
        cfg.seed = 1;
        w.init(vfs, db, cfg);
        std::string err;
        REQUIRE_MESSAGE(w.loadLevel(level, &err), err);
    }
};

} // namespace

TEST_CASE("gulf missions: every operation and attract level, 1500 frames, no script error, stall, refusal or stub") {
    REQUIRE_GULF_MISSIONS();
    Gulf d;
    const int missions = gameProfile(GameId::GulfThunder).rules.missionCount;
    REQUIRE(missions == 24);
    for (int m = 1; m <= missions + 2; ++m) {
        const std::string level = m <= missions ? std::to_string(m) : "intro" + std::to_string(m - missions);
        INFO("gulf level " << level);
        World w;
        d.start(w, level);
        for (u32 f = 0; f < 1500; ++f) w.step(botInput(w, f).toPlayerInput());
        const WorldStats& st = w.stats();
        INFO("first error: " << (st.firstErrors.empty() ? std::string() : st.firstErrors.front()));
        CHECK(st.scriptErrors == 0u);
        CHECK(st.stalls == 0u);
        CHECK(st.spawnRefused == 0u);
        CHECK_FALSE(w.gameOver());
        if (m <= missions) CHECK(w.mapPos() > 32.0f); // the level scrolls
        else CHECK(w.listCount() > 20);             // the attract level is populated at load
        for (const script::BuiltinReport::Row& row : w.report().rows()) {
            INFO("builtin " << row.name);
            CHECK(row.status != script::BuiltinStatus::Stub);
        }
        CHECK(w.host().unknownGlobals().empty());
    }
}

// Values of the run of docs/missions-status-gulf.md (as3d_sim --game gulf --level 1 --pilot
// --god, seed 1, Normal difficulty). A change of the simulation that changes how operation 1
// plays shows up here; when it is intended, update them and say why in the commit message.
TEST_CASE("gulf missions: operation 1 under the pilot in god mode ends at the same frame with the same score") {
    REQUIRE_GULF_MISSIONS();
    const u32 kEndFrame = 12423;
    const float kScore = 97320.0f;
    const float kLives = 4.0f;
    Gulf d;
    World w;
    d.start(w, "1");
    u32 end = 0;
    for (u32 f = 0; f < kEndFrame + 2000 && end == 0; ++f) {
        w.step(botInput(w, f).toPlayerInput());
        if (w.levelComplete() || w.gameOver()) end = f + 1;
    }
    INFO("end frame " << end << ", score " << w.player(0).scores << ", lives " << w.player(0).lives);
    CHECK(w.levelComplete());
    CHECK_FALSE(w.gameOver());
    CHECK(end == kEndFrame);
    CHECK(w.player(0).scores == kScore);
    CHECK(w.player(0).lives == kLives);
    CHECK(w.stats().scriptErrors == 0u);
}
