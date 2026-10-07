// Level-start precache (3DS port M1.10, as3d/precache.h, as3d/load_stats.h): after
// World::precacheScripts, WorldRenderer::precache, the play pictures and the sounds, a level
// played by the bot for a minute with rendering at 400x240 (GameView::draw, every frame) loads
// nothing on first use. Also: the counters themselves, and that the precache leaves the
// simulation as it was. Needs the game's data and, for the render run, a headless GLES context;
// skips loudly without them.
#include "doctest.h"

#include <cstdio>
#include <memory>
#include <string>

#include "../game/audio_bridge.h"
#include "../game/game_session.h"
#include "../game/game_view.h"
#include "as3d/defs.h"
#include "as3d/gfx.h"
#include "as3d/input.h"
#include "as3d/level.h"
#include "as3d/load_stats.h"
#include "as3d/platform.h"
#include "as3d/precache.h"
#include "as3d/world.h"
#include "test_data.h"

using namespace as3d;

namespace {

std::unique_ptr<GraphicsContext>& precacheContext() {
    static std::unique_ptr<GraphicsContext> ctx = [] {
        GraphicsConfig cfg;
        cfg.width = 64;
        cfg.height = 64;
        cfg.headless = true;
        return createGraphicsContext(cfg);
    }();
    if (ctx) ctx->makeCurrent();
    return ctx;
}

as3d_game::GameOptions optionsFor(int mission, bool precache) {
    as3d_game::GameOptions o;
    o.dataRoot = testdata::root();
    o.game = &testdata::game();
    o.mission = mission;
    o.levelFlow = false;
    o.precache = precache;
    return o;
}

struct PlayResult {
    PrecacheReport report;
    LoadCounts lazy;
    int lateShadows = 0;
    long frames = 0;
};

// The production path of as3d_game --precache: the view precaches the level in beginLevel
// (with the sounds through the audio bridge, on the null device) and resets the counters; then
// the bot plays `frames` frames, every one drawn into a 400x240 target.
bool playPrecached(int mission, long frames, PlayResult& out) {
    as3d_game::GameSession session;
    std::string err;
    if (!session.init(optionsFor(mission, true), &err)) {
        std::fprintf(stderr, "precache_test: mission %d: %s\n", mission, err.c_str());
        return false;
    }
    as3d_game::AudioBridge audio;
    audio.init(session.vfs(), true);
    std::unique_ptr<as3d_game::GameView> view(new as3d_game::GameView());
    view->precacheSounds = [&audio](const PrecacheSet& s) { return audio.precacheSounds(s); };
    if (!view->init(session, &err)) {
        std::fprintf(stderr, "precache_test: renderer: %s\n", err.c_str());
        return false;
    }
    out.report = view->lastPrecache();
    // beginLevel reset the counters after the precache: from here on every load is lazy.
    const int W = 400, H = 240;
    RenderTarget target;
    if (!target.create(W, H, 0)) return false;
    for (long f = 0; f < frames; ++f) {
        const int ev = session.step(botInput(session.world(), static_cast<u32>(f)));
        if (ev & as3d_game::GameSession::kLevelStarted) break; // not with levelFlow off
        view->step(session);
        audio.drain(session.world());
        audio.pump(44100 / 60);
        target.bind();
        view->draw(session, W, H);
        out.frames = f + 1;
    }
    out.lazy = LoadStats::lazyLoads();
    out.lateShadows = view->renderer().lateShadowMaps();
    return true;
}

} // namespace

TEST_CASE("load stats: counts per kind, sites, bytes and reset") {
    LoadStats::reset();
    LoadStats::count(LoadKind::Texture);
    LoadStats::count(LoadKind::Texture);
    LoadStats::count(LoadKind::Script);
    LoadStats::fileRead(100); // outside a site: Other
    {
        LoadStats::Site site;
        LoadStats::fileRead(50); // the site's own read
    }
    LoadCounts c = LoadStats::lazyLoads();
    CHECK(c.of(LoadKind::Texture) == 2);
    CHECK(c.of(LoadKind::Script) == 1);
    CHECK(c.of(LoadKind::Other) == 1);
    CHECK(c.of(LoadKind::Model) == 0);
    CHECK(c.bytes == 150);
    CHECK(c.total() == 4);
    CHECK(c.str() == "textures=2 models=0 scripts=1 sounds=0 shadows=0 other=1 bytes=150");
    const LoadCounts before = c;
    LoadStats::count(LoadKind::Sound);
    CHECK((LoadStats::lazyLoads() - before).of(LoadKind::Sound) == 1);
    LoadStats::reset();
    CHECK(LoadStats::lazyLoads().total() == 0);
    CHECK(LoadStats::lazyLoads().bytes == 0);
}

TEST_CASE("precache: the closure covers the level and the scripts compile once") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    as3d_game::GameSession session;
    std::string err;
    REQUIRE(session.init(optionsFor(1, false), &err));
    World& w = session.world();
    CHECK(w.precacheSet() == nullptr);
    const PrecacheReport r = w.precacheScripts();
    REQUIRE(w.precacheSet() != nullptr);
    const PrecacheSet& s = *w.precacheSet();
    MESSAGE("mission 1 world precache: " << r.str() << " particle systems=" << s.particleSystems.size()
                                         << " script models=" << s.models.size() << " script textures=" << s.textures.size()
                                         << " sounds=" << s.sounds.size() << " programs scanned=" << s.programsScanned);
    CHECK_FALSE(s.capped);
    CHECK(r.objects > 0);
    CHECK(r.scripts > 0);
    CHECK(s.programsScanned > 0);
    // Every placement's object is in the closure.
    const LevelData* level = w.terrain() ? w.terrain()->level() : nullptr;
    REQUIRE(level != nullptr);
    for (const Placement& p : level->placements) {
        const ObjectDef* def = session.db().findObject(level->typeName(p));
        if (!def) continue;
        bool found = false;
        for (const ObjectDef* o : s.objects) found = found || o == def;
        CHECK_MESSAGE(found, level->typeName(p));
    }
    // A second precache compiles nothing more.
    LoadStats::reset();
    const PrecacheReport again = w.precacheScripts();
    CHECK(again.scripts == 0);
    CHECK(LoadStats::lazyLoads().of(LoadKind::Script) == 0);
}

TEST_CASE("precache: the simulation is the same with and without it") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    std::string dumps[2];
    for (int k = 0; k < 2; ++k) {
        as3d_game::GameSession session;
        std::string err;
        REQUIRE(session.init(optionsFor(1, false), &err));
        if (k == 1) session.world().precacheScripts();
        for (u32 f = 0; f < 600; ++f) session.step(botInput(session.world(), f));
        dumps[k] = session.world().dumpStateJson();
    }
    CHECK(dumps[0] == dumps[1]);
}

TEST_CASE("precache: nothing loads on first use during a minute of play") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    if (!precacheContext()) {
        std::fprintf(stderr, "SKIPPED (no headless GLES context available): %s\n", __FILE__);
        return;
    }
    const int missions[] = {1, 6, 14};
    for (int mission : missions) {
        if (mission > testdata::game().rules.missionCount) continue;
        CAPTURE(mission);
        PlayResult r;
        REQUIRE(playPrecached(mission, 3600, r));
        std::printf("precache_test: %s mission %d: precache %s; after %ld frames lazy %s late_shadow_maps=%d\n",
                    testdata::gameKey().c_str(), mission, r.report.str().c_str(), r.frames, r.lazy.str().c_str(),
                    r.lateShadows);
        CHECK(r.frames == 3600);
        CHECK_FALSE(r.report.capped);
        for (int k = 0; k < kLoadKindCount; ++k) {
            CAPTURE(loadKindName(static_cast<LoadKind>(k)));
            CHECK(r.lazy.count[k] == 0);
        }
        CHECK(r.lateShadows == 0);
    }
    LoadStats::reset();
}
