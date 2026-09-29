// Scripted-bot regression (WP-49 item 8): mission 1, seed 1, Normal difficulty, the scripted
// test pilot of as3d/input.h (as3d_sim --bot). Any change of the simulation that changes how
// the mission plays shows up here. When a change is intended, update the expected values and
// say why in the commit message.
//
// History: values set after the on-screen rule of spec issue 120 and the symmetric bot weave
// (WP-49).
#include "doctest.h"

#include <string>

#include "as3d/defs.h"
#include "as3d/input.h"
#include "as3d/vfs.h"
#include "as3d/world.h"
#include "test_data.h"

using namespace as3d;

TEST_CASE("bot regression: mission 1 ends at the same frame with the same score") {
    AS3D_REQUIRE_DATA();
    constexpr u32 kExpectedEndFrame = 13662;
    constexpr float kExpectedScore = 6250.0f;
    constexpr float kExpectedLives = 2.0f;
    Vfs vfs;
    vfs.mount(makeDirSource(testdata::extractedDir()));
    DefDatabase db;
    REQUIRE(db.load(vfs));
    World w;
    WorldConfig cfg;
    cfg.seed = 1;
    cfg.difficulty = 2;
    w.init(vfs, db, cfg);
    std::string err;
    REQUIRE(w.loadLevel("1", &err));
    u32 endFrame = 0;
    for (u32 f = 0; f < 16000 && endFrame == 0; ++f) {
        w.step(botInput(f).toPlayerInput());
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
