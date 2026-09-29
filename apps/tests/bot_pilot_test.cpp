// The test pilots (WP-49 item 6): the scripted bot weaves about its start position, the
// world-aware pilot flies the lower middle of the play-field; both are deterministic and
// only read the world.
#include "doctest.h"

#include <cmath>
#include <string>

#include "as3d/defs.h"
#include "as3d/input.h"
#include "as3d/vfs.h"
#include "as3d/world.h"
#include "test_data.h"

using namespace as3d;

namespace {

struct PilotRun {
    int frames = 0, lowerMiddle = 0, nearCentre = 0, collidable = 0;
    std::string dump;
};

PilotRun fly(bool worldAware, int frames) {
    PilotRun r;
    Vfs vfs;
    vfs.mount(makeDirSource(testdata::extractedDir()));
    DefDatabase db;
    REQUIRE(db.load(vfs));
    World w;
    w.init(vfs, db, WorldConfig());
    std::string err;
    REQUIRE(w.loadLevel("1", &err));
    for (int f = 0; f < frames; ++f) {
        FrameInput in = worldAware ? botInput(w, static_cast<u32>(f)) : botInput(static_cast<u32>(f));
        w.step(in.toPlayerInput());
        int pi = w.playerEntityIndex(0);
        if (f < 400 || pi < 0) continue; // the fly-in
        const Entity& e = w.entity(pi);
        ++r.frames;
        float dy = e.f(F_ORIGIN + 1) - w.mapPos();
        if (dy >= 35.0f && dy <= 200.0f) ++r.lowerMiddle;
        if (std::fabs(e.f(F_ORIGIN) - w.camera().field[0]) < 120.0f) ++r.nearCentre;
        if (e.rt & RT_COLLIDABLE) ++r.collidable;
    }
    r.dump = w.dumpStateJson();
    return r;
}

} // namespace

TEST_CASE("pilots: the world-aware pilot keeps to the lower middle, deterministically") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    AS3D_REQUIRE_FIRST_GAME("mission 1 and pilot tuning");
    PilotRun a = fly(true, 3600), b = fly(true, 3600);
    CHECK(a.dump == b.dump);
    INFO("lower middle " << a.lowerMiddle << ", near centre " << a.nearCentre << " of " << a.frames);
    CHECK(a.lowerMiddle > a.frames * 9 / 10);
    CHECK(a.nearCentre > a.frames * 8 / 10);
    CHECK(a.collidable == a.frames);
}

TEST_CASE("pilots: the scripted bot no longer drifts into a corner") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    AS3D_REQUIRE_FIRST_GAME("mission 1 and pilot tuning");
    PilotRun s = fly(false, 3600);
    INFO("lower middle " << s.lowerMiddle << ", near centre " << s.nearCentre << " of " << s.frames);
    CHECK(s.lowerMiddle > s.frames * 9 / 10);
    CHECK(s.nearCentre > s.frames * 7 / 10);
}
