// The real game behind the real front end (WP-50), headless, driven by UI scripts and taps:
// new game to playing, pause and resume, the quit paths and what each banks, mission complete
// to the next mission with the carry-over checked in the world, game over, settings reaching
// the mixer and the key mapper, the profile across a restart, and a mission started from the
// menus playing exactly like `--level 1`. Skips loudly without game data or a GLES context.
//
// The flow and the UI script (apps/game) are compiled in here; the session, view and audio
// bridge they use come from apps/tests/game_integration_test.cpp, which compiles those.
#include "doctest.h"

#include <SDL_scancode.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <regex>
#include <string>

#include "../game/game_flow.cpp"
#include "../game/ui_script.cpp"
#include "as3d/defs.h"
#include "test_data.h"
#include "ui_test_util.h"

using namespace as3d;
using namespace as3d_game;
using ui::Screen;
namespace keys = as3d::ui::keys;

namespace {

std::string profilePath(const char* name) { return std::string(AS3D_REPO_ROOT) + "/build/" + name; }

void writeFile(const std::string& path, const std::string& text) {
    if (std::FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    }
}

struct Harness {
    GameSession session;
    AudioBridge audio;
    InputMapper keys;
    std::unique_ptr<GameFlow> flow;
    u32 frame = 0;

    bool init(const std::string& profile, bool touch = false) {
        GameOptions o;
        o.dataRoot = testdata::root();
        o.startLevel = false;
        o.levelFlow = false;
        std::string err;
        if (!session.init(o, &err)) {
            FAIL_CHECK(err);
            return false;
        }
        audio.init(session.vfs(), true);
        flow.reset(new GameFlow(session, audio));
        FlowConfig c;
        c.touch = touch;
        c.twoPlayerMode = !touch;
        c.profilePath = profile;
        c.attract = 1;
        c.showLogo = false;
        c.textsPath = testdata::extractedDir() + "/texts_v170.txt";
        c.settingsXml = testdata::originalDir() + "/data/Settings.xml";
        if (!flow->init(c, &err)) {
            FAIL_CHECK(err);
            return false;
        }
        flow->setInputMapper(&keys);
        flow->boot();
        return true;
    }
    ui::Frontend& fe() { return flow->frontend(); }
    World& world() { return session.world(); }
    Screen top() { return fe().topScreen(); }
    bool menu(Screen s) { return fe().menuOpen() && top() == s; }

    // One frame: UI events, then one world step with `in`.
    void step(const ui::UiInput& ui = {}, const FrameInput& in = {}) {
        flow->uiFrame(1.0f / 60.0f, ui);
        flow->step(in);
        if (session.hasLevel()) audio.drain(world());
        audio.pump(735);
        ++frame;
    }
    void wait(int frames) {
        for (int i = 0; i < frames; ++i) step();
    }
    // Plays a UI script from the current frame on (its frames are relative to now).
    void play(const std::string& text) {
        UiScript s;
        std::string err;
        REQUIRE_MESSAGE(s.parse(text, &err), err);
        for (u32 f = 0; f <= s.lastFrame(); ++f) {
            ui::UiInput in;
            s.eventsAt(f, in, nullptr);
            step(in);
        }
    }
    void key(int code) { step(ui::UiInput().key(code)); }
    void tap(float x, float y) { step(ui::UiInput().tap(x, y)); }
    // Taps the centre of the first visible, enabled item with this id on the top menu.
    bool tapItem(int id) {
        ui::Menu* m = fe().menus().top();
        if (!m) return false;
        for (const ui::MenuItem& it : m->items)
            if (it.id == id && !it.disabled() && !it.hidden()) {
                tap(it.hit.x + it.hit.w * 0.5f, it.hit.y + it.hit.h * 0.5f);
                return true;
            }
        return false;
    }
    // Main menu -> Start Game -> Start (mission 1, Normal, one player).
    void newGame() {
        REQUIRE(menu(Screen::MainMenu));
        play("0 tap 400 266\n");
        REQUIRE(menu(Screen::StartGame));
        play("0 tap 680 482\n");
        REQUIRE(fe().state() == ui::FrontendState::Playing);
        REQUIRE_FALSE(fe().menuOpen());
    }
    // A mission ends through the world, as EndLevel / the last life would.
    void completeMission() {
        world().endLevel();
        step();
        REQUIRE(fe().menuOpen());
    }
    void loseAllLives() {
        world().player(0).lives = -1.0f;
        world().player(1).lives = -1.0f;
        step();
        REQUIRE(menu(Screen::GameOver));
        wait(130); // the buttons appear after 2 s
    }
};

bool haveData() {
    if (testdata::available()) return true;
    std::fprintf(stderr, "SKIPPED (no game data): %s\n", __FILE__);
    return false;
}

// A state dump without the slot generations and what carries them (entity references in the
// fields and player records): the attract level that ran before the menus' mission used the
// slots, so their generation counters differ from a fresh world's, nothing else does.
std::string withoutGenerations(const std::string& dump) {
    std::string s = std::regex_replace(dump, std::regex("\"gen\": [0-9]+, "), "");
    s = std::regex_replace(s, std::regex("\"fields\": \"[0-9a-f]*\""), "");
    return std::regex_replace(s, std::regex("\"entity\": \"[0-9a-f]*\""), "");
}

// Mixed output level of the null device over `frames` frames.
double mixLevel(AudioBridge& audio, int frames) {
    std::vector<float> buf(static_cast<size_t>(frames) * 2);
    audio.audio().render(buf.data(), frames);
    double sum = 0;
    for (float v : buf) sum += static_cast<double>(v) * v;
    return std::sqrt(sum / static_cast<double>(buf.size()));
}

} // namespace

TEST_CASE("game flow: new game from the main menu to playing, pause and resume") {
    if (!haveData()) return;
    Harness h;
    std::remove(profilePath("game_flow_a.bin").c_str());
    REQUIRE(h.init(profilePath("game_flow_a.bin")));
    // The attract level runs behind the main menu, not paused.
    REQUIRE(h.menu(Screen::MainMenu));
    REQUIRE(h.session.hasLevel());
    CHECK(h.world().intermission());
    const float t0 = h.world().time();
    h.wait(10);
    CHECK(h.world().time() > t0);
    CHECK(h.flow->levelLoads() == 1);

    h.newGame();
    CHECK(h.flow->levelLoads() == 2);
    CHECK_FALSE(h.world().intermission());
    CHECK(h.session.mission() == 1);
    CHECK(h.world().numPlayers() == 1);
    CHECK(h.world().player(0).lives == doctest::Approx(2.0f));
    CHECK(h.world().player(0).heli == 1);
    CHECK(h.fe().hudVisible());

    // Gameplay input reaches the world while playing (after the fly-in, which ignores input).
    h.wait(300);
    FrameInput fire;
    fire.held[0] = ACT_FIRE;
    h.step({}, fire);
    CHECK((static_cast<u32>(h.world().player(0).action) & ACT_FIRE) != 0);

    // P pauses the world (time frozen, HUD kept); P again resumes and clears p_action.
    h.key('P');
    CHECK(h.world().paused());
    CHECK(h.fe().hudVisible());
    const float tp = h.world().time();
    h.step({}, fire);
    h.wait(5);
    CHECK(h.world().time() == tp);
    h.key('P');
    CHECK_FALSE(h.world().paused());
    CHECK(h.world().player(0).action == 0.0f);
    h.wait(2);
    CHECK(h.world().time() > tp);

    // Esc: the in-game menu, paused, HUD hidden; Resume.
    h.key(keys::Escape);
    CHECK(h.menu(Screen::InGame));
    CHECK(h.world().paused());
    CHECK_FALSE(h.fe().hudVisible());
    REQUIRE(h.tapItem(1));
    CHECK_FALSE(h.fe().menuOpen());
    CHECK_FALSE(h.world().paused());
    std::remove(profilePath("game_flow_a.bin").c_str());
}

TEST_CASE("game flow: tutorial hints go through the front end's hint box") {
    if (!haveData()) return;
    Harness h;
    REQUIRE(h.init(""));
    h.newGame();
    h.world().showHint("Press {X}^to fire");
    h.step();
    REQUIRE(h.menu(Screen::Hint));
    CHECK(h.world().paused());
    CHECK(h.fe().hudVisible());
    // The bot's confirm closes the box (as Enter does) and releases the world's pause.
    FrameInput ok;
    ok.confirm = true;
    h.step({}, ok);
    CHECK_FALSE(h.fe().menuOpen());
    CHECK_FALSE(h.world().hintShowing());
    CHECK_FALSE(h.world().paused());
}

TEST_CASE("game flow: quit paths and what each banks") {
    if (!haveData()) return;
    Harness h;
    REQUIRE(h.init(""));
    SUBCASE("in-game menu Quit: attract level, no banking") {
        h.newGame();
        h.world().player(0).scores = 5000.0f;
        h.key(keys::Escape);
        REQUIRE(h.tapItem(3));
        CHECK(h.menu(Screen::MainMenu));
        CHECK(h.world().intermission());
        CHECK(h.fe().campaign().p[0].banked == 0);
        CHECK(h.flow->levelLoads() == 3);
    }
    SUBCASE("mission complete Quit: no banking, the unlock is kept") {
        h.newGame();
        h.world().player(0).scores = 5000.0f;
        h.completeMission();
        REQUIRE(h.menu(Screen::MissionComplete));
        REQUIRE(h.tapItem(1));
        CHECK(h.menu(Screen::MainMenu));
        CHECK(h.fe().campaign().p[0].banked == 0);
        CHECK(h.fe().profile().progress.missionUnlocked[1]);
    }
    SUBCASE("mission complete Restart: same mission, lives it began with, no banking") {
        h.newGame();
        h.world().player(0).scores = 5000.0f;
        h.world().player(0).lives = 0.0f;
        h.completeMission();
        REQUIRE(h.tapItem(2));
        CHECK(h.session.mission() == 1);
        CHECK(h.world().player(0).lives == doctest::Approx(2.0f));
        CHECK(h.world().player(0).banked == 0);
    }
    SUBCASE("game over: Restart keeps the lives of the start; Quit banks, high-score check") {
        h.newGame();
        h.world().player(0).scores = 700.0f;
        h.loseAllLives();
        CHECK(h.world().gameOver());
        CHECK_FALSE(h.fe().hudVisible());
        REQUIRE(h.tapItem(1)); // Restart
        CHECK(h.session.mission() == 1);
        CHECK(h.world().player(0).lives == doctest::Approx(2.0f));
        CHECK_FALSE(h.world().gameOver());
        CHECK(h.fe().state() == ui::FrontendState::Playing);
        h.world().player(0).scores = 900.0f;
        h.loseAllLives();
        REQUIRE(h.tapItem(2)); // Quit: bank, attract level, main menu; 900 is not a high score
        CHECK(h.fe().campaign().p[0].banked == 900);
        CHECK(h.menu(Screen::MainMenu));
        CHECK(h.world().intermission());
    }
}

TEST_CASE("game flow: mission complete, Continue: carry-over in the world") {
    if (!haveData()) return;
    Harness h;
    REQUIRE(h.init(""));
    h.newGame();
    PlayerRecord& p = h.world().player(0);
    // Mid-mission state: a lost life, score, stars, upgrades, missiles and power-ups.
    p.lives = 1.0f;
    p.scores = 1234.0f;
    p.stars = 1.0f;
    p.upgrades[1] = 2;
    p.weapon = 1.0f;
    p.missiles[0] = 5;
    p.currentMissile = 0;
    p.powerups[1] = 2;
    p.currentPowerup = 1;
    h.completeMission();
    REQUIRE(h.menu(Screen::MissionComplete));
    CHECK(h.world().paused());
    CHECK_FALSE(h.fe().hudVisible());
    CHECK(h.fe().profile().progress.missionUnlocked[2] == false);
    CHECK(h.fe().profile().progress.missionUnlocked[1]);
    h.wait(120); // the tally
    REQUIRE(h.tapItem(3)); // Continue
    REQUIRE(h.fe().state() == ui::FrontendState::Playing);
    CHECK(h.session.mission() == 2);
    const PlayerRecord& q = h.world().player(0);
    CHECK(q.lives == doctest::Approx(1.0f)); // carried through banking
    CHECK(q.livesAtStart == 1);
    CHECK(q.banked == 1234);
    CHECK(q.scores == 0.0f);
    CHECK(h.session.displayScore(0) == 1234);
    CHECK(q.stars == 0.0f);
    CHECK(q.weapon == 0.0f); // machine gun, level 1
    CHECK(q.upgrades[0] == 1);
    for (int i = 1; i < 20; ++i) CHECK(q.upgrades[i] == 0);
    for (int m : q.missiles) CHECK(m == 0);
    for (int k : q.powerups) CHECK(k == 0);
    CHECK(h.fe().campaign().p[0].banked == 1234);
    CHECK_FALSE(h.world().paused());
}

TEST_CASE("game flow: settings reach the mixer and the key mapper; the profile survives a restart") {
    if (!haveData()) return;
    const std::string path = profilePath("game_flow_b.bin");
    std::remove(path.c_str());
    {
        Harness h;
        REQUIRE(h.init(path));
        // A fresh desktop profile: player 2 has its own keys.
        CHECK(h.fe().profile().settings.keys[1][static_cast<int>(Action::MoveForward)][0] == 'W');
        CHECK(h.keys.binding(1, InputAction::Forward, 0) == SDL_SCANCODE_W);
        CHECK(h.keys.binding(0, InputAction::Fire, 0) == SDL_SCANCODE_LCTRL);
        // The attract level's music plays at the default volume.
        h.wait(30);
        CHECK(mixLevel(h.audio, 4096) > 1e-4);
        // Options: effects volume to 0 (the slider's left end): the music still plays; music
        // volume to 0: the mixer is silent.
        REQUIRE(h.tapItem(3));
        REQUIRE(h.menu(Screen::Options));
        ui::MenuItem* sfx = h.fe().menus().top()->find(25);
        REQUIRE(sfx);
        h.tap(sfx->x + 4, sfx->y + 5);
        CHECK(h.fe().profile().settings.sfxVolume == 0.0f);
        mixLevel(h.audio, 4096); // let the volume change through
        CHECK(mixLevel(h.audio, 4096) > 1e-4);
        ui::MenuItem* music = h.fe().menus().top()->find(26);
        REQUIRE(music);
        h.tap(music->x + 4, music->y + 5);
        CHECK(h.fe().profile().settings.musicVolume == 0.0f);
        mixLevel(h.audio, 4096);
        CHECK(mixLevel(h.audio, 4096) < 1e-5);
        // Configure keys: Primary Attack for player 1 becomes Q.
        REQUIRE(h.tapItem(2));
        REQUIRE(h.menu(Screen::Controls));
        REQUIRE(h.tapItem(100));
        h.key('Q');
        CHECK(h.fe().profile().settings.keys[0][0][0] == 'Q');
        CHECK(h.keys.binding(0, InputAction::Fire, 0) == SDL_SCANCODE_Q);
        h.keys.keyEvent(SDL_SCANCODE_Q, true, false);
        CHECK((h.keys.takeFrame().held[0] & ACT_FIRE) != 0);
        h.keys.keyEvent(SDL_SCANCODE_Q, false, false);
        // Back to Options (saves), camera to High Pitch, back to the main menu.
        REQUIRE(h.tapItem(1));
        REQUIRE(h.menu(Screen::Options));
        REQUIRE(h.tapItem(28));
        CHECK(h.fe().profile().settings.camera == 2);
        REQUIRE(h.tapItem(1));
        REQUIRE(h.menu(Screen::MainMenu));
        // The camera setting reaches the next mission's world.
        h.newGame();
        CHECK(h.world().camera().mode == 2);
        // Mission 2 (unlocked in a fresh profile) completed unlocks mission 3.
        h.key(keys::Escape);
        REQUIRE(h.tapItem(3));
        REQUIRE(h.tapItem(1));
        h.tap(320, 160 + 4 + 20 + 5); // the second row: mission 2
        REQUIRE(h.tapItem(2));
        REQUIRE(h.session.mission() == 2);
        h.completeMission();
        CHECK(h.fe().profile().progress.missionUnlocked[2]);
    }
    {
        // A new game with the same profile file: settings and unlocks are back.
        Harness h;
        REQUIRE(h.init(path));
        const Settings& s = h.fe().profile().settings;
        CHECK(s.musicVolume == 0.0f);
        CHECK(s.camera == 2);
        CHECK(s.keys[0][0][0] == 'Q');
        CHECK(h.keys.binding(0, InputAction::Fire, 0) == SDL_SCANCODE_Q);
        CHECK(h.fe().profile().progress.missionUnlocked[2]);
    }
    std::remove(path.c_str());
}

TEST_CASE("game flow: a mission started from the menus plays like --level 1") {
    if (!haveData()) return;
    const u32 kFrames = 600;
    // Direct, as `as3d_game --level 1 --bot`.
    GameSession direct;
    GameOptions o;
    o.dataRoot = testdata::root();
    o.levelFlow = false;
    std::string err;
    REQUIRE_MESSAGE(direct.init(o, &err), err);
    // The frame of the Start tap steps the new mission once without input.
    direct.step(FrameInput());
    for (u32 f = 0; f < kFrames; ++f) direct.step(botInput(f));
    // Through the menus, with the attract level running first.
    Harness h;
    REQUIRE(h.init(""));
    h.wait(37);
    h.newGame();
    for (u32 f = 0; f < kFrames; ++f) h.step({}, botInput(f));
    CHECK(h.world().hintsShown() == direct.world().hintsShown());
    const std::string a = h.world().dumpStateJson(), b = direct.world().dumpStateJson();
    const bool same = withoutGenerations(a) == withoutGenerations(b);
    CHECK(same);
    if (!same) {
        writeFile(profilePath("game_flow_menus.json"), a);
        writeFile(profilePath("game_flow_direct.json"), b);
    }
}

TEST_CASE("game flow: the main menu draws over the attract level with the banner") {
    if (!haveData()) return;
    AS3D_REQUIRE_GLES();
    Harness h;
    REQUIRE(h.init(""));
    GameView view;
    std::string err;
    REQUIRE_MESSAGE(view.init(h.session, &err, false), err);
    REQUIRE(view.beginLevel(h.session, &err));
    h.flow->setView(&view);
    h.wait(30);
    RenderTarget target;
    REQUIRE(target.create(800, 600, 0));
    target.bind();
    h.flow->draw(800, 600);
    Image img;
    REQUIRE(target.readPixels(img));
    const u8 black[3] = {0, 0, 0};
    // The banner lights the top bar; the level shows between the bars; the buttons are drawn.
    CHECK(uitest::litIn(img, black, 100, 0, 700, 90) > 3000);
    CHECK(uitest::litIn(img, black, 20, 150, 200, 450) > 10000);
    CHECK(uitest::litIn(img, black, 240, 250, 560, 423) > 10000);
    h.flow->setView(nullptr);
}
