// AirStrike 2's own front end (FrontendStyle::SequelMenus, docs/spec/as2/frontend.md): the state
// machine with a fake game (new game, start dialogue, play, in-game menu, restart, end
// dialogue, mission complete with statistics, helicopter selection, Next, game over, the
// checkpoint and "Continue" across a restart of the program, game complete), what each button
// does to lives, score, rank, upgrades and the checkpoint (5.4), pointer-only operation in touch
// mode, profile compatibility, texts present and absent, and with the game's data: no texture of
// another game, headless renders, and the real game behind the menus. The sequel is selected
// explicitly (gameProfile, locateGameData), so all of it runs in the default pass of
// tools/ci.sh; what needs the sequel's data or a GLES context skips loudly without it.
#include "doctest.h"

#include <algorithm>
#include <cstdio>
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

const GameProfile& as2() { return gameProfile(GameId::AirStrike2); }

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

// Generic stand-ins for the texts file (never the executable's own texts).
Texts dialogueTexts() {
    Texts t;
    t.set("dialog.1.start.0", "Line one\nline two");
    t.set("dialog.1.start.0.speaker", "0");
    t.set("dialog.1.start.1", "Page two");
    t.set("dialog.1.start.1.speaker", "1");
    t.set("dialog.1.end.0", "The end");
    t.set("dialog.1.end.0.speaker", "1");
    return t;
}

FrontendContent content(bool twoPlayers = false) {
    FrontendContent c;
    c.game = &as2();
    c.videoOptions = false;
    c.twoPlayerMode = twoPlayers;
    c.screenOption = true;
    c.handOption = true;
    for (int i = 0; i < as2().rules.missionCount; i++) c.missionNames[i] = "Mission " + std::to_string(i + 1) + ": Test";
    // levels.txt: missions 4, 7, 10, 13, 16 unlock helicopters 1 to 5.
    for (int n = 1; n < as2().rules.helicopterCount; n++) c.enableHelic[3 * n] = n;
    for (int h = 0; h < as2().rules.helicopterCount; h++) c.heli[h] = {true, 300 + 100 * h, true, 1.0f};
    return c;
}

struct Rig {
    FakeGame host;
    Profile profile;
    Frontend fe;
    explicit Rig(Texts texts = {}, bool touch = false, bool twoPlayers = false, bool showLogo = false, Profile p = fresh())
        : profile(p), fe(host, profile, content(twoPlayers), std::move(texts)) {
        profile.settings.showLogo = showLogo;
        fe.setTouchMode(touch);
        fe.menus().setPointer(-50, -50);
        fe.boot();
    }
    static Profile fresh() {
        Profile p;
        p.progress = Progress::defaults(as2().rules, Progress::defaultHelicopters(&as2()));
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
    // A tap (touch) or a click (mouse) in the middle of an item's hit rectangle.
    void tapItem(int id) {
        MenuItem* it = item(id);
        REQUIRE_MESSAGE(it, "no item " << id << " on " << screenName(top()));
        REQUIRE_MESSAGE(!it->disabled(), "item " << id << " is disabled");
        frame(UiInput().tap(it->hit.x + it->hit.w * 0.5f, it->hit.y + it->hit.h * 0.5f));
    }
    // Main menu -> Start Game -> (mission) -> Next -> helicopter selection -> Start.
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
    // Lets a dialogue type out and close (Enter completes a page, again goes to the next).
    void finishDialogue() {
        for (int guard = 0; guard < 40 && top() == Screen::Dialogue && fe.menuOpen(); guard++) {
            wait(0.3f);
            if (!fe.menuOpen() || top() != Screen::Dialogue) break;
            key(keys::Enter);
        }
        wait(0.5f);
    }
};

} // namespace

TEST_CASE("as2 front end: AirStrike 2 selects the sequel's menus, and they build their screens") {
    CHECK(as2().frontend == FrontendStyle::SequelMenus);
    Rig rig;
    CHECK(rig.fe.sequel());
    CHECK_FALSE(rig.fe.plain());
    CHECK(rig.fe.menus().sequel);
    CHECK_FALSE(rig.fe.bannerVisible());
    REQUIRE(rig.top() == Screen::MainMenu);
    // Six text buttons, 246 wide, centred on 400 (as2/frontend.md 3.3).
    const int ids[] = {1, 2, 3, 4, 7, 5};
    const float ys[] = {230, 275, 320, 365, 410, 455};
    for (int i = 0; i < 6; i++) {
        const MenuItem* it = rig.item(ids[i]);
        REQUIRE(it);
        CHECK(it->type == ItemType::SequelButton);
        CHECK(it->hit.w == doctest::Approx(246));
        CHECK(it->hit.x == doctest::Approx(277));
        CHECK(it->hit.y == doctest::Approx(ys[i]));
        CHECK((it->flags & itemflag::NoHoverSound) != 0);
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
        CHECK(r.dropped() == 0); // (without assets only untextured quads are drawn)
    }
    // The first game's front end is unchanged.
    FakeGame host;
    Profile p;
    Frontend first(host, p, FrontendContent{}, Texts{});
    CHECK_FALSE(first.sequel());
    CHECK_FALSE(first.menus().sequel);
}

TEST_CASE("as2 front end: the menu opens in 0.125 s, text buttons slide in over 0.25 s") {
    Rig rig;
    rig.tapItem(1);
    REQUIRE(rig.top() == Screen::StartGame);
    Menu* m = rig.fe.menus().top();
    CHECK(m->open < 0.2f); // the frame that pushed it raised it once
    rig.frame();
    CHECK(m->open > 0.2f);
    rig.wait(0.12f);
    CHECK(m->open == doctest::Approx(1.0f));
    const MenuItem* next = m->find(2);
    REQUIRE(next);
    CHECK(next->slide < 1.0f);
    rig.wait(0.2f);
    CHECK(next->slide == doctest::Approx(1.0f));
    // A menu uncovered by a pop opens again.
    rig.tapItem(2);
    REQUIRE(rig.top() == Screen::HeliSelect);
    rig.wait(0.3f);
    rig.key(keys::Escape);
    REQUIRE(rig.top() == Screen::StartGame);
    CHECK(rig.fe.menus().top()->open < 1.0f);
}

TEST_CASE("as2 front end: a new game, the start dialogue holds the mission, the in-game menu, Restart") {
    Rig rig(dialogueTexts());
    rig.startGame(0);
    REQUIRE(rig.host.starts.size() == 1);
    const MissionStart& s = rig.host.starts.back();
    CHECK(s.mission == 0);
    CHECK(s.lives[0] == as2().rules.startLives);
    CHECK(s.banked[0] == 0);
    CHECK_FALSE(s.carryUpgrades); // the mission's loadout
    CHECK(s.helicopter[0] == 0);
    CHECK(s.difficulty == 2);
    // The start dialogue: paused, HUD shown.
    REQUIRE(rig.top() == Screen::Dialogue);
    CHECK(rig.host.paused);
    CHECK(rig.fe.paused());
    CHECK(rig.fe.hudVisible());
    rig.wait(0.3f); // opening
    // Enter completes the page, then goes to the next; never closes at once.
    rig.key(keys::Enter);
    CHECK(rig.top() == Screen::Dialogue);
    rig.key(keys::Enter);
    CHECK(rig.top() == Screen::Dialogue);
    rig.finishDialogue();
    CHECK_FALSE(rig.fe.menuOpen());
    CHECK_FALSE(rig.host.paused);
    CHECK(rig.host.clears >= 1);
    // The in-game menu: Esc, Resume.
    rig.key(keys::Escape);
    REQUIRE(rig.top() == Screen::InGame);
    CHECK(rig.host.paused);
    CHECK_FALSE(rig.fe.hudVisible());
    rig.wait(0.3f);
    rig.tapItem(1);
    CHECK_FALSE(rig.fe.menuOpen());
    CHECK_FALSE(rig.host.paused);
    // Restart: the same mission, the lives it began with, the loadout, the dialogue again.
    rig.key(keys::Escape);
    rig.wait(0.3f);
    rig.tapItem(4);
    REQUIRE(rig.host.starts.size() == 2);
    CHECK(rig.host.starts.back().restart);
    CHECK(rig.host.starts.back().mission == 0);
    CHECK_FALSE(rig.host.starts.back().carryUpgrades);
    CHECK(rig.top() == Screen::Dialogue);
    rig.finishDialogue();
    // Quit: attract level, main menu, nothing banked.
    rig.key(keys::Escape);
    rig.wait(0.3f);
    rig.tapItem(3);
    CHECK(rig.top() == Screen::MainMenu);
    CHECK(rig.fe.campaign().p[0].banked == 0);
}

TEST_CASE("as2 front end: mission end: checkpoint, end dialogue, unlocks, statistics, Next, Choose Helicopter") {
    Rig rig(dialogueTexts());
    rig.startGame(0);
    rig.finishDialogue();
    const int saves = rig.host.saves;
    rig.fe.onEndLevel(rig.report(500, 1));
    // The checkpoint is written at EndLevel, before the dialogue, and saved at once.
    CHECK(rig.profile.progress.checkpoint.mission == 1);
    CHECK(rig.profile.progress.checkpoint.lives[0] == 1);
    CHECK(rig.profile.progress.checkpoint.score[0] == 500);
    CHECK(rig.host.saves > saves);
    // The end dialogue first, the unlocks after it.
    REQUIRE(rig.top() == Screen::Dialogue);
    CHECK_FALSE(rig.fe.hudVisible());
    CHECK_FALSE(rig.profile.progress.missionUnlocked[2]);
    rig.finishDialogue();
    REQUIRE(rig.top() == Screen::MissionComplete);
    CHECK(rig.host.paused);
    CHECK(rig.profile.progress.missionUnlocked[1]);
    // Choose Helicopter: accept mode, the arrows write the choice, a locked one is not kept.
    rig.wait(0.3f);
    rig.tapItem(4);
    REQUIRE(rig.top() == Screen::HeliSelect);
    CHECK(rig.item(8));
    CHECK_FALSE(rig.item(1));
    CHECK_FALSE(rig.item(2)); // no Back in accept mode
    rig.wait(0.3f);
    rig.tapItem(6);
    CHECK(rig.fe.helicopters()[0] == 1); // written at once, locked
    CHECK(rig.item(8)->disabled());     // Accept is disabled for a locked one
    rig.key(keys::Escape);
    REQUIRE(rig.top() == Screen::MissionComplete);
    CHECK(rig.fe.helicopters()[0] == 0); // issue 240 item 4: the locked choice is not kept
    // Next: banked, the next mission, the upgrades carried.
    rig.wait(0.3f);
    rig.tapItem(3);
    REQUIRE(rig.host.starts.size() == 2);
    const MissionStart& n = rig.host.starts.back();
    CHECK(n.mission == 1);
    CHECK(n.banked[0] == 500);
    CHECK(n.lives[0] == 1);
    CHECK(n.carryUpgrades);
    CHECK(n.upgrades[0][0] == 4);
    CHECK(n.weapon[0] == 3);
    CHECK(n.rankAccumulator[0] > 0.0);
    CHECK(rig.fe.campaign().p[0].banked == 500);
}

TEST_CASE("as2 front end: each button's effect on the campaign (as2/frontend.md 5.4)") {
    SUBCASE("Mission Complete: Restart and Quit bank nothing; the checkpoint stays") {
        Rig rig;
        rig.startGame(0);
        rig.fe.onEndLevel(rig.report(700, 2));
        REQUIRE(rig.top() == Screen::MissionComplete); // no dialogue without the texts
        rig.wait(0.3f);
        rig.tapItem(2); // Restart
        CHECK(rig.host.starts.back().restart);
        CHECK(rig.host.starts.back().banked[0] == 0);
        CHECK(rig.host.starts.back().lives[0] == as2().rules.startLives);
        CHECK_FALSE(rig.host.starts.back().carryUpgrades);
        rig.fe.onEndLevel(rig.report(700, 2));
        rig.wait(0.3f);
        rig.tapItem(1); // Quit
        CHECK(rig.top() == Screen::MainMenu);
        CHECK(rig.fe.campaign().p[0].banked == 0);
        CHECK(rig.profile.progress.checkpoint.mission == 1);
        CHECK(rig.host.quits == 0);
    }
    SUBCASE("Game over: Restart keeps the lives it began with; Quit banks and checks the high score") {
        Rig rig;
        rig.startGame(0);
        rig.fe.onGameOver(rig.report(40000, -1, false));
        REQUIRE(rig.top() == Screen::GameOver);
        // Usable at once.
        rig.frame();
        rig.tapItem(1);
        CHECK(rig.host.starts.back().restart);
        CHECK(rig.host.starts.back().lives[0] == as2().rules.startLives);
        rig.fe.onGameOver(rig.report(40000, -1, false));
        rig.frame();
        rig.tapItem(2);
        CHECK(rig.fe.campaign().p[0].banked == 40000);
        CHECK(rig.top() == Screen::NameEntry); // 40000 qualifies in the fresh table
        rig.frame(UiInput().text("ACE"));
        rig.key(keys::Enter);
        CHECK(rig.top() == Screen::TopScores);
        int found = 0;
        for (const HighScore& h : rig.profile.progress.scores) found += h.name == "ACE" && h.score == 40000;
        CHECK(found == 1);
    }
}

TEST_CASE("as2 front end: Continue from the checkpoint after a restart of the program") {
    Profile savedProfile;
    {
        Rig rig;
        rig.startGame(0);
        rig.fe.onEndLevel(rig.report(1200, 1));
        rig.wait(0.3f);
        rig.tapItem(3); // Next: mission 2
        rig.fe.onEndLevel(rig.report(800, 3));
        CHECK(rig.profile.progress.checkpoint.mission == 2);
        CHECK(rig.profile.progress.checkpoint.score[0] == 2000);
        rig.wait(0.3f);
        rig.tapItem(1); // Quit: nothing banked, the checkpoint stays
        const std::vector<u8> bytes = serializeProfile(rig.profile, "as2");
        savedProfile = Rig::fresh();
        std::string why;
        REQUIRE_MESSAGE(deserializeProfile(bytes.data(), bytes.size(), savedProfile, &why, "as2"), why);
    }
    CHECK(savedProfile.progress.checkpoint.mission == 2);
    Rig rig({}, false, false, false, savedProfile);
    rig.tapItem(1);
    REQUIRE(rig.top() == Screen::StartGame);
    CHECK(rig.item(5)->selected == 2); // the checkpoint's mission is preselected
    rig.tapItem(2);
    REQUIRE(rig.top() == Screen::HeliSelect);
    CHECK(rig.item(1)->label == rig.fe.texts().get("button.continue"));
    rig.tapItem(1);
    const MissionStart& s = rig.host.starts.back();
    CHECK(s.mission == 2);
    CHECK(s.lives[0] == 3);
    CHECK(s.banked[0] == 2000);
    CHECK(s.rankAccumulator[0] == doctest::Approx(savedProfile.progress.checkpoint.rank[0]));
    // Any other mission starts afresh and forgets the checkpoint; the tutorial never continues.
    Rig other({}, false, false, false, savedProfile);
    other.startGame(0);
    CHECK(other.host.starts.back().banked[0] == 0);
    CHECK(other.profile.progress.checkpoint.mission == -1);
    // A cheat saves "none".
    Rig cheat;
    cheat.startGame(0);
    MissionReport r = cheat.report(100, 2);
    r.cheatUsed = true;
    cheat.fe.onEndLevel(r);
    CHECK(cheat.profile.progress.checkpoint.mission == -1);
}

TEST_CASE("as2 front end: helicopters unlock after the end dialogue; mission 18 ends the game") {
    Rig rig(dialogueTexts());
    Frontend& fe = rig.fe;
    for (int i = 0; i < as2().rules.missionCount; i++) rig.profile.progress.missionUnlocked[i] = true;
    rig.startGame(3); // mission 4 unlocks helicopter 1
    rig.fe.onEndLevel(rig.report(100, 2));
    CHECK(rig.profile.progress.helicopterUnlocked[1]);
    REQUIRE(rig.top() == Screen::MissionComplete);
    // Mission 18: Game Complete, its Continue only once drawn (issue 240 item 7).
    fe.debugSet("mission", "18");
    fe.onEndLevel(rig.report(300, 2));
    REQUIRE(rig.top() == Screen::GameComplete);
    CHECK(rig.item(1)->disabled());
    rig.wait(4.2f);
    CHECK_FALSE(rig.item(1)->disabled());
    const std::int64_t before = fe.campaign().p[0].banked;
    rig.wait(0.3f);
    rig.tapItem(1);
    CHECK(fe.campaign().p[0].banked == before + 300);
    CHECK((rig.top() == Screen::MainMenu || rig.top() == Screen::NameEntry));
    // After mission 18 the checkpoint is past the last mission: never a Continue.
    CHECK(rig.profile.progress.checkpoint.mission == as2().rules.missionCount);
}

TEST_CASE("as2 front end: operable by pointer only in touch mode") {
    Rig rig(dialogueTexts(), true);
    CHECK(rig.fe.touchMode());
    rig.tapItem(1);
    rig.wait(0.3f);
    // The spinner's backwards zone: difficulty Normal -> Easy.
    MenuItem* diff = rig.item(3);
    REQUIRE(diff);
    rig.frame(UiInput().tap(diff->x - 10, diff->y + 8));
    CHECK(diff->index == 1);
    rig.tapItem(2);
    rig.wait(0.3f);
    // The arrows have finger-sized hit rectangles.
    CHECK(rig.item(6)->hit.w >= 44);
    rig.tapItem(6);
    rig.tapItem(7);
    CHECK(rig.fe.helicopters()[0] == 0);
    rig.tapItem(1);
    REQUIRE(rig.top() == Screen::Dialogue);
    // Taps complete and turn the pages.
    for (int i = 0; i < 40 && rig.fe.menuOpen(); i++) {
        rig.wait(0.3f);
        rig.frame(UiInput().tap(400, 300));
    }
    rig.wait(0.5f);
    CHECK_FALSE(rig.fe.menuOpen());
    // The MENU button during play opens the in-game menu.
    const RectF menu{400 - 40, 8, 80, 20};
    rig.frame(UiInput().tap(menu.x + menu.w / 2, menu.y + menu.h / 2));
    REQUIRE(rig.top() == Screen::InGame);
    rig.wait(0.3f);
    rig.tapItem(3);
    REQUIRE(rig.top() == Screen::MainMenu);
    // Exit: finger-sized YES / NO.
    rig.tapItem(5);
    REQUIRE(rig.top() == Screen::Exit);
    rig.wait(0.3f);
    CHECK(rig.item(2)->hit.h >= 40);
    rig.tapItem(2);
    CHECK(rig.top() == Screen::MainMenu);
    // Information: the page spinner goes back by a tap; no key hints.
    rig.tapItem(4);
    rig.wait(0.3f);
    MenuItem* page = rig.item(10);
    rig.frame(UiInput().tap(page->x - 10, page->y + 8));
    CHECK(page->index == 7);
    rig.tapItem(1);
    // Name entry by the on-screen keyboard.
    rig.fe.open(Screen::NameEntry);
    rig.wait(0.3f);
    rig.tapItem(200 + 'A');
    rig.tapItem(200 + 'B');
    rig.tapItem(60); // Del
    Menu* m = rig.fe.menus().top();
    CHECK(m->find(2)->text == "A");
}

TEST_CASE("as2 front end: the intro comic, its music, speed-up and the touch Skip") {
    Rig rig({}, false, false, true);
    CHECK(rig.fe.state() == FrontendState::Intro);
    rig.frame();
    REQUIRE_FALSE(rig.host.music.empty());
    CHECK(rig.host.music.front() == "music\\track02.mo3");
    // 7 + 7 + 20 + 11 s at speed 1; a key multiplies the speed by 4 per page.
    rig.wait(6.9f);
    CHECK(rig.fe.state() == FrontendState::Intro);
    rig.wait(44.0f);
    CHECK(rig.fe.state() == FrontendState::Attract);
    CHECK(rig.host.music.back().empty()); // stopped after the comic
    Rig fast({}, false, false, true);
    fast.key(keys::Space);
    fast.wait(7.0f / 4 + 0.1f);
    fast.key(keys::Space);
    fast.wait(7.0f / 4 + 0.1f);
    fast.key(keys::Space);
    fast.wait(20.0f / 4 + 0.1f);
    fast.key(keys::Space);
    fast.wait(11.0f / 4 + 0.1f);
    CHECK(fast.fe.state() == FrontendState::Attract);
    // Touch: the Skip button ends it at once.
    Rig touch({}, true, false, true);
    touch.frame(UiInput().tap(740, 575));
    touch.frame();
    CHECK(touch.fe.state() == FrontendState::Attract);
    CHECK(touch.top() == Screen::MainMenu);
}

TEST_CASE("as2 front end: the helicopter selection asks the host for its 3D view") {
    Rig rig;
    rig.tapItem(1);
    rig.tapItem(2);
    REQUIRE(rig.top() == Screen::HeliSelect);
    ModelView v;
    CHECK_FALSE(rig.fe.modelView(v)); // not before the menu is open
    rig.wait(0.2f);
    REQUIRE(rig.fe.modelView(v));
    CHECK(v.object == as2().rules.heliObjects[0]);
    CHECK(v.viewport.x == 170);
    CHECK(v.viewport.w == 460);
    CHECK(v.origin[2] == -100.0f);
    rig.fe.drawModelViews();
    CHECK(rig.host.views.size() == 1);
    // A locked helicopter: the silhouette, no 3D view.
    rig.tapItem(6);
    CHECK_FALSE(rig.fe.modelView(v));
}

TEST_CASE("as2 front end: profile compatibility") {
    const Progress fresh = Rig::fresh().progress;
    CHECK(fresh.helicopterUnlocked[0]);
    CHECK_FALSE(fresh.helicopterUnlocked[1]);
    CHECK(fresh.checkpoint.mission == -1);
    // A version 2 as2 file of the plain front end (no CHKP chunk) keeps its unlocks and scores.
    Profile old = Rig::fresh();
    old.progress.helicopterUnlocked[2] = true;
    old.progress.missionUnlocked[5] = true;
    old.progress.insert("PLAIN", 123456, 3);
    const std::vector<u8> bytes = serializeProfile(old, "as2");
    const std::string text(bytes.begin(), bytes.end());
    CHECK(text.find("CHKP") == std::string::npos);
    Profile in = Rig::fresh();
    std::string why;
    REQUIRE_MESSAGE(deserializeProfile(bytes.data(), bytes.size(), in, &why, "as2"), why);
    CHECK(in.progress.helicopterUnlocked[2]);
    CHECK_FALSE(in.progress.helicopterUnlocked[1]); // a fresh install's unlocks only
    CHECK(in.progress.missionUnlocked[5]);
    int plain = 0;
    for (const HighScore& h : in.progress.scores) plain += h.name == "PLAIN" && h.score == 123456;
    CHECK(plain == 1);
    CHECK(in.progress.checkpoint.mission == -1);
    // With a checkpoint: written and read back.
    Profile cp = Rig::fresh();
    cp.progress.checkpoint.mission = 5;
    cp.progress.checkpoint.lives[0] = 4;
    cp.progress.checkpoint.score[1] = 777;
    cp.progress.checkpoint.rank[0] = 2.5f;
    const std::vector<u8> b2 = serializeProfile(cp, "as2");
    Profile back = Rig::fresh();
    REQUIRE(deserializeProfile(b2.data(), b2.size(), back, &why, "as2"));
    CHECK(back.progress.checkpoint.mission == 5);
    CHECK(back.progress.checkpoint.lives[0] == 4);
    CHECK(back.progress.checkpoint.score[1] == 777);
    CHECK(back.progress.checkpoint.rank[0] == doctest::Approx(2.5f));
    // The first game's file is byte for byte what it was (no checkpoint chunk).
    Profile first;
    const std::vector<u8> f = serializeProfile(first, "as3d");
    CHECK(std::string(f.begin(), f.end()).find("CHKP") == std::string::npos);
    Profile firstBack;
    REQUIRE(deserializeProfile(f.data(), f.size(), firstBack, &why, "as3d"));
    CHECK(firstBack.progress.helicopterUnlocked[1]);
}

TEST_CASE("as2 front end: texts present and absent") {
    // Absent: generic labels, no dialogue, the screens still work.
    Rig none;
    CHECK(none.fe.texts().get("button.start_game") == " Start Game ");
    none.startGame(0);
    CHECK_FALSE(none.fe.menuOpen()); // no start dialogue without the texts
    // Present: the texts file of the owner's executable, when it has been extracted.
    const GameData data = locateGameData(testdata::root(), as2());
    Blob b;
    if (data.textsFile.empty() || !readPlatformFile(data.textsFile, b)) {
        std::fprintf(stderr, "SKIPPED (no texts_as2.txt; run tools/extract_exe_texts.py --game as2): %s\n", __FILE__);
        return;
    }
    Texts t;
    CHECK(t.parse(std::string(reinterpret_cast<const char*>(b.data()), b.size())) > 250);
    CHECK(t.installed());
    CHECK(t.get("dialog.1.start.0").find('\n') != std::string::npos); // multi-line pages
    CHECK(t.get("ctl.row.2") != "-");                                   // the corrected row names
    Rig rig(t);
    rig.startGame(0);
    CHECK(rig.top() == Screen::Dialogue); // mission 1 has a start dialogue
}

// ---------------------------------------------------------------------------------------
// With the game's data
// ---------------------------------------------------------------------------------------
TEST_CASE("as2 front end: no texture of another game; headless renders in 4:3 and wide") {
    AS3D_REQUIRE_GLES();
    const GameData data = locateGameData(testdata::root(), as2());
    if (!data.present()) {
        std::fprintf(stderr, "SKIPPED (no data for as2): %s\n", __FILE__);
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
        REQUIRE_MESSAGE(assets.load(spied, &err, GameId::AirStrike2), err);
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
        CHECK(lit(img, 277, 230, 246, 30) > 3000 * s2); // Start Game's frame
        CHECK(lit(img, 140, 0, 500, 120) > 20000 * s2); // the title logo
        CHECK(lit(img, 20, 150, 200, 60) == 0);         // the level shows through
        for (Screen s : kSequelScreens) {
            if (s == Screen::Dialogue || s == Screen::MainMenu) continue;
            rig.fe.open(s);
            rig.wait(0.5f);
            render(rig.fe, img);
        }
        rig.fe.open(Screen::MainMenu);
        rig.startGame(0);
        REQUIRE(rig.top() == Screen::Dialogue);
        rig.wait(1.5f);
        render(rig.fe, img);
        CHECK(lit(img, 120, 420, 80, 120) > 5000 * s2); // the portrait
        render(rig.fe, img, true);                       // the loading comic
        CHECK(lit(img, 0, 135, 800, 330) > 150000 * s2);
        // Mission Complete: the statistics in one-player mode only (as2/frontend.md 3.8).
        long stats[2] = {0, 0};
        for (int players = 1; players <= 2; players++) {
            Rig r2({}, false, players == 2);
            r2.tapItem(1);
            r2.wait(0.3f);
            if (players == 2) r2.tapItem(4); // Game mode: Cooperative
            r2.tapItem(2);
            r2.wait(0.3f);
            r2.tapItem(1);
            r2.fe.onEndLevel(r2.report(100, 2));
            r2.wait(2.5f);
            render(r2.fe, img);
            // Orange text pixels in the statistics' rows.
            for (int y = static_cast<int>(map.toFbY(218)); y < static_cast<int>(map.toFbY(298)); y++)
                for (int x = static_cast<int>(map.toFbX(240)); x < static_cast<int>(map.toFbX(570)); x++) {
                    const u8* p = &img.rgba[(static_cast<size_t>(y) * img.width + x) * 4];
                    stats[players - 1] += p[0] > 150 && p[1] > 60 && p[2] < 60;
                }
        }
        CHECK(stats[0] > 500 * s2);
        CHECK(stats[1] == 0);
    }
    std::set<std::string> paths(all.begin(), all.end());
    for (const std::string& p : paths) {
        INFO("read: " << p);
        CHECK(vfs.exists(p)); // only files AirStrike 2 ships
        const bool mine = p.compare(0, 4, "gfx\\") == 0 || p.compare(0, 5, "menu\\") == 0;
        CHECK(mine);
        if (p.compare(0, 5, "menu\\") == 0) CHECK(p.find("cursor_") != std::string::npos); // no first-game menu picture
    }
    CHECK(paths.count("gfx\\ui\\interface.tga"));
    CHECK(paths.count("gfx\\logo\\logo.tga"));
}

TEST_CASE("as2 front end: the real game behind the menus: start, the dialogue holds, play, in-game menu, end, quit") {
    AS3D_REQUIRE_GLES();
    const GameData data = locateGameData(testdata::root(), as2());
    if (!data.present() || (!data.hasExtracted && data.paks.empty())) {
        std::fprintf(stderr, "SKIPPED (no data for as2): %s\n", __FILE__);
        return;
    }
    using namespace as3d_game;
    GameSession session;
    GameOptions o;
    o.dataRoot = testdata::root();
    o.game = &as2();
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
    tapItem(2);
    REQUIRE(fe.topScreen() == Screen::HeliSelect);
    for (int i = 0; i < 20; i++) step();
    target.bind();
    flow.draw(400, 300);
    CHECK(flow.modelViewsDrawn() == 1); // the helicopter preview
    tapItem(1);
    REQUIRE(fe.state() == FrontendState::Playing);
    CHECK(session.mission() == 1);
    CHECK_FALSE(session.world().intermission());
    const bool dialogue = !data.textsFile.empty();
    if (dialogue) {
        // The start dialogue holds the mission: the world does not advance under it.
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
        std::fprintf(stderr, "SKIPPED part (no texts_as2.txt: no dialogue): %s\n", __FILE__);
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
    // The end of the mission: the checkpoint, then (after the end dialogue) Mission Complete.
    session.world().endLevel();
    for (int i = 0; i < 5 && fe.state() == FrontendState::Playing && !fe.menuOpen(); i++) step();
    REQUIRE(fe.menuOpen());
    CHECK(flow.profile().progress.checkpoint.mission == 1);
    for (int i = 0; i < 80 && fe.topScreen() == Screen::Dialogue; i++) {
        for (int k = 0; k < 20; k++) step();
        step(UiInput().key(keys::Enter));
    }
    for (int i = 0; i < 40; i++) step();
    REQUIRE(fe.topScreen() == Screen::MissionComplete);
    CHECK(flow.profile().progress.missionUnlocked[1]);
    tapItem(1); // Quit
    CHECK(fe.topScreen() == Screen::MainMenu);
    CHECK(session.world().intermission());
}
