// The typed cheat codes (docs/spec/engine-behaviour.md 14; as2/engine-behaviour.delta.md 14,
// the same words in Gulf Thunder): typed text is lower-cased into a rolling 32-character
// buffer, only outside intermission levels; when the buffer contains a code word it is
// cleared, the cheat is applied, the "cheat used" flag is set (rank Cheater, no score
// posting, no checkpoint in the sequels), `sounds\cheat.wav` plays and a message shows for
// 3 s. The buffer and the effects live here; the host (apps/game/game_session.cpp) feeds the
// typed characters, keeps the flag and shows the message.
#pragma once

#include <string>

#include "as3d/game_profile.h"

namespace as3d {

class World;

enum class Cheat : int {
    None = 0,
    GodMode,   // toggles god mode: player damage ignored
    Lives,     // p_lives = 99 for both players
    Weapons,   // every weapon slot not owned becomes owned at level 1, both players
    Missiles,  // +99 of every missile type (cap 99), both players; selects type 0 if none
    PowerUps,  // +99 of the game's power-up slots (cap 99), both players
    EndLevel,  // mission complete
    GameOver,  // game over
    Credits,   // the credits box (the first game only)
};

struct CheatCode {
    const char* word;
    Cheat cheat;
};

// The code words of a game, terminated by a {nullptr, Cheat::None} entry.
const CheatCode* cheatCodes(GameId game);

// The rolling buffer. feed() lower-cases the character, appends it, keeps the last 32 and
// returns the cheat whose word the buffer now ends with (the buffer is cleared then), else
// Cheat::None. Letters only: the words are letters, so anything else just scrolls the buffer.
class CheatInput {
public:
    explicit CheatInput(GameId game = GameId::AirStrike3D) : game_(game) {}
    void setGame(GameId game) { game_ = game; clear(); }
    Cheat feed(char c);
    void clear() { buffer_.clear(); }
    const std::string& buffer() const { return buffer_; }

private:
    GameId game_;
    std::string buffer_;
};

// Applies a cheat to the world (the player records, god mode, the level flow) and returns the
// texts key of its message ("cheat.god_on", "cheat.god_off", "cheat.lives", "cheat.weapons",
// "cheat.missiles", "cheat.powerups"), or "" for the cheats without one. Cheat::Credits and
// Cheat::None change nothing.
const char* applyCheat(World& world, Cheat cheat);

constexpr size_t kCheatBufferSize = 32;
constexpr float kCheatMessageSeconds = 3.0f;

} // namespace as3d
