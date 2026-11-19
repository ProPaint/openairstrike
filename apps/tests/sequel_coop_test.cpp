// The sequels' co-operative mode (AirStrike 2 and Gulf Thunder, FrontendStyle::SequelMenus):
// Start Game's "Game mode" spinner (as2/frontend.md 3.4), the helicopter selection's "Player:"
// spinner and its Start rule (3.18), both players' records through the campaign: G_NewGame's
// lives and loadout for both (5.1), the checkpoint and Next (5.3, 5.4), Mission Complete without
// statistics, game over only when both are out and no high-score check (5.9); then the real game
// behind the menus with two helicopters, the two-player HUD, Restart and Next; and the host's
// gate: the desktop offers Cooperative, a host that offers one player (FlowConfig::twoPlayerMode
// false) does not. Gulf Thunder runs the same code with its skin (gulf/frontend.delta.md 3.4:
// "changed (look only)"). What needs a game's data or a GLES context skips loudly without it.
#include "doctest.h"

#include <cstdio>
#include <string>
#include <vector>

#include "../game/game_flow.h"
#include "../game/game_view.h"
#include "as3d/defs.h"
#include "as3d/frontend.h"
#include "as3d/game_data.h"
#include "ui_test_util.h"

using namespace as3d;
using namespace as3d::ui;

namespace {

enum StartId { kNext = 2, kGameMode = 4, kMissionList = 5 };
enum HeliId { kHeliStart = 1, kPlayerSpinner = 5, kArrowNext = 6, kArrowPrev = 7, kAccept = 8 };

struct FakeGame : GameHost {
    std::vector<MissionStart> starts;
    int clears = 0, saves = 0;
    bool paused = false;
    void startMission(const MissionStart& s) override { starts.push_back(s); paused = false; }
    void loadAttract() override {}
    void setPaused(bool p) override { paused = p; }
    void clearPlayerActions() override { clears++; }
    void settingsChanged(const Settings&) override {}
    void saveProfile(const Profile&) override { saves++; }
    void quit() override {}
    void drawModel(const ModelView&) override {}
    void playMusic(const std::string&) override {}
};

FrontendContent content(const GameProfile& g, bool twoPlayers) {
    FrontendContent c;
    c.game = &g;
    c.videoOptions = false;
    c.twoPlayerMode = twoPlayers;
    for (int i = 0; i < g.rules.missionCount; i++) c.missionNames[i] = "Mission " + std::to_string(i + 1);
    for (int h = 0; h < g.rules.helicopterCount; h++) c.heli[h] = {true, 300 + 100 * h, true, 1.0f};
    c.enableHelic[3] = 1;
    return c;
}

struct Rig {
    FakeGame host;
    Profile profile;
    Frontend fe;
    Rig(const GameProfile& g, bool twoPlayers) : profile(fresh(g)), fe(host, profile, content(g, twoPlayers), Texts{}) {
        profile.settings.showLogo = false;
        fe.menus().setPointer(-50, -50);
        fe.boot();
    }
    static Profile fresh(const GameProfile& g) {
        Profile p;
        p.progress = Progress::defaults(g.rules, Progress::defaultHelicopters(&g));
        p.progress.helicopterUnlocked[1] = true; // a second helicopter to choose
        return p;
    }
    void frame(const UiInput& in = {}) { fe.update(1.0f / 60.0f, in); }
    void wait(float s) {
        for (float t = 0; t < s; t += 1.0f / 60.0f) frame();
    }
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
    MissionReport report(double score, int lives1, int lives2, bool complete) const {
        MissionReport r;
        r.players[0] = {score, lives1, 3, 5};
        r.players[1] = {score / 2, lives2, 1, 1};
        r.totals = {10, 1000, 20};
        r.hasUpgrades = true;
        r.upgrades[0][0] = 4;
        r.upgrades[1][2] = 3;
        r.weapon[0] = 0;
        r.weapon[1] = 2;
        if (complete) {
            r.hasCheckpoint = true;
            r.checkpointMission = fe.campaign().mission + 1;
            r.checkpointLives[0] = lives1;
            r.checkpointLives[1] = lives2;
            r.checkpointScore[0] = fe.campaign().p[0].banked + static_cast<std::int64_t>(score);
            r.checkpointScore[1] = fe.campaign().p[1].banked + static_cast<std::int64_t>(score / 2);
            for (int p = 0; p < 2; p++) r.checkpointRank[p] = 0.8f;
        }
        return r;
    }
};

void menusTwoPlayers(const GameProfile& g) {
    INFO(g.key);
    SUBCASE("a host with one player: Start Game offers Single Player only, no Player spinner") {
        Rig rig(g, false);
        rig.tapItem(1);
        REQUIRE(rig.top() == Screen::StartGame);
        REQUIRE(rig.item(kGameMode));
        CHECK(rig.item(kGameMode)->values.size() == 1);
        rig.tapItem(kNext);
        REQUIRE(rig.top() == Screen::HeliSelect);
        CHECK_FALSE(rig.item(kPlayerSpinner));
        rig.tapItem(kHeliStart);
        REQUIRE(rig.host.starts.size() == 1);
        CHECK(rig.host.starts.back().players == 1);
    }
    SUBCASE("Cooperative: both helicopters, both records, the checkpoint, Next, no statistics, game over") {
        Rig rig(g, true);
        rig.tapItem(1);
        REQUIRE(rig.top() == Screen::StartGame);
        rig.wait(0.3f);
        REQUIRE(rig.item(kGameMode)->values.size() == 2);
        rig.tapItem(kGameMode); // "Cooperative", the flag at once
        CHECK(rig.item(kGameMode)->index == 1);
        CHECK(rig.fe.twoPlayers());
        rig.tapItem(kNext);
        REQUIRE(rig.top() == Screen::HeliSelect);
        rig.wait(0.3f);
        // Player 1 first; the arrows write the shown player's choice.
        REQUIRE(rig.item(kPlayerSpinner));
        CHECK(rig.item(kPlayerSpinner)->index == 0);
        rig.tapItem(kArrowNext);
        CHECK(rig.fe.helicopters()[0] == 1);
        CHECK(rig.fe.helicopters()[1] == 0);
        rig.tapItem(kPlayerSpinner); // Player 2
        rig.tapItem(kArrowNext);
        rig.tapItem(kArrowNext); // helicopter 2: locked
        CHECK(rig.fe.helicopters()[1] == 2);
        CHECK(rig.item(kHeliStart)->disabled()); // either player's locked choice holds Start
        rig.tapItem(kArrowPrev);
        CHECK(rig.fe.helicopters()[1] == 1);
        CHECK_FALSE(rig.item(kHeliStart)->disabled());
        rig.tapItem(kHeliStart);
        // G_NewGame (5.1): both records from the reset checkpoint, the loadout for both.
        REQUIRE(rig.host.starts.size() == 1);
        const MissionStart s = rig.host.starts.back();
        CHECK(s.players == 2);
        CHECK(s.helicopter[0] == 1);
        CHECK(s.helicopter[1] == 1);
        CHECK(s.lives[0] == g.rules.startLives);
        CHECK(s.lives[1] == g.rules.startLives);
        CHECK(s.banked[1] == 0);
        CHECK_FALSE(s.carryUpgrades);
        CHECK_FALSE(rig.fe.menuOpen()); // no texts: no start dialogue
        // Resume clears both players' p_action (the host's clearPlayerActions does both).
        const int clears = rig.host.clears;
        rig.frame(UiInput().key(keys::Escape));
        REQUIRE(rig.top() == Screen::InGame);
        rig.wait(0.3f);
        rig.tapItem(1);
        CHECK(rig.host.clears > clears);
        // The end: the checkpoint holds both records.
        rig.fe.onEndLevel(rig.report(600, 1, 3, true));
        REQUIRE(rig.top() == Screen::MissionComplete);
        const CampaignCheckpoint& cp = rig.profile.progress.checkpoint;
        CHECK(cp.mission == 1);
        CHECK(cp.lives[0] == 1);
        CHECK(cp.lives[1] == 3);
        CHECK(cp.score[0] == 600);
        CHECK(cp.score[1] == 300);
        CHECK(rig.fe.facts().endSlot == -1); // no high score in two-player mode
        // Restart: as the mission began, for both.
        rig.wait(0.3f);
        rig.tapItem(2);
        CHECK(rig.host.starts.back().restart);
        CHECK(rig.host.starts.back().players == 2);
        CHECK(rig.host.starts.back().lives[1] == g.rules.startLives);
        CHECK(rig.host.starts.back().banked[1] == 0);
        // Next: both banked, both lives, both players' upgrades carried.
        rig.fe.onEndLevel(rig.report(600, 1, 3, true));
        rig.wait(0.3f);
        rig.tapItem(3);
        const MissionStart n = rig.host.starts.back();
        CHECK(n.mission == 1);
        CHECK(n.players == 2);
        CHECK(n.banked[0] == 600);
        CHECK(n.banked[1] == 300);
        CHECK(n.lives[0] == 1);
        CHECK(n.lives[1] == 3);
        CHECK(n.helicopter[1] == 1);
        CHECK(n.carryUpgrades == g.rules.upgradesCarryToNextMission);
        if (n.carryUpgrades) {
            CHECK(n.upgrades[1][2] == 3);
            CHECK(n.weapon[1] == 2);
        }
        // Game over (both out): Restart keeps both lives; Quit banks both, no name entry.
        rig.fe.onGameOver(rig.report(90000, -1, -1, false));
        REQUIRE(rig.top() == Screen::GameOver);
        rig.frame();
        rig.tapItem(1);
        CHECK(rig.host.starts.back().lives[0] == 1);
        CHECK(rig.host.starts.back().lives[1] == 3);
        rig.fe.onGameOver(rig.report(90000, -1, -1, false));
        rig.frame();
        rig.tapItem(2);
        CHECK(rig.top() == Screen::MainMenu); // 90000 would qualify for one player
        CHECK(rig.fe.campaign().p[1].banked == 300 + 45000);
        // Continue from the checkpoint: both records again.
        rig.tapItem(1);
        rig.wait(0.3f);
        CHECK(rig.item(kGameMode)->index == 1); // the flag is kept
        rig.item(kMissionList)->selected = 1;
        rig.tapItem(kNext);
        rig.tapItem(kHeliStart);
        const MissionStart c = rig.host.starts.back();
        CHECK(c.players == 2);
        CHECK(c.lives[1] == 3);
        CHECK(c.banked[1] == 300);
    }
}

// The real game behind the menus in co-operative mode.
void realGameTwoPlayers(const GameProfile& g) {
    AS3D_REQUIRE_GLES();
    const GameData data = locateGameData(testdata::root(), g);
    if (!data.present() || (!data.hasExtracted && data.paks.empty())) {
        std::fprintf(stderr, "SKIPPED (no data for %s): %s\n", g.key, __FILE__);
        return;
    }
    using namespace as3d_game;
    GameSession session;
    GameOptions o;
    o.dataRoot = testdata::root();
    o.game = &g;
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
    FlowConfig c; // the desktop's: two players offered
    c.attract = 1;
    c.showLogo = false;
    c.textsPath = data.textsFile;
    REQUIRE_MESSAGE(flow.init(c, &err), err);
    flow.boot();
    Frontend& fe = flow.frontend();
    REQUIRE(fe.sequel());
    flow.profile().progress.helicopterUnlocked[1] = true;
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
        REQUIRE_MESSAGE(it, "no item " << id << " on " << screenName(fe.topScreen()));
        REQUIRE_MESSAGE(!it->disabled(), "item " << id << " is disabled");
        step(UiInput().tap(it->hit.x + it->hit.w * 0.5f, it->hit.y + it->hit.h * 0.5f));
    };
    auto closeDialogue = [&]() {
        for (int i = 0; i < 80 && fe.menuOpen() && fe.topScreen() == Screen::Dialogue; i++) {
            for (int k = 0; k < 20; k++) step();
            step(UiInput().key(keys::Enter));
        }
        for (int i = 0; i < 40; i++) step();
    };
    auto bothFlying = [&]() {
        const World& w = session.world();
        REQUIRE(w.numPlayers() == 2);
        for (int p = 0; p < 2; p++) {
            const int pi = w.playerEntityIndex(p);
            REQUIRE(pi >= 0);
            CHECK(w.entity(pi).name == g.rules.heliObjects[p == 0 ? 0 : 1]);
        }
        // Player 2 spawns 100 units right of player 1 (as2/engine-behaviour.delta.md 7.5).
        CHECK(w.entity(w.playerEntityIndex(1)).f(F_ORIGIN) > w.entity(w.playerEntityIndex(0)).f(F_ORIGIN));
    };
    tapItem(1);
    REQUIRE(fe.topScreen() == Screen::StartGame);
    for (int i = 0; i < 20; i++) step();
    tapItem(kGameMode);
    REQUIRE(fe.twoPlayers());
    tapItem(kNext);
    REQUIRE(fe.topScreen() == Screen::HeliSelect);
    for (int i = 0; i < 20; i++) step();
    tapItem(kPlayerSpinner);
    tapItem(kArrowNext); // player 2 in helicopter 1
    REQUIRE(fe.helicopters()[1] == 1);
    target.bind();
    flow.draw(400, 300);
    CHECK(flow.modelViewsDrawn() == 1); // player 2's helicopter preview
    tapItem(kHeliStart);
    REQUIRE(fe.state() == FrontendState::Playing);
    bothFlying();
    // Both records start with the campaign's lives.
    CHECK(session.world().player(1).livesAtStart == g.rules.startLives);
    closeDialogue();
    REQUIRE_FALSE(fe.menuOpen());
    for (int i = 0; i < 120; i++) step();
    CHECK(as3d_game::hudStateOf(session).playerCount == 2);
    target.bind();
    flow.draw(400, 300); // the two-player HUD (as2/frontend.md 4.3)
    // In-game menu, Restart: still two players, the lives they began with.
    step(UiInput().key(keys::Escape));
    REQUIRE(fe.topScreen() == Screen::InGame);
    for (int i = 0; i < 20; i++) step();
    tapItem(4);
    REQUIRE(fe.state() == FrontendState::Playing);
    bothFlying();
    closeDialogue();
    REQUIRE_FALSE(fe.menuOpen());
    for (int i = 0; i < 60; i++) step();
    // The end: the checkpoint has both records, then Next starts the next mission with both.
    const int lives2 = static_cast<int>(session.world().player(1).lives);
    session.world().endLevel();
    for (int i = 0; i < 5 && !fe.menuOpen(); i++) step();
    REQUIRE(fe.menuOpen());
    CHECK(flow.profile().progress.checkpoint.mission == 1);
    CHECK(flow.profile().progress.checkpoint.lives[1] == lives2);
    closeDialogue();
    REQUIRE(fe.topScreen() == Screen::MissionComplete);
    for (int i = 0; i < 20; i++) step();
    tapItem(3); // Next
    REQUIRE(fe.state() == FrontendState::Playing);
    CHECK(session.mission() == 2);
    bothFlying();
    CHECK(session.world().player(1).livesAtStart == lives2);
    closeDialogue();
    for (int i = 0; i < 60; i++) step();
    // Game over: Quit to the main menu, no name entry.
    session.world().setGameOver();
    for (int i = 0; i < 5 && !fe.menuOpen(); i++) step();
    REQUIRE(fe.topScreen() == Screen::GameOver);
    for (int i = 0; i < 20; i++) step();
    tapItem(2);
    CHECK(fe.topScreen() == Screen::MainMenu);
    CHECK(session.world().intermission());
}

} // namespace

TEST_CASE("sequel co-op: AirStrike 2's Game mode, Player spinner and both records through the campaign") {
    menusTwoPlayers(gameProfile(GameId::AirStrike2));
}

TEST_CASE("sequel co-op: Gulf Thunder's Game mode, Player spinner and both records through the campaign") {
    menusTwoPlayers(gameProfile(GameId::GulfThunder));
}

TEST_CASE("sequel co-op: AirStrike 2's real game with two helicopters: HUD, Restart, Next, game over") {
    realGameTwoPlayers(gameProfile(GameId::AirStrike2));
}

TEST_CASE("sequel co-op: Gulf Thunder's real game with two helicopters: HUD, Restart, Next, game over") {
    realGameTwoPlayers(gameProfile(GameId::GulfThunder));
}

TEST_CASE("sequel co-op: the desktop flow offers Cooperative in the sequels, a one-player host does not") {
    // GameFlow's gate: the game's front end has two players, the host offers them; without data
    // the flow still builds its front end (no level), which is all this needs.
    for (const GameId id : {GameId::AirStrike2, GameId::GulfThunder}) {
        const GameProfile& g = gameProfile(id);
        for (const bool offered : {true, false}) {
            INFO(g.key << (offered ? ", host offers two players" : ", host offers one"));
            using namespace as3d_game;
            const GameData data = locateGameData(testdata::root(), g);
            if (!data.present() || (!data.hasExtracted && data.paks.empty())) {
                std::fprintf(stderr, "SKIPPED (no data for %s): %s\n", g.key, __FILE__);
                return;
            }
            GameSession session;
            GameOptions o;
            o.dataRoot = testdata::root();
            o.game = &g;
            o.startLevel = false;
            o.levelFlow = false;
            std::string err;
            REQUIRE_MESSAGE(session.init(o, &err), err);
            AudioBridge audio;
            audio.init(session.vfs(), true);
            GameFlow flow(session, audio);
            FlowConfig c;
            c.attract = 1;
            c.showLogo = false;
            c.twoPlayerMode = offered;
            REQUIRE_MESSAGE(flow.init(c, &err), err);
            flow.boot();
            Frontend& fe = flow.frontend();
            fe.open(Screen::StartGame);
            const MenuItem* mode = fe.menus().top()->find(kGameMode);
            REQUIRE(mode);
            CHECK(mode->values.size() == (offered ? 2u : 1u));
        }
    }
}
