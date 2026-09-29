// Every mission, briefly (WP-49 item 7): missions 1 to 20 each run for 1500 frames under the
// world-aware pilot in god mode and must not raise a script error or a stall, nor call an
// unimplemented builtin. The complete runs (to EndLevel) are in docs/missions-status.md.
#include "doctest.h"

#include <string>

#include "as3d/defs.h"
#include "as3d/input.h"
#include "as3d/vfs.h"
#include "as3d/world.h"
#include "test_data.h"

using namespace as3d;

TEST_CASE("all missions: 1500 frames each without script errors") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    if (testdata::game().id != GameId::AirStrike3D) {
        // The sequel's rules and pilot: its missions are played by as2_missions_test.cpp.
        std::fprintf(stderr, "SKIPPED (game '%s': its missions are played by as2_missions_test.cpp): %s\n",
                     testdata::gameKey().c_str(), __FILE__);
        return;
    }
    Vfs vfs;
    vfs.mount(makeDirSource(testdata::extractedDir()));
    DefDatabase db;
    REQUIRE(db.load(vfs));
    const int first = static_cast<int>(testdata::expectedInt("missions.first"));
    const int last = static_cast<int>(testdata::expectedInt("missions.last"));
    for (int m = first; m <= last; ++m) {
        World w;
        WorldConfig cfg;
        cfg.godMode = true;
        w.init(vfs, db, cfg);
        std::string err;
        INFO("mission " << m);
        REQUIRE_MESSAGE(w.loadLevel(std::to_string(m), &err), err);
        for (u32 f = 0; f < 1500; ++f) w.step(botInput(w, f).toPlayerInput());
        const WorldStats& st = w.stats();
        std::string first = st.firstErrors.empty() ? std::string() : st.firstErrors.front();
        INFO("first error: " << first);
        CHECK(st.scriptErrors == 0u);
        CHECK(st.stalls == 0u);
        CHECK(st.spawnRefused == 0u);
        CHECK_FALSE(w.gameOver());
        CHECK(w.mapPos() > 32.0f); // the level scrolls
        for (const script::BuiltinReport::Row& row : w.report().rows()) {
            INFO("builtin " << row.name);
            CHECK(row.status != script::BuiltinStatus::Stub);
        }
    }
}
