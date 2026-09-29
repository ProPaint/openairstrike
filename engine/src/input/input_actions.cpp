// Action names, the frame input and the scripted test pilot (as3d/input.h).
#include "as3d/input.h"

namespace as3d {

namespace {
struct ActionInfo {
    const char* name;
    u32 bit;
};
constexpr ActionInfo kActions[kInputActionCount] = {
    {"fire", ACT_FIRE},
    {"missile", ACT_MISSILE},
    {"powerup", ACT_POWERUP},
    {"forward", ACT_FORWARD},
    {"backward", ACT_BACKWARD},
    {"left", ACT_LEFT},
    {"right", ACT_RIGHT},
    {"next_missile", ACT_NEXT_MISSILE},
    {"next_powerup", ACT_NEXT_POWERUP},
    {"next_weapon", ACT_NEXT_WEAPON},
    {"confirm", 0},
    {"pause", 0},
};
} // namespace

const char* inputActionName(InputAction a) {
    int i = static_cast<int>(a);
    return (i >= 0 && i < kInputActionCount) ? kActions[i].name : "?";
}

bool inputActionFromName(const std::string& name, InputAction& out) {
    for (int i = 0; i < kInputActionCount; ++i) {
        if (name == kActions[i].name) {
            out = static_cast<InputAction>(i);
            return true;
        }
    }
    return false;
}

u32 inputActionBit(InputAction a) {
    int i = static_cast<int>(a);
    return (i >= 0 && i < kInputActionCount) ? kActions[i].bit : 0u;
}

PlayerInput FrameInput::toPlayerInput() const {
    PlayerInput p;
    for (int k = 0; k < kMaxPlayers; ++k) p.action[k] = held[k];
    p.confirm = confirm;
    return p;
}

FrameInput mergeFrameInput(const FrameInput& a, const FrameInput& b) {
    FrameInput out;
    for (int k = 0; k < kMaxPlayers; ++k) out.held[k] = a.held[k] | b.held[k];
    out.confirm = a.confirm || b.confirm;
    out.pausePressed = a.pausePressed || b.pausePressed;
    return out;
}

FrameInput botInput(u32 frame) {
    // Must stay identical to the pilot of apps/sim_tool/main.cpp (--bot).
    FrameInput in;
    u32 a = ACT_FIRE;
    if ((frame / 120) % 2) a |= ACT_LEFT;
    else a |= ACT_RIGHT;
    if ((frame / 30) % 2) a |= ACT_MISSILE;
    if (frame % 600 == 300) a |= ACT_POWERUP;
    in.held[0] = in.held[1] = a;
    in.confirm = true;
    return in;
}

} // namespace as3d
