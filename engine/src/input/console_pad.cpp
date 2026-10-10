// Console pad to FrameInput and UiInput (as3d/console_pad.h).
#include "as3d/console_pad.h"

#include <algorithm>
#include <cctype>
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

namespace {
std::string lower(const std::string& s) {
    std::string o;
    for (char c : s) o += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
}
} // namespace

bool padButtonFromName(const std::string& name, uint32_t* out) {
    static const struct { const char* n; uint32_t bit; } kNames[] = {
        {"a", pad::A}, {"b", pad::B}, {"x", pad::X}, {"y", pad::Y}, {"l", pad::L}, {"r", pad::R},
        {"zl", pad::ZL}, {"zr", pad::ZR}, {"start", pad::Start}, {"select", pad::Select},
        {"none", 0}, {"", 0}};
    const std::string n = lower(name);
    for (const auto& e : kNames)
        if (n == e.n) {
            *out = e.bit;
            return true;
        }
    return false;
}

bool applyBindingOption(PadBindings& b, const std::string& key, const std::string& value) {
    const std::string k = lower(key);
    if (k.compare(0, 5, "bind_") != 0) return false;
    uint32_t* slot = nullptr;
    const std::string what = k.substr(5);
    if (what == "fire") slot = &b.fire;
    else if (what == "missile") slot = &b.missile;
    else if (what == "powerup") slot = &b.powerUp;
    else if (what == "nextweapon") slot = &b.nextWeapon;
    else if (what == "nextmissile") slot = &b.nextMissile;
    else if (what == "nextpowerup") slot = &b.nextPowerUp;
    else if (what == "pause") slot = &b.pause;
    uint32_t bit = 0;
    if (!slot || !padButtonFromName(value, &bit)) return false;
    *slot = bit;
    return true;
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

uint32_t gamepadHeldBits(const GamepadSnapshot& s, uint32_t before, const GamepadOptions& o) {
    using namespace gamepad;
    static const struct { int button; uint32_t bit; } kButtons[] = {
        {A, pad::A}, {B, pad::B}, {X, pad::X}, {Y, pad::Y}, {Back, pad::Select}, {Start, pad::Start},
        {LeftShoulder, pad::L}, {RightShoulder, pad::R}, {DUp, pad::DUp}, {DDown, pad::DDown},
        {DLeft, pad::DLeft}, {DRight, pad::DRight}};
    uint32_t held = 0;
    for (const auto& b : kButtons)
        if (s.buttons[b.button]) held |= b.bit;
    if (s.axes[TriggerLeft] > o.triggerPress) held |= pad::ZL;
    if (s.axes[TriggerRight] > o.triggerPress) held |= pad::ZR;
    // The left stick as the D-pad: beyond stickPress, or beyond stickRelease while held before.
    // The stick's own bit is not kept apart from the D-pad's: with the D-pad held the stick
    // direction keeps the lower threshold, which only makes it stickier.
    auto dir = [&](int value, uint32_t bit) {
        const int threshold = (before & bit) ? o.stickRelease : o.stickPress;
        if (value > threshold) held |= bit;
    };
    const int x = s.axes[LeftX], y = s.axes[LeftY];
    dir(x, pad::DRight);
    dir(-x, pad::DLeft);
    dir(y, pad::DDown); // y down
    dir(-y, pad::DUp);
    return held;
}

PadBindings gamepadBindings() {
    PadBindings b = defaultBindings();
    b.nextMissile |= pad::L;
    b.nextPowerUp |= pad::ZL;
    return b;
}

namespace {
constexpr uint32_t kDirBits[4] = {pad::DUp, pad::DDown, pad::DLeft, pad::DRight};
constexpr int kDirKeys[4] = {ui::keys::Up, ui::keys::Down, ui::keys::Left, ui::keys::Right};
} // namespace

bool GamepadInput::update(uint32_t held, float dt) {
    before_ = now_;
    PadState p;
    p.held = held;
    p.down = held & ~before_.held;
    p.up = before_.held & ~held;
    now_ = p;
    pendingDown_ |= p.down;
    for (int i = 0; i < 4; ++i) {
        repeats_[i] = 0;
        if (!(held & kDirBits[i])) {
            heldFor_[i] = 0;
            continue;
        }
        if (p.down & kDirBits[i]) continue; // the press itself is the first key
        const float t0 = heldFor_[i], t1 = heldFor_[i] + dt;
        heldFor_[i] = t1;
        // Repeats at kRepeatDelay, then every kRepeatEvery: those that fall in (t0, t1].
        auto count = [](float t) { return t < kRepeatDelay ? 0 : 1 + static_cast<int>((t - kRepeatDelay) / kRepeatEvery); };
        repeats_[i] = std::min(count(t1) - count(t0), 3);
    }
    return p.down != 0;
}

void GamepadInput::releaseAll() {
    before_ = now_ = PadState();
    pendingDown_ = 0;
    for (int i = 0; i < 4; ++i) {
        heldFor_[i] = 0;
        repeats_[i] = 0;
    }
}

FrameInput GamepadInput::takeFrame(const PadBindings& bindings) {
    PadState p = now_;
    p.down = pendingDown_;
    p.held |= pendingDown_; // a tap shorter than a step still holds for one
    pendingDown_ = 0;
    return padToFrameInput(p, bindings);
}

ui::UiInput GamepadInput::menuInputs(bool back) const {
    PadState p = now_;
    if (!back) p.down &= ~(pad::B | pad::Start);
    ui::UiInput in = padToUiInputs(p, before_, ui::Mapping(), 0, 0); // no touch screen here
    if (back && (p.down & pad::Select)) in.key(ui::keys::Escape);
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < repeats_[i]; ++k) in.key(kDirKeys[i]);
    return in;
}

ui::UiInput GamepadInput::playingInputs(const PadBindings& bindings) const {
    ui::UiInput in;
    if ((bindings.pause && (now_.down & bindings.pause)) || (now_.down & pad::Select)) in.key(ui::keys::Escape);
    return in;
}

bool parsePadScript(const std::string& text, std::vector<PadScriptPress>& out) {
    static const struct { const char* n; uint32_t bit; } kDpad[] = {
        {"up", pad::DUp}, {"down", pad::DDown}, {"left", pad::DLeft}, {"right", pad::DRight}};
    size_t pos = 0;
    unsigned last = 0;
    while (pos < text.size()) {
        size_t comma = text.find(',', pos);
        if (comma == std::string::npos) comma = text.size();
        std::string item = text.substr(pos, comma - pos);
        pos = comma + 1;
        while (!item.empty() && item.front() == ' ') item.erase(item.begin());
        while (!item.empty() && item.back() == ' ') item.pop_back();
        if (item.empty()) continue;
        const size_t colon = item.find(':');
        if (colon == std::string::npos || colon == 0) return false;
        char* end = nullptr;
        const unsigned long f = std::strtoul(item.c_str(), &end, 10);
        if (end != item.c_str() + colon || f < last) return false;
        PadScriptPress p;
        p.frame = static_cast<unsigned>(f);
        const std::string what = lower(item.substr(colon + 1));
        if (!what.empty() && what[0] == 't' && what.size() > 1 && std::isdigit(static_cast<unsigned char>(what[1]))) {
            const char* s = what.c_str() + 1;
            char* e = nullptr;
            const long x = std::strtol(s, &e, 10);
            if (*e != '/') return false;
            const long y = std::strtol(e + 1, &e, 10);
            if (*e != '\0') return false;
            p.touch = true;
            p.x = static_cast<int>(x);
            p.y = static_cast<int>(y);
        } else {
            size_t a = 0;
            while (a <= what.size()) {
                size_t plus = what.find('+', a);
                if (plus == std::string::npos) plus = what.size();
                const std::string name = what.substr(a, plus - a);
                a = plus + 1;
                uint32_t bit = 0;
                bool found = false;
                for (const auto& d : kDpad)
                    if (name == d.n) {
                        bit = d.bit;
                        found = true;
                    }
                if (!found && (name.empty() || name == "none" || !padButtonFromName(name, &bit))) return false;
                p.buttons |= bit;
            }
        }
        out.push_back(p);
        last = p.frame;
    }
    return true;
}

void applyPadScript(const std::vector<PadScriptPress>& script, unsigned frame, unsigned holdFrames, PadState& pad) {
    for (const PadScriptPress& p : script) {
        if (p.frame > frame) break;
        if (frame >= p.frame + holdFrames) continue;
        pad.held |= p.buttons;
        if (p.touch) {
            pad.held |= pad::Touch;
            pad.touch = true;
            pad.touchX = p.x;
            pad.touchY = p.y;
        }
    }
}

} // namespace as3d
