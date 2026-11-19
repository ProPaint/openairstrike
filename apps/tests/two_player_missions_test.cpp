// The two-player soak: every mission of the three games with two players, each flown by its own
// world-aware pilot (as3d::botInputPlayers), so that the paths only a second player reaches run
// (player 2's spawn at x + 100, its helicopter and loadout, its pick-ups, deaths, respawns and
// game over with both records, the scripts' IsMultiplayer / IsPlayerInGame / GetPlayersDistance
// and player2 branches, as2/engine-behaviour.delta.md 7.5, gulf/engine-behaviour.delta.md 7).
// Each mission runs twice: 2400 frames in god mode at Normal (both players must be alive at the
// end, and the level scrolls), then 1800 frames without god mode at Nightmare, where the players
// die and respawn and the game may end. Neither may raise a script error, a stall, a refused
// spawn, a forced respawn or a stub builtin. The missions played to their end with two pilots are
// in docs/missions-status-as2.md and docs/missions-status-gulf.md ("Two players"), the first
// game's in docs/missions-status.md.
//
// Each game's soak runs in its own pass of tools/ci.sh (AS3D_GAME=as3d, as2, gulf; at most
// about 25 s each), and skips loudly in the other passes or without the game's data.
#include "doctest.h"

#include <cstdio>
#include <string>

#include "as3d/defs.h"
#include "as3d/game_data.h"
#include "as3d/input.h"
#include "as3d/script_host.h"
#include "as3d/vfs.h"
#include "as3d/world.h"
#include "test_data.h"

using namespace as3d;

namespace {

void soakTwoPlayers(GameId id) {
    const GameProfile& game = gameProfile(id);
    if (testdata::gameKey() != game.key) {
        std::fprintf(stderr, "SKIPPED (the %s two-player soak runs in the %s pass): %s\n", game.key, game.key, __FILE__);
        return;
    }
    const GameData data = locateGameData(testdata::root(), game);
    if (!data.hasExtracted) {
        std::fprintf(stderr, "SKIPPED (no %s data in %s): %s\n", game.key, data.extractedDir.c_str(), __FILE__);
        return;
    }
    Vfs vfs;
    vfs.mount(makeDirSource(data.extractedDir));
    DefDatabase db;
    REQUIRE(db.load(vfs));
    for (int m = 1; m <= game.rules.missionCount; ++m) {
        for (int pass = 0; pass < 2; ++pass) {
            const bool god = pass == 0;
            INFO(game.key << " mission " << m << (god ? ", god mode, Normal" : ", Nightmare"));
            World w;
            WorldConfig cfg;
            cfg.rules = &game.rules;
            cfg.players = 2;
            cfg.heli[0] = 0;
            cfg.heli[1] = game.rules.helicopterCount > 1 ? 1 : 0; // the second player in another helicopter
            cfg.godMode = god;
            cfg.difficulty = god ? 2 : 4;
            cfg.seed = 1;
            w.init(vfs, db, cfg);
            std::string err;
            REQUIRE_MESSAGE(w.loadLevel(std::to_string(m), &err), err);
            REQUIRE(w.numPlayers() == 2);
            const u32 frames = god ? 2400u : 1800u;
            for (u32 f = 0; f < frames && !w.gameOver(); ++f) w.step(botInputPlayers(w, f).toPlayerInput());
            const WorldStats& st = w.stats();
            INFO("first error: " << (st.firstErrors.empty() ? std::string() : st.firstErrors.front()));
            CHECK(st.scriptErrors == 0u);
            CHECK(st.stalls == 0u);
            CHECK(st.spawnRefused == 0u);
            CHECK(st.forcedRespawns == 0u);
            if (god) {
                CHECK_FALSE(w.gameOver());
                CHECK(w.mapPos() > 32.0f); // the level scrolls
                CHECK(w.playerEntityIndex(0) >= 0);
                CHECK(w.playerEntityIndex(1) >= 0);
            }
            for (const script::BuiltinReport::Row& row : w.report().rows()) {
                INFO("builtin " << row.name);
                CHECK(row.status != script::BuiltinStatus::Stub);
            }
            CHECK(w.host().unknownGlobals().empty());
        }
    }
}

} // namespace

TEST_CASE("two players: every AirStrike 3D mission with two pilots, god mode and Nightmare") {
    soakTwoPlayers(GameId::AirStrike3D);
}

TEST_CASE("two players: every AirStrike 2 mission with two pilots, god mode and Nightmare") {
    soakTwoPlayers(GameId::AirStrike2);
}

TEST_CASE("two players: every Gulf Thunder operation with two pilots, god mode and Nightmare") {
    soakTwoPlayers(GameId::GulfThunder);
}
