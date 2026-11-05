// The typed cheat codes (as3d/cheats.h; docs/spec/engine-behaviour.md 14, as2 delta 14): the
// rolling buffer and the effects on the player records and the level flow, per game.
#include "doctest.h"

#include <string>

#include "as3d/cheats.h"
#include "as3d/game_profile.h"
#include "world_test_util.h"

using namespace worldtest;

namespace {

Cheat type(CheatInput& in, const std::string& word) {
    Cheat last = Cheat::None;
    for (char c : word) last = in.feed(c);
    return last;
}

const char* kSequelHelis =
    "player_1 {\n player\n flag FL_TEMPORARY\n health 500\n speed 1.0\n}\n"
    "player_2 {\n player\n flag FL_TEMPORARY\n health 400\n speed 1.25\n}\n";

} // namespace

TEST_CASE("cheats: the rolling buffer matches a word at its end, lower-cased, and is cleared") {
    CheatInput in;
    CHECK(in.feed('x') == Cheat::None);
    CHECK(type(in, "IWANNAB") == Cheat::None);
    CHECK(in.feed('e') == Cheat::GodMode);
    CHECK(in.buffer().empty());
    // The word survives anything typed before it: the buffer keeps the last 32 characters.
    type(in, std::string(40, 'q'));
    CHECK(in.buffer().size() == kCheatBufferSize);
    CHECK(type(in, "endisnear") == Cheat::Lives);
    CHECK(type(in, "armorychamber") == Cheat::Weapons);
    CHECK(type(in, "launchmenow") == Cheat::Missiles);
    CHECK(type(in, "iamstronger") == Cheat::PowerUps);
    CHECK(type(in, "messwiththebest") == Cheat::EndLevel);
    CHECK(type(in, "dieliketherest") == Cheat::GameOver);
    CHECK(type(in, "givemecredit") == Cheat::Credits);
    // A match clears the buffer: the word has to be typed whole again.
    CHECK(type(in, "iwannabe") == Cheat::GodMode);
    CHECK(in.feed('e') == Cheat::None);
    // The sequels' words are not the first game's, and the other way round.
    CHECK(type(in, "invulnerability") == Cheat::None);
    in.setGame(GameId::AirStrike2);
    CHECK(type(in, "invulnerability") == Cheat::GodMode);
    CHECK(type(in, "igonnaliveforever") == Cheat::Lives);
    CHECK(type(in, "showmetheweapons") == Cheat::Weapons);
    CHECK(type(in, "moremoreweapons") == Cheat::Missiles);
    CHECK(type(in, "glitteringprizes") == Cheat::PowerUps);
    CHECK(type(in, "deadlineisnear") == Cheat::EndLevel);
    CHECK(type(in, "diediediemydarling") == Cheat::GameOver);
    CHECK(type(in, "iwannabe") == Cheat::None);
    CHECK(type(in, "givemecredit") == Cheat::None);
    in.setGame(GameId::GulfThunder);
    CHECK(type(in, "invulnerability") == Cheat::GodMode);
}

TEST_CASE("cheats: the first game's effects on both player records and the level flow") {
    Rig r;
    r.config.players = 2;
    r.start(true);
    World& w = r.world;
    REQUIRE(w.game() == GameId::AirStrike3D);

    CHECK_FALSE(w.godMode());
    CHECK(std::string(applyCheat(w, Cheat::GodMode)) == "cheat.god_on");
    CHECK(w.godMode());
    CHECK(std::string(applyCheat(w, Cheat::GodMode)) == "cheat.god_off");
    CHECK_FALSE(w.godMode());

    CHECK(std::string(applyCheat(w, Cheat::Lives)) == "cheat.lives");
    CHECK(w.player(0).lives == 99.0f);
    CHECK(w.player(1).lives == 99.0f);

    w.player(0).upgrades[2] = 3; // owned at level 3: kept
    CHECK(std::string(applyCheat(w, Cheat::Weapons)) == "cheat.weapons");
    CHECK(w.player(0).upgrades[2] == 3);
    CHECK(w.player(0).upgrades[0] == 1);
    CHECK(w.player(1).upgrades[19] == 1);

    w.player(0).missiles[1] = 5;
    CHECK(std::string(applyCheat(w, Cheat::Missiles)) == "cheat.missiles");
    CHECK(w.player(0).missiles[1] == 99); // capped
    CHECK(w.player(0).missiles[4] == 99);
    CHECK(w.player(1).missiles[0] == 99);
    CHECK(w.player(0).currentMissile == 0);
    applyCheat(w, Cheat::Missiles);
    CHECK(w.player(0).missiles[1] == 99);

    CHECK(std::string(applyCheat(w, Cheat::PowerUps)) == "cheat.powerups");
    CHECK(w.player(0).powerups[0] == 99);
    CHECK(w.player(0).powerups[3] == 99);
    CHECK(w.player(0).powerups[4] == 0);      // slots 0..3 only in the first game
    CHECK(w.player(0).currentPowerup == -1);  // no selection in the first game

    CHECK(std::string(applyCheat(w, Cheat::Credits)) == "");
    CHECK(std::string(applyCheat(w, Cheat::None)) == "");
    CHECK_FALSE(w.levelComplete());
    applyCheat(w, Cheat::EndLevel);
    CHECK(w.levelComplete());
    CHECK_FALSE(w.gameOver());
    applyCheat(w, Cheat::GameOver);
    CHECK(w.gameOver());
}

TEST_CASE("cheats: the sequels' slots: weapons 0..8, power-ups 0..5 and 8 with slot 0 selected") {
    Rig r;
    r.config.rules = &gameProfile(GameId::AirStrike2).rules;
    r.config.players = 1;
    r.obj(kSequelHelis);
    r.start(true);
    World& w = r.world;
    REQUIRE(w.game() == GameId::AirStrike2);
    applyCheat(w, Cheat::Weapons);
    CHECK(w.player(0).upgrades[0] == 1);
    CHECK(w.player(0).upgrades[8] == 1);
    CHECK(w.player(0).upgrades[9] == 0);
    applyCheat(w, Cheat::PowerUps);
    CHECK(w.player(0).powerups[5] == 99);
    CHECK(w.player(0).powerups[6] == 0);
    CHECK(w.player(0).powerups[7] == 0);
    CHECK(w.player(0).powerups[8] == 99);
    CHECK(w.player(0).currentPowerup == 0);
    // The second record is written too, whatever the player count (both records exist).
    CHECK(w.player(1).powerups[8] == 99);
    applyCheat(w, Cheat::GameOver); // the GameOver builtin's path in the sequels
    CHECK(w.gameOver());
}
