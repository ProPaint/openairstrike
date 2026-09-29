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
    // The sequels' mouse control (as2/engine-behaviour.delta.md 7.2, issue as2/272): player
    // 1's relative mouse motion during this step (x right, y up) while MouseControl is on.
    // Not part of input scripts (neither recorded nor compared): a recorded run with mouse
    // steering does not replay.
    bool mouseSteer = false;
    float mouseDx = 0.0f, mouseDy = 0.0f;

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

// Keys the game handles itself rather than through p_action: Escape quits when the game runs
// without the front end (`--level N`; with it, Escape goes to the menus), F12 takes a
// screenshot.
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

// ORs two inputs of the same frame (keyboard plus touch, bot plus touch).
FrameInput mergeFrameInput(const FrameInput& a, const FrameInput& b);

// The original's key codes (Windows virtual keys extended as in engine-behaviour.md 7.2, the
// codes of the settings and of the front end) and binding codes of the mapper above.
// vkToBinding: a keyboard VK gives its SDL scancode (left Ctrl / Shift / Alt for the
// side-less VK_CONTROL / VK_SHIFT / VK_MENU); 200, 201, 202 (mouse 1, 2, 3) give the left,
// right and middle mouse buttons; joystick codes (203 to 238, 241 to 244), the wheel and
// unknown codes give kNoBinding (no joystick support).
int vkToBinding(int vk);
// SDL scancode to VK (both sides of Ctrl / Shift / Alt give the side-less VK, keypad Enter
// gives Return); 0 for keys the original has no code for.
int scancodeToVk(int scancode);
// SDL mouse button (1 left, 2 middle, 3 right) to the original's code 200 to 202, else 0.
int mouseButtonToVk(int button);

// Mouse control (MouseControl = 1, engine-behaviour.md 7.3): the direction bits player 1's
// p_action gets from the pointer and the helicopter's screen centre, both in the virtual
// 800x600 screen with y down. A 20-pixel dead zone per axis.
u32 mouseControlBits(float mouseX, float mouseY, float heliX, float heliY);
constexpr u32 kDirectionBits = ACT_FORWARD | ACT_BACKWARD | ACT_LEFT | ACT_RIGHT;

// ---------------------------------------------------------------------------------------
// Touch (docs/android.md "Controls"; the choices are in docs/spec/issues/100).
//
// Positions are normalised to the framebuffer: (0, 0) top-left, (1, 1) bottom-right, the
// same convention as SDL finger events. The helicopter position given to the mapper is in
// the original's virtual 800x600 screen (y down), the space of the HUD and of the
// original's mouse control (engine-behaviour.md 7.3).
//
// Movement is relative: a finger that goes down outside the buttons sets a target point at
// the helicopter's current screen position; moving the finger moves the target by the
// finger's displacement times `gain`. Each frame the direction bits steer the helicopter
// toward the target through the ordinary keyboard movement model (the player script owns
// speed and acceleration), like the original's mouse control (engine-behaviour.md 7.3) but
// with a smaller dead zone and a short look-ahead on the helicopter's own screen velocity
// so it brakes instead of overshooting. The primary weapon fires while any finger is down (except on the pause
// button). Buttons: missile and power-up are held; next missile, next weapon and next
// power-up send their one-shot bits while held (the world acts on the press only); pause
// toggles. While the game is paused, a tap anywhere continues.
// ---------------------------------------------------------------------------------------

enum class TouchPhase { Down, Move, Up, Cancel };

enum class TouchButton : int {
    Missile = 0,
    PowerUp,
    NextMissile,
    NextWeapon,
    NextPowerUp,
    Pause,
    Count
};
constexpr int kTouchButtonCount = static_cast<int>(TouchButton::Count);
const char* touchButtonName(TouchButton b);

struct TouchRect {
    float x = 0, y = 0, w = 0, h = 0; // normalised framebuffer coordinates
    bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// Framebuffer pixels the controls must stay out of (display cutouts, rounded corners).
struct SafeInsets {
    int left = 0, top = 0, right = 0, bottom = 0;
};

// A round button, in framebuffer pixels (origin top-left).
struct TouchCircle {
    float x = 0, y = 0, r = 0;
    bool contains(float px, float py) const { return (px - x) * (px - x) + (py - y) * (py - y) <= r * r; }
};

struct TouchLayoutOptions {
    SafeInsets insets;
    // Display density in dots per inch as the platform reports it; 0 (or implausible) =
    // unknown: the screen is taken for a landscape phone, 68 mm tall.
    float dpi = 0;
    bool leftHanded = false;  // Settings::leftHanded: the cluster at the left, pause at the right
    bool screen4x3 = false;   // Settings::screenMode == kScreen4x3: the bars are black
};

// The on-screen buttons (docs/spec/issues/140): round, sized in millimetres from the display
// density. Missile and power-up are the two big buttons in the corner under the thumb, each
// with its "next" button as a smaller satellite; next weapon sits above power-up. Pause is a
// small round button in the top corner on the other side.
struct TouchLayout {
    int fbWidth = 800, fbHeight = 600;
    // True when the buttons sit in the side bars outside the 4:3 play-field (screens wider
    // than 4:3 with room for them); false when they are drawn inside it.
    bool outside = false;
    bool leftHanded = false;
    bool screen4x3 = false;
    float alpha = 1.0f;                     // resting opacity of the buttons (see TouchFade)
    float pixelsPerMm = 1.0f;               // the density used
    TouchRect playField;                    // the centred 4:3 field (whole screen if narrower)
    TouchRect buttons[kTouchButtonCount];   // bounding boxes of `circles`, normalised
    TouchCircle circles[kTouchButtonCount]; // drawn buttons, framebuffer pixels
    TouchCircle hit[kTouchButtonCount];     // touch areas, framebuffer pixels (at least 9 mm across)
    // The button under a finger (normalised framebuffer coordinates), -1 for none. Where touch
    // areas overlap, the button whose centre is nearest (relative to its radius) wins.
    int hitTest(float nx, float ny) const;
    bool hits(TouchButton b, float nx, float ny) const;
};
TouchLayout computeTouchLayout(int fbWidth, int fbHeight, const TouchLayoutOptions& options);
TouchLayout computeTouchLayout(int fbWidth, int fbHeight, const SafeInsets& insets = {});

// The buttons' opacity over time: full while a button is held and for 2 s after the last
// button use, then easing over 0.5 s to the layout's resting opacity, so they hide less of
// the scene. Starts at full opacity (the player sees them when play begins).
constexpr float kTouchFadeHold = 2.0f;
constexpr float kTouchFadeTime = 0.5f;
float touchFadeAlpha(float secondsSinceUse, float restingAlpha);
class TouchFade {
public:
    void update(float dt, bool buttonHeld);
    void reset() { idle_ = 0; }
    float alpha(float restingAlpha) const { return touchFadeAlpha(idle_, restingAlpha); }
    float idleSeconds() const { return idle_; }

private:
    float idle_ = 0;
};

struct TouchSettings {
    float gain = 1.5f;          // helicopter displacement per finger displacement
    float deadZone = 8.0f;      // virtual pixels (the original's mouse control uses 20)
    float lookAhead = 10.0f;    // frames of the helicopter's screen velocity to anticipate
    float maxLead = 160.0f;     // the target stays within this many virtual pixels of the helicopter
    bool autoFire = true;
};

// The Options "Touch speed" step (Settings::touchSpeed, 0..4) as a factor on the drag gain
// and on the target's lead: 1, 1.25, 1.5, 1.75, 2 (docs/spec/issues/140: the lead limits a
// fast swipe, the helicopter's own top speed is left alone). Step 0 is the WP-48 feel.
float touchSpeedFactor(int step);
void applyTouchSpeed(TouchSettings& settings, int step);

class TouchMapper {
public:
    TouchMapper();

    void setScreen(int fbWidth, int fbHeight, const SafeInsets& insets = {});
    void setScreen(int fbWidth, int fbHeight, const TouchLayoutOptions& options);
    const TouchLayout& layout() const { return layout_; }
    TouchSettings& settings() { return settings_; }

    // One touch event. `id` identifies the finger for its lifetime (SDL finger id).
    void touchEvent(long long id, TouchPhase phase, float x, float y);
    // Lifts every finger (focus lost, app in background).
    void releaseAll();

    // The helicopter's screen centre for the coming frame, in virtual 800x600 pixels (y
    // down); valid = false while there is no visible player helicopter.
    void setPlayerScreen(bool valid, float vx, float vy);
    // Whether the game is paused by the player or by the app lifecycle: a tap then only
    // sends the pause edge that continues the game.
    void setPaused(bool paused) { paused_ = paused; }

    // The input for the next simulation frame.
    FrameInput takeFrame();

    // For drawing: whether a button is held, the drag target (virtual pixels).
    bool buttonHeld(TouchButton b) const;
    bool dragging() const { return dragFinger_ >= 0; }
    bool targetValid() const { return targetValid_; }
    float targetX() const { return targetX_; }
    float targetY() const { return targetY_; }
    int fingersDown() const;

    static constexpr int kMaxFingers = 10;

private:
    struct Finger {
        bool down = false;
        long long id = 0;
        int button = -1;       // TouchButton index, -1 = play area
        bool consumed = false; // went down while paused: no effect until lifted
        float x = 0, y = 0;    // last position (normalised)
    };
    int findFinger(long long id) const;
    int hitButton(float x, float y) const;
    float virtualPerNormX() const;
    float virtualPerNormY() const;

    TouchLayout layout_;
    TouchSettings settings_;
    Finger fingers_[kMaxFingers];
    int dragFinger_ = -1;      // index into fingers_
    bool targetValid_ = false;
    float targetX_ = 0, targetY_ = 0;
    float pendingDx_ = 0, pendingDy_ = 0; // finger motion (virtual px) not yet applied to the target
    bool playerValid_ = false;
    float playerX_ = 0, playerY_ = 0;
    bool prevValid_ = false;
    float prevX_ = 0, prevY_ = 0;
    bool paused_ = false;
    u32 latched_ = 0;          // bits pressed since the last takeFrame()
    bool confirmLatched_ = false;
    bool pauseLatched_ = false;
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
