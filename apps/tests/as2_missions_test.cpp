// AirStrike 2 on its shipped levels (package C6): every mission and both attract levels run
// 1500 frames under the world-aware pilot in god mode with no script error, stall, refused
// spawn or stub builtin (the complete runs are in docs/missions-status-as2.md); and drawing
// every frame, with craters and skid trails live, leaves the simulation byte-identical. The
// sequel is selected explicitly (gameProfile(GameId::AirStrike2), locateGameData), so these
// run in every pass of tools/ci.sh; they skip loudly without its data or a GLES context.
#include "doctest.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <string>

#include "as3d/defs.h"
#include "as3d/game_data.h"
#include "as3d/gfx.h"
#include "as3d/input.h"
#include "as3d/platform.h"
#include "as3d/script_host.h"
#include "as3d/vfs.h"
#include "as3d/world.h"
#include "as3d/world_render.h"
#include "test_data.h"

using namespace as3d;

namespace {

const GameData& as2Data() {
    static const GameData d = locateGameData(testdata::root(), gameProfile(GameId::AirStrike2));
    return d;
}

// These play the whole of the sequel's levels (about 25 s): they run in the default (as3d)
// pass and in the as2 pass of tools/ci.sh, not again in the passes of the other games.
#define REQUIRE_AS2_DATA()                                                                                         \
    if (!as2Data().hasExtracted) {                                                                                 \
        std::fprintf(stderr, "SKIPPED (no AirStrike 2 data in %s): %s\n", as2Data().extractedDir.c_str(), __FILE__); \
        return;                                                                                                    \
    }                                                                                                              \
    if (testdata::gameKey() != "as3d" && testdata::gameKey() != "as2") {                                          \
        std::fprintf(stderr, "SKIPPED (AirStrike 2 play tests run in the as3d and as2 passes): %s\n", __FILE__);    \
        return;                                                                                                    \
    }

struct As2 {
    Vfs vfs;
    DefDatabase db;
    As2() {
        vfs.mount(makeDirSource(as2Data().extractedDir));
        REQUIRE(db.load(vfs));
    }
    void start(World& w, const std::string& level) {
        WorldConfig cfg;
        cfg.rules = &gameProfile(GameId::AirStrike2).rules;
        cfg.godMode = true;
        w.init(vfs, db, cfg);
        std::string err;
        REQUIRE_MESSAGE(w.loadLevel(level, &err), err);
    }
};

GraphicsContext* glContext() {
    static std::unique_ptr<GraphicsContext> ctx = [] {
        GraphicsConfig cfg;
        cfg.width = 64;
        cfg.height = 64;
        cfg.headless = true;
        cfg.title = "as3d_tests";
        return createGraphicsContext(cfg);
    }();
    if (ctx) ctx->makeCurrent();
    return ctx.get();
}

} // namespace

TEST_CASE("as2 missions: every mission and attract level, 1500 frames, no script error, stall, refusal or stub") {
    REQUIRE_AS2_DATA();
    As2 d;
    const int missions = gameProfile(GameId::AirStrike2).rules.missionCount;
    for (int m = 1; m <= missions + 2; ++m) {
        const std::string level = m <= missions ? std::to_string(m) : "intro" + std::to_string(m - missions);
        INFO("as2 level " << level);
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

TEST_CASE("as2 rendering does not perturb the simulation: craters and skid trails live") {
    REQUIRE_AS2_DATA();
    GraphicsContext* ctx = glContext();
    if (!ctx) {
        std::fprintf(stderr, "SKIPPED (no headless GLES context available): %s\n", __FILE__);
        return;
    }
    As2 d;
    const u32 frames = 1200;
    // Craters under the camera at fixed frames (the same simulation input in both runs), on
    // top of whatever the level's explosions make.
    auto crater = [](World& w, u32 f) {
        if (f == 150 || f == 600 || f == 601) w.terraMorph(640.0f, w.mapPos() + 300.0f, "morphmaps/map2.tga");
    };
    std::string reference;
    {
        World w;
        d.start(w, "2");
        for (u32 f = 0; f < frames; ++f) {
            crater(w, f);
            w.step(botInput(w, f).toPlayerInput());
        }
        reference = w.dumpStateJson();
    }
    World w;
    d.start(w, "2");
    WorldRenderer renderer;
    std::string err;
    REQUIRE_MESSAGE(renderer.init(d.vfs, d.db, &err), err);
    REQUIRE_MESSAGE(renderer.beginLevel(w, &err), err);
    RenderTarget target;
    REQUIRE(target.create(320, 240, 0));
    int maxTrails = 0;
    for (u32 f = 0; f < frames; ++f) {
        crater(w, f);
        w.step(botInput(w, f).toPlayerInput());
        target.bind();
        renderer.render(w, 320, 240);
        maxTrails = std::max(maxTrails, renderer.lastStats().skidTrails);
    }
    CHECK(w.terrainRevision() >= 3);
    CHECK(maxTrails > 0);
    CHECK(w.dumpStateJson() == reference);

    // The crater reaches the pixels: the same world drawn before and after a big stamp under
    // the view's centre, without a step in between (only the terrain changed).
    WorldRenderOptions noParticles;
    noParticles.particles = false;
    Image before, after;
    target.bind();
    renderer.render(w, 320, 240, noParticles);
    REQUIRE(target.readPixels(before));
    const float cx = w.camera().field[0], cy = w.mapPos() + 330.0f;
    const u32 rev = w.terrainRevision();
    for (int k = 0; k < 4; ++k) w.terraMorph(cx, cy, "morphmaps/map1_big.tga");
    REQUIRE(w.terrainRevision() > rev);
    target.bind();
    renderer.render(w, 320, 240, noParticles);
    REQUIRE(target.readPixels(after));
    long changed = 0;
    for (size_t i = 0; i + 3 < before.rgba.size(); i += 4) {
        int d = 0;
        for (int c = 0; c < 3; ++c) d = std::max(d, std::abs(before.rgba[i + c] - after.rgba[i + c]));
        changed += d > 24 ? 1 : 0;
    }
    MESSAGE("pixels changed by the crater: " << changed);
    CHECK(changed > 200);
}
