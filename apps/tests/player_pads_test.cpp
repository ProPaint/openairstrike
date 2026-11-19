// Two players on one screen: which pad flies for which player (as3d/player_pads.h), the
// front end's Players screen and "2 Players" gate (screens_players.cpp, screens_start.cpp), and
// player 2's keys of the split keyboard (apps/game/game_flow.h). CPU only.
#include "doctest.h"

#include <string>
#include <vector>

#include "as3d/frontend.h"
#include "as3d/player_pads.h"
#include "../game/game_flow.h" // its functions are in game_flow_test.cpp's copy of game_flow.cpp

using namespace as3d;
using namespace as3d::ui;

TEST_CASE("player pads: automatic sides, numbers, held bits per player") {
    SUBCASE("keyboard: the first pad flies player 1, the second player 2, then the one with fewer") {
        PlayerPads p;
        p.setKeyboard(true);
        CHECK(p.twoPlayersPossible()); // the split keyboard alone
        CHECK(p.pads()[static_cast<size_t>(p.connect(10, "Pad A", "Pad A/g1"))].player == 0);
        CHECK(p.pads()[static_cast<size_t>(p.connect(11, "Pad B", "Pad B/g2"))].player == 1);
        CHECK(p.pads()[static_cast<size_t>(p.connect(12, "Pad C", "Pad C/g3"))].player == 0);
        CHECK(p.padsOf(0) == 2);
        CHECK(p.padsOf(1) == 1);
        CHECK(p.pads()[0].number == 1);
        CHECK(p.pads()[1].number == 2);
        CHECK(p.pads()[2].number == 3);
        // Numbers stay with their pads; a free one is reused.
        p.disconnect(11);
        CHECK(p.count() == 2);
        CHECK(p.find(11) == -1);
        CHECK(p.pads()[static_cast<size_t>(p.connect(13, "Pad D", "Pad D/g4"))].number == 2);
        // An id already known keeps its entry.
        CHECK(p.connect(13, "Pad D", "Pad D/g4") == p.find(13));
        CHECK(p.count() == 3);

        p.setHeld(10, 0x1);
        p.setHeld(12, 0x10);
        p.setHeld(13, 0x100);
        const int side13 = p.pads()[static_cast<size_t>(p.find(13))].player;
        CHECK(p.heldOf(0) == (0x11u | (side13 == 0 ? 0x100u : 0u)));
        CHECK(p.heldOf(1) == (side13 == 1 ? 0x100u : 0u));
        p.setHeld(99, 0xFF); // unknown id: nothing
        CHECK((p.heldOf(0) | p.heldOf(1)) == 0x111u);
    }
    SUBCASE("touch: the touch screen is player 1's, so the first pad flies player 2") {
        PlayerPads p;
        p.setTouch(true);
        CHECK_FALSE(p.twoPlayersPossible());
        CHECK(p.hasDevice(0));
        CHECK_FALSE(p.hasDevice(1));
        CHECK(p.pads()[static_cast<size_t>(p.connect(1, "Pad", "Pad/g"))].player == 1);
        CHECK(p.twoPlayersPossible());
        CHECK(p.hasDevice(1));
        // A second pad: a tie (touch + nothing against one pad) goes to player 1.
        CHECK(p.pads()[static_cast<size_t>(p.connect(2, "Pad 2", "Pad 2/g"))].player == 0);
    }
    SUBCASE("nothing but pads (a TV box): two pads are needed") {
        PlayerPads p;
        CHECK_FALSE(p.twoPlayersPossible());
        p.connect(1, "A", "A/1");
        CHECK_FALSE(p.twoPlayersPossible());
        CHECK_FALSE(p.hasDevice(1));
        p.connect(2, "B", "B/2");
        CHECK(p.twoPlayersPossible());
        CHECK(p.hasDevice(0));
        CHECK(p.hasDevice(1));
    }
}

TEST_CASE("player pads: a side chosen is remembered for the run, identical pads apart") {
    PlayerPads p;
    p.setKeyboard(true);
    // Two pads of the same model (same name and GUID).
    p.connect(1, "Xbox", "Xbox/abc");
    p.connect(2, "Xbox", "Xbox/abc");
    CHECK(p.pads()[0].player == 0);
    CHECK(p.pads()[1].player == 1);
    // Swapped on the Players screen.
    p.setPlayer(0, 1);
    p.setPlayer(1, 0);
    p.setPlayer(1, 5);  // not a player: ignored
    p.setPlayer(7, 0);  // not a pad: ignored
    CHECK(p.pads()[0].player == 1);
    CHECK(p.pads()[1].player == 0);
    // Both unplugged and plugged in again (new ids, as SDL gives): each rank gets its side back.
    p.disconnect(1);
    p.disconnect(2);
    CHECK(p.count() == 0);
    CHECK(p.pads()[static_cast<size_t>(p.connect(3, "Xbox", "Xbox/abc"))].player == 1);
    CHECK(p.pads()[static_cast<size_t>(p.connect(4, "Xbox", "Xbox/abc"))].player == 0);
    // Another model has no memory: automatic (fewer devices: a tie, player 1).
    CHECK(p.pads()[static_cast<size_t>(p.connect(5, "DualSense", "DualSense/def"))].player == 0);
}

TEST_CASE("player pads: pushes wait for the Players screen") {
    PlayerPads p;
    p.connect(1, "A", "A/1");
    p.connect(2, "B", "B/2");
    CHECK(p.takeMove(0) == 0);
    p.pushMove(1, 1);
    p.pushMove(2, -5); // any left push is -1
    p.pushMove(9, 1);  // unknown: nothing
    CHECK(p.takeMove(0) == 1);
    CHECK(p.takeMove(0) == 0); // taken
    CHECK(p.takeMove(1) == -1);
    CHECK(p.takeMove(-1) == 0);
    CHECK(p.takeMove(2) == 0);
    p.pushMove(1, -1);
    p.clearMoves();
    CHECK(p.takeMove(0) == 0);
}

namespace {

struct FakeGame : GameHost {
    std::vector<MissionStart> starts;
    void startMission(const MissionStart& s) override { starts.push_back(s); }
    void loadAttract() override {}
    void setPaused(bool) override {}
    void clearPlayerActions() override {}
    void settingsChanged(const Settings&) override {}
    void saveProfile(const Profile&) override {}
    void quit() override {}
};

struct Rig {
    FakeGame game;
    Profile profile;
    PlayerPads pads;
    Frontend fe;
    explicit Rig(bool touch = false, bool twoPlayerMode = true)
        : fe(game, profile,
             [&] {
                 FrontendContent c;
                 for (int i = 0; i < kMissionCount; i++) c.missionNames[i] = "Mission " + std::to_string(i + 1);
                 c.twoPlayerMode = twoPlayerMode;
                 return c;
             }(),
             Texts{}) {
        pads.setTouch(touch);
        pads.setKeyboard(!touch);
        fe.setTouchMode(touch);
        fe.setPlayerPads(&pads);
        fe.boot();
    }
    void tap(float x, float y) { fe.update(0.016f, UiInput().tap(x, y)); }
    void key(int k) { fe.update(0.016f, UiInput().key(k)); }
    Screen top() const { return fe.topScreen(); }
    MenuItem* item(int id) {
        Menu* m = fe.menus().top();
        if (!m) return nullptr;
        for (MenuItem& it : m->items)
            if (it.id == id) return &it;
        return nullptr;
    }
    bool shown(int id) {
        MenuItem* it = item(id);
        return it && !it->hidden();
    }
    bool tapItem(int id) {
        MenuItem* it = item(id);
        if (!it || it->disabled() || it->hidden()) return false;
        tap(it->hit.x + it->hit.w * 0.5f, it->hit.y + it->hit.h * 0.5f);
        return true;
    }
    // Main menu -> Start Game with "2 Players" -> Start: the Players screen.
    void toPlayers() {
        REQUIRE(tapItem(1));
        REQUIRE(top() == Screen::StartGame);
        REQUIRE(tapItem(5)); // Game mode -> 2 Players
        REQUIRE(fe.twoPlayers());
        REQUIRE(tapItem(2));
        REQUIRE(top() == Screen::Players);
    }
};

} // namespace

TEST_CASE("players screen: Start Game with two players goes through it; Back, Enter and Start") {
    Rig rig;
    rig.toPlayers();
    CHECK(rig.game.starts.empty());
    CHECK(std::string(screenName(Screen::Players)) == "players");
    // Back: Start Game again, still two players.
    REQUIRE(rig.tapItem(1));
    CHECK(rig.top() == Screen::StartGame);
    REQUIRE(rig.tapItem(2));
    REQUIRE(rig.top() == Screen::Players);
    rig.key(keys::Escape);
    CHECK(rig.top() == Screen::StartGame);
    // Enter with no focus (a pad's A on the fresh screen) starts.
    REQUIRE(rig.tapItem(2));
    REQUIRE(rig.top() == Screen::Players);
    rig.key(keys::Enter);
    REQUIRE(rig.game.starts.size() == 1);
    CHECK(rig.game.starts.back().players == 2);
    CHECK(rig.fe.state() == FrontendState::Playing);

    // One player never sees it.
    Rig one;
    REQUIRE(one.tapItem(1));
    REQUIRE(one.tapItem(2));
    REQUIRE(one.game.starts.size() == 1);
    CHECK(one.game.starts.back().players == 1);
}

TEST_CASE("players screen: pushes and clicks move pads; Start waits for a device per player") {
    Rig rig(true); // a phone: touch is player 1, no keyboard
    rig.pads.connect(7, "Pad", "Pad/g");
    REQUIRE(rig.pads.pads()[0].player == 1);
    rig.pads.pushMove(7, 1); // before the screen: forgotten when it opens
    rig.toPlayers();
    rig.fe.update(0.016f, {});
    CHECK(rig.pads.pads()[0].player == 1);
    MenuItem* start = rig.item(2);
    REQUIRE(start);
    CHECK_FALSE(start->disabled());
    // Pushed left: player 1 has touch and the pad, player 2 nothing: Start is refused.
    rig.pads.pushMove(7, -1);
    rig.fe.update(0.016f, {});
    CHECK(rig.pads.pads()[0].player == 0);
    CHECK(rig.item(2)->disabled());
    rig.key(keys::Enter);
    CHECK(rig.game.starts.empty());
    // A tap on the pad's row (player 1's panel, below the touch line) gives it back to player 2.
    rig.tap(60 + 165, 165 + 40 + 26 + 8);
    CHECK(rig.pads.pads()[0].player == 1);
    rig.fe.update(0.016f, {});
    CHECK_FALSE(rig.item(2)->disabled());
    REQUIRE(rig.tapItem(2));
    REQUIRE(rig.game.starts.size() == 1);
    CHECK(rig.game.starts.back().players == 2);
}

TEST_CASE("players screen: draws with no data, every device listed") {
    Rig rig;
    rig.pads.connect(1, "A very long controller name that cannot fit in the panel at all", "x/1");
    rig.pads.connect(2, "B", "B/2");
    rig.pads.setHeld(2, 1);
    rig.toPlayers();
    UiAssets none;
    Renderer2D r;
    rig.fe.update(0.5f, {});
    r.begin(800, 600);
    rig.fe.draw(r, none);
    CHECK(r.dropped() == 0);
}

TEST_CASE("start game: 2 Players only while a second player has a device") {
    Rig phone(true); // touch, no keyboard, no pad
    REQUIRE(phone.tapItem(1));
    REQUIRE(phone.top() == Screen::StartGame);
    CHECK_FALSE(phone.shown(5));
    // A pad plugged in while the screen is up: the choice appears...
    phone.pads.connect(3, "Pad", "Pad/g");
    phone.fe.update(0.016f, {});
    CHECK(phone.shown(5));
    REQUIRE(phone.tapItem(5));
    CHECK(phone.fe.twoPlayers());
    // ...and goes with it, back to one player.
    phone.pads.disconnect(3);
    phone.fe.update(0.016f, {});
    CHECK_FALSE(phone.shown(5));
    CHECK_FALSE(phone.fe.twoPlayers());
    REQUIRE(phone.tapItem(2));
    REQUIRE(phone.game.starts.size() == 1);
    CHECK(phone.game.starts.back().players == 1);

    // The game's or the platform's gate stays shut whatever the devices.
    Rig closed(false, false);
    REQUIRE(closed.tapItem(1));
    CHECK_FALSE(closed.shown(5));
    CHECK_FALSE(closed.fe.twoPlayerChoice());

    // The keyboard alone is enough.
    Rig desk;
    REQUIRE(desk.tapItem(1));
    CHECK(desk.shown(5));
}

TEST_CASE("split keyboard: player 2's keys") {
    Settings s = Settings::defaults();
    CHECK(as3d_game::player2KeysCopyPlayer1(s)); // the built-in table binds both alike
    as3d_game::applyDesktopPlayer2Keys(s);
    CHECK_FALSE(as3d_game::player2KeysCopyPlayer1(s));
    CHECK(s.keys[1][static_cast<int>(Action::PrimaryAttack)][0] == 'F');
    CHECK(s.keys[1][static_cast<int>(Action::MoveForward)][0] == 'W');
    CHECK(s.keys[0][static_cast<int>(Action::MoveForward)][0] == keys::Up); // player 1 untouched

    // The web: player 1 on Space / X / C, player 2 fires with V (F is the page's full screen).
    Settings w = Settings::defaults();
    applyWebKeyBindings(w);
    CHECK(as3d_game::player2KeysCopyPlayer1(w));
    as3d_game::applyWebPlayer2Keys(w);
    CHECK(w.keys[1][static_cast<int>(Action::PrimaryAttack)][0] == 'V');
    CHECK(w.keys[1][static_cast<int>(Action::MissileAttack)][0] == 'G');
    CHECK(w.keys[1][static_cast<int>(Action::UseItem)][0] == 'H');
    CHECK(w.keys[0][static_cast<int>(Action::PrimaryAttack)][0] == keys::Space);
    // No key flies both players, and none is the page's F.
    for (int a = 0; a < kActionCount; ++a)
        for (int b = 0; b < kActionCount; ++b) {
            CHECK(w.keys[1][a][0] != w.keys[0][b][0]);
            CHECK(s.keys[1][a][0] != s.keys[0][b][0]);
        }
    for (int a = 0; a < kActionCount; ++a) CHECK(w.keys[1][a][0] != 'F');
}
