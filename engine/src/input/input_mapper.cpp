// Keyboard and mouse to FrameInput (as3d/input.h, docs/spec/engine-behaviour.md 7.2).
#include <SDL_scancode.h>

#include <algorithm>

#include "as3d/input.h"

namespace as3d {

namespace {
constexpr int kMouseLeft = kMouseButtonCode + 1;
constexpr int kMouseMiddle = kMouseButtonCode + 2;
constexpr int kMouseRight = kMouseButtonCode + 3;

// The original binds Windows virtual keys, where VK_CONTROL and VK_SHIFT stand for either
// side; SDL reports the two sides separately.
int canonicalCode(int code) {
    if (code == SDL_SCANCODE_RCTRL) return SDL_SCANCODE_LCTRL;
    if (code == SDL_SCANCODE_RSHIFT) return SDL_SCANCODE_LSHIFT;
    return code;
}
} // namespace

InputMapper::InputMapper() {
    clearBindings();
    // Player 1: config.ini [Controls] as shipped (VK codes 38/40/37/39 arrows, 17 Ctrl,
    // 16 Shift, 32 Space, 49/50/51/52 the digits 1-4, 200-202 the mouse buttons). The
    // stick codes 241-244 and joystick buttons are not mapped (no joystick support yet).
    bind(0, InputAction::Forward, 0, SDL_SCANCODE_UP);
    bind(0, InputAction::Backward, 0, SDL_SCANCODE_DOWN);
    bind(0, InputAction::Left, 0, SDL_SCANCODE_LEFT);
    bind(0, InputAction::Right, 0, SDL_SCANCODE_RIGHT);
    bind(0, InputAction::Fire, 0, SDL_SCANCODE_LCTRL);
    bind(0, InputAction::Fire, 1, kMouseLeft);
    bind(0, InputAction::Missile, 0, SDL_SCANCODE_LSHIFT);
    bind(0, InputAction::Missile, 1, kMouseRight);
    bind(0, InputAction::PowerUp, 0, SDL_SCANCODE_SPACE);
    bind(0, InputAction::PowerUp, 1, kMouseMiddle);
    bind(0, InputAction::NextMissile, 0, SDL_SCANCODE_1);
    bind(0, InputAction::NextWeapon, 0, SDL_SCANCODE_2);
    bind(0, InputAction::NextPowerUp, 0, SDL_SCANCODE_3);
    bind(0, InputAction::NextPowerUp, 1, SDL_SCANCODE_4);
    bind(0, InputAction::Confirm, 0, SDL_SCANCODE_RETURN);
    bind(0, InputAction::Confirm, 1, SDL_SCANCODE_KP_ENTER);
    bind(0, InputAction::Pause, 0, SDL_SCANCODE_P);
    bind(0, InputAction::Pause, 1, SDL_SCANCODE_PAUSE);
}

void InputMapper::clearBindings() {
    for (auto& p : bindings_)
        for (auto& a : p)
            for (int& s : a) s = kNoBinding;
}

void InputMapper::bind(int player, InputAction action, int slot, int code) {
    int a = static_cast<int>(action);
    if (player < 0 || player >= kMaxPlayers || a < 0 || a >= kInputActionCount || slot < 0 || slot >= kBindingSlots)
        return;
    bindings_[player][a][slot] = code < 0 ? kNoBinding : canonicalCode(code);
}

int InputMapper::binding(int player, InputAction action, int slot) const {
    int a = static_cast<int>(action);
    if (player < 0 || player >= kMaxPlayers || a < 0 || a >= kInputActionCount || slot < 0 || slot >= kBindingSlots)
        return kNoBinding;
    return bindings_[player][a][slot];
}

void InputMapper::press(int code, bool down) {
    code = canonicalCode(code);
    auto it = std::find(downCodes_.begin(), downCodes_.end(), code);
    bool wasDown = it != downCodes_.end();
    if (down == wasDown) return;
    if (down) {
        if (downCodes_.size() < 64) downCodes_.push_back(code);
    } else {
        downCodes_.erase(it);
    }
    // Recompute the held bits. A press is also latched until the next frame is taken, so a
    // tap shorter than one frame still reaches the simulation (the original ORs the bit in
    // on WM_KEYDOWN, whatever the frame timing).
    for (int p = 0; p < kMaxPlayers; ++p) {
        u32 held = 0;
        for (int a = 0; a < kInputActionCount; ++a) {
            u32 bit = inputActionBit(static_cast<InputAction>(a));
            if (!bit) continue;
            for (int s = 0; s < kBindingSlots; ++s) {
                int b = bindings_[p][a][s];
                if (b != kNoBinding && std::find(downCodes_.begin(), downCodes_.end(), b) != downCodes_.end()) held |= bit;
            }
        }
        latched_[p] |= held & ~state_.held[p];
        state_.held[p] = held;
    }
    bool confirm = false;
    for (int s = 0; s < kBindingSlots; ++s) {
        int b = bindings_[0][static_cast<int>(InputAction::Confirm)][s];
        if (b != kNoBinding && std::find(downCodes_.begin(), downCodes_.end(), b) != downCodes_.end()) confirm = true;
    }
    if (confirm && !state_.confirm) confirmLatched_ = true;
    state_.confirm = confirm;
    if (down) {
        for (int s = 0; s < kBindingSlots; ++s) {
            if (bindings_[0][static_cast<int>(InputAction::Pause)][s] == code) state_.pausePressed = true;
        }
    }
}

Hotkey InputMapper::keyEvent(int scancode, bool down, bool repeat) {
    if (repeat) return Hotkey::None;
    if (down && scancode == SDL_SCANCODE_ESCAPE) return Hotkey::Quit;
    if (down && scancode == SDL_SCANCODE_F12) return Hotkey::Screenshot;
    press(scancode, down);
    return Hotkey::None;
}

void InputMapper::mouseButtonEvent(int button, bool down) {
    if (button < 1 || button > 8) return;
    press(kMouseButtonCode + button, down);
}

void InputMapper::releaseAll() {
    std::vector<int> codes = downCodes_;
    for (int c : codes) press(c, false);
}

FrameInput InputMapper::takeFrame() {
    FrameInput out = state_;
    for (int p = 0; p < kMaxPlayers; ++p) {
        out.held[p] |= latched_[p];
        latched_[p] = 0;
    }
    out.confirm = out.confirm || confirmLatched_;
    confirmLatched_ = false;
    state_.pausePressed = false;
    return out;
}

} // namespace as3d
