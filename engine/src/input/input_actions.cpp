// Action names, the frame input and the scripted test pilot (as3d/input.h).
#include "as3d/input.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

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
    p.mouseSteer = mouseSteer;
    p.mouse[0] = mouseDx;
    p.mouse[1] = mouseDy;
    return p;
}

u32 mouseControlBits(float mouseX, float mouseY, float heliX, float heliY) {
    constexpr float kDeadZone = 20.0f;
    u32 bits = 0;
    if (mouseX > heliX + kDeadZone) bits |= ACT_RIGHT;
    if (mouseX < heliX - kDeadZone) bits |= ACT_LEFT;
    if (mouseY < heliY - kDeadZone) bits |= ACT_FORWARD;
    if (mouseY > heliY + kDeadZone) bits |= ACT_BACKWARD;
    return bits;
}

FrameInput mergeFrameInput(const FrameInput& a, const FrameInput& b) {
    FrameInput out;
    for (int k = 0; k < kMaxPlayers; ++k) out.held[k] = a.held[k] | b.held[k];
    out.confirm = a.confirm || b.confirm;
    out.pausePressed = a.pausePressed || b.pausePressed;
    out.mouseSteer = a.mouseSteer || b.mouseSteer;
    out.mouseDx = a.mouseDx + b.mouseDx;
    out.mouseDy = a.mouseDy + b.mouseDy;
    return out;
}

namespace {

bool isPickupName(const std::string& n) {
    if (n == "item_speeddown") return false;
    return n.compare(0, 5, "item_") == 0 || n.compare(0, 5, "ammo_") == 0 || n.compare(0, 6, "bonus_") == 0;
}

// Presses toward the velocity that reaches `target` smoothly: the player script accelerates
// by 1000 units/s^2 while a key is held, up to 150 units/s (engine-behaviour.md 7.3).
u32 steer(float pos, float vel, float target, u32 plus, u32 minus) {
    float want = std::max(-150.0f, std::min(150.0f, 3.0f * (target - pos)));
    if (vel < want - 12.0f) return plus;
    if (vel > want + 12.0f) return minus;
    return 0u;
}

} // namespace

FrameInput botInput(const World& w, u32 frame) {
    FrameInput in;
    in.confirm = true;
    u32 a = ACT_FIRE;
    if ((frame / 30) % 2) a |= ACT_MISSILE;
    if (frame % 600 == 300) a |= ACT_POWERUP;
    const int pi = w.playerEntityIndex(0);
    if (pi < 0 || w.entity(pi).f(F_DEAD) != 0.0f) {
        in.held[0] = in.held[1] = a;
        return in;
    }
    const Entity& p = w.entity(pi);
    const Vec3 o = p.v3(F_ORIGIN), v = p.v3(F_VELOCITY);
    const float mp = w.mapPos();
    const float cx = w.camera().field[0];
    const float kTwoPi = 6.2831853f;
    // Home: the lower middle of the play-field, weaving gently.
    float tx = cx + 50.0f * std::sin(static_cast<float>(frame) * kTwoPi / 480.0f);
    float ty = mp + 95.0f + 25.0f * std::sin(static_cast<float>(frame) * kTwoPi / 900.0f);

    // Aim: line up under the toughest on-screen enemy ahead (bosses and their parts are
    // often attached children, so every slot is looked at), the nearest among equals.
    float bestHealth = 0.0f, bestDist = 1.0e9f;
    for (int i = 0; i < kMaxEntitySlots; ++i) {
        if (!w.validIndex(i)) continue;
        const Entity& e = w.entity(i);
        if ((e.rt & RT_REMOVED) || !(e.rt & RT_COLLIDABLE) || e.f(F_CLASS) != kClassEnemy) continue;
        if (e.f(F_DEAD) != 0.0f || !(e.f(F_HEALTH) > 0.0f)) continue;
        Vec3 r = e.v3(F_BASE_ORIGIN) - o;
        if (r.y < 40.0f || r.y > 450.0f || std::fabs(e.f(F_BASE_ORIGIN) - cx) > 200.0f) continue;
        float d = std::sqrt(r.x * r.x + r.y * r.y);
        if (e.maxHealth > bestHealth || (e.maxHealth == bestHealth && d < bestDist)) {
            bestHealth = e.maxHealth;
            bestDist = d;
            tx = e.f(F_BASE_ORIGIN) + 12.0f * std::sin(static_cast<float>(frame) * kTwoPi / 120.0f);
        }
    }

    // Pick-ups ahead and not far to the side: fly over the nearest.
    float best = 1.0e9f;
    const std::vector<int> list = w.listEntities();
    for (int i : list) {
        const Entity& e = w.entity(i);
        if ((e.rt & RT_REMOVED) || e.parent >= 0 || e.touchMode != 2 || !isPickupName(e.name)) continue;
        Vec3 r = e.v3(F_ORIGIN) - o;
        if (r.y < -30.0f || r.y > 260.0f || std::fabs(e.f(F_ORIGIN) - cx) > 170.0f) continue;
        float d = std::sqrt(r.x * r.x + r.y * r.y);
        if (d < best) {
            best = d;
            tx = e.f(F_ORIGIN);
            ty = std::max(mp + 40.0f, std::min(e.f(F_ORIGIN + 1), mp + 170.0f));
        }
    }

    // Dodge: enemy fire and rammers whose path passes near the player in the next 0.6 s.
    float threatDx = 0.0f, closest = 45.0f;
    bool threat = false;
    for (int i : list) {
        const Entity& e = w.entity(i);
        if ((e.rt & RT_REMOVED) || e.touchMode != 2 || isPickupName(e.name)) continue;
        if (e.f(F_CLASS) != kClassProjectile && e.f(F_CLASS) != kClassEnemy) continue;
        if (e.f(F_DEAD) != 0.0f) continue;
        Vec3 r = e.v3(F_ORIGIN) - o;
        if (r.y < -60.0f || r.y > 300.0f) continue;
        Vec3 rv = e.v3(F_VELOCITY) - v;
        for (int k = 0; k <= 6; ++k) {
            float t = 0.1f * static_cast<float>(k);
            float dx = r.x + rv.x * t, dy = r.y + rv.y * t;
            float d = std::sqrt(dx * dx + dy * dy);
            if (d < closest) {
                closest = d;
                threatDx = dx;
                threat = true;
            }
        }
    }
    if (threat) {
        float side = threatDx > 0.0f ? -1.0f : 1.0f;
        if (std::fabs(o.x - cx) > 90.0f) side = o.x > cx ? -1.0f : 1.0f; // not into the edge
        tx = o.x + side * 90.0f;
    }

    a |= steer(o.x, v.x, tx, ACT_RIGHT, ACT_LEFT);
    a |= steer(o.y - mp, v.y, ty - mp, ACT_FORWARD, ACT_BACKWARD);
    in.held[0] = in.held[1] = a;
    return in;
}

FrameInput botInput(u32 frame) {
    // as3d_sim --bot and as3d_game --bot both call this. A 8 s cycle symmetric about the
    // start position, so the helicopter stays around the lower middle instead of drifting
    // into a corner: right 1 s, left 2 s, right 1 s, then forward and back 0.6 s each.
    FrameInput in;
    u32 a = ACT_FIRE;
    const u32 t = frame % 480;
    if (t < 60 || (t >= 180 && t < 240)) a |= ACT_RIGHT;
    else if (t >= 60 && t < 180) a |= ACT_LEFT;
    else if (t >= 260 && t < 296) a |= ACT_FORWARD;
    else if (t >= 320 && t < 356) a |= ACT_BACKWARD;
    if ((frame / 30) % 2) a |= ACT_MISSILE;
    if (frame % 600 == 300) a |= ACT_POWERUP;
    in.held[0] = in.held[1] = a;
    in.confirm = true;
    return in;
}

} // namespace as3d
