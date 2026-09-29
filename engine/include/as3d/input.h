// Player input: the action layer between the keyboard/mouse and the simulation's
// PlayerInput (docs/spec/engine-behaviour.md 7.2), a deterministic input script format for
// playback and recording, and the scripted test pilot shared with `as3d_sim --bot`.
//
// This header has no SDL types: key codes are SDL scancodes passed as plain ints, mouse
// buttons are SDL button numbers (1 left, 2 middle, 3 right). The SDL-specific tables live
// in engine/src/input/input_mapper.cpp.
//
// Input script format (one event per line, '#' starts a comment, blank lines ignored):
//
//     <frame> <action> <value>
//
// `frame` is the 0-based simulation frame the event applies to (the input of the
// world.step() call with that index). `action` is one of the names below; a "p2_" prefix
// addresses player 2. For held actions `value` is 1 (pressed) or 0 (released) and the state
// persists until the next event for that action. `pause` is an edge: `pause 1` toggles the
// pause on that frame, `pause 0` is ignored. Events are applied in file order within a
// frame; frames must not decrease.
#pragma once

#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/world.h"

namespace as3d {

enum class InputAction : int {
    Fire = 0,     // p_action 0x001
    Missile,      // 0x002
    PowerUp,      // 0x004
    Forward,      // 0x010
    Backward,     // 0x020
    Left,         // 0x040
    Right,        // 0x080
    NextMissile,  // 0x100
    NextPowerUp,  // 0x200
    NextWeapon,   // 0x400
    Confirm,      // the OK button of a tutorial hint box (shared by both players)
    Pause,        // edge: toggles the pause
    Count
};
constexpr int kInputActionCount = static_cast<int>(InputAction::Count);

const char* inputActionName(InputAction a);
// Accepts the names printed by inputActionName; false for anything else.
bool inputActionFromName(const std::string& name, InputAction& out);
// The p_action bit of a player action, 0 for Confirm and Pause.
u32 inputActionBit(InputAction a);

// The complete input of one simulation frame.
struct FrameInput {
    u32 held[kMaxPlayers] = {0, 0}; // held p_action bits per player
    bool confirm = false;           // held
    bool pausePressed = false;      // edge: toggle the pause this frame

    PlayerInput toPlayerInput() const;
    bool operator==(const FrameInput& o) const {
        return held[0] == o.held[0] && held[1] == o.held[1] && confirm == o.confirm &&
               pausePressed == o.pausePressed;
    }
    bool operator!=(const FrameInput& o) const { return !(*this == o); }
};

// The deterministic test pilot of `as3d_sim --bot` and `as3d_game --bot`: fire held,
// missiles pulsed every half second, a power-up every 10 s, a weave symmetric about the start
// position (right 1 s, left 2 s, right 1 s, forward and back), hint boxes confirmed. A pure
// function of the frame, so both tools produce the same input.
FrameInput botInput(u32 frame);

// A pilot that looks at the world (`as3d_sim --pilot`): the same fire, missile and power-up
// pattern, flying the lower middle of the play-field with a gentle weave, over the nearest
// pick-up ahead, and sideways out of the path of enemy fire and rammers. Deterministic (a
// pure function of the world state and the frame); it reads the world, never writes it.
FrameInput botInput(const World& world, u32 frame);

// ---------------------------------------------------------------------------------------
// Keyboard and mouse.
// ---------------------------------------------------------------------------------------

// Binding codes: an SDL scancode, or kMouseButtonCode + SDL button number.
constexpr int kMouseButtonCode = 0x10000;
constexpr int kNoBinding = -1;
constexpr int kBindingSlots = 2;

// Keys the game handles itself rather than through p_action: Escape quits (there is no
// in-game menu yet), F12 takes a screenshot.
enum class Hotkey { None, Quit, Screenshot };

class InputMapper {
public:
    // Default bindings: player 1 as in the shipped config.ini [Controls] section (arrows,
    // Ctrl or left mouse = fire, Shift or right mouse = missile, Space or middle mouse =
    // power-up, 1 = next missile, 2 = next weapon, 3 / 4 = next power-up); player 2 has no
    // keys bound (its shipped bindings are the same keys, which would drive both players).
    // Return / keypad Enter / left mouse confirm a hint box. P and Pause toggle the pause.
    InputMapper();

    void clearBindings();
    // Player 0 or 1, slot 0 or 1, code as above (kNoBinding to clear). Ignores bad values.
    void bind(int player, InputAction action, int slot, int code);
    int binding(int player, InputAction action, int slot) const;

    // Feed window events. `repeat` key events (autorepeat) are ignored: held bits are
    // re-applied by the world every frame (docs/spec/issues/033).
    Hotkey keyEvent(int scancode, bool down, bool repeat);
    void mouseButtonEvent(int button, bool down);
    // Window focus lost: releases everything (no stuck keys).
    void releaseAll();

    // The input for the next simulation frame; consumes the pause edge.
    FrameInput takeFrame();
    const FrameInput& peek() const { return state_; }

private:
    void press(int code, bool down);

    int bindings_[kMaxPlayers][kInputActionCount][kBindingSlots];
    std::vector<int> downCodes_; // codes currently held (at most 64)
    FrameInput state_;
    u32 latched_[kMaxPlayers] = {0, 0}; // pressed since the last takeFrame()
    bool confirmLatched_ = false;
};

// ---------------------------------------------------------------------------------------
// Input scripts.
// ---------------------------------------------------------------------------------------

struct InputScriptEvent {
    u32 frame = 0;
    int player = 0; // 0 or 1; always 0 for Confirm and Pause
    InputAction action = InputAction::Fire;
    int value = 0;  // 0 or 1
    bool operator==(const InputScriptEvent& o) const {
        return frame == o.frame && player == o.player && action == o.action && value == o.value;
    }
};

class InputScript {
public:
    // Replaces the content. On error returns false with `error` naming the line; the script
    // is left empty. Bounded: at most 4 million events.
    bool parse(const std::string& text, std::string* error);
    bool load(const std::string& filePath, std::string* error);
    std::string serialize() const;
    bool save(const std::string& filePath) const;

    void add(const InputScriptEvent& e) { events_.push_back(e); }
    const std::vector<InputScriptEvent>& events() const { return events_; }
    void clear() { events_.clear(); }

private:
    std::vector<InputScriptEvent> events_;
};

// Plays a script back frame by frame. Frames must be requested in increasing order.
class InputScriptPlayer {
public:
    explicit InputScriptPlayer(const InputScript& script) : script_(&script) {}
    FrameInput frame(u32 frame);
    bool finished() const { return next_ >= script_->events().size(); }

private:
    const InputScript* script_;
    size_t next_ = 0;
    FrameInput held_;
};

// Records the input of consecutive frames as the minimal event list (only changes, plus
// pause edges).
class InputRecorder {
public:
    void record(u32 frame, const FrameInput& in);
    const InputScript& script() const { return script_; }

private:
    InputScript script_;
    FrameInput last_;
};

} // namespace as3d
