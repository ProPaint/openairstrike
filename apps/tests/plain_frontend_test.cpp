// The plain front end of the sequels (FrontendStyle::PlainList, docs/spec/as2/issues/260) with a
// fake game and the profiles of `as2` and `gulf`: the full walk (new game, mission complete,
// Next, game over, high score, the quit paths), mission and helicopter unlocking, a
// pointer-only walk in touch mode and a keyboard-only walk, that no texture of the first
// game is asked for, headless renders with pixel statistics, and a real mission start of
// AirStrike 2 behind the front end. The sequel is selected explicitly (gameProfile,
// locateGameData), so all of it also runs in the default `as3d` pass of tools/ci.sh; what
// needs the sequel's data or a GLES context skips loudly without it.
#include "doctest.h"

#include <algorithm>
#include <cstdio>
#include <memory>
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

const GameId kSequels[] = {GameId::AirStrike2, GameId::GulfThunder};

// AirStrike 2 has its own menus now (FrontendStyle::SequelMenus, apps/tests/as2_frontend_test.cpp);
// the plain front end is still tested with its data and rules through a copy of its profile.
const GameProfile& plainProfile(GameId id) {
    static GameProfile copies[kGameCount];
    static bool made[kGameCount] = {};
    const int i = static_cast<int>(id);
    if (!made[i]) {
        copies[i] = gameProfile(id);
        copies[i].frontend = FrontendStyle::PlainList;
        made[i] = true;
    }
    return copies[i];
}

// The fake game: records every call.
struct FakeGame : GameHost {
    std::vector<MissionStart> starts;
    int attracts = 0, saves = 0, quits = 0;
    bool paused = false;
    void startMission(const MissionStart& s) override { starts.push_back(s); paused = false; }
    void loadAttract() override { attracts++; }
    void setPaused(bool p) override { paused = p; }
    void clearPlayerActions() override {}
    void settingsChanged(const Settings&) override {}
    void saveProfile(const Profile&) override { saves++; }
    void quit() override { quits++; }
};

// Content of a sequel without its data: generic names, and the unlocks of as2 (docs/spec/as2/
// engine-behaviour.delta.md 7.6: entry n after mission 3n + 1 for n = 1..5).
FrontendContent fakeContent(const GameProfile& g) {
    FrontendContent c;
    c.game = &g;
    c.videoOptions = false;
    c.twoPlayerMode = false;
    c.screenOption = true; // as the game host offers them
    c.handOption = true;
    for (int i = 0; i < g.rules.missionCount; i++) c.missionNames[i] = "Test " + std::to_string(i + 1);
    for (int n = 1; n < g.rules.helicopterCount; n++) c.enableHelic[3 * n] = n;
    for (int h = 0; h < g.rules.helicopterCount; h++) c.heli[h] = {true, 100 + 50 * h, true, 1.0f + 0.25f * static_cast<float>(h)};
    return c;
}

struct Rig {
    const GameProfile& game;
    FakeGame host;
    Profile profile;
    Frontend fe;
    explicit Rig(GameId id, bool touch = false, FrontendContent content = {}, bool useContent = false)
        : game(plainProfile(id)),
          profile{Progress::defaults(game.rules, 1), Settings::defaults()},
          fe(host, profile, useContent ? std::move(content) : fakeContent(game), Texts{}) {
        fe.setTouchMode(touch);
        fe.menus().setPointer(-50, -50);
        fe.boot();
    }
    const GameRules& rules() const { return game.rules; }
    void tap(float x, float y) { fe.update(0.016f, UiInput().tap(x, y)); }
    void key(int k) { fe.update(0.016f, UiInput().key(k)); }
    void wait(float s) {
        for (float t = 0; t < s; t += 0.05f) fe.update(0.05f, {});
    }
    Screen top() const { return fe.topScreen(); }
    MenuItem* item(int id) {
        Menu* m = fe.menus().top();
        return m ? m->find(id) : nullptr;
    }
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
    // A tap on row `row` of the list item `id`.
    void tapListRow(int id, int row) {
        MenuItem* l = item(id);
        REQUIRE(l);
        tap(l->x + 30, l->y + 4 + 20.0f * static_cast<float>(row) + 8);
    }
    void startGame(int mission = 0) {
        REQUIRE(tapItem(1));
        REQUIRE(top() == Screen::StartGame);
        if (mission > 0) tapListRow(3, mission);
        REQUIRE(tapItem(2));
    }
    MissionReport report(double score, int lives, int weapon = 0) const {
        MissionReport r;
        r.players[0].score = score;
        r.players[0].lives = lives;
        r.hasUpgrades = true;
        r.weapon[0] = weapon;
        r.upgrades[0][weapon] = 3;
        r.upgrades[0][1] = 5;
        return r;
    }
};

// The sequel's data (extracted files or paks) on a Vfs, with a recording source on top that
// logs every path anything asks the file system for.
struct Recorder : IFileSource {
    std::vector<std::string>* log;
    explicit Recorder(std::vector<std::string>* l) : log(l) {}
    bool exists(const std::string& p) override { log->push_back(p); return false; }
    bool read(const std::string& p, Blob&) override { log->push_back(p); return false; }
    void list(std::vector<std::string>&) override {}
};

struct SequelData {
    GameData data;
    Vfs vfs;
    std::vector<std::string> requested;
    DefDatabase db;
    bool ok = false;
    explicit SequelData(GameId id) {
        data = locateGameData(testdata::root(), gameProfile(id));
        if (!data.present()) return;
        if (data.hasExtracted) {
            vfs.mount(makeDirSource(data.extractedDir));
        } else {
            for (const std::string& p : data.paks) {
                auto src = makePakSource(openFileStream(p));
                if (!src) return;
                vfs.mount(std::move(src));
            }
        }
        db.load(vfs);
        vfs.mount(std::make_unique<Recorder>(&requested));
        ok = true;
    }
    FrontendContent content() const {
        const GameProfile& g = plainProfile(data.game->id);
        FrontendContent c = fakeContent(g);
        for (int& e : c.enableHelic) e = -1;
        int i = 0;
        for (const LevelDef& d : db.levels()) {
            if (d.name.empty() || i >= g.rules.missionCount) continue;
            c.missionNames[i] = d.name;
            c.enableHelic[i] = d.enableHelic;
            i++;
        }
        for (int h = 0; h < g.rules.helicopterCount; h++) {
            c.heli[h] = {};
            if (const ObjectDef* o = db.findObject(g.rules.heliObjects[h])) c.heli[h] = {true, o->health, o->hasSpeed, o->speed};
        }
        return c;
    }
};

#define REQUIRE_SEQUEL_DATA(var, id)                                                              \
    SequelData var(id);                                                                           \
    if (!var.ok) {                                                                                \
        std::fprintf(stderr, "SKIPPED (no data for %s): %s\n", gameProfile(id).key, __FILE__);   \
        return;                                                                                   \
    }

} // namespace

TEST_CASE("plain front end: the sequels select it, the first game does not") {
    CHECK(gameProfile(GameId::AirStrike2).frontend == FrontendStyle::SequelMenus); // its own menus now (package E)
    CHECK(gameProfile(GameId::GulfThunder).frontend == FrontendStyle::PlainList);
    CHECK(gameProfile(GameId::AirStrike3D).frontend == FrontendStyle::V170Menus);
    Rig rig(GameId::AirStrike2);
    CHECK(rig.fe.plain());
    CHECK(rig.fe.menus().plain);
    CHECK_FALSE(rig.fe.bannerVisible()); // no 3D banner: the sequels have none
    FakeGame host;
    Profile p;
    Frontend first(host, p, FrontendContent{}, Texts{});
    CHECK_FALSE(first.plain());
    CHECK_FALSE(first.menus().plain);
    // A fresh save unlocks helicopter 0 only, and missions 1 and 2.
    const Progress fresh = Progress::defaults(gameProfile(GameId::AirStrike2).rules, 1);
    CHECK(fresh.helicopterUnlocked[0]);
    for (int i = 1; i < kMaxHelicopters; i++) CHECK_FALSE(fresh.helicopterUnlocked[i]);
    CHECK(fresh.missionUnlocked[0]);
    CHECK(fresh.missionUnlocked[1]);
    CHECK_FALSE(fresh.missionUnlocked[2]);
    // The first game's default is unchanged.
    CHECK(Progress::defaults().helicopterUnlocked[1]);
}

TEST_CASE("plain front end: main menu, exit confirmation, no Information") {
    for (GameId id : kSequels) {
        Rig rig(id);
        CHECK(rig.host.attracts == 1);
        REQUIRE(rig.top() == Screen::MainMenu);
        Menu* m = rig.fe.menus().top();
        int buttons = 0;
        for (const MenuItem& it : m->items) buttons += it.type == ItemType::Custom;
        CHECK(buttons == 4); // Start Game, Top Scores, Options, Exit
        // Esc and right click do nothing here.
        rig.key(keys::Escape);
        rig.fe.update(0, UiInput().press(keys::Mouse2));
        CHECK(rig.fe.menus().depth() == 1);
        // Top Scores and back; Options and back.
        REQUIRE(rig.tapItem(2));
        CHECK(rig.top() == Screen::TopScores);
        REQUIRE(rig.tapItem(1));
        REQUIRE(rig.tapItem(3));
        CHECK(rig.top() == Screen::Options);
        REQUIRE(rig.tapItem(1));
        CHECK(rig.top() == Screen::MainMenu);
        // Exit: No returns, Yes quits and saves.
        REQUIRE(rig.tapItem(4));
        CHECK(rig.top() == Screen::Exit);
        REQUIRE(rig.tapItem(2));
        CHECK(rig.top() == Screen::MainMenu);
        REQUIRE(rig.tapItem(4));
        REQUIRE(rig.tapItem(1));
        CHECK(rig.host.quits == 1);
        CHECK(rig.host.saves >= 1);
        // Information is never opened.
        rig.fe.open(Screen::Information);
        CHECK(rig.top() != Screen::Information);
    }
}

TEST_CASE("plain front end: Start Game lists the game's missions, difficulties and helicopters") {
    for (GameId id : kSequels) {
        Rig rig(id);
        const GameRules& r = rig.rules();
        REQUIRE(rig.tapItem(1));
        REQUIRE(rig.top() == Screen::StartGame);
        MenuItem* list = rig.item(3);
        REQUIRE(list);
        REQUIRE(static_cast<int>(list->entries.size()) == r.missionCount);
        for (int i = 0; i < r.missionCount; i++) {
            const ListEntry& e = list->entries[static_cast<size_t>(i)];
            CHECK(e.enabled == (i < 2)); // missions 1 and 2 only on a fresh save
            const bool bonus = std::find(r.bonusMissions, r.bonusMissions + kMaxSpecialMissions, i + 1) !=
                               r.bonusMissions + kMaxSpecialMissions;
            const bool boss = std::find(r.bossMissions, r.bossMissions + kMaxSpecialMissions, i + 1) !=
                              r.bossMissions + kMaxSpecialMissions;
            CHECK((e.text.find("[Bonus]") != std::string::npos) == bonus);
            CHECK((e.text.find("[Boss]") != std::string::npos) == boss);
            CHECK(e.text.find("Test " + std::to_string(i + 1)) != std::string::npos);
        }
        MenuItem* diff = rig.item(4);
        REQUIRE(diff);
        CHECK(static_cast<int>(diff->values.size()) == r.difficultyCount);
        CHECK(diff->index == r.defaultDifficulty);
        int rows = 0;
        for (int h = 0; h < r.helicopterCount; h++) {
            MenuItem* row = rig.item(100 + h);
            REQUIRE(row);
            rows++;
            CHECK(row->disabled() == (h != 0)); // locked ones are greyed and cannot be chosen
        }
        CHECK(rows == r.helicopterCount);
        CHECK(rig.item(100 + r.helicopterCount) == nullptr);
        CHECK(rig.item(5) == nullptr); // no game mode: single player only
        CHECK(rig.item(1));
        CHECK(rig.item(2));
    }
    // AirStrike 2 in particular: 18 missions, bonus 7 and 13, boss 6, 12 and 18, 6 helicopters.
    Rig as2(GameId::AirStrike2);
    REQUIRE(as2.tapItem(1));
    CHECK(as2.item(3)->entries.size() == 18);
    CHECK(as2.item(3)->entries[6].text.find("[Bonus]") != std::string::npos);
    CHECK(as2.item(3)->entries[17].text.find("[Boss]") != std::string::npos);
    CHECK(as2.item(105));
    CHECK_FALSE(as2.item(106));
}

TEST_CASE("plain front end: the full walk, new game to game over, high score and the quit paths") {
    for (GameId id : kSequels) {
        Rig rig(id);
        const GameRules& r = rig.rules();
        rig.startGame();
        REQUIRE(rig.host.starts.size() == 1);
        MissionStart s = rig.host.starts.back();
        CHECK(s.mission == 0);
        CHECK(s.difficulty == r.defaultDifficulty);
        CHECK(s.players == 1);
        CHECK(s.helicopter[0] == 0);
        CHECK(s.lives[0] == r.startLives);
        CHECK_FALSE(s.carryUpgrades);
        CHECK_FALSE(s.restart);
        CHECK(rig.fe.state() == FrontendState::Playing);
        CHECK(rig.fe.hudVisible());
        CHECK_FALSE(rig.fe.menuOpen());

        // P pauses; Esc: the in-game menu (Resume, Options, Quit).
        rig.key('P');
        CHECK(rig.fe.paused());
        rig.key('P');
        CHECK_FALSE(rig.fe.paused());
        rig.key(keys::Escape);
        REQUIRE(rig.top() == Screen::InGame);
        CHECK(rig.host.paused);
        CHECK_FALSE(rig.fe.hudVisible());
        REQUIRE(rig.tapItem(2));
        CHECK(rig.top() == Screen::Options);
        REQUIRE(rig.tapItem(1));
        CHECK(rig.top() == Screen::InGame);
        rig.key(keys::Escape); // Esc resumes
        CHECK_FALSE(rig.fe.menuOpen());
        CHECK_FALSE(rig.host.paused);
        rig.key(keys::Escape);
        REQUIRE(rig.tapItem(1)); // Resume
        CHECK_FALSE(rig.fe.menuOpen());

        // A tutorial hint through the hint box: OK button after the opening animation.
        rig.fe.showTutorialHint("Press {X}^to fire");
        REQUIRE(rig.top() == Screen::Hint);
        CHECK(rig.host.paused);
        CHECK(rig.fe.hudVisible());
        rig.wait(0.5f);
        REQUIRE(rig.tapItem(1));
        CHECK_FALSE(rig.fe.menuOpen());
        CHECK_FALSE(rig.host.paused);

        // Mission complete: statistics, Next banks and starts mission 2 with the upgrades.
        rig.fe.onEndLevel(rig.report(7000, 2, 2));
        REQUIRE(rig.top() == Screen::MissionComplete);
        CHECK(rig.host.paused);
        CHECK_FALSE(rig.fe.hudVisible());
        CHECK(rig.profile.progress.missionUnlocked[2] == false);
        CHECK(rig.profile.progress.missionUnlocked[1]);
        rig.wait(2.5f);
        REQUIRE(rig.tapItem(3));
        REQUIRE(rig.host.starts.size() == 2);
        s = rig.host.starts.back();
        CHECK(s.mission == 1);
        CHECK(s.banked[0] == 7000);
        CHECK(s.lives[0] == 2);
        CHECK(s.carryUpgrades == r.upgradesCarryToNextMission);
        if (s.carryUpgrades) {
            CHECK(s.weapon[0] == 2);
            CHECK(s.upgrades[0][2] == 3);
            CHECK(s.upgrades[0][1] == 5);
        }

        // Mission complete, Restart: the same mission, no banking, no carried upgrades.
        rig.fe.onEndLevel(rig.report(1000, 2));
        rig.wait(2.5f);
        REQUIRE(rig.tapItem(2));
        s = rig.host.starts.back();
        CHECK(rig.host.starts.size() == 3);
        CHECK(s.mission == 1);
        CHECK(s.restart);
        CHECK_FALSE(s.carryUpgrades);
        CHECK(s.banked[0] == 7000);

        // Game over: the buttons appear after 2 s; Restart keeps the mission and the lives.
        rig.fe.onGameOver(rig.report(400, -1));
        REQUIRE(rig.top() == Screen::GameOver);
        CHECK_FALSE(rig.tapItem(1)); // hidden at first
        rig.wait(2.5f);
        REQUIRE(rig.tapItem(1));
        CHECK(rig.host.starts.size() == 4);
        CHECK(rig.host.starts.back().mission == 1);
        CHECK(rig.host.starts.back().restart);
        CHECK(rig.host.starts.back().lives[0] == 2);

        // Game over, Quit: banks, and a qualifying score opens the name entry.
        rig.fe.onGameOver(rig.report(2500000, -1));
        rig.wait(2.5f);
        const int attractsBefore = rig.host.attracts;
        REQUIRE(rig.tapItem(2));
        CHECK(rig.host.attracts == attractsBefore + 1);
        REQUIRE(rig.top() == Screen::NameEntry);
        rig.fe.update(0.1f, UiInput().text("Ace"));
        REQUIRE(rig.tapItem(1)); // OK
        REQUIRE(rig.top() == Screen::TopScores);
        CHECK(rig.profile.progress.scores[0].name == "Ace");
        CHECK(rig.profile.progress.scores[0].score == 2507000);
        REQUIRE(rig.tapItem(1));
        CHECK(rig.top() == Screen::MainMenu);

        // In-game menu Quit: no banking, no high-score check.
        rig.startGame();
        rig.key(keys::Escape);
        REQUIRE(rig.tapItem(3));
        CHECK(rig.top() == Screen::MainMenu);
        CHECK(rig.fe.state() == FrontendState::Attract);
        CHECK_FALSE(rig.fe.campaign().active);

        // Mission complete, Quit: nothing banked, the unlock stays.
        rig.startGame();
        rig.fe.onEndLevel(rig.report(3000000, 2));
        rig.wait(2.5f);
        REQUIRE(rig.tapItem(1));
        CHECK(rig.top() == Screen::MainMenu);
        CHECK(rig.profile.progress.scores[0].name == "Ace");
    }
}

TEST_CASE("plain front end: missions and helicopters unlock as the spec says") {
    Rig rig(GameId::AirStrike2);
    const GameRules& r = rig.rules();
    REQUIRE(r.missionCount == 18);
    REQUIRE(r.helicopterCount == 6);
    rig.startGame();
    for (int m = 0; m < r.missionCount; m++) {
        REQUIRE(rig.host.starts.back().mission == m);
        rig.fe.onEndLevel(rig.report(100, 2));
        const int next = (m + 1) % r.missionCount;
        CHECK(rig.profile.progress.missionUnlocked[next]);
        // Entry n opens after missions 4, 7, 10, 13 and 16 (indices 3, 6, 9, 12, 15), in table order.
        int expected = 1;
        for (int k = 3; k <= m; k += 3) expected++;
        int unlocked = 0;
        for (int h = 0; h < r.helicopterCount; h++) {
            if (rig.profile.progress.helicopterUnlocked[h]) unlocked++;
            CHECK(rig.profile.progress.helicopterUnlocked[h] == (h < expected));
        }
        CHECK(unlocked == expected);
        if (m == r.missionCount - 1) {
            CHECK(rig.top() == Screen::GameComplete);
            break;
        }
        REQUIRE(rig.top() == Screen::MissionComplete);
        // The helicopter rows of Mission Complete follow the unlocks.
        for (int h = 0; h < r.helicopterCount; h++) {
            MenuItem* row = rig.item(100 + h);
            REQUIRE(row);
            CHECK(row->disabled() == (h >= expected));
        }
        rig.wait(2.5f);
        REQUIRE(rig.tapItem(3));
    }
    // Game complete: Continue banks and returns to the main menu (no qualifying score here).
    REQUIRE(rig.tapItem(1));
    CHECK(rig.top() == Screen::MainMenu);
    // Start Game now offers everything, and a chosen helicopter reaches the mission start.
    REQUIRE(rig.tapItem(1));
    for (int i = 0; i < r.missionCount; i++) CHECK(rig.item(3)->entries[static_cast<size_t>(i)].enabled);
    REQUIRE(rig.tapItem(100 + 4));
    REQUIRE(rig.tapItem(2));
    CHECK(rig.host.starts.back().helicopter[0] == 4);
    // Mission Complete: a helicopter chosen there is the one the next mission starts with.
    rig.fe.onEndLevel(rig.report(1, 2));
    rig.wait(2.5f);
    REQUIRE(rig.tapItem(100 + 2));
    REQUIRE(rig.tapItem(3));
    CHECK(rig.host.starts.back().helicopter[0] == 2);
    // A locked row cannot be chosen.
    Rig fresh(GameId::AirStrike2);
    REQUIRE(fresh.tapItem(1));
    CHECK_FALSE(fresh.tapItem(103));
    CHECK(fresh.fe.helicopters()[0] == 0);
}

TEST_CASE("plain front end: unlocks with the levels.txt of the game") {
    for (GameId id : kSequels) {
        REQUIRE_SEQUEL_DATA(sd, id);
        const FrontendContent c = sd.content();
        const GameRules& r = gameProfile(id).rules;
        int named = 0;
        for (int i = 0; i < r.missionCount; i++) named += !c.missionNames[i].empty();
        CHECK(named == r.missionCount);
        if (id == GameId::AirStrike2) {
            // enableHelic n at missions 4, 7, 10, 13, 16 (1 to 5, one each).
            for (int i = 0; i < r.missionCount; i++) {
                const bool at = i == 3 || i == 6 || i == 9 || i == 12 || i == 15;
                CHECK((c.enableHelic[i] >= 0) == at);
                if (at) CHECK(c.enableHelic[i] == (i + 1) / 3 - 0);
            }
        }
        for (int h = 0; h < r.helicopterCount; h++) {
            CHECK(c.heli[h].known);
            CHECK(c.heli[h].health > 0);
        }
        Rig rig(id, false, c, true);
        rig.startGame();
        for (int m = 0; m < r.missionCount - 1; m++) {
            rig.fe.onEndLevel(rig.report(100, 2));
            const int e = c.enableHelic[m];
            if (e >= 0 && e < r.helicopterCount) CHECK(rig.profile.progress.helicopterUnlocked[e]);
            rig.wait(2.5f);
            REQUIRE(rig.tapItem(3));
        }
        int unlocked = 0;
        for (int h = 0; h < r.helicopterCount; h++) unlocked += rig.profile.progress.helicopterUnlocked[h];
        CHECK(unlocked >= 1);
    }
}

TEST_CASE("plain front end: operable by pointer only in touch mode") {
    for (GameId id : kSequels) {
        Rig rig(id, true);
        rig.fe.menus().setPointer(-50, -50);
        // Main menu -> Start Game: pick mission 2, difficulty by tapping the spinner, Start.
        REQUIRE(rig.tapItem(1));
        REQUIRE(rig.top() == Screen::StartGame);
        rig.tapListRow(3, 1);
        CHECK(rig.item(3)->selected == 1);
        rig.tapListRow(3, 5); // locked: the selection stays
        CHECK(rig.item(3)->selected == 1);
        MenuItem* diff = rig.item(4);
        const int before = diff->index;
        rig.tap(diff->hit.x + diff->hit.w - 4, diff->hit.y + 8); // right half: next value
        CHECK(rig.item(4)->index == (before + 1) % static_cast<int>(diff->values.size()));
        REQUIRE(rig.tapItem(100));
        REQUIRE(rig.tapItem(2));
        REQUIRE(rig.host.starts.size() == 1);
        CHECK(rig.host.starts[0].mission == 1);
        CHECK(rig.host.starts[0].difficulty == (before + 1) % rig.rules().difficultyCount);
        // MENU button of the touch play controls opens the in-game menu; Options and back; Resume.
        rig.tap(400, 17);
        REQUIRE(rig.top() == Screen::InGame);
        REQUIRE(rig.tapItem(2));
        REQUIRE(rig.top() == Screen::Options);
        // The touch-only rows: Screen, Controls (hand), Touch speed, Show FPS.
        CHECK(rig.item(41)); // hand
        CHECK(rig.item(42)); // touch speed
        CHECK(rig.item(43)); // show FPS
        REQUIRE(rig.tapItem(2)); // Configure Controls
        REQUIRE(rig.top() == Screen::Controls);
        REQUIRE(rig.tapItem(1));
        REQUIRE(rig.tapItem(1));
        REQUIRE(rig.top() == Screen::InGame);
        REQUIRE(rig.tapItem(1));
        CHECK_FALSE(rig.fe.menuOpen());
        // Hint box, then mission complete, Next, game over, Restart, Quit, name entry by the
        // on-screen keyboard, Top Scores, back.
        rig.fe.showTutorialHint("Tap the button");
        rig.wait(0.5f);
        REQUIRE(rig.tapItem(1));
        rig.fe.onEndLevel(rig.report(9000, 2));
        rig.wait(2.5f);
        REQUIRE(rig.tapItem(3));
        rig.fe.onGameOver(rig.report(3000000, -1));
        rig.wait(2.5f);
        REQUIRE(rig.tapItem(1));
        rig.fe.onGameOver(rig.report(3000000, -1));
        rig.wait(2.5f);
        REQUIRE(rig.tapItem(2));
        REQUIRE(rig.top() == Screen::NameEntry);
        REQUIRE(rig.tapItem(200 + 'Z'));
        REQUIRE(rig.tapItem(200 + 'E'));
        REQUIRE(rig.tapItem(1));
        REQUIRE(rig.top() == Screen::TopScores);
        CHECK(rig.profile.progress.scores[0].name == "ZE");
        REQUIRE(rig.tapItem(1));
        // Exit by taps.
        REQUIRE(rig.tapItem(4));
        REQUIRE(rig.tapItem(1));
        CHECK(rig.host.quits == 1);
    }
}

TEST_CASE("plain front end: operable by keyboard on the desktop") {
    Rig rig(GameId::AirStrike2);
    // Down focuses the first item, Enter activates it.
    rig.key(keys::Down);
    rig.key(keys::Enter);
    REQUIRE(rig.top() == Screen::StartGame);
    // Down on the mission list moves the selection (mission 2 is the last unlocked); Enter starts it.
    rig.key(keys::Down);
    rig.key(keys::Down);
    CHECK(rig.item(3)->selected == 1);
    rig.key(keys::Enter);
    REQUIRE(rig.host.starts.size() == 1);
    CHECK(rig.host.starts[0].mission == 1);
    // Esc opens the in-game menu; Up and Down move over the buttons, Enter on Quit leaves.
    rig.key(keys::Escape);
    REQUIRE(rig.top() == Screen::InGame);
    rig.key(keys::Down);
    rig.key(keys::Down);
    rig.key(keys::Down);
    rig.key(keys::Enter);
    CHECK(rig.top() == Screen::MainMenu);
    // Esc leaves Start Game, Options.
    rig.key(keys::Down);
    rig.key(keys::Enter);
    REQUIRE(rig.top() == Screen::StartGame);
    rig.key(keys::Escape);
    CHECK(rig.top() == Screen::MainMenu);
    rig.tapItem(3);
    REQUIRE(rig.top() == Screen::Options);
    rig.key(keys::Escape);
    CHECK(rig.top() == Screen::MainMenu);
    // Helicopter rows are reachable with Tab and chosen with Enter once unlocked.
    rig.fe.debugSet("unlock", "1");
    REQUIRE(rig.tapItem(1));
    rig.fe.menus().setPointer(-50, -50);
    Menu* m = rig.fe.menus().top();
    int guard = 0;
    while ((m->focused < 0 || m->items[static_cast<size_t>(m->focused)].id != 102) && guard++ < 20) rig.key(keys::Tab);
    REQUIRE(guard < 20);
    rig.key(keys::Enter);
    CHECK(rig.fe.helicopters()[0] == 2);
}

TEST_CASE("plain front end: the first game's front end is unchanged") {
    FakeGame host;
    Profile p;
    Frontend fe(host, p, FrontendContent{}, Texts{});
    fe.boot();
    CHECK(fe.bannerVisible());
    CHECK(fe.helicopters()[0] == 1);
    Menu* m = fe.menus().top();
    REQUIRE(m);
    int pictures = 0;
    for (const MenuItem& it : m->items) pictures += it.type == ItemType::Button;
    CHECK(pictures == 5);
    fe.open(Screen::StartGame);
    CHECK(fe.menus().top()->find(6)); // the helicopter grid
    CHECK(fe.menus().top()->find(5)); // game mode
}

TEST_CASE("plain front end: every screen builds and draws without data") {
    for (GameId id : kSequels)
        for (bool touch : {false, true}) {
            Rig rig(id, touch);
            UiAssets none;
            Renderer2D r;
            for (Screen s : kAllScreens) {
                if (s == Screen::Information) continue;
                rig.fe.open(s);
                rig.fe.update(2.5f, {});
                r.begin(800, 600);
                rig.fe.draw(r, none);
                CHECK(r.dropped() == 0);
                CHECK_FALSE(r.quads().empty());
            }
        }
}

namespace {

// Renders the plain screens of `id` at `w` x `h` with the sequel's real assets, and returns the
// paths of every texture the front end asked for.
struct RenderRig {
    SequelData& sd;
    UiAssets assets;
    Renderer2D r;
    RenderTarget target;
    int w, h;
    bool ok = false;
    RenderRig(SequelData& s, int width, int height) : sd(s), w(width), h(height) {
        std::string err;
        if (!assets.load(sd.vfs, &err)) return;
        if (!r.init(&err)) return;
        if (!target.create(w, h, 0)) return;
        ok = true;
    }
    void render(Frontend& fe, Image& img, bool loading = false, float progress = 0.5f) {
        target.bind();
        clear({40 / 255.0f, 60 / 255.0f, 80 / 255.0f, 1}, true);
        r.begin(w, h);
        if (loading) {
            drawLoadingScreen(r, assets, progress, false);
        } else {
            fe.draw(r, assets);
        }
        CHECK(r.dropped() == 0);
        r.flush();
        REQUIRE(target.readPixels(img));
    }
};

Rig makeRig(SequelData& sd, GameId id, bool touch) { return Rig(id, touch, sd.content(), true); }

} // namespace

TEST_CASE("plain front end: no texture of the first game is requested") {
    AS3D_REQUIRE_GLES();
    for (GameId id : kSequels) {
        REQUIRE_SEQUEL_DATA(sd, id);
        RenderRig rr(sd, 800, 600);
        REQUIRE(rr.ok);
        sd.requested.clear(); // the assets' own load (font, HUD sheets, cursors) is not the front end's
        for (bool touch : {false, true}) {
            Rig rig = makeRig(sd, id, touch);
            Image img;
            for (Screen s : kAllScreens) {
                if (s == Screen::Information) continue;
                rig.fe.open(s);
                rig.fe.update(2.5f, {});
                rr.render(rig.fe, img);
            }
            rig.fe.open(Screen::MainMenu);
            rr.render(rig.fe, img);
            rr.render(rig.fe, img, true); // the loading screen
        }
        std::set<std::string> paths(sd.requested.begin(), sd.requested.end());
        for (const std::string& p : paths) {
            INFO("requested: " << p);
            CHECK(sd.vfs.exists(p)); // only files the sequel ships (the recorder answers false)
            CHECK(p.compare(0, 5, "menu\\") != 0);
        }
        CHECK(paths.empty()); // ... and the plain screens need none at all
    }
}

TEST_CASE("plain front end: screens render in the right places, 4:3 and wide") {
    AS3D_REQUIRE_GLES();
    REQUIRE_SEQUEL_DATA(sd, GameId::AirStrike2);
    const u8 bg[3] = {40, 60, 80};
    const u8 black[3] = {0, 0, 0};
    for (int mode = 0; mode < 2; mode++) {
        const int W = mode == 0 ? 800 : 2400, H = mode == 0 ? 600 : 1080;
        RenderRig rr(sd, W, H);
        REQUIRE(rr.ok);
        // Virtual rectangle -> pixels of this mapping.
        const Mapping map = computeMapping(W, H);
        auto lit = [&](const Image& img, const u8* ref, float x, float y, float w, float h) {
            return uitest::litIn(img, ref, static_cast<int>(map.toFbX(x)), static_cast<int>(map.toFbY(y)),
                                 static_cast<int>(map.toFbX(x + w)), static_cast<int>(map.toFbY(y + h)));
        };
        const long scale2 = static_cast<long>(map.scaleX * map.scaleY);
        Image img;
        {
            Rig rig = makeRig(sd, GameId::AirStrike2, false);
            rr.render(rig.fe, img);
            CHECK(lit(img, black, 240, 238, 320, 210) * 10 > 2000 * scale2);  // four buttons
            CHECK(lit(img, black, 200, 20, 400, 60) > 200 * scale2 / 4);       // the title
            CHECK(lit(img, bg, 20, 150, 200, 300) == 0);                       // the level shows through
            // The bars run to the edges of a wide framebuffer.
            const u8* p = &img.rgba[(static_cast<size_t>(20) * W + 2) * 4];
            CHECK(p[0] + p[1] + p[2] < 30);
            const u8* q = &img.rgba[(static_cast<size_t>(H - 10) * W + W - 3) * 4];
            CHECK(q[0] + q[1] + q[2] < 30);
        }
        {
            Rig rig = makeRig(sd, GameId::AirStrike2, false);
            rig.tapItem(1);
            rig.fe.menus().setPointer(-50, -50);
            rr.render(rig.fe, img);
            CHECK(lit(img, bg, 40, 134, 400, 324) * 10 > 3000 * scale2);   // mission list
            CHECK(lit(img, bg, 470, 134, 290, 180) * 10 > 1500 * scale2);  // helicopter rows
            CHECK(lit(img, black, 40, 526, 150, 40) * 10 > 400 * scale2);  // Back
            CHECK(lit(img, black, 610, 526, 150, 40) * 10 > 400 * scale2); // Start
            CHECK(lit(img, bg, 0, 0, 1, 1) < 4);                            // no stray fill over the level's corner
        }
        {
            Rig rig = makeRig(sd, GameId::AirStrike2, true);
            rig.fe.open(Screen::MissionComplete);
            rig.wait(3.0f);
            rr.render(rig.fe, img);
            CHECK(lit(img, bg, 210, 120, 380, 120) * 10 > 2500 * scale2);  // statistics box
            CHECK(lit(img, bg, 210, 320, 380, 150) * 10 > 1500 * scale2);  // helicopter list
        }
        {
            Rig rig = makeRig(sd, GameId::AirStrike2, false);
            rig.startGame();
            rig.fe.onGameOver(rig.report(0, -1));
            rig.wait(3.0f);
            rr.render(rig.fe, img);
            // The whole scene is tinted red and the title box is dark.
            const u8* p = &img.rgba[(static_cast<size_t>(map.toFbY(100)) * W + static_cast<size_t>(map.toFbX(100))) * 4];
            CHECK(p[0] > 46);
            CHECK(p[1] < 40);
            CHECK(lit(img, bg, 150, 190, 500, 90) * 10 > 8000 * scale2);
            CHECK(lit(img, black, 240, 316, 320, 40) * 10 > 600 * scale2); // Restart and Quit
        }
        {
            Rig rig = makeRig(sd, GameId::AirStrike2, false);
            rr.render(rig.fe, img, true);
            CHECK(uitest::litIn(img, black, static_cast<int>(map.toFbX(250)), static_cast<int>(map.toFbY(330)),
                                static_cast<int>(map.toFbX(550)), static_cast<int>(map.toFbY(344))) > 100);
            CHECK(lit(img, black, 300, 230, 200, 40) > 100);
        }
    }
}

TEST_CASE("plain front end: a real AirStrike 2 mission starts from the plain menus and quits back") {
    AS3D_REQUIRE_GLES();
    REQUIRE_SEQUEL_DATA(sd, GameId::AirStrike2);
    if (!sd.data.hasExtracted && sd.data.paks.empty()) return;
    using namespace as3d_game;
    GameSession session;
    GameOptions o;
    o.dataRoot = testdata::root();
    o.game = &plainProfile(GameId::AirStrike2);
    o.startLevel = false;
    o.levelFlow = false;
    std::string err;
    REQUIRE_MESSAGE(session.init(o, &err), err);
    AudioBridge audio;
    audio.init(session.vfs(), true);
    GameFlow flow(session, audio);
    FlowConfig c;
    c.twoPlayerMode = true; // the desktop default; the plain front end ignores it
    c.attract = 1;
    c.showLogo = false;
    REQUIRE_MESSAGE(flow.init(c, &err), err);
    flow.boot();
    Frontend& fe = flow.frontend();
    REQUIRE(fe.plain());
    CHECK(session.hasLevel()); // the attract level behind the menus
    CHECK(session.world().intermission());
    auto step = [&](const UiInput& in = {}) {
        flow.uiFrame(1.0f / 60.0f, in);
        flow.step({});
    };
    auto tapItem = [&](int id) {
        Menu* m = fe.menus().top();
        REQUIRE(m);
        for (const MenuItem& it : m->items)
            if (it.id == id && !it.disabled() && !it.hidden()) {
                step(UiInput().tap(it.hit.x + it.hit.w * 0.5f, it.hit.y + it.hit.h * 0.5f));
                return;
            }
        FAIL("no item " << id);
    };
    tapItem(1);
    REQUIRE(fe.topScreen() == Screen::StartGame);
    tapItem(2);
    REQUIRE(fe.state() == FrontendState::Playing);
    CHECK_FALSE(fe.menuOpen());
    CHECK_FALSE(session.world().intermission());
    CHECK(session.mission() == 1);
    CHECK(fe.hudVisible());
    CHECK(session.world().player(0).heli == 0);
    for (int i = 0; i < 120; i++) step();
    step(UiInput().key(keys::Escape));
    REQUIRE(fe.topScreen() == Screen::InGame);
    CHECK(session.world().paused());
    tapItem(3);
    CHECK(fe.topScreen() == Screen::MainMenu);
    CHECK(session.world().intermission());
}
