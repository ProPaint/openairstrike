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
#include "../game/game_view.h"
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

    bool init(const std::string& profile, bool touch = false, bool console = false) {
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
        c.consoleOptions = console;
        c.profilePath = profile;
        c.attract = 1;
        c.showLogo = false;
        c.textsPath = testdata::extractedDir() + "/texts_v170.txt";
        c.settingsXml = testdata::installDir() + "/data/Settings.xml";
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
    if (testdata::game().id != GameId::AirStrike3D) {
        // These drive the first game's menus (its screens, pictures and taps). The plain front
        // end of the sequels has its own flow tests in plain_frontend_test.cpp.
        std::fprintf(stderr, "SKIPPED (game '%s': the flow tests drive the first game's menus): %s\n",
                     testdata::gameKey().c_str(), __FILE__);
        return false;
    }
    if (testdata::available() && testdata::playable()) return true;
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
        if (AS3D_WITH_OPENMPT) CHECK(mixLevel(h.audio, 4096) > 1e-4);  // no music in the stub build
        // Options: effects volume to 0 (the slider's left end): the music still plays; music
        // volume to 0: the mixer is silent.
        REQUIRE(h.tapItem(3));
        REQUIRE(h.menu(Screen::Options));
        ui::MenuItem* sfx = h.fe().menus().top()->find(25);
        REQUIRE(sfx);
        h.tap(sfx->x + 4, sfx->y + 5);
        CHECK(h.fe().profile().settings.sfxVolume == 0.0f);
        mixLevel(h.audio, 4096); // let the volume change through
        if (AS3D_WITH_OPENMPT) CHECK(mixLevel(h.audio, 4096) > 1e-4);
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

TEST_CASE("game flow: Touch speed, Screen and Controls never reach keyboard or bot play") {
    if (!haveData()) return;
    // The same mission with the bot, then the keyboard, under opposite settings.
    std::string dumps[2];
    for (int k = 0; k < 2; ++k) {
        const std::string path = profilePath(k == 0 ? "wp51_settings_a.bin" : "wp51_settings_b.bin");
        Profile p;
        p.settings.touchSpeed = k == 0 ? 0 : kTouchSpeedSteps - 1;
        p.settings.screenMode = k == 0 ? kScreenWide : kScreen4x3;
        p.settings.leftHanded = k == 1;
        REQUIRE(saveProfileFile(path, p));
        Harness h;
        REQUIRE(h.init(path));
        CHECK(h.fe().profile().settings.touchSpeed == p.settings.touchSpeed);
        h.wait(37);
        h.newGame();
        for (u32 f = 0; f < 300; ++f) h.step({}, botInput(f));
        // Keyboard: right arrow held for a second, through the mapper as the window does.
        h.keys.keyEvent(SDL_SCANCODE_RIGHT, true, false);
        for (int f = 0; f < 60; ++f) h.step({}, h.keys.takeFrame());
        h.keys.keyEvent(SDL_SCANCODE_RIGHT, false, false);
        for (int f = 0; f < 60; ++f) h.step({}, h.keys.takeFrame());
        dumps[k] = h.world().dumpStateJson();
        std::remove(path.c_str());
    }
    CHECK(dumps[0].size() > 1000);
    CHECK(dumps[0] == dumps[1]);
}

TEST_CASE("game flow: console options and pad navigation (the 3DS front end, M2.1)") {
    if (!haveData()) return;
    Harness h;
    REQUIRE(h.init("", true, true)); // touch mode, console options, nothing saved
    REQUIRE(h.menu(Screen::MainMenu));
    auto item = [&](int id) -> const ui::MenuItem* {
        const ui::Menu* m = h.fe().menus().top();
        if (!m) return nullptr;
        for (const ui::MenuItem& it : m->items)
            if (it.id == id) return &it;
        return nullptr;
    };
    auto focusedId = [&]() {
        const ui::Menu* m = h.fe().menus().top();
        return m && m->focused >= 0 ? m->items[static_cast<size_t>(m->focused)].id : -1;
    };

    // Options (main menu item 3): no Configure controls (2), Mouse Control (29), Screen (40),
    // Controls hand (41) or touch speed (42); the volumes and the camera stay.
    h.key(keys::Down); // the first press focuses Start Game
    CHECK(focusedId() == 1);
    h.key(keys::Down);
    h.key(keys::Down);
    CHECK(focusedId() == 3);
    h.key(keys::Enter);
    REQUIRE(h.menu(Screen::Options));
    REQUIRE(item(2));
    CHECK(item(2)->hidden());
    REQUIRE(item(29));
    CHECK(item(29)->hidden());
    CHECK(item(40) == nullptr);
    CHECK(item(41) == nullptr);
    CHECK(item(42) == nullptr);
    REQUIRE(item(28));
    CHECK_FALSE(item(28)->hidden());
    // Pad mode: Down leaves a spinner (the first game's spinners keep it otherwise).
    h.key(keys::Down);
    const int a = focusedId();
    h.key(keys::Down);
    CHECK(focusedId() != a);
    h.key(keys::Escape);
    REQUIRE(h.menu(Screen::MainMenu));

    // Start Game by pad alone: Down to the list, the spinner, the grid, Back, Start; A starts.
    h.key(keys::Down); // the focus starts anew on a menu shown again (touch mode)
    CHECK(focusedId() == 1);
    h.key(keys::Enter);
    REQUIRE(h.menu(Screen::StartGame));
    const ui::MenuItem* gm = item(5);
    REQUIRE(gm);
    CHECK(gm->hidden()); // one player only
    h.key(keys::Down);
    CHECK(focusedId() == 3); // the mission list
    h.key(keys::Down);
    CHECK(focusedId() == 4); // difficulty
    h.key(keys::Down);
    CHECK(focusedId() == 6); // the helicopter grid (game mode is hidden)
    h.key(keys::Down);
    CHECK(focusedId() == 1); // Back
    h.key(keys::Down);
    CHECK(focusedId() == 2); // Start
    h.key(keys::Enter);
    CHECK(h.fe().state() == ui::FrontendState::Playing);
    CHECK_FALSE(h.fe().menuOpen());
    CHECK(h.session.mission() == 1);
    CHECK(h.world().numPlayers() == 1);
}

// ---------------------------------------------------------------------------------------
// The sequels' front end on a console (the 3DS, docs/3ds-port.md M5.2): touch mode, console
// options and pad navigation, through the real GameFlow over the game's session.
// ---------------------------------------------------------------------------------------
namespace {

bool haveSequelData() {
    if (testdata::game().frontend != FrontendStyle::SequelMenus) {
        std::fprintf(stderr, "SKIPPED (game '%s': these drive the sequels' menus): %s\n", testdata::gameKey().c_str(),
                     __FILE__);
        return false;
    }
    if (testdata::available() && testdata::playable()) return true;
    std::fprintf(stderr, "SKIPPED (no game data): %s\n", __FILE__);
    return false;
}

struct SequelHarness {
    GameSession session;
    AudioBridge audio;
    std::unique_ptr<GameFlow> flow;

    bool init() {
        GameOptions o;
        o.dataRoot = testdata::root();
        o.game = &testdata::game();
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
        c.touch = true;
        c.twoPlayerMode = false;
        c.mouseControlOption = false;
        c.touchMenuButton = false;
        c.consoleOptions = true;
        c.attract = 1;
        c.showLogo = false;
        c.textsPath = testdata::extractedDir() + "/" + testdata::game().textsFile;
        c.settingsXml = testdata::installDir() + "/data/Settings.xml";
        if (!flow->init(c, &err)) {
            FAIL_CHECK(err);
            return false;
        }
        flow->frontend().setHudTarget(ui::HudTarget::Bottom320x240);
        flow->boot();
        return true;
    }
    ui::Frontend& fe() { return flow->frontend(); }
    World& world() { return session.world(); }
    bool menu(Screen s) { return fe().menuOpen() && fe().topScreen() == s; }
    void step(const ui::UiInput& ui = {}, const FrameInput& in = {}) {
        flow->uiFrame(1.0f / 60.0f, ui);
        flow->step(in);
        if (session.hasLevel()) audio.drain(world());
        audio.pump(735);
    }
    void wait(int frames) {
        for (int i = 0; i < frames; ++i) step();
    }
    void key(int code) {
        step(ui::UiInput().key(code));
        wait(10); // the sequels' menus open over 0.125 s
    }
    int focusedId() {
        const ui::Menu* m = fe().menus().top();
        return m && m->focused >= 0 ? m->items[static_cast<size_t>(m->focused)].id : -1;
    }
    // Down until the item `id` has the focus (at most 12 presses).
    bool focus(int id) {
        for (int k = 0; k < 12 && focusedId() != id; ++k) key(keys::Down);
        return focusedId() == id;
    }
};

// Counts the model views GameFlow::draw asks the view for, as the 3DS composer draws them.
struct CountingView : FlowView {
    int drawn = 0;
    std::string object;
    bool init(GameSession&, std::string*, bool) override { return true; }
    bool beginLevel(GameSession&, std::string*) override { return true; }
    void drawFrame(const GameSession&, int, int, const FrameLayers& l) override {
        if (l.frontend && l.frontend->modelViewVisible()) l.frontend->drawModelViews();
    }
    void drawModel(GameSession&, const ui::ModelView& v) override {
        ++drawn;
        object = v.object;
    }
};

} // namespace

TEST_CASE("game flow: the sequels' front end by pad, model view and mission 1 as --level 1 (3DS, M5.2)") {
    if (!haveSequelData()) return;
    const GameRules& rules = testdata::game().rules;
    SequelHarness h;
    REQUIRE(h.init());
    h.wait(30);
    REQUIRE(h.menu(Screen::MainMenu));
    // Options: no Configure controls.
    REQUIRE(h.focus(3));
    h.key(keys::Enter);
    REQUIRE(h.menu(Screen::Options));
    for (const ui::MenuItem& it : h.fe().menus().top()->items)
        if (it.id == 2) CHECK(it.hidden());
    h.key(keys::Escape);
    REQUIRE(h.menu(Screen::MainMenu));
    // Start Game -> Next by pad.
    REQUIRE(h.focus(1));
    h.key(keys::Enter);
    REQUIRE(h.menu(Screen::StartGame));
    REQUIRE(h.focus(2));
    h.key(keys::Enter);
    REQUIRE(h.menu(Screen::HeliSelect));
    // The model view of the first helicopter; Left / Right browse (locked ones show no model).
    ui::ModelView v;
    REQUIRE(h.fe().modelView(v));
    CHECK(v.object == rules.heliObjects[0]);
    CHECK(h.fe().helicopters()[0] == 0);
    h.key(keys::Right);
    CHECK(h.fe().helicopters()[0] == 1);
    h.key(keys::Left);
    CHECK(h.fe().helicopters()[0] == 0);
    h.key(keys::Left);
    CHECK(h.fe().helicopters()[0] == rules.helicopterCount - 1);
    h.key(keys::Right);
    REQUIRE(h.fe().modelView(v));
    CountingView view;
    h.flow->setView(&view);
    h.flow->draw(320, 240);
    CHECK(view.drawn == 1);
    CHECK(view.object == rules.heliObjects[0]);
    h.flow->setView(nullptr);
    // Start: the mission loads; AirStrike 2's start dialogue pauses it (Gulf Thunder's mission 1
    // has none). Each frame as the 3DS loop with menu_bot: the UI first, then a step, the bot's
    // unless a menu holds the world (a hint box does not: the bot's confirm closes it).
    REQUIRE(h.focus(1));
    u32 bot = 0;
    auto frame = [&](const ui::UiInput& in) {
        h.flow->uiFrame(1.0f / 60.0f, in);
        const bool hint = h.world().hintShowing() && h.menu(Screen::Hint);
        h.flow->step(h.fe().menuOpen() && !hint ? FrameInput() : botInput(bot++));
    };
    frame(ui::UiInput().key(keys::Enter));
    REQUIRE(h.fe().state() == ui::FrontendState::Playing);
    REQUIRE(h.session.mission() == 1);
    // The world stays paused (its clock still) until the dialogue closes; Enter pages through it.
    for (int k = 0; k < 60 && h.fe().menuOpen(); ++k) {
        CHECK(h.menu(Screen::Dialogue));
        CHECK(h.world().paused());
        CHECK(h.world().time() == 0.0f);
        frame(ui::UiInput().key(keys::Enter));
        for (int w = 0; w < 10 && h.fe().menuOpen(); ++w) frame({});
    }
    REQUIRE_FALSE(h.fe().menuOpen());
    REQUIRE_FALSE(h.world().paused());
    // 600 bot steps of the running world (hint boxes closed by the bot's confirm), then the
    // state equals a direct start's after 600 bot frames (as3d_sim --bot).
    for (int k = 0; k < 3000 && bot < 600; ++k) frame({});
    REQUIRE(bot == 600);
    GameSession direct;
    GameOptions o;
    o.dataRoot = testdata::root();
    o.game = &testdata::game();
    o.levelFlow = false;
    // The menus' helicopter: a fresh save of a sequel unlocks entry 0 only (as3d_sim --heli 0;
    // its default, 1, is locked there).
    o.world.heli[0] = h.fe().helicopters()[0];
    std::string err;
    REQUIRE_MESSAGE(direct.init(o, &err), err);
    for (u32 f = 0; f < 600; ++f) direct.step(botInput(f));
    // The end state of as3d_sim --bot (the lines mode=bot and the 3DS's menu_bot_steps log).
    // Not the whole state dump: the attract level used the entity slots first, so a free slot
    // can be another one (Gulf Thunder: one effect lands in slot 32 instead of 158), and the
    // world's frame counter also counts the steps paused under the start dialogue.
    const World& m = h.world();
    const World& d = direct.world();
    CHECK(m.mapPos() == d.mapPos());
    CHECK(m.time() == d.time());
    CHECK(m.frame() >= d.frame());
    CHECK(m.listCount() == d.listCount());
    CHECK(m.slotsInUse() == d.slotsInUse());
    CHECK(m.stats().entitiesCreated == d.stats().entitiesCreated);
    CHECK(m.stats().entitiesFreed == d.stats().entitiesFreed);
    CHECK(m.stats().maxSlotsInUse == d.stats().maxSlotsInUse);
    CHECK(m.player(0).scores == d.player(0).scores);
    CHECK(m.player(0).kills == d.player(0).kills);
    const int pm = m.playerEntityIndex(0), pd = d.playerEntityIndex(0);
    REQUIRE(pm >= 0);
    REQUIRE(pd >= 0);
    CHECK(m.entity(pm).f(F_ORIGIN) == d.entity(pd).f(F_ORIGIN));
    CHECK(m.entity(pm).f(F_ORIGIN + 1) == d.entity(pd).f(F_ORIGIN + 1));
    CHECK(m.entity(pm).f(F_HEALTH) == d.entity(pd).f(F_HEALTH));
    MESSAGE(testdata::gameKey() << " mission 1 from the menus, 600 bot steps: map_pos " << m.mapPos() << ", list "
                                << m.listCount() << ", created " << m.stats().entitiesCreated << " / freed "
                                << m.stats().entitiesFreed << ", score " << m.player(0).scores);
}

TEST_CASE("game flow: the sequels' end screens by pad, back to the main menu (3DS, M5.2)") {
    if (!haveSequelData()) return;
    SequelHarness h;
    REQUIRE(h.init());
    h.wait(30);
    auto start = [&]() {
        REQUIRE(h.menu(Screen::MainMenu));
        REQUIRE(h.focus(1));
        h.key(keys::Enter);
        REQUIRE(h.focus(2));
        h.key(keys::Enter);
        REQUIRE(h.menu(Screen::HeliSelect));
        REQUIRE(h.focus(1));
        h.key(keys::Enter);
        for (int k = 0; k < 60 && h.fe().menuOpen(); ++k) h.key(keys::Enter);
        REQUIRE(h.fe().state() == ui::FrontendState::Playing);
        REQUIRE_FALSE(h.fe().menuOpen());
    };
    start();
    // The in-game menu (START), Resume by pad.
    h.key(keys::Escape);
    REQUIRE(h.menu(Screen::InGame));
    CHECK(h.world().paused());
    REQUIRE(h.focus(1));
    h.key(keys::Enter);
    CHECK_FALSE(h.fe().menuOpen());
    CHECK_FALSE(h.world().paused());
    // Mission complete (EndLevel): the end dialogue if any, then Mission Complete; Quit.
    h.world().endLevel();
    h.step();
    for (int k = 0; k < 60 && h.menu(Screen::Dialogue); ++k) h.key(keys::Enter);
    REQUIRE(h.menu(Screen::MissionComplete));
    REQUIRE(h.focus(1)); // Quit
    h.key(keys::Enter);
    h.wait(5);
    REQUIRE(h.menu(Screen::MainMenu));
    // Game over: Quit banks and checks the high scores (the name entry when it qualifies).
    start();
    h.world().player(0).lives = -1.0f;
    h.world().player(1).lives = -1.0f;
    h.step();
    REQUIRE(h.menu(Screen::GameOver));
    REQUIRE(h.focus(2)); // Quit
    h.key(keys::Enter);
    h.wait(5);
    if (h.menu(Screen::NameEntry)) {
        // Pad mode over the touch keyboard: Down to its first key, A types it, Right the next.
        h.key(keys::Down);
        h.key(keys::Enter);
        h.key(keys::Right);
        h.key(keys::Enter);
        std::string name;
        for (const ui::MenuItem& it : h.fe().menus().top()->items)
            if (it.id == 2) name = it.text;
        CHECK(name == "AB");
        h.key(keys::Up); // the name field; A commits
        CHECK(h.focusedId() == 2);
        h.key(keys::Enter);
    }
    if (h.menu(Screen::TopScores)) h.key(keys::Escape);
    h.wait(5);
    CHECK(h.menu(Screen::MainMenu));
}
