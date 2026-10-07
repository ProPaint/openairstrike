// Console pad model: bindings to p_action bits, circle pad dead zone, edges, the touch
// screen as a pointer, and the key codes of the front end.
#include "doctest.h"

#include "as3d/console_pad.h"

using namespace as3d;

namespace {
u32 bit(InputAction a) { return inputActionBit(a); }

PadState held(uint32_t buttons) {
    PadState p;
    p.held = buttons;
    return p;
}
} // namespace

TEST_CASE("pad: button bits equal libctru's KEY_* values") {
    CHECK(pad::A == 1u);
    CHECK(pad::Start == 8u);
    CHECK(pad::DDown == 128u);
    CHECK(pad::ZR == (1u << 15));
    CHECK(pad::Touch == (1u << 20));
    CHECK(pad::CPadDown == 0x80000000u);
}

TEST_CASE("pad: default bindings drive the actions and never need L") {
    const PadBindings b = defaultBindings();
    CHECK(padToFrameInput(held(pad::A), b).held[0] == bit(InputAction::Fire));
    CHECK(padToFrameInput(held(pad::B), b).held[0] == bit(InputAction::Missile));
    CHECK(padToFrameInput(held(pad::X), b).held[0] == bit(InputAction::PowerUp));
    CHECK(padToFrameInput(held(pad::Y), b).held[0] == bit(InputAction::NextWeapon));
    CHECK(padToFrameInput(held(pad::R), b).held[0] == bit(InputAction::NextMissile));
    CHECK(padToFrameInput(held(pad::ZR), b).held[0] == bit(InputAction::NextPowerUp));
    CHECK(padToFrameInput(held(pad::L), b).held[0] == 0u);
    CHECK(padToFrameInput(held(pad::A | pad::B), b).held[0] ==
          (bit(InputAction::Fire) | bit(InputAction::Missile)));
    CHECK(padToFrameInput(held(0), b).held[1] == 0u);
}

TEST_CASE("pad: D-pad and circle pad steer, with a dead zone") {
    const PadBindings b = defaultBindings();
    CHECK(padToFrameInput(held(pad::DUp), b).held[0] == bit(InputAction::Forward));
    CHECK(padToFrameInput(held(pad::DDown | pad::DLeft), b).held[0] ==
          (bit(InputAction::Backward) | bit(InputAction::Left)));
    CHECK(padToFrameInput(held(pad::DRight), b).held[0] == bit(InputAction::Right));

    PadState s;
    s.cx = 40;
    s.cy = -40; // exactly the dead zone: still centred
    CHECK(padToFrameInput(s, b).held[0] == 0u);
    s.cx = 41;
    s.cy = 100;
    CHECK(padToFrameInput(s, b).held[0] == (bit(InputAction::Right) | bit(InputAction::Forward)));
    s.cx = -100;
    s.cy = -41;
    CHECK(padToFrameInput(s, b).held[0] == (bit(InputAction::Left) | bit(InputAction::Backward)));
    s.cx = 20;
    s.cy = 0;
    CHECK(padToFrameInput(s, b, 10).held[0] == bit(InputAction::Right));
    // D-pad and circle pad together do not cancel
    s.cx = 100;
    CHECK(padToFrameInput(s, b).held[0] == bit(InputAction::Right));
}

TEST_CASE("pad: pause and confirm are edges") {
    const PadBindings b = defaultBindings();
    PadState p;
    p.held = pad::Start | pad::A;
    FrameInput in = padToFrameInput(p, b);
    CHECK_FALSE(in.pausePressed);
    CHECK_FALSE(in.confirm);
    p.down = pad::Start;
    CHECK(padToFrameInput(p, b).pausePressed);
    p.down = pad::A;
    CHECK(padToFrameInput(p, b).confirm);
}

TEST_CASE("pad: front end keys from down edges") {
    const ui::Mapping map = ui::computeMapping(800, 600);
    PadState before, now;
    now.down = pad::DUp | pad::A;
    ui::UiInput in = padToUiInputs(now, before, map, 320, 240);
    REQUIRE(in.events.size() == 4);
    CHECK(in.events[0].type == ui::UiEvent::Type::Press);
    CHECK(in.events[0].code == ui::keys::Up);
    CHECK(in.events[1].type == ui::UiEvent::Type::Release);
    CHECK(in.events[2].code == ui::keys::Enter);

    now = PadState();
    now.held = pad::DDown; // held without an edge: nothing
    CHECK(padToUiInputs(now, before, map, 320, 240).events.empty());

    now = PadState();
    now.down = pad::B;
    in = padToUiInputs(now, before, map, 320, 240);
    REQUIRE(in.events.size() == 2);
    CHECK(in.events[0].code == ui::keys::Escape);
    now.down = pad::Start;
    CHECK(padToUiInputs(now, before, map, 320, 240).events[0].code == ui::keys::Escape);
    now.down = pad::DLeft | pad::DRight | pad::DDown;
    CHECK(padToUiInputs(now, before, map, 320, 240).events.size() == 6);
}

TEST_CASE("pad: touch screen becomes a pointer press, drag and release") {
    const ui::Mapping map = ui::computeMapping(800, 600);
    PadState idle, t1, t2;
    t1.touch = true;
    t1.touchX = 160;
    t1.touchY = 120;
    t2 = t1;
    t2.touchX = 320;
    t2.touchY = 240;

    ui::UiInput in = padToUiInputs(t1, idle, map, 320, 240);
    REQUIRE(in.events.size() == 2);
    CHECK(in.events[0].type == ui::UiEvent::Type::PointerMove);
    CHECK(in.events[0].x == doctest::Approx(400.0f));
    CHECK(in.events[0].y == doctest::Approx(300.0f));
    CHECK(in.events[1].type == ui::UiEvent::Type::Press);
    CHECK(in.events[1].code == ui::keys::Mouse1);

    in = padToUiInputs(t2, t1, map, 320, 240); // dragging
    REQUIRE(in.events.size() == 1);
    CHECK(in.events[0].type == ui::UiEvent::Type::PointerMove);
    CHECK(in.events[0].x == doctest::Approx(800.0f));

    in = padToUiInputs(idle, t2, map, 320, 240);
    REQUIRE(in.events.size() == 1);
    CHECK(in.events[0].type == ui::UiEvent::Type::Release);
    CHECK(in.events[0].code == ui::keys::Mouse1);

    CHECK(padToUiInputs(idle, idle, map, 320, 240).events.empty());
}
