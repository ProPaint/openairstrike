// Scripted-bot regression (WP-49 item 8): mission 1, seed 1, Normal difficulty, the scripted
// test pilot of as3d/input.h (as3d_sim --bot). Any change of the simulation that changes how
// the mission plays shows up here. When a change is intended, update the expected values and
// say why in the commit message.
//
// History: values set after the on-screen rule of spec issue 120 and the symmetric bot weave
// (WP-49). AirStrike 2 (package D1): its own values, recorded from the build of that package,
// under the world-aware pilot in god mode (expected.json "pilot": "world", "god": true), the
// way its missions were played to the end (docs/missions-status-as2.md).
#include "doctest.h"

#include <string>

#include "as3d/defs.h"
#include "as3d/input.h"
#include "as3d/vfs.h"
#include "as3d/world.h"
#include "test_data.h"

using namespace as3d;

TEST_CASE("bot regression: mission 1 ends at the same frame with the same score") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    // Mission, seed, difficulty and the expected result are in expected.json ("bot_regression").
    const u32 kExpectedEndFrame = static_cast<u32>(testdata::expectedInt("bot_regression.end_frame"));
    const float kExpectedScore = static_cast<float>(testdata::expectedNumber("bot_regression.score"));
    const float kExpectedLives = static_cast<float>(testdata::expectedNumber("bot_regression.lives"));
    Vfs vfs;
    vfs.mount(makeDirSource(testdata::extractedDir()));
    DefDatabase db;
    REQUIRE(db.load(vfs));
    World w;
    WorldConfig cfg;
    cfg.rules = &testdata::game().rules;
    const testdata::Json* pilot = testdata::expectedAt("bot_regression.pilot");
    const bool worldPilot = pilot && pilot->s == "world";
    const testdata::Json* god = testdata::expectedAt("bot_regression.god");
    cfg.godMode = god && god->kind == testdata::Json::Bool && god->b;
    cfg.seed = static_cast<u32>(testdata::expectedInt("bot_regression.seed"));
    cfg.difficulty = static_cast<int>(testdata::expectedInt("bot_regression.difficulty"));
    w.init(vfs, db, cfg);
    std::string err;
    REQUIRE(w.loadLevel(std::to_string(testdata::expectedInt("bot_regression.mission")), &err));
    u32 endFrame = 0;
    const u32 maxFrames = kExpectedEndFrame + 2000;
    for (u32 f = 0; f < maxFrames && endFrame == 0; ++f) {
        w.step((worldPilot ? botInput(w, f) : botInput(f)).toPlayerInput());
        if (w.levelComplete() || w.gameOver()) endFrame = f + 1;
    }
    INFO("end frame " << endFrame << ", score " << w.player(0).scores << ", lives " << w.player(0).lives);
    CHECK(w.levelComplete());
    CHECK_FALSE(w.gameOver());
    CHECK(endFrame == kExpectedEndFrame);
    CHECK(w.player(0).scores == kExpectedScore);
    CHECK(w.player(0).lives == kExpectedLives);
    CHECK(w.stats().scriptErrors == 0u);
}
