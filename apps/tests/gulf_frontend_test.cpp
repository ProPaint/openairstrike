// Gulf Thunder's own front end (FrontendStyle::SequelMenus with Gulf Thunder's look,
// docs/spec/gulf/frontend.delta.md): the state machine with a fake game (new game, the portrait
// dialogues of operations 10 and 24, play, in-game menu, restart, mission complete, helicopter
// selection, Next, game over, Continue from the checkpoint, game complete), its layouts (main
// menu, in-game menu, the 37-pixel text button, seven Information pages, no intro comic),
// pointer-only operation in touch mode, compatibility with a save written by the plain front end,
// texts present and absent, the corrected text addresses of tools/exe_texts/gulf.json, and with
// the game's data: no texture of another game, headless renders in 4:3 and wide, and the real
// game behind the menus. The game is selected explicitly (gameProfile, locateGameData), so all of
// it runs in the default pass of tools/ci.sh; what needs Gulf Thunder's data or a GLES context
// skips loudly without it.
#include "doctest.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "../game/game_flow.h"
#include "as3d/defs.h"
#include "as3d/frontend.h"
#include "as3d/game_data.h"
#include "ui_test_util.h"

using namespace as3d;
using namespace as3d::ui;

namespace {

const GameProfile& gulf() { return gameProfile(GameId::GulfThunder); }

// The fake game: records every call.
struct FakeGame : GameHost {
    std::vector<MissionStart> starts;
    std::vector<std::string> music;
    std::vector<ModelView> views;
    int attracts = 0, saves = 0, quits = 0, clears = 0;
    bool paused = false;
    Profile saved;
    void startMission(const MissionStart& s) override { starts.push_back(s); paused = false; }
    void loadAttract() override { attracts++; }
    void setPaused(bool p) override { paused = p; }
    void clearPlayerActions() override { clears++; }
    void settingsChanged(const Settings&) override {}
    void saveProfile(const Profile& p) override { saves++; saved = p; }
    void quit() override { quits++; }
    void drawModel(const ModelView& v) override { views.push_back(v); }
    void playMusic(const std::string& path) override { music.push_back(path); }
};

// Generic stand-ins for the texts file (never the executable's own texts): the three dialogues.
Texts dialogueTexts() {
    Texts t;
    t.set("dialog.10.start.0", "Line one\nline two");
    t.set("dialog.10.start.0.speaker", "1");
    t.set("dialog.10.start.1", "Page two");
    t.set("dialog.10.start.1.speaker", "0");
    t.set("dialog.10.start.2", "Page three");
    t.set("dialog.10.start.2.speaker", "0");
    t.set("dialog.10.end.0", "The end");
    t.set("dialog.10.end.0.speaker", "1");
    t.set("dialog.10.end.1", "Well done");
    t.set("dialog.10.end.1.speaker", "0");
    t.set("dialog.24.start.0", "Last one");
    t.set("dialog.24.start.0.speaker", "0");
    t.set("dialog.24.start.1", "Good luck");
    t.set("dialog.24.start.1.speaker", "0");
    return t;
}

FrontendContent content(bool logoPage = false) {
    FrontendContent c;
    c.game = &gulf();
    c.videoOptions = false;
    c.twoPlayerMode = false;
    c.screenOption = true;
    c.handOption = true;
    for (int i = 0; i < gulf().rules.missionCount; i++) c.missionNames[i] = "Operation " + std::to_string(i + 1) + ": Test";
    // levels.txt: operations 4 and 9 unlock helicopters 1 and 2.
    c.enableHelic[3] = 1;
    c.enableHelic[8] = 2;
    for (int h = 0; h < gulf().rules.helicopterCount; h++) c.heli[h] = {true, 300 + 100 * h, true, 1.0f};
    if (logoPage) c.intros.push_back(IntroPage{}); // Settings.xml's DivoGames page
    c.version = "v 2.71";
    c.copyright = "Copyright 2003-2007 DivoGames";
    return c;
}

struct Rig {
    FakeGame host;
    Profile profile;
    Frontend fe;
    explicit Rig(Texts texts = {}, bool touch = false, bool showLogo = false, Profile p = fresh())
        : profile(p), fe(host, profile, content(showLogo), std::move(texts)) {
        profile.settings.showLogo = showLogo;
        fe.setTouchMode(touch);
        fe.menus().setPointer(-50, -50);
        fe.boot();
    }
    static Profile fresh() {
        Profile p;
        p.progress = Progress::defaults(gulf().rules, Progress::defaultHelicopters(&gulf()));
        return p;
    }
    void frame(const UiInput& in = {}) { fe.update(1.0f / 60.0f, in); }
    void wait(float s) {
        for (float t = 0; t < s; t += 1.0f / 60.0f) frame();
    }
    void key(int k) { frame(UiInput().key(k)); }
    Screen top() const { return fe.topScreen(); }
    MenuItem* item(int id) {
        Menu* m = fe.menus().top();
        return m ? m->find(id) : nullptr;
    }
    void tapItem(int id) {
        MenuItem* it = item(id);
        REQUIRE_MESSAGE(it, "no item " << id << " on " << screenName(top()));
        REQUIRE_MESSAGE(!it->disabled(), "item " << id << " is disabled");
        frame(UiInput().tap(it->hit.x + it->hit.w * 0.5f, it->hit.y + it->hit.h * 0.5f));
    }
    // Main menu -> Start Game -> (operation) -> Next -> helicopter selection -> Start.
    void startGame(int mission = 0) {
        REQUIRE(top() == Screen::MainMenu);
        tapItem(1);
        REQUIRE(top() == Screen::StartGame);
        MenuItem* list = item(5);
        REQUIRE(list);
        list->selected = mission;
        tapItem(2);
        REQUIRE(top() == Screen::HeliSelect);
        tapItem(1);
    }
    MissionReport report(double score, int lives, bool complete = true) const {
        MissionReport r;
        r.players[0] = {score, lives, 3, 5};
        r.players[1] = {score / 2, lives, 1, 1};
        r.totals = {10, 1000, 20};
        r.hasUpgrades = true;
        r.upgrades[0][0] = 4;
        r.upgrades[0][3] = 2;
        r.weapon[0] = 3;
        if (complete) {
            r.hasCheckpoint = true;
            r.checkpointMission = fe.campaign().mission + 1;
            for (int p = 0; p < 2; p++) {
                r.checkpointLives[p] = lives;
                r.checkpointScore[p] = fe.campaign().p[p].banked + static_cast<std::int64_t>(score);
                r.checkpointRank[p] = static_cast<float>(fe.campaign().p[p].rankAccumulator) + 0.8f;
            }
        }
        return r;
    }
    void finishDialogue() {
        for (int guard = 0; guard < 40 && top() == Screen::Dialogue && fe.menuOpen(); guard++) {
            wait(0.3f);
            if (!fe.menuOpen() || top() != Screen::Dialogue) break;
            key(keys::Enter);
        }
        wait(0.5f);
    }
    void unlockAll() {
        for (int i = 0; i < gulf().rules.missionCount; i++) profile.progress.missionUnlocked[i] = true;
        for (int h = 0; h < gulf().rules.helicopterCount; h++) profile.progress.helicopterUnlocked[h] = true;
        fe.debugSet("unlock", "1");
    }
};

std::string readFileText(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("gulf front end: Gulf Thunder selects the sequel's menus with its own layout") {
    CHECK(gulf().frontend == FrontendStyle::SequelMenus);
    Rig rig;
    CHECK(rig.fe.sequel());
    CHECK_FALSE(rig.fe.plain());
    CHECK(rig.fe.menus().sequel);
    CHECK_FALSE(rig.fe.bannerVisible());
    REQUIRE(rig.top() == Screen::MainMenu);
    // Six text buttons at y 250 to 475, 180 wide and 37 high, centred on 400, no margin (3.3, 2.2).
    const int ids[] = {1, 2, 3, 4, 7, 5};
    const float ys[] = {250, 295, 340, 385, 430, 475};
    for (int i = 0; i < 6; i++) {
        const MenuItem* it = rig.item(ids[i]);
        REQUIRE(it);
        CHECK(it->type == ItemType::SequelButton);
        CHECK(it->hit.w == doctest::Approx(180));
        CHECK(it->hit.h == doctest::Approx(37));
        CHECK(it->hit.x == doctest::Approx(310));
        CHECK(it->hit.y == doctest::Approx(ys[i]));
    }
    // Esc is swallowed; every screen builds and draws without data.
    rig.key(keys::Escape);
    CHECK(rig.top() == Screen::MainMenu);
    UiAssets none;
    Renderer2D r;
    for (Screen s : kSequelScreens) {
        if (s == Screen::Dialogue) continue;
        rig.fe.open(s);
        rig.wait(0.5f);
        r.begin(800, 600);
        rig.fe.draw(r, none);
        CHECK(r.dropped() == 0);
    }
    // A button's hit width is its caption's (no 46-pixel margin): Back, in the Start Game screen.
    rig.fe.open(Screen::MainMenu);
    rig.tapItem(1);
    const MenuItem* back = rig.item(1);
    REQUIRE(back);
    CHECK(back->hit.w == doctest::Approx(back->captionW));
    CHECK(back->hit.h == doctest::Approx(37));
    CHECK(back->hit.x == doctest::Approx(40));
    // AirStrike 2's are unchanged by Gulf Thunder's look (the look is per front end).
    FakeGame host;
    Profile p;
    p.settings.showLogo = false;
    FrontendContent c2;
    c2.game = &gameProfile(GameId::AirStrike2);
    Frontend as2fe(host, p, c2, Texts{});
    as2fe.boot();
    const MenuItem* as2start = as2fe.menus().top()->find(1);
    REQUIRE(as2start);
    CHECK(as2start->hit.w == doctest::Approx(246));
    CHECK(as2start->hit.h == doctest::Approx(30));
    // ... and the main menu's buttons come back at Gulf Thunder's measures when it draws again.
    rig.fe.open(Screen::MainMenu);
    CHECK(rig.item(1)->hit.w == doctest::Approx(180));
}

TEST_CASE("gulf front end: the in-game menu, Options from it, positions and widths") {
    Rig rig;
    rig.startGame(0);
    CHECK_FALSE(rig.fe.menuOpen()); // operation 1 has no dialogue
    rig.key(keys::Escape);
    REQUIRE(rig.top() == Screen::InGame);
    // Resume, Options, Restart, Quit at y 300, 345, 390, 435, 160 wide, centred on 400 (3.13).
    const int ids[] = {1, 2, 4, 3};
    const float ys[] = {300, 345, 390, 435};
    for (int i = 0; i < 4; i++) {
        const MenuItem* it = rig.item(ids[i]);
        REQUIRE(it);
        CHECK(it->hit.y == doctest::Approx(ys[i]));
        CHECK(it->hit.w == doctest::Approx(160));
        CHECK(it->hit.x == doctest::Approx(320));
    }
    rig.wait(0.3f);
    rig.tapItem(2);
    REQUIRE(rig.top() == Screen::Options);
    rig.wait(0.3f);
    rig.key(keys::Escape);
    CHECK(rig.top() == Screen::InGame);
}

TEST_CASE("gulf front end: a new game, the dialogues of operations 10 and 24, Restart, Quit") {
    Rig rig(dialogueTexts());
    rig.unlockAll();
    // Operation 1: no start dialogue.
    rig.startGame(0);
    REQUIRE(rig.host.starts.size() == 1);
    CHECK(rig.host.starts.back().mission == 0);
    CHECK(rig.host.starts.back().lives[0] == gulf().rules.startLives);
    CHECK_FALSE(rig.host.starts.back().carryUpgrades);
    CHECK_FALSE(rig.fe.menuOpen());
    rig.key(keys::Escape);
    rig.wait(0.3f);
    rig.tapItem(3); // Quit
    REQUIRE(rig.top() == Screen::MainMenu);
    // Operation 10: three pages, paused with the HUD shown; Enter completes, then turns a page.
    rig.startGame(9);
    REQUIRE(rig.host.starts.back().mission == 9);
    REQUIRE(rig.top() == Screen::Dialogue);
    CHECK(rig.host.paused);
    CHECK(rig.fe.hudVisible());
    rig.wait(0.3f);
    rig.key(keys::Enter);
    CHECK(rig.top() == Screen::Dialogue);
    rig.finishDialogue();
    CHECK_FALSE(rig.fe.menuOpen());
    CHECK_FALSE(rig.host.paused);
    // Restart: the same operation, its dialogue again.
    rig.key(keys::Escape);
    rig.wait(0.3f);
    rig.tapItem(4);
    REQUIRE(rig.host.starts.size() == 3);
    CHECK(rig.host.starts.back().restart);
    CHECK(rig.top() == Screen::Dialogue);
    rig.finishDialogue();
    rig.key(keys::Escape);
    rig.wait(0.3f);
    rig.tapItem(3);
    CHECK(rig.top() == Screen::MainMenu);
    // Operation 24: two pages; 11 (bonus operation after the boss) has none.
    rig.startGame(23);
    REQUIRE(rig.host.starts.back().mission == 23);
    REQUIRE(rig.top() == Screen::Dialogue);
    rig.finishDialogue();
    rig.key(keys::Escape);
    rig.wait(0.3f);
    rig.tapItem(3);
    rig.startGame(10);
    CHECK_FALSE(rig.fe.menuOpen());
}

TEST_CASE("gulf front end: mission end: checkpoint, end dialogue, unlocks, statistics, Next, Choose Helicopter") {
    Rig rig(dialogueTexts());
    rig.unlockAll();
    rig.startGame(9); // operation 10: a start and an end dialogue
    rig.finishDialogue();
    const int saves = rig.host.saves;
    rig.fe.onEndLevel(rig.report(500, 1));
    CHECK(rig.profile.progress.checkpoint.mission == 10);
    CHECK(rig.profile.progress.checkpoint.score[0] == 500);
    CHECK(rig.host.saves > saves);
    // The two-page end dialogue first, Mission Complete after it.
    REQUIRE(rig.top() == Screen::Dialogue);
    CHECK_FALSE(rig.fe.hudVisible());
    rig.finishDialogue();
    REQUIRE(rig.top() == Screen::MissionComplete);
    CHECK(rig.host.paused);
    rig.wait(0.3f);
    // Choose Helicopter: accept mode; three helicopters, the arrow wraps modulo 3.
    rig.tapItem(4);
    REQUIRE(rig.top() == Screen::HeliSelect);
    CHECK(rig.item(8));
    CHECK_FALSE(rig.item(2));
    rig.wait(0.3f);
    for (int i = 0; i < 3; i++) rig.tapItem(6);
    CHECK(rig.fe.helicopters()[0] == 0);
    rig.tapItem(7);
    CHECK(rig.fe.helicopters()[0] == 2);
    rig.tapItem(6);
    rig.tapItem(8); // Accept
    REQUIRE(rig.top() == Screen::MissionComplete);
    // Next: banked, operation 11, the upgrades carried.
    rig.wait(0.3f);
    rig.tapItem(3);
    REQUIRE(rig.host.starts.size() == 2);
    const MissionStart& n = rig.host.starts.back();
    CHECK(n.mission == 10);
    CHECK(n.banked[0] == 500);
    CHECK(n.lives[0] == 1);
    CHECK(n.carryUpgrades);
    CHECK(n.upgrades[0][0] == 4);
}

TEST_CASE("gulf front end: helicopters unlock after operations 4 and 9; the last operation ends the game") {
    Rig rig;
    for (int i = 0; i < gulf().rules.missionCount; i++) rig.profile.progress.missionUnlocked[i] = true;
    rig.startGame(3); // operation 4 unlocks helicopter 1
    rig.fe.onEndLevel(rig.report(100, 2));
    CHECK(rig.profile.progress.helicopterUnlocked[1]);
    CHECK_FALSE(rig.profile.progress.helicopterUnlocked[2]);
    REQUIRE(rig.top() == Screen::MissionComplete);
    rig.fe.debugSet("mission", "9");
    rig.fe.onEndLevel(rig.report(100, 2));
    CHECK(rig.profile.progress.helicopterUnlocked[2]);
    // Operation 24: Game Complete (there is no Next after it), its Continue only once drawn.
    rig.fe.debugSet("mission", "24");
    rig.fe.onEndLevel(rig.report(300, 2));
    REQUIRE(rig.top() == Screen::GameComplete);
    CHECK(rig.item(1)->disabled());
    rig.wait(4.2f);
    CHECK_FALSE(rig.item(1)->disabled());
    const std::int64_t before = rig.fe.campaign().p[0].banked;
    rig.wait(0.3f);
    rig.tapItem(1);
    CHECK(rig.fe.campaign().p[0].banked == before + 300);
    CHECK((rig.top() == Screen::MainMenu || rig.top() == Screen::NameEntry));
    CHECK(rig.profile.progress.checkpoint.mission == gulf().rules.missionCount);
}

TEST_CASE("gulf front end: game over, Restart and Quit with the high-score check") {
    Rig rig;
    rig.startGame(0);
    rig.fe.onGameOver(rig.report(40000, -1, false));
    REQUIRE(rig.top() == Screen::GameOver);
    // The two buttons sit on y 520, Restart on the right at 550, Quit on the left at 250.
    CHECK(rig.item(1)->hit.y == doctest::Approx(520));
    CHECK(rig.item(1)->hit.x + rig.item(1)->hit.w * 0.5f == doctest::Approx(550));
    CHECK(rig.item(2)->hit.x + rig.item(2)->hit.w * 0.5f == doctest::Approx(250));
    rig.frame();
    rig.tapItem(1);
    CHECK(rig.host.starts.back().restart);
    CHECK(rig.host.starts.back().lives[0] == gulf().rules.startLives);
    rig.fe.onGameOver(rig.report(40000, -1, false));
    rig.frame();
    rig.tapItem(2);
    CHECK(rig.fe.campaign().p[0].banked == 40000);
    REQUIRE(rig.top() == Screen::NameEntry);
    rig.frame(UiInput().text("ACE"));
    rig.key(keys::Enter);
    CHECK(rig.top() == Screen::TopScores);
    int found = 0;
    for (const HighScore& h : rig.profile.progress.scores) found += h.name == "ACE" && h.score == 40000;
    CHECK(found == 1);
}

TEST_CASE("gulf front end: Continue from the campaign checkpoint after a restart of the program") {
    Profile savedProfile;
    {
        Rig rig;
        rig.startGame(0);
        rig.fe.onEndLevel(rig.report(1200, 1));
        rig.wait(0.3f);
        rig.tapItem(3); // Next: operation 2
        rig.fe.onEndLevel(rig.report(800, 3));
        CHECK(rig.profile.progress.checkpoint.mission == 2);
        CHECK(rig.profile.progress.checkpoint.score[0] == 2000);
        rig.wait(0.3f);
        rig.tapItem(1); // Quit: nothing banked, the checkpoint stays
        const std::vector<u8> bytes = serializeProfile(rig.profile, "gulf");
        savedProfile = Rig::fresh();
        std::string why;
        REQUIRE_MESSAGE(deserializeProfile(bytes.data(), bytes.size(), savedProfile, &why, "gulf"), why);
    }
    CHECK(savedProfile.progress.checkpoint.mission == 2);
    Rig rig({}, false, false, savedProfile);
    rig.tapItem(1);
    REQUIRE(rig.top() == Screen::StartGame);
    CHECK(rig.item(5)->selected == 2); // the checkpoint's operation is preselected
    rig.tapItem(2);
    REQUIRE(rig.top() == Screen::HeliSelect);
    CHECK(rig.item(1)->label == " Continue "); // Gulf Thunder's own caption for it
    rig.tapItem(1);
    const MissionStart& s = rig.host.starts.back();
    CHECK(s.mission == 2);
    CHECK(s.lives[0] == 3);
    CHECK(s.banked[0] == 2000);
    // Any other operation starts afresh and forgets the checkpoint.
    Rig other({}, false, false, savedProfile);
    other.startGame(0);
    CHECK(other.host.starts.back().banked[0] == 0);
    CHECK(other.profile.progress.checkpoint.mission == -1);
}

TEST_CASE("gulf front end: operable by pointer only in touch mode") {
    Rig rig(dialogueTexts(), true);
    rig.unlockAll();
    CHECK(rig.fe.touchMode());
    rig.tapItem(1);
    rig.wait(0.3f);
    MenuItem* diff = rig.item(3);
    REQUIRE(diff);
    rig.frame(UiInput().tap(diff->x - 10, diff->y + 8));
    CHECK(diff->index == 1); // the backwards zone
    rig.item(5)->selected = 9;
    rig.tapItem(2);
    rig.wait(0.3f);
    CHECK(rig.item(6)->hit.w >= 44); // finger-sized arrows
    rig.tapItem(6);
    rig.tapItem(7);
    CHECK(rig.fe.helicopters()[0] == 0);
    rig.tapItem(1);
    REQUIRE(rig.top() == Screen::Dialogue);
    for (int i = 0; i < 40 && rig.fe.menuOpen(); i++) {
        rig.wait(0.3f);
        rig.frame(UiInput().tap(400, 300)); // taps complete and turn the pages
    }
    rig.wait(0.5f);
    CHECK_FALSE(rig.fe.menuOpen());
    // The MENU button during play opens the in-game menu (a Gulf Thunder text button at the top).
    rig.frame(UiInput().tap(400, 20));
    REQUIRE(rig.top() == Screen::InGame);
    rig.wait(0.3f);
    rig.tapItem(3);
    REQUIRE(rig.top() == Screen::MainMenu);
    rig.tapItem(5);
    REQUIRE(rig.top() == Screen::Exit);
    rig.wait(0.3f);
    CHECK(rig.item(2)->hit.h >= 40);
    rig.tapItem(2);
    CHECK(rig.top() == Screen::MainMenu);
    // Information: seven pages, the spinner goes back by a tap (wrapping to the last one).
    rig.tapItem(4);
    rig.wait(0.3f);
    MenuItem* page = rig.item(10);
    REQUIRE(page);
    CHECK(page->values.size() == 7);
    rig.frame(UiInput().tap(page->x - 10, page->y + 8));
    CHECK(page->index == 6);
    rig.tapItem(1);
    // Name entry by the on-screen keyboard.
    rig.fe.open(Screen::NameEntry);
    rig.wait(0.3f);
    rig.tapItem(200 + 'A');
    rig.tapItem(200 + 'B');
    rig.tapItem(60);
    CHECK(rig.fe.menus().top()->find(2)->text == "A");
}

TEST_CASE("gulf front end: logo pages only, no intro comic; the brightness comes back") {
    Rig rig({}, false, true);
    CHECK(rig.fe.state() == FrontendState::Intro);
    CHECK(rig.fe.brightness() == doctest::Approx(0.5f)); // forced during the logo pages
    rig.frame();
    CHECK(rig.host.music.empty()); // no comic music
    rig.wait(7.0f);
    CHECK(rig.fe.state() == FrontendState::Intro);
    rig.wait(2.0f); // the DivoGames page: 8 s, then the menu (AirStrike 2 would go on to a comic)
    CHECK(rig.fe.state() == FrontendState::Attract);
    CHECK(rig.top() == Screen::MainMenu);
    CHECK(rig.fe.brightness() == doctest::Approx(rig.profile.settings.brightness));
    // A key speeds the page up by 4.
    Rig fast({}, false, true);
    fast.key(keys::Space);
    fast.wait(8.0f / 4 + 0.3f);
    CHECK(fast.fe.state() == FrontendState::Attract);
}

TEST_CASE("gulf front end: the helicopter selection: three helicopters, the 3D view, Continue") {
    Rig rig;
    rig.tapItem(1);
    rig.tapItem(2);
    REQUIRE(rig.top() == Screen::HeliSelect);
    ModelView v;
    CHECK_FALSE(rig.fe.modelView(v));
    rig.wait(0.2f);
    REQUIRE(rig.fe.modelView(v));
    CHECK(v.object == gulf().rules.heliObjects[0]);
    CHECK(rig.item(6)->x == 600);
    CHECK(rig.item(7)->x == 180);
    CHECK(rig.item(1)->label == "  Start  "); // no checkpoint: Start
    rig.tapItem(6);
    CHECK(rig.fe.helicopters()[0] == 1);
    CHECK_FALSE(rig.fe.modelView(v)); // locked: the silhouette
    CHECK(rig.item(1)->disabled());
    rig.key(keys::Escape);
    CHECK(rig.fe.helicopters()[0] == 0);
}

TEST_CASE("gulf front end: Information has seven pages with the texts' keys 1, 2, 3, 5, 6, 7, 8") {
    Texts t;
    for (int k : {1, 2, 3, 5, 6, 7, 8}) t.set("info." + std::to_string(k) + ".title", "TITLE " + std::to_string(k));
    for (int n = 1; n <= 7; n++) t.set("info.pages." + std::to_string(n), std::to_string(n) + " of 7");
    Rig rig(t);
    REQUIRE(rig.fe.texts().installed());
    rig.tapItem(4);
    rig.wait(0.3f);
    MenuItem* page = rig.item(10);
    REQUIRE(page);
    REQUIRE(page->values.size() == 7);
    CHECK(page->values[6] == "7 of 7");
    // PgDown walks the seven pages and wraps; each draws its own page's title.
    const int keyOf[] = {1, 2, 3, 5, 6, 7, 8};
    UiAssets none;
    Renderer2D r;
    for (int i = 0; i < 8; i++) {
        r.begin(800, 600);
        rig.fe.draw(r, none);
        CHECK(rig.fe.menus().top()->find(10)->index == i % 7);
        (void)keyOf;
        rig.key(keys::PageDown);
    }
    CHECK(rig.fe.menus().top()->find(10)->index == 1);
    rig.key(keys::PageUp);
    rig.key(keys::PageUp);
    CHECK(rig.fe.menus().top()->find(10)->index == 6);
}

TEST_CASE("gulf front end: a save written by the plain front end is read by the sequel front end") {
    // The plain front end (a copy of Gulf Thunder's profile with FrontendStyle::PlainList) plays
    // operation 1 and quits with a score.
    GameProfile plainCopy = gulf();
    plainCopy.frontend = FrontendStyle::PlainList;
    FakeGame host;
    Profile plainProfile;
    plainProfile.progress = Progress::defaults(gulf().rules, Progress::defaultHelicopters(&gulf()));
    FrontendContent pc = content();
    pc.game = &plainCopy;
    {
        Frontend plain(host, plainProfile, pc, Texts{});
        plain.boot();
        REQUIRE(plain.plain());
        auto tap = [&](int id) {
            const MenuItem* it = plain.menus().top()->find(id);
            REQUIRE(it);
            plain.update(1.0f / 60.0f, UiInput().tap(it->hit.x + it->hit.w * 0.5f, it->hit.y + it->hit.h * 0.5f));
        };
        tap(1); // Start Game
        tap(2); // Next: the operation starts
        REQUIRE_FALSE(host.starts.empty());
        plain.onEndLevel([] {
            MissionReport r;
            r.players[0] = {900, 2, 3, 5};
            r.totals = {10, 1000, 20};
            return r;
        }());
        plainProfile.progress.insert("PLAIN", 123456, 3);
    }
    CHECK(plainProfile.progress.missionUnlocked[1]);
    CHECK(plainProfile.progress.checkpoint.mission == -1); // the plain front end never sets one
    const std::vector<u8> bytes = serializeProfile(plainProfile, "gulf");
    CHECK(std::string(bytes.begin(), bytes.end()).find("CHKP") == std::string::npos);
    Profile in = Rig::fresh();
    std::string why;
    REQUIRE_MESSAGE(deserializeProfile(bytes.data(), bytes.size(), in, &why, "gulf"), why);
    Rig rig({}, false, false, in);
    CHECK(rig.fe.sequel());
    rig.tapItem(1);
    REQUIRE(rig.top() == Screen::StartGame);
    const MenuItem* list = rig.item(5);
    REQUIRE(list);
    REQUIRE(list->entries.size() == 24);
    CHECK(list->entries[0].enabled);
    CHECK(list->entries[1].enabled); // unlocked by the plain front end's play
    CHECK_FALSE(list->entries[5].enabled);
    int scores = 0;
    for (const HighScore& h : rig.profile.progress.scores) scores += h.name == "PLAIN" && h.score == 123456;
    CHECK(scores == 1);
    // And the other way: a sequel-front-end save with a checkpoint is read by a file reader that
    // knows the game (the checkpoint chunk is optional).
    Profile cp = Rig::fresh();
    cp.progress.checkpoint.mission = 5;
    cp.progress.checkpoint.lives[0] = 4;
    const std::vector<u8> b2 = serializeProfile(cp, "gulf");
    Profile back = Rig::fresh();
    REQUIRE_MESSAGE(deserializeProfile(b2.data(), b2.size(), back, &why, "gulf"), why);
    CHECK(back.progress.checkpoint.mission == 5);
    CHECK(back.progress.checkpoint.lives[0] == 4);
    // Counts: 24 missions and 3 helicopters are what a Gulf Thunder file holds.
    CHECK(gulf().rules.missionCount == 24);
    CHECK(gulf().rules.helicopterCount == 3);
}

TEST_CASE("gulf front end: texts present and absent; tools/exe_texts/gulf.json points at the start of its strings") {
    // Absent: Gulf Thunder's own captions (padding sets the widths), no dialogue.
    Rig none;
    CHECK(none.fe.texts().get("button.start_game") == " Start Game "); // the generic default is AirStrike 2's...
    const MenuItem* start = none.item(1);
    REQUIRE(start);
    CHECK(start->hit.w == doctest::Approx(180)); // ...but the screen uses Gulf Thunder's own (no padding)
    none.startGame(0);
    CHECK_FALSE(none.fe.menuOpen());
    // Present: the texts file of the owner's executable, when it has been extracted.
    const GameData data = locateGameData(testdata::root(), gulf());
    Blob b;
    if (data.textsFile.empty() || !readPlatformFile(data.textsFile, b)) {
        std::fprintf(stderr, "SKIPPED (no texts_gulf.txt; run tools/extract_exe_texts.py --game gulf): %s\n", __FILE__);
    } else {
        Texts t;
        CHECK(t.parse(std::string(reinterpret_cast<const char*>(b.data()), b.size())) >= 228);
        CHECK(t.installed());
        // The corrected addresses (issue gulf/402): the controls rows, the credits lines, the three
        // helicopters, seven Information pages, the captions Gulf Thunder has twice.
        CHECK(t.get("ctl.row.2") != "-");
        CHECK(t.get("ctl.row.9") != t.get("ctl.row.6"));
        CHECK(t.loaded("heli.2"));
        CHECK_FALSE(t.loaded("heli.3"));
        CHECK_FALSE(t.loaded("info.pages.8"));
        CHECK(t.get("info.pages.7").find("of 7") != std::string::npos);
        CHECK(t.get("info.pages.3") != t.get("info.pages.2"));
        CHECK(t.loaded("credits.22"));
        CHECK(t.get("credits.0").front() != '{');
        CHECK(t.get("credits.1").front() == '{'); // a name
        CHECK(t.get("credits.3") != t.get("credits.1"));
        CHECK(t.get("credits.3").front() != '{');
        CHECK(t.get("button.restart.gameover") != t.get("button.restart"));
        CHECK(t.get("button.continue.heli") != t.get("button.continue"));
        CHECK(t.get("title.top_scores").find("Post") == std::string::npos);
        CHECK(t.get("title.hint").size() > 3);
        CHECK(t.get("dialog.10.start.0").find('\n') != std::string::npos);
        Rig rig(t);
        rig.unlockAll();
        rig.startGame(9);
        CHECK(rig.top() == Screen::Dialogue); // operation 10 has a start dialogue
        Rig one(t);
        one.startGame(0);
        CHECK_FALSE(one.fe.menuOpen()); // operation 1 has none
    }
    // The address list: every text entry's address is the first byte of a string of the executable.
    const std::string exe = testdata::root() + "/third_party_local/games/gulf/AirStrike3D II - Gulf.exe";
    const std::string bytes = readFileText(exe);
    const std::string json = readFileText(std::string(AS3D_REPO_ROOT) + "/tools/exe_texts/gulf.json");
    REQUIRE_FALSE(json.empty());
    if (bytes.size() < 0x90000) {
        std::fprintf(stderr, "SKIPPED part (no Gulf Thunder executable): %s\n", __FILE__);
        return;
    }
    int checked = 0;
    const size_t ent = json.find("\"entries\"");
    const size_t rem = json.find("\"removed\"");
    for (size_t p = json.find("\"key\":", ent); p != std::string::npos && p < rem; p = json.find("\"key\":", p + 1)) {
        auto field = [&](const char* name) {
            const size_t f = json.find(std::string("\"") + name + "\":", p);
            const size_t s = json.find('"', json.find(':', f) + 1);
            return json.substr(s + 1, json.find('"', s + 1) - s - 1);
        };
        if (field("kind") == "u32") continue;
        const unsigned long addr = std::stoul(field("address"), nullptr, 16);
        const std::string key = field("key");
        // Sections: .rdata at 0x47e000 (raw 0x7e000), .data at 0x496000 (raw 0x96000): offset = address - 0x400000.
        const size_t off = addr - 0x400000;
        REQUIRE_MESSAGE(off < bytes.size(), key);
        INFO("key " << key);
        CHECK(bytes[off - 1] == '\0'); // the first byte of a string
        CHECK(bytes[off] != '\0');
        checked++;
    }
    CHECK(checked > 200);
}

// ---------------------------------------------------------------------------------------
// With the game's data
// ---------------------------------------------------------------------------------------
TEST_CASE("gulf front end: no texture of another game; headless renders in 4:3 and wide") {
    AS3D_REQUIRE_GLES();
    const GameData data = locateGameData(testdata::root(), gulf());
    if (!data.present()) {
        std::fprintf(stderr, "SKIPPED (no data for gulf): %s\n", __FILE__);
        return;
    }
    Vfs vfs;
    if (data.hasExtracted) vfs.mount(makeDirSource(data.extractedDir));
    else
        for (const std::string& p : data.paks) vfs.mount(makePakSource(openFileStream(p)));
    std::vector<std::string> all;
    struct Spy : IFileSource {
        Vfs* inner;
        std::vector<std::string>* log;
        bool exists(const std::string& p) override { return inner->exists(p); }
        bool read(const std::string& p, Blob& out) override { log->push_back(p); return inner->read(p, out); }
        void list(std::vector<std::string>&) override {}
    };
    Vfs spied;
    auto spy = std::make_unique<Spy>();
    spy->inner = &vfs;
    spy->log = &all;
    spied.mount(std::move(spy));
    for (int mode = 0; mode < 2; mode++) {
        const int W = mode == 0 ? 800 : 2400, H = mode == 0 ? 600 : 1080;
        UiAssets assets;
        std::string err;
        REQUIRE_MESSAGE(assets.load(spied, &err, GameId::GulfThunder), err);
        Renderer2D r;
        REQUIRE(r.init(&err));
        RenderTarget target;
        REQUIRE(target.create(W, H, 0));
        const Mapping map = computeMapping(W, H);
        const u8 bg[3] = {40, 60, 80};
        auto render = [&](Frontend& fe, Image& img, bool loading = false) {
            target.bind();
            clear({40 / 255.0f, 60 / 255.0f, 80 / 255.0f, 1}, true);
            r.begin(W, H);
            if (loading) drawLoadingScreen(r, assets, 0.5f, false);
            else fe.draw(r, assets);
            CHECK(r.dropped() == 0);
            r.flush();
            REQUIRE(target.readPixels(img));
        };
        auto lit = [&](const Image& img, float x, float y, float w, float h) {
            return uitest::litIn(img, bg, static_cast<int>(map.toFbX(x)), static_cast<int>(map.toFbY(y)),
                                 static_cast<int>(map.toFbX(x + w)), static_cast<int>(map.toFbY(y + h)));
        };
        const long s2 = std::max(1L, static_cast<long>(map.scaleX * map.scaleY));
        Rig rig(dialogueTexts(), mode == 1);
        Image img;
        rig.wait(0.5f);
        render(rig.fe, img);
        CHECK(lit(img, 310, 250, 180, 37) > 2500 * s2); // Start Game's frame
        CHECK(lit(img, 144, 0, 512, 256) > 50000 * s2); // the title bar with the emblem
        CHECK(lit(img, 0, 0, 800, 97) > 20000 * s2);    // the black letterbox bar
        CHECK(lit(img, 0, 100, 800, 440) > 300000 * s2); // the scan-line texture over the middle band
        for (Screen s : kSequelScreens) {
            if (s == Screen::Dialogue || s == Screen::MainMenu) continue;
            rig.fe.open(s);
            rig.wait(0.5f);
            render(rig.fe, img);
        }
        // A panel: Exit's (210, 230, 380, 160) with its red title tab.
        rig.fe.open(Screen::Exit);
        rig.wait(0.5f);
        render(rig.fe, img);
        CHECK(lit(img, 210, 230, 380, 160) > 10000 * s2);
        rig.fe.open(Screen::MainMenu);
        rig.unlockAll();
        rig.startGame(9);
        REQUIRE(rig.top() == Screen::Dialogue);
        rig.wait(1.5f);
        render(rig.fe, img);
        CHECK(lit(img, 120, 420, 80, 120) > 5000 * s2); // the portrait
        render(rig.fe, img, true);                       // the loading comic, one for every operation
        CHECK(lit(img, 0, 65, 832, 512) > 250000 * s2);
        CHECK(lit(img, 340, 575, 200, 10) > 200 * s2);   // the progress bar
    }
    std::set<std::string> paths(all.begin(), all.end());
    for (const std::string& p : paths) {
        INFO("read: " << p);
        CHECK(vfs.exists(p)); // only files Gulf Thunder ships
        const bool mine = p.compare(0, 4, "gfx\\") == 0 || p.compare(0, 5, "menu\\") == 0;
        CHECK(mine);
        if (p.compare(0, 5, "menu\\") == 0) CHECK(p.find("cursor_") != std::string::npos);
        // Nothing of AirStrike 2's front end: its interface atlas, title logo, intro comic.
        CHECK(p != "gfx\\ui\\interface.tga");
        CHECK(p != "gfx\\logo\\logo.tga");
        CHECK(p != "gfx\\logo\\glow.tga");
        CHECK(p != "gfx\\logo\\two3.tga");
        CHECK(p.find("comix\\intro") == std::string::npos);
    }
    CHECK(paths.count("gfx\\ui\\interface_gulf.tga"));
    CHECK(paths.count("gfx\\logo\\logo_gulf.tga"));
    CHECK(paths.count("gfx\\logo\\lines_gulf.tga"));
    CHECK(paths.count("gfx\\ui\\comix\\loading1_0_0.tga"));
}

TEST_CASE("gulf front end: the real game behind the menus: start, the dialogue holds, play, in-game menu, end, quit") {
    AS3D_REQUIRE_GLES();
    const GameData data = locateGameData(testdata::root(), gulf());
    if (!data.present() || (!data.hasExtracted && data.paks.empty())) {
        std::fprintf(stderr, "SKIPPED (no data for gulf): %s\n", __FILE__);
        return;
    }
    using namespace as3d_game;
    GameSession session;
    GameOptions o;
    o.dataRoot = testdata::root();
    o.game = &gulf();
    o.startLevel = false;
    o.levelFlow = false;
    std::string err;
    REQUIRE_MESSAGE(session.init(o, &err), err);
    AudioBridge audio;
    audio.init(session.vfs(), true);
    GameView view;
    REQUIRE_MESSAGE(view.init(session, &err, false), err);
    GameFlow flow(session, audio);
    flow.setView(&view);
    FlowConfig c;
    c.attract = 1;
    c.showLogo = false;
    c.textsPath = data.textsFile;
    REQUIRE_MESSAGE(flow.init(c, &err), err);
    flow.boot();
    Frontend& fe = flow.frontend();
    REQUIRE(fe.sequel());
    // Unlock everything so that operation 10 can be chosen.
    for (int i = 0; i < gulf().rules.missionCount; i++) flow.profile().progress.missionUnlocked[i] = true;
    for (int h = 0; h < gulf().rules.helicopterCount; h++) flow.profile().progress.helicopterUnlocked[h] = true;
    RenderTarget target;
    REQUIRE(target.create(400, 300, 0));
    auto step = [&](const UiInput& in = {}) {
        flow.uiFrame(1.0f / 60.0f, in);
        flow.step({});
    };
    auto tapItem = [&](int id) {
        Menu* m = fe.menus().top();
        REQUIRE(m);
        const MenuItem* it = m->find(id);
        REQUIRE_MESSAGE(it, "no item " << id);
        step(UiInput().tap(it->hit.x + it->hit.w * 0.5f, it->hit.y + it->hit.h * 0.5f));
    };
    CHECK(session.world().intermission());
    tapItem(1);
    REQUIRE(fe.topScreen() == Screen::StartGame);
    const bool dialogue = !data.textsFile.empty();
    fe.menus().top()->find(5)->selected = dialogue ? 9 : 0;
    tapItem(2);
    REQUIRE(fe.topScreen() == Screen::HeliSelect);
    for (int i = 0; i < 20; i++) step();
    target.bind();
    flow.draw(400, 300);
    CHECK(flow.modelViewsDrawn() == 1); // the helicopter preview
    tapItem(1);
    REQUIRE(fe.state() == FrontendState::Playing);
    CHECK(session.mission() == (dialogue ? 10 : 1));
    CHECK_FALSE(session.world().intermission());
    if (dialogue) {
        // The start dialogue of operation 10 holds the mission: the world does not advance under it.
        REQUIRE(fe.topScreen() == Screen::Dialogue);
        CHECK(session.world().paused());
        const float t0 = session.world().time();
        for (int i = 0; i < 60; i++) step();
        CHECK(session.world().time() == t0);
        CHECK(fe.hudVisible());
        for (int i = 0; i < 60 && fe.menuOpen(); i++) {
            for (int k = 0; k < 20; k++) step();
            step(UiInput().key(keys::Enter));
        }
        for (int i = 0; i < 40; i++) step();
    } else {
        std::fprintf(stderr, "SKIPPED part (no texts_gulf.txt: no dialogue): %s\n", __FILE__);
    }
    REQUIRE_FALSE(fe.menuOpen());
    CHECK_FALSE(session.world().paused());
    const float t1 = session.world().time();
    for (int i = 0; i < 120; i++) step();
    CHECK(session.world().time() > t1);
    step(UiInput().key(keys::Escape));
    REQUIRE(fe.topScreen() == Screen::InGame);
    CHECK(session.world().paused());
    for (int i = 0; i < 20; i++) step();
    tapItem(1); // Resume
    CHECK_FALSE(session.world().paused());
    // The end of the operation: the checkpoint, then (after operation 10's end dialogue) Mission Complete.
    session.world().endLevel();
    for (int i = 0; i < 5 && fe.state() == FrontendState::Playing && !fe.menuOpen(); i++) step();
    REQUIRE(fe.menuOpen());
    CHECK(flow.profile().progress.checkpoint.mission == (dialogue ? 10 : 1));
    for (int i = 0; i < 80 && fe.topScreen() == Screen::Dialogue; i++) {
        for (int k = 0; k < 20; k++) step();
        step(UiInput().key(keys::Enter));
    }
    for (int i = 0; i < 40; i++) step();
    REQUIRE(fe.topScreen() == Screen::MissionComplete);
    tapItem(1); // Quit
    CHECK(fe.topScreen() == Screen::MainMenu);
    CHECK(session.world().intermission());
}
