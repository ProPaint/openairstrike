// Ours (two players on one screen, docs/web.md "Two players", docs/android.md): which game
// controller flies for which player. No platform headers: the window loop (apps/game/game_loop.cpp)
// tells it what is plugged in and pressed, the front end's Players screen (screens_players.cpp)
// shows it and moves pads between the players, and the loop reads each player's pads back.
//
// In a one-player game every pad flies player 1, whatever its side here; the sides matter only
// when two players fly. A pad's side is, in this order: the side it was given on the Players
// screen or had when it was last plugged in during this run (by its model, the name and GUID,
// and its rank among the pads of that model, so two identical pads keep their own sides), else
// the automatic choice: the player who has fewer devices (the touch screen counts for player 1),
// player 1 on a tie. So one pad with a keyboard flies player 1 (player 2 takes W A S D), one pad
// on a phone flies player 2 (touch is player 1), two pads fly one player each.
//
// It lives in the ui module (the front end uses it and the input module depends on ui); the
// pad's button bits are the host's (as3d/console_pad.h pad::), kept here only to OR them per
// player.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace as3d {

class PlayerPads {
public:
    struct Pad {
        int id = -1;          // the host's handle (SDL's instance id), unique while plugged in
        std::string name;     // as the system names it ("Xbox 360 Controller")
        int number = 1;       // "Gamepad N" on screen: the smallest number free when it came
        int player = 0;       // 0 or 1
        uint32_t held = 0;    // the host's button bits of the last read
        std::string memory;   // model and rank: what its side is remembered by
        int move = 0;         // -1 / +1: pushed towards player 1 / 2 and not taken yet
    };

    // What else the players have (the host sets it every frame). touch: the touch controls fly
    // player 1. keyboard: a keyboard with both players' key sets (the split keyboard, issue 130).
    void setTouch(bool on) { touch_ = on; }
    void setKeyboard(bool on) { keyboard_ = on; }
    bool touch() const { return touch_; }
    bool keyboard() const { return keyboard_; }

    // A controller plugged in (`key`: its model, e.g. name and GUID). Returns its index. An id
    // already known keeps its entry.
    int connect(int id, const std::string& name, const std::string& key);
    void disconnect(int id);
    int find(int id) const; // index, -1 if unknown

    // The pad's button bits of this frame.
    void setHeld(int id, uint32_t held);
    // The pad was pushed left (-1, towards player 1) or right (+1, towards player 2): kept until
    // the Players screen takes it.
    void pushMove(int id, int dir);
    int takeMove(int index);
    void clearMoves();

    // The Players screen: gives the pad to `player` (remembered for this run).
    void setPlayer(int index, int player);

    const std::vector<Pad>& pads() const { return pads_; }
    int count() const { return static_cast<int>(pads_.size()); }
    int padsOf(int player) const;
    // The button bits of `player`'s pads ORed.
    uint32_t heldOf(int player) const;
    // Whether `player` has anything to fly with: a pad of its own, the keyboard (both players
    // have a key set), or for player 1 the touch screen.
    bool hasDevice(int player) const;
    // Whether a second player can fly at all: the keyboard, one pad beside the touch screen, or
    // two pads. The front end offers "2 Players" only then.
    bool twoPlayersPossible() const;

private:
    int automaticSide() const;

    std::vector<Pad> pads_;
    std::map<std::string, int> remembered_; // Pad::memory -> player
    bool touch_ = false, keyboard_ = false;
};

} // namespace as3d
