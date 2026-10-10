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
#include "../game/game_flow.h"
#include "../game/game_view.h"
#include "../game/touch_overlay.h"
#include "as3d/defs.h"
#include "as3d/frontend.h"
#include "as3d/gfx.h"
#include "as3d/input.h"
#include "as3d/level.h"
#include "as3d/load_stats.h"
#include "as3d/platform.h"
#include "as3d/precache.h"
#include "as3d/touch_controls.h"
#include "as3d/ui.h"
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
    CHECK(c.str() == "textures=2 models=0 scripts=1 sounds=0 shadows=0 stamps=0 other=1 bytes=150");
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
        // Every kind but the TerraMorph stamps (the sequels), which World::terraMorph reads at
        // every call whose name misses its table (as3d/load_stats.h, Stamp): not precachable
        // without changing the simulation.
        for (int k = 0; k < kLoadKindCount; ++k) {
            if (static_cast<LoadKind>(k) == LoadKind::Stamp) continue;
            const std::string kind = loadKindName(static_cast<LoadKind>(k));
            CAPTURE(kind);
            CHECK(r.lazy.count[k] == 0);
        }
        CHECK(r.lateShadows == 0);
    }
    LoadStats::reset();
}

// ---------------------------------------------------------------------------------------
// Precache completeness for everything a player can trigger (3DS engineering round 2, P1): the
// weapon tour (as3d/input.h applyWeaponTour: every weapon at every upgrade level, every
// missile, every power-up, the pilot collecting the items) over the 3DS frame layout (world
// above, HUD and touch controls on the 320x240 bottom screen: GameView::dualScreen), a pause, a
// hint box and the mission end; then the front end (the first game): every screen reachable
// from the main menu, a mission started from the menus, the in-game menu, the mission
// complete screen. Nothing may load on first use.
// ---------------------------------------------------------------------------------------

namespace {

constexpr int kDualW = 400, kDualH = 480;

// The bottom screen's touch buttons as the 3DS draws them (GameView::dualScreen leaves them to
// the host).
void drawDualTouch(as3d_game::GameView& view, const as3d_game::GameSession& session, TouchMapper& touch) {
    if (!view.hudAvailable() || !session.hasLevel()) return;
    const as3d_game::DualScreenRects rects = as3d_game::dualScreenRects(kDualW, kDualH);
    ui::HudPlayer hud = as3d_game::hudStateOf(session).players[0];
    as3d_game::TouchOverlayState ts;
    ts.player = &hud;
    ts.assets = &view.assets();
    ts.rules = &session.rules();
    as3d_game::drawTouchControlsIn(view.overlay(), rects.bottom.x, rects.bottom.y, rects.bottom.width,
                                   rects.bottom.height, touch, ts);
}

TouchMapper dualTouchMapper() {
    TouchMapper touch;
    const as3d_game::DualScreenRects rects = as3d_game::dualScreenRects(kDualW, kDualH);
    touch.setScreen(rects.bottom.width, rects.bottom.height,
                    TouchLayoutOptions::forBottomScreen320x240(132.0f * rects.scale));
    return touch;
}

struct LazyLog { // the AS3D_LAZY lines name what the closure missed
    LazyLog() { LoadStats::setLogLazy(true); }
    ~LazyLog() { LoadStats::setLogLazy(false); }
};

// The weapon tour on `mission`, precached as as3d_game --precache: `frames` frames of the pilot
// with the tour, a pause of 60 frames at 1/3, a hint box at 1/2 (the pilot's confirm closes it),
// the level ended 300 frames before the end; every frame drawn in the 3DS layout.
bool playTour(int mission, long frames, PlayResult& out) {
    as3d_game::GameOptions o = optionsFor(mission, true);
    o.world.godMode = true; // the tour keeps flying: a lost life clears the items
    // Disarmed until the precache's reset: the session's own loading is not play.
    LoadStats::setLogLazy(false);
    LoadStats::setLogLazy(true);
    as3d_game::GameSession session;
    std::string err;
    if (!session.init(o, &err)) {
        std::fprintf(stderr, "precache_test: mission %d: %s\n", mission, err.c_str());
        return false;
    }
    as3d_game::AudioBridge audio;
    audio.init(session.vfs(), true);
    std::unique_ptr<as3d_game::GameView> view(new as3d_game::GameView());
    view->dualScreen = true;
    view->precacheSounds = [&audio](const PrecacheSet& s) { return audio.precacheSounds(s); };
    if (!view->init(session, &err)) {
        std::fprintf(stderr, "precache_test: renderer: %s\n", err.c_str());
        return false;
    }
    out.report = view->lastPrecache();
    RenderTarget target;
    if (!target.create(kDualW, kDualH, 0)) return false;
    TouchMapper touch = dualTouchMapper();
    for (long f = 0; f < frames; ++f) {
        LoadStats::setFrame(f);
        World& w = session.world();
        if (f == frames / 3) w.setPaused(true);
        if (f == frames / 3 + 60) w.setPaused(false);
        if (f == frames / 2) w.showHint("precache_test: a hint box");
        if (f == frames - 300) w.endLevel();
        FrameInput in = botInput(w, static_cast<u32>(f));
        if (f < frames - 300) applyWeaponTour(w, static_cast<u32>(f), in);
        const int ev = session.step(in);
        if (ev & as3d_game::GameSession::kLevelStarted) break; // not with levelFlow off
        view->step(session);
        audio.drain(session.world());
        audio.pump(44100 / 60);
        target.bind();
        view->draw(session, kDualW, kDualH);
        drawDualTouch(*view, session, touch);
        out.frames = f + 1;
    }
    out.lazy = LoadStats::lazyLoads();
    out.lateShadows = view->renderer().lateShadowMaps();
    return true;
}

void checkNoLazy(const LoadCounts& lazy) {
    for (int k = 0; k < kLoadKindCount; ++k) {
        if (static_cast<LoadKind>(k) == LoadKind::Stamp) continue; // not precachable (see above)
        const std::string kind = loadKindName(static_cast<LoadKind>(k));
        CAPTURE(kind);
        CHECK(lazy.count[k] == 0);
    }
}

} // namespace

TEST_CASE("precache: every weapon, missile, power-up and item, a pause, a hint and the mission end load nothing") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    if (!precacheContext()) {
        std::fprintf(stderr, "SKIPPED (no headless GLES context available): %s\n", __FILE__);
        return;
    }
    LazyLog log;
    const int missions[] = {1, 6, 14};
    for (int mission : missions) {
        if (mission > testdata::game().rules.missionCount) continue;
        CAPTURE(mission);
        PlayResult r;
        REQUIRE(playTour(mission, 3600, r));
        std::printf("precache_test: %s mission %d weapon tour: precache %s; after %ld frames lazy %s late_shadow_maps=%d\n",
                    testdata::gameKey().c_str(), mission, r.report.str().c_str(), r.frames, r.lazy.str().c_str(),
                    r.lateShadows);
        CHECK(r.frames == 3600);
        CHECK_FALSE(r.report.capped);
        checkNoLazy(r.lazy);
        CHECK(r.lateShadows == 0);
    }
    LoadStats::reset();
}

namespace {

// The 3DS front end on the desktop (ctr_menu_loop.cpp consoleFlow: touch mode without the MENU
// button, console options, one player; the HUD on the 320x240 bottom screen), every frame drawn
// in the 3DS layout.
struct FlowRun {
    as3d_game::GameSession session;
    as3d_game::AudioBridge audio;
    std::unique_ptr<as3d_game::GameView> view;
    std::unique_ptr<as3d_game::GameFlow> flow;
    RenderTarget target;
    TouchMapper touch = dualTouchMapper();
    u32 frame = 0;

    bool init() {
        as3d_game::GameOptions o = optionsFor(1, true);
        o.startLevel = false;
        std::string err;
        if (!session.init(o, &err)) {
            FAIL_CHECK(err);
            return false;
        }
        audio.init(session.vfs(), true);
        view.reset(new as3d_game::GameView());
        view->dualScreen = true;
        view->precacheSounds = [this](const PrecacheSet& s) { return audio.precacheSounds(s); };
        if (!view->init(session, &err, false) || !target.create(kDualW, kDualH, 0)) {
            FAIL_CHECK(err);
            return false;
        }
        flow.reset(new as3d_game::GameFlow(session, audio));
        as3d_game::FlowConfig c;
        c.touch = true;
        c.twoPlayerMode = false;
        c.mouseControlOption = false;
        c.touchMenuButton = false;
        c.consoleOptions = true;
        c.attract = 1;
        c.showLogo = false;
        c.textsPath = testdata::extractedDir() + "/texts_v170.txt";
        c.settingsXml = testdata::installDir() + "/data/Settings.xml";
        if (!flow->init(c, &err)) {
            FAIL_CHECK(err);
            return false;
        }
        flow->setView(view.get());
        flow->setScreenSize(320, 240);
        flow->frontend().setHudTarget(ui::HudTarget::Bottom320x240);
        flow->boot();
        return true;
    }
    ui::Frontend& fe() { return flow->frontend(); }
    // One frame: UI events, a world step (the pilot and the weapon tour while playing), drawn.
    void step(const ui::UiInput& ui = {}, bool tour = false) {
        LoadStats::setFrame(static_cast<long>(frame));
        flow->uiFrame(1.0f / 60.0f, ui);
        FrameInput in;
        if (tour && session.hasLevel()) { // the pilot's confirm also closes the tutorial's hint boxes
            in = botInput(session.world(), frame);
            if (flow->playing()) applyWeaponTour(session.world(), frame, in);
        }
        flow->step(in);
        if (session.hasLevel()) audio.drain(session.world());
        audio.pump(735);
        target.bind();
        flow->draw(kDualW, kDualH);
        if (flow->playing()) drawDualTouch(*view, session, touch);
        ++frame;
    }
    void wait(int frames, bool tour = false) {
        for (int i = 0; i < frames; ++i) step({}, tour);
    }
    void tap(float x, float y) { step(ui::UiInput().tap(x, y)); }
    void key(int code) { step(ui::UiInput().key(code)); }
};

} // namespace

TEST_CASE("precache: the front end's screens and a mission started from the menus load nothing (the first game)") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    if (testdata::game().id != GameId::AirStrike3D) {
        std::fprintf(stderr, "SKIPPED (game '%s': drives the first game's menus): %s\n", testdata::gameKey().c_str(), __FILE__);
        return;
    }
    if (!precacheContext()) {
        std::fprintf(stderr, "SKIPPED (no headless GLES context available): %s\n", __FILE__);
        return;
    }
    LazyLog log;
    LoadStats::setLogLazy(false); // armed by the attract level's precache
    LoadStats::setLogLazy(true);
    FlowRun h;
    REQUIRE(h.init());
    h.wait(60);
    REQUIRE(h.fe().menuOpen());
    REQUIRE(h.fe().topScreen() == ui::Screen::MainMenu);
    // Every screen the main menu reaches (the Information pages one by one), back to the main
    // menu after each.
    const ui::Screen screens[] = {ui::Screen::StartGame, ui::Screen::TopScores, ui::Screen::Options,
                                  ui::Screen::Information, ui::Screen::Exit};
    for (ui::Screen s : screens) {
        h.fe().open(s);
        h.wait(45);
        if (s == ui::Screen::Information)
            for (int page = 2; page <= 10 && h.fe().debugSet("page", std::to_string(page)); ++page) h.wait(10);
        h.fe().open(ui::Screen::MainMenu);
        h.wait(20);
    }
    const LoadCounts frontEnd = LoadStats::lazyLoads();
    std::printf("precache_test: as3d front end (attract level 1, every screen): lazy %s\n", frontEnd.str().c_str());
    {
        INFO("the front end");
        checkNoLazy(frontEnd);
    }
    // Start Game, Start: mission 1 from the menus (the view precaches it and resets the counters).
    h.tap(400, 266);
    REQUIRE(h.fe().topScreen() == ui::Screen::StartGame);
    h.tap(680, 482);
    REQUIRE(h.fe().state() == ui::FrontendState::Playing);
    REQUIRE(h.session.mission() == 1);
    h.wait(900, true);
    for (int i = 0; i < 600 && h.fe().menuOpen(); ++i) h.wait(1, true); // a tutorial hint box
    // The in-game menu (START), back to play; a hint box; the mission end and its screen.
    h.key(ui::keys::Escape);
    CHECK(h.fe().topScreen() == ui::Screen::InGame);
    h.wait(40);
    h.key(ui::keys::Escape);
    h.wait(300, true);
    h.session.world().showHint("precache_test: a hint box");
    h.wait(60);
    FrameInput ok;
    ok.confirm = true;
    h.flow->step(ok);
    h.wait(300, true);
    h.session.world().endLevel();
    h.wait(400);
    CHECK(h.fe().menuOpen());
    const LoadCounts mission = LoadStats::lazyLoads();
    std::printf("precache_test: as3d mission 1 from the menus (tour, in-game menu, hint, mission complete): lazy %s\n",
                mission.str().c_str());
    {
        INFO("mission 1 from the menus");
        checkNoLazy(mission);
    }
    LoadStats::reset();
}
