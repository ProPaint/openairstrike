// Projected-shadow rotation keys fixed at spawn (render-pipeline.md 5.3): map objects keep
// their placement byte (even when a path turns them); `create` is covered in builtins_test.cpp.
#include "doctest.h"

#include "as3d/defs.h"
#include "as3d/level.h"
#include "as3d/vfs.h"
#include "as3d/world.h"
#include "test_data.h"

using namespace as3d;

TEST_CASE("shadow key: map objects keep their placement byte") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    AS3D_REQUIRE_FIRST_GAME("map objects");
    Vfs vfs;
    vfs.mount(makeDirSource(testdata::extractedDir()));
    DefDatabase db;
    REQUIRE(db.load(vfs));
    World w;
    w.init(vfs, db, WorldConfig());
    std::string err;
    REQUIRE(w.loadLevel("1", &err));
    int placed = 0, others = 0;
    PlayerInput in;
    for (int f = 0; f < 600; ++f) {
        w.step(in);
        for (int i : w.listEntities()) {
            const Entity& e = w.entity(i);
            if (!e.hasShadowKey) {
                ++others; // the player, projectiles, effects: no spawn key
                continue;
            }
            // A placement byte, whatever the entity's yaw is now (path followers turn).
            CHECK(e.shadowKey >= 0);
            CHECK(e.shadowKey < 12);
            ++placed;
        }
    }
    CHECK(placed > 0);
    CHECK(others > 0);
}
