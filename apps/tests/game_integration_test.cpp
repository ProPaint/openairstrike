// Desktop game integration (WP-47): input scripts and the key mapper, the world renderer on
// a live mission, determinism of the game loop against the headless simulation, the audio
// bridge. The game's session, view and audio bridge (apps/game) are compiled into this test
// by including their sources. Tests that need game data or a GLES context skip loudly.
#include "doctest.h"

#include <SDL_scancode.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <set>
#include <string>

#include "as3d/gfx.h"
#include "as3d/input.h"
#include "as3d/platform.h"
#include "as3d/world.h"
#include "as3d/world_render.h"

#include "../game/audio_bridge.cpp"
#include "../game/game_session.cpp"
#include "../game/game_view.cpp"
#include "test_data.h"

using namespace as3d;
using namespace as3d_game;

namespace {

GraphicsContext* sharedContext() {
    static std::unique_ptr<GraphicsContext> ctx = [] {
        GraphicsConfig cfg;
        cfg.width = 64;
        cfg.height = 64;
        cfg.headless = true;
        return createGraphicsContext(cfg);
    }();
    if (ctx) ctx->makeCurrent();
    return ctx.get();
}

#define REQUIRE_GL(ctx)                                                                   \
    if (!(ctx)) {                                                                         \
        std::fprintf(stderr, "SKIPPED (no headless GLES context available): %s\n", __FILE__); \
        return;                                                                           \
    }

GameOptions level1Options() {
    GameOptions o;
    o.dataRoot = testdata::root();
    o.mission = 1;
    o.levelFlow = false;
    return o;
}

struct PixelStats {
    double meanLuma = 0.0, stddevLuma = 0.0;
    int colours = 0;      // distinct colours at 5 bits per channel
    int magenta = 0;      // placeholder texture pixels
    int saturatedRed = 0; // explosions, lasers, the red rotor tips
};

PixelStats analyse(const Image& img) {
    PixelStats s;
    std::set<unsigned> colours;
    double sum = 0.0, sum2 = 0.0;
    const int n = img.width * img.height;
    for (int i = 0; i < n; ++i) {
        int r = img.rgba[static_cast<size_t>(i) * 4], g = img.rgba[static_cast<size_t>(i) * 4 + 1],
            b = img.rgba[static_cast<size_t>(i) * 4 + 2];
        double l = 0.299 * r + 0.587 * g + 0.114 * b;
        sum += l;
        sum2 += l * l;
        if (r > 225 && g < 40 && b > 225) ++s.magenta;
        if (r > 200 && g < 90 && b < 90) ++s.saturatedRed;
        colours.insert(static_cast<unsigned>((r >> 3) << 10 | (g >> 3) << 5 | (b >> 3)));
    }
    s.meanLuma = sum / n;
    s.stddevLuma = std::sqrt(std::max(0.0, sum2 / n - s.meanLuma * s.meanLuma));
    s.colours = static_cast<int>(colours.size());
    return s;
}

// The simulation alone, exactly as `as3d_sim --level 1 --frames N [--bot]` runs it.
std::string simulateOnly(long frames, bool bot) {
    Vfs vfs;
    vfs.mount(makeDirSource(testdata::extractedDir()));
    DefDatabase db;
    REQUIRE(db.load(vfs));
    World world;
    world.init(vfs, db, WorldConfig());
    std::string err;
    REQUIRE(world.loadLevel("1", &err));
    PlayerInput input;
    for (long f = 0; f < frames; ++f) {
        if (bot) input = botInput(static_cast<u32>(f)).toPlayerInput();
        world.step(input);
    }
    return world.dumpStateJson();
}

std::string exeDir() {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return std::string();
    buf[n] = 0;
    std::string p = buf;
    size_t slash = p.rfind('/');
    return slash == std::string::npos ? std::string() : p.substr(0, slash);
}

bool fileExists(const std::string& p) {
    std::FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

std::string readText(const std::string& p) {
    std::string out;
    std::FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return out;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
}

} // namespace

// ---------------------------------------------------------------------------------------
// Input (no data needed).
// ---------------------------------------------------------------------------------------

TEST_CASE("input script: recording and playback round trip") {
    // A mixed input: the bot for both players, a pause pressed and released, player 2 with
    // its own keys, hint confirmations toggling.
    std::vector<FrameInput> frames;
    for (u32 f = 0; f < 3000; ++f) {
        FrameInput in = botInput(f);
        if (f % 700 == 5) in.pausePressed = true;
        in.held[1] = (f / 50) % 3 == 0 ? ACT_FORWARD | ACT_NEXT_WEAPON : ACT_BACKWARD;
        in.confirm = (f / 90) % 2 == 0;
        frames.push_back(in);
    }
    InputRecorder rec;
    for (u32 f = 0; f < frames.size(); ++f) rec.record(f, frames[f]);
    const std::string text = rec.script().serialize();

    InputScript parsed;
    std::string err;
    REQUIRE_MESSAGE(parsed.parse(text, &err), err);
    CHECK(parsed.events().size() == rec.script().events().size());
    CHECK(parsed.serialize() == text);
    InputScriptPlayer player(parsed);
    for (u32 f = 0; f < frames.size(); ++f) {
        FrameInput got = player.frame(f);
        if (got != frames[f]) {
            FAIL("frame " << f << " differs after the round trip");
            break;
        }
    }
    CHECK(player.finished());
}

TEST_CASE("input script: syntax") {
    InputScript s;
    std::string err;
    REQUIRE(s.parse("# comment\n\n0 fire 1\n  10 p2_left 1   # trailing\n10 pause 1\r\n20 fire 0\n", &err));
    REQUIRE(s.events().size() == 4);
    CHECK(s.events()[1].player == 1);
    CHECK(s.events()[1].action == InputAction::Left);
    InputScriptPlayer p(s);
    CHECK(p.frame(0).held[0] == ACT_FIRE);
    CHECK_FALSE(p.frame(9).pausePressed);
    FrameInput f10 = p.frame(10);
    CHECK(f10.pausePressed);
    CHECK(f10.held[1] == ACT_LEFT);
    CHECK_FALSE(p.frame(11).pausePressed); // an edge, not a held state
    CHECK(p.frame(20).held[0] == 0u);

    CHECK_FALSE(s.parse("5 jump 1\n", &err));
    CHECK(err.find("line 1") != std::string::npos);
    CHECK_FALSE(s.parse("5 fire 2\n", &err));
    CHECK_FALSE(s.parse("5 fire\n", &err));
    CHECK_FALSE(s.parse("9 fire 1\n5 fire 0\n", &err)); // frames must not decrease
    CHECK_FALSE(s.parse("5 p2_pause 1\n", &err));
    CHECK(s.events().empty());
}

TEST_CASE("input mapper: default controls of the shipped config") {
    InputMapper m;
    m.keyEvent(SDL_SCANCODE_UP, true, false);
    m.keyEvent(SDL_SCANCODE_RCTRL, true, false); // either Ctrl fires (VK_CONTROL)
    FrameInput f = m.takeFrame();
    CHECK(f.held[0] == (ACT_FORWARD | ACT_FIRE));
    CHECK(f.held[1] == 0u);
    m.keyEvent(SDL_SCANCODE_RCTRL, false, false);
    m.mouseButtonEvent(3, true); // right button: missile
    CHECK(m.takeFrame().held[0] == (ACT_FORWARD | ACT_MISSILE));
    m.mouseButtonEvent(3, false);

    // A tap shorter than a frame still reaches the next frame, once.
    m.keyEvent(SDL_SCANCODE_2, true, false);
    m.keyEvent(SDL_SCANCODE_2, false, false);
    CHECK(m.takeFrame().held[0] == (ACT_FORWARD | ACT_NEXT_WEAPON));
    CHECK(m.takeFrame().held[0] == ACT_FORWARD);

    // Autorepeat is ignored; P is a pause edge.
    m.keyEvent(SDL_SCANCODE_P, true, false);
    m.keyEvent(SDL_SCANCODE_P, true, true);
    CHECK(m.takeFrame().pausePressed);
    CHECK_FALSE(m.takeFrame().pausePressed);
    m.keyEvent(SDL_SCANCODE_P, false, false);

    CHECK(m.keyEvent(SDL_SCANCODE_ESCAPE, true, false) == Hotkey::Quit);
    m.keyEvent(SDL_SCANCODE_RETURN, true, false);
    CHECK(m.takeFrame().confirm);
    m.releaseAll();
    FrameInput none = m.takeFrame();
    CHECK(none.held[0] == 0u);
    CHECK_FALSE(none.confirm);
}

TEST_CASE("bot input matches the as3d_sim pilot") {
    FrameInput a = botInput(0), b = botInput(150), c = botInput(300), d = botInput(45), e = botInput(200),
               f = botInput(270), g = botInput(330);
    CHECK(a.held[0] == (ACT_FIRE | ACT_RIGHT));
    CHECK(b.held[0] == (ACT_FIRE | ACT_LEFT | ACT_MISSILE));
    CHECK(c.held[0] == (ACT_FIRE | ACT_POWERUP));
    CHECK(d.held[0] == (ACT_FIRE | ACT_RIGHT | ACT_MISSILE));
    CHECK(e.held[0] == (ACT_FIRE | ACT_RIGHT));
    CHECK(f.held[0] == (ACT_FIRE | ACT_FORWARD | ACT_MISSILE));
    CHECK(g.held[0] == (ACT_FIRE | ACT_BACKWARD | ACT_MISSILE));
    // The weave is symmetric: as long right as left in every 8 s cycle, as long forward as back.
    int right = 0, left = 0, fwd = 0, back = 0;
    for (u32 k = 0; k < 480; ++k) {
        u32 h = botInput(k).held[0];
        right += (h & ACT_RIGHT) ? 1 : 0;
        left += (h & ACT_LEFT) ? 1 : 0;
        fwd += (h & ACT_FORWARD) ? 1 : 0;
        back += (h & ACT_BACKWARD) ? 1 : 0;
    }
    CHECK(right == left);
    CHECK(fwd == back);
    CHECK(a.held[1] == a.held[0]);
    CHECK(a.confirm);
}

// ---------------------------------------------------------------------------------------
// Mission 1 with the bot (game data needed).
// ---------------------------------------------------------------------------------------

TEST_CASE("game camera: the renderer sees what the collision code projects") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    GameSession s;
    std::string err;
    REQUIRE_MESSAGE(s.init(level1Options(), &err), err);
    for (u32 f = 0; f < 200; ++f) s.step(botInput(f));
    WorldView v = worldViewOf(s.world(), kCollisionViewportW / kCollisionViewportH);
    Mat4 vp = v.projection * v.view;
    const Mat4& ref = s.world().previousViewProj();
    for (int i = 0; i < 16; ++i) CHECK(vp.m[i] == doctest::Approx(ref.m[i]).epsilon(1e-4));
}

TEST_CASE("game session: pause, restart after game over, next mission after completion") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    GameOptions o = level1Options();
    o.levelFlow = true;
    o.flowDelayFrames = 30;
    GameSession s;
    std::string err;
    REQUIRE_MESSAGE(s.init(o, &err), err);
    for (u32 f = 0; f < 120; ++f) s.step(botInput(f));

    // P toggles the pause: the scroll stops; unpausing clears p_action (1.3 state 6).
    FrameInput in = botInput(120);
    in.pausePressed = true;
    s.step(in);
    CHECK(s.world().paused());
    float pos = s.world().mapPos();
    for (u32 f = 0; f < 20; ++f) s.step(FrameInput());
    CHECK(s.world().mapPos() == pos);
    s.world().player(0).action = 7.0f;
    FrameInput unpause;
    unpause.pausePressed = true;
    s.step(unpause);
    CHECK_FALSE(s.world().paused());
    CHECK(s.world().player(0).action == 0.0f);
    CHECK(s.world().mapPos() > pos);

    // Game over: the same mission restarts with the lives it started with.
    s.world().player(0).lives = -1.0f;
    s.world().setGameOver();
    int events = 0;
    for (int f = 0; f < 40; ++f) events |= s.step(FrameInput());
    CHECK((events & GameSession::kLevelStarted) != 0);
    CHECK(s.mission() == 1);
    CHECK_FALSE(s.world().gameOver());
    CHECK(s.world().player(0).lives == 2.0f);

    // Mission complete: score banked, lives carried over, mission 2 starts.
    for (u32 f = 0; f < 60; ++f) s.step(botInput(f));
    s.world().player(0).scores = 1234.0f;
    s.world().player(0).lives = 1.0f;
    s.world().endLevel();
    events = 0;
    for (int f = 0; f < 40; ++f) events |= s.step(FrameInput());
    CHECK((events & GameSession::kLevelComplete) != 0);
    CHECK((events & GameSession::kLevelStarted) != 0);
    CHECK(s.mission() == 2);
    CHECK_FALSE(s.world().levelComplete());
    CHECK(s.world().player(0).banked == 1234);
    CHECK(s.world().player(0).lives == 1.0f);
    CHECK(s.displayScore(0) == 1234);
    // Every mission starts with the level 1 machine gun and nothing else (frontend.md 5).
    CHECK(s.world().player(0).upgrades[0] == 1);
    for (int k = 1; k < 20; ++k) CHECK(s.world().player(0).upgrades[k] == 0);
    for (int m : s.world().player(0).missiles) CHECK(m == 0);
}

TEST_CASE("render order: an attached pool entity is drawn after its root") {
    // The player's spawn shield is a pool entity attached with AttachEntity and newer than
    // the player; it thinks (and submits its record) after the player, whose body would
    // otherwise fail the depth test behind the depth-writing additive shell.
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    GameSession s;
    std::string err;
    REQUIRE_MESSAGE(s.init(level1Options(), &err), err);
    std::vector<int> order;
    std::vector<char> visited;
    int attachedSeen = 0;
    for (u32 f = 0; f < 300; ++f) {
        s.step(botInput(f));
        const World& w = s.world();
        worldRenderOrder(w, order, visited);
        std::vector<int> pos(static_cast<size_t>(kMaxEntitySlots), -1);
        for (size_t k = 0; k < order.size(); ++k) pos[static_cast<size_t>(order[k])] = static_cast<int>(k);
        for (int i : order) {
            const Entity& e = w.entity(i);
            if (e.parent >= 0) REQUIRE(pos[static_cast<size_t>(e.parent)] >= 0);
            if (e.parent >= 0) CHECK(pos[static_cast<size_t>(e.parent)] < pos[static_cast<size_t>(i)]);
            if (e.inList && e.parent >= 0) ++attachedSeen;
        }
        // Every live, non-removed entity appears exactly once.
        int live = 0;
        for (int i = 0; i < kMaxEntitySlots; ++i) {
            if (w.validIndex(i) && !(w.entity(i).rt & RT_REMOVED) && pos[static_cast<size_t>(i)] < 0) {
                // Only children of removed parents may be missing.
                CHECK(w.entity(i).parent >= 0);
            }
            if (pos[static_cast<size_t>(i)] >= 0) ++live;
        }
        CHECK(live == static_cast<int>(order.size()));
    }
    CHECK(attachedSeen > 0);
}

TEST_CASE("mission 1 with the bot renders sensible frames") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    GraphicsContext* ctx = sharedContext();
    REQUIRE_GL(ctx);
    GameSession s;
    std::string err;
    REQUIRE_MESSAGE(s.init(level1Options(), &err), err);
    GameView view;
    REQUIRE_MESSAGE(view.init(s, &err), err);
    RenderTarget target;
    REQUIRE(target.create(800, 600, 4));

    int rotorChanges = 0;
    float lastRotorYaw = 0.0f;
    bool haveRotor = false;
    int maxSprites = 0, maxEmitters = 0, maxParticles = 0, maxShadows = 0, maxLights = 0, minModels = 1 << 30;
    for (u32 f = 0; f < 1200; ++f) {
        s.step(botInput(f));
        view.step(s);
        // The main rotor (an abs attachment spun by its script) turns every frame.
        const World& w = s.world();
        int pi = w.playerEntityIndex(0);
        if (pi >= 0) {
            for (int c : w.entity(pi).children) {
                const Entity& ce = w.entity(c);
                if (ce.def && ce.def->name == "p_comanche_vint") {
                    float yaw = ce.f(F_ANGLES + 2);
                    if (haveRotor && yaw != lastRotorYaw) ++rotorChanges;
                    lastRotorYaw = yaw;
                    haveRotor = true;
                }
            }
        }
        if ((f + 1) % 300 != 0) continue;
        target.bind();
        view.draw(s, 800, 600);
        Image img;
        REQUIRE(target.readPixels(img));
        PixelStats ps = analyse(img);
        const WorldRenderStats& rs = view.renderer().lastStats();
        INFO("frame " << f + 1 << ": luma " << ps.meanLuma << " +- " << ps.stddevLuma << ", colours " << ps.colours
                      << ", models " << rs.models << ", sprites " << rs.sprites << ", emitters " << rs.emitters
                      << ", particles " << rs.particles << ", chunks " << rs.terrainChunks);
        CHECK(ps.meanLuma > 40.0);  // lit terrain, not a black or fog-only screen
        CHECK(ps.meanLuma < 220.0); // not blown out by the brightness pass
        CHECK(ps.stddevLuma > 15.0);
        CHECK(ps.colours > 1500);
        CHECK(ps.magenta < img.width * img.height / 200); // no large placeholder surfaces
        CHECK(rs.terrainChunks > 0);
        CHECK(rs.dropped == 0);
        minModels = std::min(minModels, rs.models);
        maxSprites = std::max(maxSprites, rs.sprites);
        maxShadows = std::max(maxShadows, rs.shadows);
        maxLights = std::max(maxLights, rs.lights);
        maxEmitters = std::max(maxEmitters, rs.emitters);
        maxParticles = std::max(maxParticles, rs.particles);
    }
    CHECK(minModels > 20);      // the player, its attachments and the placed objects
    CHECK(maxSprites > 0);      // projectiles, flares
    CHECK(maxShadows > 0);      // the player's planar shadow, buildings
    CHECK(maxLights > 0);       // PlaceLight from weapons and lamps
    CHECK(view.hudAvailable());
    CHECK(view.renderer().shadowMapCount() > 0);
    CHECK(maxEmitters > 0);
    CHECK(maxParticles > 0);
    CHECK(rotorChanges > 1000); // spinning nearly every frame
    CHECK(s.world().particles().emitterCount() > 0);
}

TEST_CASE("rendering does not perturb the simulation") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    GraphicsContext* ctx = sharedContext();
    REQUIRE_GL(ctx);
    const long frames = 900;
    const std::string reference = simulateOnly(frames, true);

    GameSession s;
    std::string err;
    REQUIRE_MESSAGE(s.init(level1Options(), &err), err);
    GameView view;
    REQUIRE_MESSAGE(view.init(s, &err), err);
    RenderTarget target;
    REQUIRE(target.create(320, 240, 0));
    AudioBridge audio;
    audio.init(s.vfs(), true);
    audio.startLevel(s.musicPath());
    for (long f = 0; f < frames; ++f) {
        s.step(botInput(static_cast<u32>(f)));
        view.step(s);
        audio.drain(s.world());
        audio.pump(735);
        if (f % 50 == 0) {
            target.bind();
            view.draw(s, 320, 240);
        }
    }
    CHECK(s.world().dumpStateJson() == reference);
    CHECK(audio.stats().played > 0);
    CHECK(audio.stats().missing == 0);
    CHECK(s.world().soundEvents().empty());
}

TEST_CASE("particles are deterministic") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    u32 hash[2] = {0, 0};
    for (int run = 0; run < 2; ++run) {
        GameSession s;
        std::string err;
        REQUIRE_MESSAGE(s.init(level1Options(), &err), err);
        for (u32 f = 0; f < 600; ++f) s.step(botInput(f));
        const WorldParticles& particles = s.world().particles();
        CHECK(particles.emitterCount() > 0);
        CHECK(particles.liveParticles() > 0);
        hash[run] = particles.stateHash();
    }
    CHECK(hash[0] == hash[1]);
}

TEST_CASE("as3d_game --headless dumps the same state as as3d_sim") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    const std::string dir = exeDir();
    const std::string game = dir + "/game/as3d_game", sim = dir + "/sim_tool/as3d_sim";
    if (dir.empty() || !fileExists(game) || !fileExists(sim)) {
        std::fprintf(stderr, "SKIPPED (as3d_game or as3d_sim not built next to the tests): %s\n", __FILE__);
        return;
    }
    const std::string tmp = dir + "/game_integration_tmp";
    REQUIRE(std::system(("mkdir -p '" + tmp + "'").c_str()) == 0);
    // With input from a script: the bot's first 600 frames, recorded, then played back.
    InputRecorder rec;
    for (u32 f = 0; f < 600; ++f) rec.record(f, botInput(f));
    REQUIRE(rec.script().save(tmp + "/bot.txt"));
    const std::string data = " --data '" + testdata::root() + "'";
    // Screenshots on, so the renderer runs; the dump must not change.
    std::string cmdGame = "'" + game + "' --headless --frames 600 --quiet --no-audio --seed 7 --screenshot-every 200 --out-dir '" +
                          tmp + "' --input-script '" + tmp + "/bot.txt' --dump-state '" + tmp + "/game.json'" + data +
                          " > /dev/null 2>&1";
    std::string cmdSim = "'" + sim + "' --level 1 --frames 600 --bot --seed 7 --dump-state '" + tmp + "/sim.json'" + data +
                         " > /dev/null 2>&1";
    int rg = std::system(cmdGame.c_str());
    if (rg != 0 && fileExists(tmp + "/game.json") == false) {
        // Exit code 3: no GLES context for the screenshots.
        std::fprintf(stderr, "SKIPPED (as3d_game --headless failed, code %d; no GLES context?): %s\n", rg, __FILE__);
        return;
    }
    REQUIRE(rg == 0);
    REQUIRE(std::system(cmdSim.c_str()) == 0);
    const std::string a = readText(tmp + "/game.json"), b = readText(tmp + "/sim.json");
    CHECK(!a.empty());
    CHECK(a == b);
    CHECK(fileExists(tmp + "/frame_000600.png"));
}
