// Front-end state machine tests with a fake game (frontend.md 1, 3, 5): new game, mission
// complete, continue, game over, restart, high-score entry, the quit paths and what each banks,
// pointer-only operability of every screen in touch mode, texts and Settings.xml. CPU only.
#include "doctest.h"

#include <string>
#include <vector>

#include "as3d/frontend.h"
#include "ui_test_util.h"

using namespace as3d;
using namespace as3d::ui;

namespace {

// The fake game: records every call.
struct FakeGame : GameHost {
    std::vector<MissionStart> starts;
    int attracts = 0, clears = 0, saves = 0, quits = 0, settings = 0, videos = 0, gameOverMusics = 0;
    bool paused = false;
    void startMission(const MissionStart& s) override { starts.push_back(s); paused = false; }
    void loadAttract() override { attracts++; }
    void setPaused(bool p) override { paused = p; }
    void clearPlayerActions() override { clears++; }
    void settingsChanged(const Settings&) override { settings++; }
    void applyVideoSettings(const Settings&) override { videos++; }
    void saveProfile(const Profile&) override { saves++; }
    void quit() override { quits++; }
    void gameOverMusic() override { gameOverMusics++; }
};

FrontendContent content() {
    FrontendContent c;
    for (int i = 0; i < kMissionCount; i++) c.missionNames[i] = "Mission " + std::to_string(i + 1);
    c.enableHelic[2] = 7;
    return c;
}

struct Rig {
    FakeGame game;
    Profile profile;
    Frontend fe;
    explicit Rig(bool touch = false, bool withIntro = false)
        : fe(game, profile, [&] {
              FrontendContent c = content();
              if (withIntro) c.intros.push_back(IntroPage{});
              return c;
          }(), Texts{}) {
        fe.setTouchMode(touch);
        fe.boot();
    }
    void tap(float x, float y) { fe.update(0.016f, UiInput().tap(x, y)); }
    void key(int k) { fe.update(0.016f, UiInput().key(k)); }
    void wait(float s) {
        for (float t = 0; t < s; t += 0.05f) fe.update(0.05f, {});
    }
    Screen top() const { return fe.topScreen(); }
    // Centre of the first visible, enabled item with this id on the top menu.
    bool tapItem(int id) {
        Menu* m = fe.menus().top();
        if (!m) return false;
        for (const MenuItem& it : m->items)
            if (it.id == id && !it.disabled() && !it.hidden()) {
                tap(it.hit.x + it.hit.w * 0.5f, it.hit.y + it.hit.h * 0.5f);
                return true;
            }
        return false;
    }
    void startGame() {
        REQUIRE(tapItem(1)); // Start Game
        REQUIRE(top() == Screen::StartGame);
        REQUIRE(tapItem(2)); // Start
    }
};

MissionReport report(double score, int lives, double stars = 0, int starTotal = 0, double maxScore = 0) {
    MissionReport r;
    r.players[0].score = score;
    r.players[0].lives = lives;
    r.players[0].stars = stars;
    r.totals.starTotal = starTotal;
    r.totals.maxScore = maxScore;
    return r;
}

} // namespace

TEST_CASE("frontend boot: intro pages, speed-up, then attract level and main menu") {
    Rig rig(false, true);
    CHECK(rig.fe.state() == FrontendState::Intro);
    CHECK(rig.fe.brightness() == doctest::Approx(0.5f));
    CHECK(rig.game.attracts == 0);
    rig.fe.update(0.5f, UiInput().key(keys::Space)); // speed x4
    rig.wait(2.0f);                                    // 8.5 s at x4
    CHECK(rig.fe.state() == FrontendState::Attract);
    CHECK(rig.game.attracts == 1);
    CHECK(rig.top() == Screen::MainMenu);
    CHECK(rig.fe.bannerVisible());

    Rig direct;
    CHECK(direct.fe.state() == FrontendState::Attract);
    CHECK(direct.top() == Screen::MainMenu);
    // The main menu cannot be closed.
    direct.key(keys::Escape);
    direct.fe.update(0, UiInput().press(keys::Mouse2));
    CHECK(direct.fe.menus().depth() == 1);
}

TEST_CASE("frontend walk: new game, mission complete, continue, game over, restart, quit") {
    Rig rig;
    rig.startGame();
    REQUIRE(rig.game.starts.size() == 1);
    MissionStart s = rig.game.starts.back();
    CHECK(s.mission == 0);
    CHECK(s.difficulty == 2);
    CHECK(s.players == 1);
    CHECK(s.helicopter[0] == 1);
    CHECK(s.lives[0] == 2);
    CHECK(rig.fe.state() == FrontendState::Playing);
    CHECK(rig.fe.hudVisible());
    CHECK_FALSE(rig.fe.menuOpen());

    // P pauses and unpauses; the HUD stays.
    rig.key('P');
    CHECK(rig.fe.paused());
    CHECK(rig.fe.hudVisible());
    rig.key('P');
    CHECK_FALSE(rig.fe.paused());
    // Hotkeys: F6 raises the effects volume, F9 cycles the camera.
    rig.key(keys::F6);
    CHECK(rig.profile.settings.sfxVolume == doctest::Approx(0.6f));
    rig.key(keys::F9);
    CHECK(rig.profile.settings.camera == 2);
    // Gameplay keys are left to the game.
    CHECK_FALSE(rig.fe.update(0.016f, UiInput().key(keys::Ctrl)));

    // Esc: in-game menu, paused, HUD hidden; Resume.
    rig.key(keys::Escape);
    CHECK(rig.top() == Screen::InGame);
    CHECK(rig.fe.paused());
    CHECK_FALSE(rig.fe.hudVisible());
    const int clears = rig.game.clears;
    REQUIRE(rig.tapItem(1));
    CHECK_FALSE(rig.fe.menuOpen());
    CHECK_FALSE(rig.fe.paused());
    CHECK(rig.fe.hudVisible());
    CHECK(rig.game.clears == clears + 1);

    // Mission 1 complete: unlocks mission 2 (already) and saves; nothing banked yet.
    const int saves = rig.game.saves;
    rig.fe.onEndLevel(report(1000, 1, 2, 4, 2000));
    CHECK(rig.top() == Screen::MissionComplete);
    CHECK(rig.fe.paused());
    CHECK_FALSE(rig.fe.hudVisible());
    CHECK(rig.game.saves == saves + 1);
    CHECK(rig.fe.campaign().p[0].banked == 0);
    rig.key(keys::Escape); // swallowed
    CHECK(rig.top() == Screen::MissionComplete);
    // Pick helicopter 0 in the grid, then Continue: bank, next mission.
    rig.tap(224 + 10, 304 + 10);
    REQUIRE(rig.tapItem(3));
    s = rig.game.starts.back();
    CHECK(s.mission == 1);
    CHECK(s.helicopter[0] == 0);
    CHECK(s.lives[0] == 1);
    CHECK(s.banked[0] == 1000);
    CHECK(rig.fe.campaign().p[0].rankAccumulator == doctest::Approx(0.5 + 0.25));

    // Game over: buttons appear after 2 s; Restart replays with the lives the mission began with.
    rig.fe.onGameOver(report(500, -1));
    CHECK(rig.top() == Screen::GameOver);
    CHECK(rig.game.gameOverMusics == 1);
    CHECK_FALSE(rig.tapItem(1));
    rig.wait(2.1f);
    REQUIRE(rig.tapItem(1));
    s = rig.game.starts.back();
    CHECK(s.restart);
    CHECK(s.mission == 1);
    CHECK(s.lives[0] == 1);
    CHECK(s.banked[0] == 1000);

    // Game over -> Quit banks, loads the attract level; 1500 does not reach the table.
    rig.fe.onGameOver(report(500, -1));
    rig.wait(2.1f);
    const int attracts = rig.game.attracts;
    REQUIRE(rig.tapItem(2));
    CHECK(rig.fe.campaign().p[0].banked == 1500);
    CHECK(rig.game.attracts == attracts + 1);
    CHECK(rig.fe.state() == FrontendState::Attract);
    CHECK(rig.top() == Screen::MainMenu);
    CHECK(rig.fe.menus().depth() == 1);
}

TEST_CASE("frontend: what the other quit paths bank") {
    SUBCASE("mission complete: Quit and Restart bank nothing") {
        Rig rig;
        rig.startGame();
        rig.fe.onEndLevel(report(5000, 2));
        REQUIRE(rig.tapItem(2)); // Restart
        CHECK(rig.game.starts.back().restart);
        CHECK(rig.game.starts.back().mission == 0);
        CHECK(rig.fe.campaign().p[0].banked == 0);
        rig.fe.onEndLevel(report(5000, 2));
        REQUIRE(rig.tapItem(1)); // Quit
        CHECK(rig.fe.campaign().p[0].banked == 0);
        CHECK(rig.top() == Screen::MainMenu);
        // The unlock is kept.
        CHECK(rig.profile.progress.missionUnlocked[1]);
    }
    SUBCASE("in-game menu Quit: no banking, no high-score check") {
        Rig rig;
        rig.startGame();
        rig.key(keys::Escape);
        REQUIRE(rig.tapItem(3));
        CHECK(rig.top() == Screen::MainMenu);
        CHECK_FALSE(rig.fe.campaign().active);
        CHECK(rig.fe.campaign().p[0].banked == 0);
    }
    SUBCASE("mission 3 unlocks helicopter 7 and mission 4") {
        Rig rig;
        rig.tapItem(1);
        // Mission 3 is locked: clicking its row does not select it.
        rig.tap(320, 160 + 4 + 40 + 5);
        CHECK(rig.fe.menus().top()->find(3)->selected == 0);
        rig.key(keys::Escape);
        rig.profile.progress.missionUnlocked[2] = true;
        rig.tapItem(1); // rebuilt with the new unlocks
        rig.tap(320, 160 + 4 + 40 + 5);
        CHECK(rig.fe.menus().top()->find(3)->selected == 2);
        REQUIRE(rig.tapItem(2));
        CHECK(rig.game.starts.back().mission == 2);
        rig.fe.onEndLevel(report(1, 1));
        CHECK(rig.profile.progress.helicopterUnlocked[7]);
        CHECK(rig.profile.progress.missionUnlocked[3]);
    }
}

TEST_CASE("frontend: game complete, high-score name entry and the table") {
    Rig rig;
    rig.startGame();
    // Jump to the last mission of the campaign.
    rig.fe.debugSet("mission", "20");
    rig.fe.onEndLevel(report(120000, 3, 1, 1, 100000));
    CHECK(rig.top() == Screen::GameComplete);
    rig.wait(3.0f);
    CHECK_FALSE(rig.fe.takeSounds().empty()); // typing
    REQUIRE(rig.tapItem(1)); // Continue: bank, main menu, high-score check
    CHECK(rig.fe.campaign().p[0].banked == 120000);
    CHECK(rig.top() == Screen::NameEntry);
    CHECK(rig.fe.menus().depth() == 2);
    CHECK(rig.fe.wantsTextInput());
    rig.key(keys::Escape); // swallowed: no way to skip
    CHECK(rig.top() == Screen::NameEntry);
    const int saves = rig.game.saves;
    rig.fe.update(0.016f, UiInput().text("Ace").key(keys::Enter));
    CHECK(rig.top() == Screen::TopScores);
    CHECK(rig.game.saves == saves + 1);
    const HighScore& h = rig.profile.progress.scores[11];
    CHECK(h.name == "Ace");
    CHECK(h.score == 120000);
    // Rank: accumulator (1 + 0.6) x 1.07 = 1.71 -> Rookie.
    CHECK(h.rank == 1);
    rig.key(keys::Escape);
    CHECK(rig.top() == Screen::MainMenu);
}

TEST_CASE("frontend: two players skip the high-score check and alternate helicopter choices") {
    Rig rig;
    rig.tapItem(1);
    Menu* m = rig.fe.menus().top();
    REQUIRE(m);
    rig.tapItem(5); // Game mode -> 2 Players
    CHECK(rig.fe.twoPlayers());
    rig.tap(224 + 10, 304 + 10);      // cell 0 -> player 1
    rig.tap(224 + 72 + 10, 304 + 10); // cell 1 -> player 2
    CHECK(rig.fe.helicopters()[0] == 0);
    CHECK(rig.fe.helicopters()[1] == 1);
    REQUIRE(rig.tapItem(2));
    CHECK(rig.game.starts.back().players == 2);
    MissionReport r = report(900000, -1);
    r.players[1].score = 900000;
    rig.fe.onGameOver(r);
    rig.wait(2.1f);
    REQUIRE(rig.tapItem(2));
    CHECK(rig.fe.campaign().p[1].banked == 900000);
    CHECK(rig.top() == Screen::MainMenu); // no name entry
}

TEST_CASE("frontend: exit confirmation, options, tutorial hint") {
    Rig rig;
    REQUIRE(rig.tapItem(5));
    CHECK(rig.top() == Screen::Exit);
    rig.key(keys::Escape);
    CHECK(rig.top() == Screen::MainMenu);
    rig.tapItem(5);
    REQUIRE(rig.tapItem(1)); // Yes
    CHECK(rig.game.quits == 1);
    CHECK(rig.game.saves >= 1);

    Rig opt;
    REQUIRE(opt.tapItem(3));
    CHECK(opt.top() == Screen::Options);
    Menu* m = opt.fe.menus().top();
    CHECK(m->find(3)->hidden()); // Apply hidden until a video value differs
    const int changes = opt.game.settings;
    MenuItem* music = m->find(26);
    opt.tap(music->x + 8, music->y + 5); // slider to its minimum
    CHECK(opt.profile.settings.musicVolume == 0.0f);
    CHECK(opt.game.settings > changes);
    opt.tapItem(23); // Fullscreen On -> Off: Apply appears, colour depth disabled
    CHECK_FALSE(m->find(3)->hidden());
    CHECK(m->find(22)->disabled());
    CHECK(opt.profile.settings.fullscreen); // not applied yet
    REQUIRE(opt.tapItem(3));
    CHECK_FALSE(opt.profile.settings.fullscreen);
    CHECK(opt.game.videos == 1);
    CHECK(opt.top() == Screen::MainMenu);

    // In a mission the video items are disabled.
    Rig play;
    play.startGame();
    play.key(keys::Escape);
    REQUIRE(play.tapItem(2));
    CHECK(play.top() == Screen::Options);
    CHECK(play.fe.menus().top()->find(20)->disabled());
    play.key(keys::Escape);
    CHECK(play.top() == Screen::InGame);

    // Hint: pauses, keeps the HUD, OK appears after the opening, Esc closes and resumes.
    Rig hint;
    hint.startGame();
    hint.fe.showTutorialHint("Press {X}^to fire");
    CHECK(hint.top() == Screen::Hint);
    CHECK(hint.fe.paused());
    CHECK(hint.fe.hudVisible());
    CHECK_FALSE(hint.tapItem(1));
    hint.wait(0.35f);
    REQUIRE(hint.tapItem(1));
    CHECK_FALSE(hint.fe.paused());
    CHECK_FALSE(hint.fe.menuOpen());
    hint.fe.showTutorialHint("again");
    hint.fe.update(0.016f, UiInput().press(keys::Mouse2));
    CHECK_FALSE(hint.fe.paused()); // ours: right click resumes too
}

TEST_CASE("frontend: controls capture binds the next key, Esc cancels, Backspace unbinds") {
    Rig rig;
    rig.tapItem(3);
    REQUIRE(rig.tapItem(2)); // Configure keys
    CHECK(rig.top() == Screen::Controls);
    REQUIRE(rig.tapItem(100)); // Primary Attack: capture starts
    CHECK(rig.fe.menus().top()->find(1)->disabled());
    rig.key('Q');
    CHECK(rig.profile.settings.keys[0][0][0] == 'Q');
    CHECK(rig.profile.settings.keys[0][0][1] == 17);
    CHECK_FALSE(rig.fe.menus().top()->find(1)->disabled());
    rig.tapItem(101);
    rig.key(keys::Escape); // cancel, not bound, menu stays
    CHECK(rig.top() == Screen::Controls);
    CHECK(rig.profile.settings.keys[0][1][0] == 50);
    rig.key(keys::Backspace); // focused row 101 (the last tapped)
    CHECK(rig.profile.settings.keys[0][1][0] == 0);
    CHECK(rig.profile.settings.keys[0][1][1] == 50);
}

TEST_CASE("frontend touch mode: every screen is operable with the pointer alone") {
    // Only taps are sent below: no key events at all.
    Rig rig(true);
    // Main -> Exit -> No.
    REQUIRE(rig.tapItem(5));
    REQUIRE(rig.tapItem(2));
    CHECK(rig.top() == Screen::MainMenu);
    // Top Scores -> Back.
    REQUIRE(rig.tapItem(2));
    REQUIRE(rig.tapItem(1));
    CHECK(rig.top() == Screen::MainMenu);
    // Information: the page spinner goes back with a tap left of its value, Back.
    REQUIRE(rig.tapItem(4));
    MenuItem* page = rig.fe.menus().top()->find(10);
    rig.tap(page->x - 2, page->y + 8);
    CHECK(page->index == 9);
    rig.tap(page->x + 30, page->y + 8);
    CHECK(page->index == 0);
    REQUIRE(rig.tapItem(1));
    CHECK(rig.top() == Screen::MainMenu);
    // Options -> Controls: a tapped row waits for a key; Cancel and Clear are on screen.
    REQUIRE(rig.tapItem(3));
    REQUIRE(rig.tapItem(2));
    REQUIRE(rig.tapItem(102));
    REQUIRE(rig.tapItem(50)); // Cancel
    CHECK(rig.profile.settings.keys[0][2][0] == 16);
    REQUIRE(rig.tapItem(102));
    REQUIRE(rig.tapItem(51)); // Clear
    CHECK(rig.profile.settings.keys[0][2][0] == 0);
    REQUIRE(rig.tapItem(1)); // Back to Options
    REQUIRE(rig.tapItem(1)); // Back to main
    CHECK(rig.top() == Screen::MainMenu);
    // Start Game: difficulty backwards by tap, Back, then Start.
    REQUIRE(rig.tapItem(1));
    MenuItem* diff = rig.fe.menus().top()->find(4);
    rig.tap(diff->x - 2, diff->y + 8);
    CHECK(diff->index == 1);
    REQUIRE(rig.tapItem(1));
    REQUIRE(rig.tapItem(1));
    REQUIRE(rig.tapItem(2));
    CHECK(rig.game.starts.back().difficulty == kDefaultDifficulty);
    // Playing: the MENU button opens the in-game menu; Resume.
    rig.tap(400, 16);
    CHECK(rig.top() == Screen::InGame);
    REQUIRE(rig.tapItem(1));
    CHECK_FALSE(rig.fe.menuOpen());
    // Hint: OK.
    rig.fe.showTutorialHint("Hint");
    rig.wait(0.35f);
    REQUIRE(rig.tapItem(1));
    CHECK_FALSE(rig.fe.menuOpen());
    // Game over: Quit after 2 s, high score via the on-screen keyboard.
    rig.fe.onGameOver(report(2000000, -1));
    rig.wait(2.1f);
    REQUIRE(rig.tapItem(2));
    CHECK(rig.top() == Screen::NameEntry);
    CHECK_FALSE(rig.fe.wantsTextInput()); // our own keyboard, not the system one
    REQUIRE(rig.tapItem(200 + 'J'));
    REQUIRE(rig.tapItem(200 + 'O'));
    REQUIRE(rig.tapItem(200 + 'X'));
    REQUIRE(rig.tapItem(60)); // Del
    REQUIRE(rig.tapItem(200 + 'E'));
    REQUIRE(rig.tapItem(1)); // OK
    CHECK(rig.top() == Screen::TopScores);
    CHECK(rig.profile.progress.scores[0].name == "JOE");
    REQUIRE(rig.tapItem(1));
    CHECK(rig.top() == Screen::MainMenu);
    // Mission complete: Continue; Game complete: Continue.
    REQUIRE(rig.tapItem(1));
    REQUIRE(rig.tapItem(2));
    rig.fe.onEndLevel(report(1, 1));
    REQUIRE(rig.tapItem(3));
    CHECK(rig.fe.state() == FrontendState::Playing);
    rig.fe.debugSet("mission", "20");
    rig.fe.onEndLevel(report(1, 1));
    REQUIRE(rig.tapItem(1));
    CHECK(rig.top() == Screen::MainMenu);
}

TEST_CASE("frontend: every screen builds and draws without data") {
    for (bool touch : {false, true}) {
        Rig rig(touch);
        UiAssets none;
        Renderer2D r;
        for (Screen s : kAllScreens) {
            rig.fe.open(s);
            rig.fe.update(0.5f, {});
            r.begin(800, 600);
            rig.fe.draw(r, none);
            CHECK(r.dropped() == 0);
            CHECK(std::string(screenName(s)).size() > 0);
            Screen back;
            CHECK(screenFromName(screenName(s), back));
            CHECK(back == s);
        }
    }
}

TEST_CASE("frontend headless: screens render in the right places") {
    AS3D_REQUIRE_DATA(); AS3D_REQUIRE_PLAYABLE();
    AS3D_REQUIRE_GLES();
    Vfs vfs;
    REQUIRE(uitest::mountPaks(vfs));
    UiAssets assets;
    std::string err;
    REQUIRE_MESSAGE(assets.load(vfs, &err), err);
    Renderer2D r;
    REQUIRE_MESSAGE(r.init(&err), err);
    RenderTarget target;
    REQUIRE(target.create(800, 600, 0));
    const u8 bg[3] = {40, 60, 80};
    auto render = [&](Frontend& fe, Image& img) {
        target.bind();
        clear({40 / 255.0f, 60 / 255.0f, 80 / 255.0f, 1}, true);
        r.begin(800, 600);
        fe.draw(r, assets);
        CHECK(r.dropped() == 0);
        r.flush();
        REQUIRE(target.readPixels(img));
    };
    Image img;
    {
        // Main menu: black bars, five buttons in the middle, level visible elsewhere.
        Rig rig;
        rig.fe.menus().setPointer(-50, -50);
        render(rig.fe, img);
        const u8 black[3] = {0, 0, 0};
        CHECK(uitest::litIn(img, black, 0, 0, 800, 90) < 100);    // top bar
        CHECK(uitest::litIn(img, bg, 240, 250, 560, 423) > 3000); // buttons
        CHECK(uitest::litIn(img, bg, 20, 150, 200, 450) == 0);    // level shows through
    }
    {
        // Start Game: list, grid and the two buttons.
        Rig rig;
        rig.tapItem(1);
        rig.fe.menus().setPointer(-50, -50);
        render(rig.fe, img);
        CHECK(uitest::litIn(img, bg, 295, 160, 715, 266) > 3000);
        CHECK(uitest::litIn(img, bg, 224, 304, 576, 440) > 5000);
        CHECK(uitest::litIn(img, bg, 50, 450, 178, 500) > 500);
        CHECK(uitest::litIn(img, bg, 605, 450, 755, 500) > 500);
    }
    {
        // Game over tints the whole scene red (FILTER) and shows the title.
        Rig rig;
        rig.startGame();
        rig.fe.onGameOver(report(0, -1));
        rig.wait(3.0f);
        render(rig.fe, img);
        const u8* p = &img.rgba[(static_cast<size_t>(100) * 800 + 100) * 4];
        CHECK(p[0] == 40);
        CHECK(p[1] < 35);
        CHECK(uitest::litIn(img, bg, 230, 200, 570, 264) > 3000);
    }
    {
        // Touch name entry: the keyboard is drawn under the panel.
        Rig rig(true);
        rig.fe.open(Screen::NameEntry);
        render(rig.fe, img);
        CHECK(uitest::litIn(img, bg, 201, 390, 600, 494) > 5000);
    }
}

TEST_CASE("frontend texts: file format, defaults, missing texts") {
    Texts t;
    CHECK(t.get("rank.6") == "Elite");
    CHECK(t.get("difficulty.2") == "Normal");
    CHECK(t.get("info.pages.3") == "3 of 10");
    CHECK(t.get("info.1.0").empty());
    CHECK_FALSE(t.installed());
    const int n = t.parse("# comment\n\ninfo.1.title = \"TITLE\"\ninfo.1.0 = \"a \\\"quoted\\\" \\\\ word\"\r\n"
                          "bad line\nkey = unquoted\nopen = \"no end\nrank.6 = \"Top\"\n");
    CHECK(n == 3);
    CHECK(t.installed());
    CHECK(t.get("info.1.0") == "a \"quoted\" \\ word");
    CHECK(t.get("rank.6") == "Top");
    CHECK_FALSE(t.loaded("open"));
}

TEST_CASE("frontend Settings.xml: info, intros, logotypes") {
    const char* xml =
        "<?xml version=\"1.0\"?>\n<Settings>\n"
        "<Info version=\"v 9\" copyright=\"(c) someone\" />\n"
        "<Intros>\n<!-- <Image name=\"x.tga\"> -->\n"
        "<ImageTemp name=\"a.tga\"><BackColor r=\"1\" g=\"2\" b=\"3\" /></ImageTemp>\n"
        "<Image name=\"Gfx\\b.tga\">\n<BackColor r=\"255\" g=\"0\" b=\"51\" />\n</Image>\n"
        "<BuiltIn name=\"DivoGames\" />\n</Intros>\n"
        "<Logotypes><Image x=\"10\" y=\"20\" name=\"Gfx\\l.tga\" invertAxisY=\"1\" invertAxisX=\"1\" /></Logotypes>\n"
        "</Settings>\n";
    FrontendContent c;
    REQUIRE(parseSettingsXml(xml, c));
    CHECK(c.version == "v 9");
    CHECK(c.copyright == "(c) someone");
    REQUIRE(c.intros.size() == 2);
    CHECK_FALSE(c.intros[0].divoGames);
    CHECK(c.intros[0].image == "Gfx\\b.tga");
    CHECK(c.intros[0].back.r == doctest::Approx(1.0f));
    CHECK(c.intros[0].back.b == doctest::Approx(0.2f));
    CHECK(c.intros[1].divoGames);
    REQUIRE(c.logos.size() == 1);
    CHECK(c.logos[0].x == 10.0f);
    CHECK(c.logos[0].invertX);
    CHECK(c.logos[0].invertY);
}

TEST_CASE("frontend Settings.xml: the re-release's portal branding is removed") {
    const char* xml =
        "<Settings>\n"
        "<Info version=\"v 1.70\" copyright=\"Copyright 2010 GameTonic.com, DivoGames Ltd.\" />\n"
        "<Intros>\n<Image name=\"Gfx\\logo2.tga\"><BackColor r=\"1\" g=\"2\" b=\"3\" /></Image>\n"
        "<Image name=\"Gfx\\other.tga\"><BackColor r=\"1\" g=\"2\" b=\"3\" /></Image>\n"
        "<BuiltIn name=\"DivoGames\" />\n</Intros>\n"
        "<Logotypes><Image x=\"10\" y=\"20\" name=\"Gfx\\logo2s.tga\" />"
        "<Image x=\"1\" y=\"2\" name=\"gfx\\keep.tga\" /></Logotypes>\n"
        "</Settings>\n";
    FrontendContent c;
    REQUIRE(parseSettingsXml(xml, c));
    removeRereleaseBranding(c);
    CHECK(c.version == "v 1.70");
    CHECK(c.copyright == "Copyright 2010 DivoGames Ltd.");
    REQUIRE(c.logos.size() == 1);
    CHECK(c.logos[0].path == "gfx\\keep.tga");
    REQUIRE(c.intros.size() == 2);
    CHECK(c.intros[0].image == "Gfx\\other.tga");
    CHECK(c.intros[1].divoGames);

    FrontendContent plain;
    plain.copyright = "(c) someone";
    removeRereleaseBranding(plain);
    CHECK(plain.copyright == "(c) someone");
}

TEST_CASE("frontend options (ours, issue 140): Screen and Controls rows, operable by taps") {
    FakeGame game;
    Profile profile;
    FrontendContent c = content();
    c.videoOptions = false; // as the port's host
    c.screenOption = false;
    c.handOption = true;
    Frontend fe(game, profile, c, Texts{});
    fe.setTouchMode(true);
    fe.boot();
    auto tap = [&](float x, float y) { fe.update(0.016f, UiInput().tap(x, y)); };
    // Not offered while the window is 4:3 (desktop); Controls only in touch mode.
    fe.open(Screen::Options);
    Menu* m = fe.menus().top();
    CHECK(m->find(40) == nullptr);
    REQUIRE(m->find(41) != nullptr);
    fe.update(0.016f, UiInput().key(keys::Escape));
    fe.setScreenOptionShown(true);
    fe.open(Screen::Options);
    m = fe.menus().top();
    MenuItem* screen = m->find(40);
    MenuItem* hand = m->find(41);
    REQUIRE(screen != nullptr);
    REQUIRE(hand != nullptr);
    CHECK(screen->y < hand->y);
    CHECK(screen->values.size() == 2u);
    CHECK(screen->index == 0);
    const int changes = game.settings;
    // A tap right of the value cycles forward, applied live.
    tap(screen->x + 40, screen->y + 5);
    CHECK(profile.settings.screenMode == kScreen4x3);
    CHECK(game.settings > changes);
    tap(hand->x + 40, hand->y + 5);
    CHECK(profile.settings.leftHanded);
    MenuItem* speed = m->find(42);
    REQUIRE(speed != nullptr);
    CHECK(speed->y > hand->y);
    CHECK(speed->values.size() == static_cast<size_t>(kTouchSpeedSteps));
    CHECK(speed->index == kDefaultTouchSpeed);
    tap(speed->x + 40, speed->y + 5);
    CHECK(profile.settings.touchSpeed == kDefaultTouchSpeed + 1);
    MenuItem* fps = m->find(43);
    REQUIRE(fps != nullptr);
    CHECK(fps->y == 320.0f); // the port's free 3D Sound row, below Music Volume
    tap(fps->x + 40, fps->y + 5);
    CHECK(profile.settings.showFps);
    // A tap on the "<" goes back.
    tap(screen->x - 10, screen->y + 5);
    CHECK(profile.settings.screenMode == kScreenWide);
    // Keyboard mode has no Controls row.
    FakeGame g2;
    Profile p2;
    Frontend desk(g2, p2, c, Texts{});
    desk.boot();
    desk.setScreenOptionShown(true);
    desk.open(Screen::Options);
    CHECK(desk.menus().top()->find(40) != nullptr);
    CHECK(desk.menus().top()->find(41) == nullptr);
    CHECK(desk.menus().top()->find(42) == nullptr);
    CHECK(desk.menus().top()->find(43) != nullptr); // Show FPS on both targets
}

namespace {

// A game of 18 missions and 6 helicopters: a modified copy of the first game's profile.
const GameProfile& smallGame() {
    static const GameProfile g = [] {
        GameProfile p = gameProfile(GameId::AirStrike3D);
        p.rules.missionCount = 18;
        p.rules.helicopterCount = 6;
        return p;
    }();
    return g;
}

struct SmallRig {
    FakeGame game;
    Profile profile;
    Frontend fe;
    SmallRig()
        : profile{Progress::defaults(smallGame().rules), Settings::defaults()},
          fe(game, profile, [] {
              FrontendContent c;
              c.game = &smallGame();
              for (int i = 0; i < 18; i++) c.missionNames[i] = "Mission " + std::to_string(i + 1);
              c.enableHelic[16] = 5;
              return c;
          }(), Texts{}) {
        fe.boot();
    }
    void tap(float x, float y) { fe.update(0.016f, UiInput().tap(x, y)); }
    bool tapItem(int id) {
        Menu* m = fe.menus().top();
        if (!m) return false;
        for (const MenuItem& it : m->items)
            if (it.id == id && !it.disabled() && !it.hidden()) {
                tap(it.hit.x + it.hit.w * 0.5f, it.hit.y + it.hit.h * 0.5f);
                return true;
            }
        return false;
    }
};

} // namespace

TEST_CASE("frontend: a game of 18 missions and 6 helicopters") {
    SmallRig rig;
    REQUIRE(rig.tapItem(1)); // Start Game
    REQUIRE(rig.fe.topScreen() == Screen::StartGame);
    Menu* m = rig.fe.menus().top();
    const MenuItem* list = m->find(3);
    REQUIRE(list);
    CHECK(list->entries.size() == 18);
    const MenuItem* grid = m->find(6);
    REQUIRE(grid);
    CHECK(grid->grid.count == 6);
    // Helicopter 7 does not exist: the tap on cell 6 (second row, second cell) changes nothing.
    rig.profile.progress.helicopterUnlocked[6] = true; // even if a file had it set
    rig.fe.debugSet("unlock", "1");
    rig.tap(224 + 72 + 10, 304 + 72 + 10);
    CHECK(rig.fe.menus().top()->find(6)->grid.choice[0] == 1);
    rig.tap(224 + 10, 304 + 72 + 10); // cell 5
    CHECK(rig.fe.menus().top()->find(6)->grid.choice[0] == 5);
    // Difficulty debug values are clamped to the mission count.
    rig.fe.debugSet("mission", "99");
    CHECK(rig.fe.campaign().mission == 17);
    REQUIRE(rig.tapItem(2)); // Start: campaign starts at the list's selection (mission 1)
    REQUIRE_FALSE(rig.game.starts.empty());
    CHECK(rig.game.starts.back().mission == 0);
    // Mission 17 unlocks helicopter 5, mission 18 is the last: Game Complete.
    rig.fe.debugSet("mission", "17");
    rig.fe.onEndLevel(report(1000, 2));
    CHECK(rig.fe.topScreen() == Screen::MissionComplete);
    CHECK(rig.profile.progress.helicopterUnlocked[5]);
    CHECK(rig.profile.progress.missionUnlocked[17]);
    CHECK(rig.fe.campaign().nextMission() == 17);
    rig.fe.debugSet("mission", "18");
    rig.fe.onEndLevel(report(1000, 2));
    CHECK(rig.fe.topScreen() == Screen::GameComplete);
    CHECK(rig.fe.campaign().nextMission() == 0);
}
