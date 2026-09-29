// The player at the edges of the play-field (WP-49 item 6, owner's report forwarded by the
// orchestrator): in the original the helicopter can be partly outside the screen at the
// sides and at the bottom and can still fire there. Mission 1, god mode, the player pushed to
// the extreme positions the clamps allow with fire held.
#include "doctest.h"

#include <set>
#include <utility>

#include "as3d/defs.h"
#include "as3d/input.h"
#include "as3d/vfs.h"
#include "as3d/world.h"
#include "test_data.h"

using namespace as3d;

namespace {

struct EdgeRun {
    int projectiles = 0;     // player projectiles created while held at the edge
    int framesOnScreen = 0;  // frames with the player's on-screen bit (0x08) set
    int frames = 0;
    ScreenRect lastRect;
    Vec3 lastOrigin;
};

// Flies mission 1 for `warm` frames with the plain fire-only input, then holds `dir` (and
// fire) for `hold` frames and counts the player's new projectiles during the last `count`.
EdgeRun runEdge(u32 dir, int hold, int count) {
    EdgeRun r;
    Vfs vfs;
    vfs.mount(makeDirSource(testdata::extractedDir()));
    DefDatabase db;
    REQUIRE(db.load(vfs));
    World w;
    WorldConfig cfg;
    cfg.godMode = true;
    w.init(vfs, db, cfg);
    std::string err;
    REQUIRE(w.loadLevel("1", &err));
    PlayerInput in;
    in.confirm = true;
    const int warm = 400; // past the fly-in
    std::set<std::pair<int, u32>> seen;
    for (int f = 0; f < warm + hold; ++f) {
        in.action[0] = ACT_FIRE | (f >= warm ? dir : 0u);
        w.step(in);
        if (f < warm + hold - count) {
            for (int i : w.listEntities()) seen.insert({i, w.entity(i).generation});
            continue;
        }
        int pi = w.playerEntityIndex(0);
        REQUIRE(pi >= 0);
        const Entity& pe = w.entity(pi);
        ++r.frames;
        if (pe.rt & RT_COLLIDABLE) ++r.framesOnScreen;
        r.lastRect = pe.rect;
        r.lastOrigin = pe.v3(F_ORIGIN);
        for (int i : w.listEntities()) {
            const Entity& e = w.entity(i);
            if (!seen.insert({i, e.generation}).second) continue;
            if (e.f(F_CLASS) == kClassProjectile && e.touchMode == 1) ++r.projectiles;
        }
    }
    return r;
}

} // namespace

TEST_CASE("player: fires in the middle of the screen (reference for the edge tests)") {
    AS3D_REQUIRE_DATA();
    EdgeRun mid = runEdge(0u, 240, 120);
    CHECK(mid.framesOnScreen == mid.frames);
    CHECK(mid.projectiles > 5);
}

// docs/spec/issues/120-player-fire-at-screen-edge.md: at the clamps the player's box is
// partly outside the 800x600 window; the on-screen bit 0x08 is an overlap test, so the
// player stays collidable and Shoot fires (it did not with the old containment rule).
TEST_CASE("player: fires at the left, right and bottom limits (issue 120, T4)") {
    AS3D_REQUIRE_DATA();
    const u32 dirs[3] = {ACT_LEFT, ACT_RIGHT, ACT_BACKWARD};
    const char* names[3] = {"left", "right", "bottom"};
    for (int k = 0; k < 3; ++k) {
        EdgeRun r = runEdge(dirs[k], 300, 120);
        INFO(names[k] << ": origin " << r.lastOrigin.x << " " << r.lastOrigin.y << ", rect x " << r.lastRect.min[0]
                      << ".." << r.lastRect.max[0] << " y " << r.lastRect.min[1] << ".." << r.lastRect.max[1]
                      << ", on screen " << r.framesOnScreen << "/" << r.frames);
        CHECK(r.projectiles > 5);
        CHECK(r.framesOnScreen == r.frames);
    }
}
