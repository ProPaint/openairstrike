// Console pad model (no platform headers): a handheld's buttons, circle pad and touch screen
// as one plain snapshot, and its translation to the game's FrameInput and to the front end's
// UiInput. The button bits equal libctru's KEY_* values, so the 3DS backend copies hidKeysHeld /
// Down / Up straight in.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "as3d/input.h"
#include "as3d/menu.h"
#include "as3d/ui.h"

namespace as3d {

namespace pad {
constexpr uint32_t A = 1u << 0, B = 1u << 1, Select = 1u << 2, Start = 1u << 3;
constexpr uint32_t DRight = 1u << 4, DLeft = 1u << 5, DUp = 1u << 6, DDown = 1u << 7;
constexpr uint32_t R = 1u << 8, L = 1u << 9, X = 1u << 10, Y = 1u << 11;
constexpr uint32_t ZL = 1u << 14, ZR = 1u << 15;
constexpr uint32_t Touch = 1u << 20;
constexpr uint32_t CStickRight = 1u << 24, CStickLeft = 1u << 25, CStickUp = 1u << 26,
                   CStickDown = 1u << 27;
constexpr uint32_t CPadRight = 1u << 28, CPadLeft = 1u << 29, CPadUp = 1u << 30,
                   CPadDown = 1u << 31;
} // namespace pad

struct PadState {
    uint32_t held = 0, down = 0, up = 0; // buttons held, pressed and released this frame
    int16_t cx = 0, cy = 0;              // circle pad, -156..156 on the 3DS, y up
    bool touch = false;                  // a finger is on the touch screen
    int touchX = 0, touchY = 0;          // in touch-screen pixels
};

// Which pad button drives each action (0 = none). A button may drive several actions only if
// the caller sets it so; the defaults use each button once.
struct PadBindings {
    uint32_t fire = 0, missile = 0, powerUp = 0;
    uint32_t nextWeapon = 0, nextMissile = 0, nextPowerUp = 0;
    uint32_t pause = 0;
};

// A fire, B missile, X power-up, Y next weapon, R next missile, ZR next power-up, START
// pause. L is never required.
PadBindings defaultBindings();

// A pad button by name (case-insensitive): A B X Y L R ZL ZR START SELECT, and NONE or an empty
// name for 0 (unbound). False for anything else (`out` unchanged).
bool padButtonFromName(const std::string& name, uint32_t* out);
// One "bind_<action>=<button>" override of a config file (run.ini): the actions are fire,
// missile, powerup, nextweapon, nextmissile, nextpowerup, pause. False when the key is not a
// binding or the button is unknown (bindings unchanged).
bool applyBindingOption(PadBindings& bindings, const std::string& key, const std::string& value);

// Player 1's FrameInput from the pad: directions from the D-pad or from the circle pad beyond
// `deadZone` (up = Forward), the bound buttons as action bits, `pausePressed` on the pause
// button's down edge and `confirm` on A's down edge.
FrameInput padToFrameInput(const PadState& pad, const PadBindings& bindings, int deadZone = 40);

// The front end's input for one frame: D-pad presses (down edges) as the arrow keys, A as
// Enter, B and START as Escape, and the touch screen as a pointer: on touch start a move and a
// press of Mouse1, while held a move, on release a release of Mouse1. The touch point is
// converted from touch-screen pixels (touchW x touchH) to the virtual 800x600 screen through
// `map` (as the bottom-screen/framebuffer mapping of the caller). `before` is the previous
// frame's state, used only for the touch edges.
ui::UiInput padToUiInputs(const PadState& now, const PadState& before, const ui::Mapping& map,
                          int touchW, int touchH);

// ---------------------------------------------------------------------------------------
// Game controllers (docs/web.md "Controllers"): SDL_GameController on the desktop, Android and the
// web (where Emscripten's SDL reports the browser's "standard" gamepads as game controllers),
// folded into the same PadState and bindings as the 3DS. The snapshot is SDL's layout without
// SDL's headers (game_loop.cpp checks the indices against SDL_CONTROLLER_*).
// ---------------------------------------------------------------------------------------

namespace gamepad {
// SDL_GameControllerButton order (Xbox names: A bottom, B right, X left, Y top).
enum Button : int {
    A = 0, B, X, Y, Back, Guide, Start, LeftStick, RightStick, LeftShoulder, RightShoulder,
    DUp, DDown, DLeft, DRight, ButtonCount
};
// SDL_GameControllerAxis order: sticks -32768..32767 (y down), triggers 0..32767.
enum Axis : int { LeftX = 0, LeftY, RightX, RightY, TriggerLeft, TriggerRight, AxisCount };
} // namespace gamepad

struct GamepadSnapshot {
    bool buttons[gamepad::ButtonCount] = {};
    int16_t axes[gamepad::AxisCount] = {};
};

struct GamepadOptions {
    int stickPress = 13000;   // a stick direction counts beyond 40% of its travel...
    int stickRelease = 9800;  // ...and stays until it is back under 30% (no chatter at the edge)
    int triggerPress = 16000; // a trigger is a button beyond half its travel
};

// One controller's buttons as pad:: bits: A B X Y as the pad's, the shoulders L / R, the
// triggers ZL / ZR, Start, Back as Select, and the D-pad and the left stick both as the D-pad.
// `heldBefore` (the bits of the previous read) gives the stick its hysteresis.
uint32_t gamepadHeldBits(const GamepadSnapshot& s, uint32_t heldBefore, const GamepadOptions& o = {});

// A controller's bindings: defaultBindings() (A fire, B missile, X power-up, Y next weapon,
// R next missile, ZR next power-up, Start pause) with L and ZL as second next missile / next
// power-up buttons, so either hand cycles them.
PadBindings gamepadBindings();

// The controllers of a windowed loop (game_loop.cpp): read once per displayed frame, taken by
// the fixed simulation steps (0 to a few per frame) and by the menus.
class GamepadInput {
public:
    // Once per displayed frame: the held pad:: bits of every controller ORed, and the frame's
    // duration (for the menus' key repeat). True when a button or direction went down: the
    // player is using the pad (the window puts the menus in pad mode).
    bool update(uint32_t held, float dt);
    // Controllers gone, focus lost: nothing held, no pending edge.
    void releaseAll();
    const PadState& pad() const { return now_; }

    // The input of one simulation step. Presses since the last step are kept until a step
    // takes them (a frame may run no step), and a press released before it still reaches it.
    FrameInput takeFrame(const PadBindings& bindings);
    // Forgets the presses no step has taken (they went to a menu, not to the game).
    void dropPending() { pendingDown_ = 0; }

    // The menus' input for this frame (padToUiInputs): D-pad and stick as the arrows, held ones
    // repeating after kRepeatDelay seconds every kRepeatEvery, A as Enter; with `back`, B,
    // Start and Back as Escape (the game selector has no back: Escape would quit).
    ui::UiInput menuInputs(bool back = true) const;
    // During play: Escape (the in-game menu) on the pause button or Back.
    ui::UiInput playingInputs(const PadBindings& bindings) const;

    static constexpr float kRepeatDelay = 0.4f, kRepeatEvery = 0.1f;

private:
    PadState now_, before_;
    uint32_t pendingDown_ = 0; // down edges no step has taken yet
    float heldFor_[4] = {0, 0, 0, 0};
    int repeats_[4] = {0, 0, 0, 0}; // repeat keys due this frame per direction (up, down, left, right)
};

// A scripted pad (the 3DS run.ini pad_script=, for unattended runs in the emulator): presses at
// displayed frames.
struct PadScriptPress {
    unsigned frame = 0;
    uint32_t buttons = 0;  // pad:: bits
    bool touch = false;    // a touch at (x, y) in touch-screen pixels
    int x = 0, y = 0;
};
// "F:NAME[,F:NAME...]" with NAME one or more of A B X Y L R ZL ZR START SELECT UP DOWN LEFT RIGHT
// joined by '+' (case-insensitive), or T<x>/<y> for a touch; frames must not decrease. False on
// a syntax error (`out` keeps the presses before it).
bool parsePadScript(const std::string& text, std::vector<PadScriptPress>& out);
// Adds what the script holds at displayed frame `frame` to `pad` (held bits; touch and its
// point): each press lasts `holdFrames` frames from its own. Edges are the caller's.
void applyPadScript(const std::vector<PadScriptPress>& script, unsigned frame, unsigned holdFrames, PadState& pad);

} // namespace as3d
