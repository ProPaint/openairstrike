// Console pad to FrameInput and UiInput (as3d/console_pad.h).
#include "as3d/console_pad.h"

#include <cstdlib>

namespace as3d {

PadBindings defaultBindings() {
    PadBindings b;
    b.fire = pad::A;
    b.missile = pad::B;
    b.powerUp = pad::X;
    b.nextWeapon = pad::Y;
    b.nextMissile = pad::R;
    b.nextPowerUp = pad::ZR;
    b.pause = pad::Start;
    return b;
}

FrameInput padToFrameInput(const PadState& p, const PadBindings& b, int deadZone) {
    FrameInput in;
    u32 bits = 0;
    auto act = [&](uint32_t button, InputAction a) {
        if (button && (p.held & button)) bits |= inputActionBit(a);
    };
    act(b.fire, InputAction::Fire);
    act(b.missile, InputAction::Missile);
    act(b.powerUp, InputAction::PowerUp);
    act(b.nextWeapon, InputAction::NextWeapon);
    act(b.nextMissile, InputAction::NextMissile);
    act(b.nextPowerUp, InputAction::NextPowerUp);
    if ((p.held & pad::DUp) || p.cy > deadZone) bits |= inputActionBit(InputAction::Forward);
    if ((p.held & pad::DDown) || p.cy < -deadZone) bits |= inputActionBit(InputAction::Backward);
    if ((p.held & pad::DLeft) || p.cx < -deadZone) bits |= inputActionBit(InputAction::Left);
    if ((p.held & pad::DRight) || p.cx > deadZone) bits |= inputActionBit(InputAction::Right);
    in.held[0] = bits;
    in.pausePressed = b.pause && (p.down & b.pause) != 0;
    in.confirm = (p.down & pad::A) != 0;
    return in;
}

ui::UiInput padToUiInputs(const PadState& now, const PadState& before, const ui::Mapping& map,
                          int touchW, int touchH) {
    ui::UiInput in;
    auto key = [&](uint32_t button, int code) {
        if (now.down & button) in.key(code);
    };
    key(pad::DUp, ui::keys::Up);
    key(pad::DDown, ui::keys::Down);
    key(pad::DLeft, ui::keys::Left);
    key(pad::DRight, ui::keys::Right);
    key(pad::A, ui::keys::Enter);
    if (now.down & (pad::B | pad::Start)) in.key(ui::keys::Escape);

    if (touchW > 0 && touchH > 0) {
        auto pointer = [&] {
            const float fx = static_cast<float>(now.touchX) * map.fbWidth / touchW;
            const float fy = static_cast<float>(now.touchY) * map.fbHeight / touchH;
            in.move(map.toVirtX(fx), map.toVirtY(fy));
        };
        if (now.touch) {
            pointer();
            if (!before.touch) in.press(ui::keys::Mouse1);
        } else if (before.touch) {
            in.release(ui::keys::Mouse1);
        }
    }
    return in;
}

} // namespace as3d
