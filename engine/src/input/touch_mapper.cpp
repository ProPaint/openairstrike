// Touch events to FrameInput: relative drag steering, auto-fire, on-screen buttons
// (as3d/input.h; the choices are in docs/spec/issues/100-touch-controls.md).
#include <algorithm>
#include <cmath>

#include "as3d/input.h"

namespace as3d {

namespace {

constexpr u32 kButtonBits[kTouchButtonCount] = {
    ACT_MISSILE,      // Missile
    ACT_POWERUP,      // PowerUp
    ACT_NEXT_MISSILE, // NextMissile
    ACT_NEXT_WEAPON,  // NextWeapon
    ACT_NEXT_POWERUP, // NextPowerUp
    0,                // Pause (an edge, not a bit)
};

constexpr const char* kButtonNames[kTouchButtonCount] = {
    "missile", "powerup", "next_missile", "next_weapon", "next_powerup", "pause",
};

constexpr float kVirtualW = 800.0f;
constexpr float kVirtualH = 600.0f;

TouchRect normalised(float x, float y, float w, float h, float fw, float fh) {
    TouchRect r;
    r.x = x / fw;
    r.y = y / fh;
    r.w = w / fw;
    r.h = h / fh;
    return r;
}

} // namespace

const char* touchButtonName(TouchButton b) {
    int i = static_cast<int>(b);
    return (i >= 0 && i < kTouchButtonCount) ? kButtonNames[i] : "?";
}

TouchLayout computeTouchLayout(int fbWidth, int fbHeight, const SafeInsets& insets) {
    TouchLayout L;
    L.fbWidth = std::max(fbWidth, 1);
    L.fbHeight = std::max(fbHeight, 1);
    const float fw = static_cast<float>(L.fbWidth);
    const float fh = static_cast<float>(L.fbHeight);
    const float inL = std::min(std::max(static_cast<float>(insets.left), 0.0f), fw * 0.25f);
    const float inR = std::min(std::max(static_cast<float>(insets.right), 0.0f), fw * 0.25f);
    const float inT = std::min(std::max(static_cast<float>(insets.top), 0.0f), fh * 0.25f);
    const float inB = std::min(std::max(static_cast<float>(insets.bottom), 0.0f), fh * 0.25f);

    // The play-field, by the rule of the 2D layer (ui::computeMapping): wider than 4:3 has
    // bars left and right, 5:4 to 4:3 is stretched, narrower is letterboxed.
    float pfX = 0, pfY = 0, pfW = fw, pfH = fh;
    if (fw * 3.0f > fh * 4.0f) {
        pfW = fh * 4.0f / 3.0f;
        pfX = (fw - pfW) * 0.5f;
    } else if (fw * 4.0f < fh * 5.0f) {
        pfH = fw * 3.0f / 4.0f;
        pfY = (fh - pfH) * 0.5f;
    }
    L.playField = normalised(pfX, pfY, pfW, pfH, fw, fh);

    const float margin = 0.03f * fh;
    const float gap = 0.025f * fh;
    const float rightBarX = pfX + pfW;
    const float rightBar = (fw - inR) - rightBarX;
    const float leftBar = pfX - inL;

    float big = std::min(0.15f * fh, rightBar - margin);
    L.outside = big >= 0.10f * fh;

    float px[kTouchButtonCount], py[kTouchButtonCount], ps[kTouchButtonCount];
    float colCentre, bottom;
    if (L.outside) {
        L.alpha = 0.85f;
        colCentre = rightBarX + rightBar * 0.5f;
        bottom = fh - inB - margin;
    } else {
        L.alpha = 0.4f;
        big = 0.12f * fh;
        float colRight = std::min(pfX + pfW, fw - inR) - margin;
        colCentre = colRight - big * 0.5f;
        bottom = std::min(pfY + pfH, fh - inB) - margin;
    }
    const float small = big * 0.75f;
    // The column, bottom up: missile (the most used), power-up, then the three switches.
    const TouchButton order[5] = {TouchButton::Missile, TouchButton::PowerUp, TouchButton::NextMissile,
                                  TouchButton::NextWeapon, TouchButton::NextPowerUp};
    float y = bottom;
    for (int k = 0; k < 5; ++k) {
        int b = static_cast<int>(order[k]);
        float s = k < 2 ? big : small;
        y -= s;
        px[b] = colCentre - s * 0.5f;
        py[b] = y;
        ps[b] = s;
        y -= gap;
    }
    // Pause: the top of the left bar when it has room, else the top of the right bar
    // (outside), or the top centre of the play-field (inside, clear of the HUD bars).
    const int p = static_cast<int>(TouchButton::Pause);
    if (L.outside) {
        float s = std::min(0.10f * fh, big);
        ps[p] = s;
        py[p] = inT + margin;
        if (leftBar - margin >= s) px[p] = inL + (leftBar - s) * 0.5f;
        else px[p] = colCentre - s * 0.5f;
    } else {
        float s = 0.08f * fh;
        ps[p] = s;
        px[p] = pfX + pfW * 0.5f - s * 0.5f;
        py[p] = std::max(pfY, inT) + margin * 0.5f;
    }
    for (int b = 0; b < kTouchButtonCount; ++b) {
        L.buttons[b] = normalised(px[b], py[b], ps[b], ps[b], fw, fh);
        float g = gap * 0.5f;
        L.hit[b] = normalised(px[b] - g, py[b] - g, ps[b] + 2 * g, ps[b] + 2 * g, fw, fh);
    }
    return L;
}

TouchMapper::TouchMapper() { setScreen(800, 600); }

void TouchMapper::setScreen(int fbWidth, int fbHeight, const SafeInsets& insets) {
    layout_ = computeTouchLayout(fbWidth, fbHeight, insets);
}

int TouchMapper::findFinger(long long id) const {
    for (int i = 0; i < kMaxFingers; ++i) {
        if (fingers_[i].down && fingers_[i].id == id) return i;
    }
    return -1;
}

int TouchMapper::hitButton(float x, float y) const {
    for (int b = kTouchButtonCount - 1; b >= 0; --b) { // pause first
        if (layout_.hit[b].contains(x, y)) return b;
    }
    return -1;
}

float TouchMapper::virtualPerNormX() const {
    return layout_.playField.w > 0 ? kVirtualW / layout_.playField.w : kVirtualW;
}
float TouchMapper::virtualPerNormY() const {
    return layout_.playField.h > 0 ? kVirtualH / layout_.playField.h : kVirtualH;
}

int TouchMapper::fingersDown() const {
    int n = 0;
    for (const Finger& f : fingers_) n += f.down ? 1 : 0;
    return n;
}

bool TouchMapper::buttonHeld(TouchButton b) const {
    int i = static_cast<int>(b);
    for (const Finger& f : fingers_) {
        if (f.down && !f.consumed && f.button == i) return true;
    }
    return false;
}

void TouchMapper::touchEvent(long long id, TouchPhase phase, float x, float y) {
    if (!(x == x) || !(y == y)) return; // NaN
    x = std::min(std::max(x, 0.0f), 1.0f);
    y = std::min(std::max(y, 0.0f), 1.0f);
    int idx = findFinger(id);
    if (phase == TouchPhase::Down && idx >= 0) phase = TouchPhase::Move; // repeated down
    switch (phase) {
        case TouchPhase::Down: {
            for (int i = 0; i < kMaxFingers; ++i) {
                if (!fingers_[i].down) {
                    idx = i;
                    break;
                }
            }
            if (idx < 0) return; // more fingers than slots: ignored
            Finger& f = fingers_[idx];
            f = Finger();
            f.down = true;
            f.id = id;
            f.x = x;
            f.y = y;
            if (paused_) {
                // A tap continues the paused game and does nothing else until lifted.
                f.consumed = true;
                pauseLatched_ = true;
                return;
            }
            f.button = hitButton(x, y);
            confirmLatched_ = true; // a tap also closes a tutorial hint box
            if (f.button == static_cast<int>(TouchButton::Pause)) {
                pauseLatched_ = true;
                return;
            }
            if (f.button >= 0) latched_ |= kButtonBits[f.button];
            else if (dragFinger_ < 0) {
                dragFinger_ = idx;
                targetValid_ = false; // set from the helicopter's position at the next frame
                pendingDx_ = pendingDy_ = 0;
            }
            if (settings_.autoFire) latched_ |= ACT_FIRE;
            return;
        }
        case TouchPhase::Move: {
            if (idx < 0) return;
            Finger& f = fingers_[idx];
            if (idx == dragFinger_) {
                pendingDx_ += (x - f.x) * virtualPerNormX() * settings_.gain;
                pendingDy_ += (y - f.y) * virtualPerNormY() * settings_.gain;
            }
            f.x = x;
            f.y = y;
            return;
        }
        case TouchPhase::Up:
        case TouchPhase::Cancel: {
            if (idx < 0) return;
            fingers_[idx] = Finger();
            if (idx == dragFinger_) {
                dragFinger_ = -1;
                targetValid_ = false;
                pendingDx_ = pendingDy_ = 0;
            }
            return;
        }
    }
}

void TouchMapper::releaseAll() {
    for (Finger& f : fingers_) f = Finger();
    dragFinger_ = -1;
    targetValid_ = false;
    pendingDx_ = pendingDy_ = 0;
    latched_ = 0;
    confirmLatched_ = false;
}

void TouchMapper::setPlayerScreen(bool valid, float vx, float vy) {
    playerValid_ = valid && vx == vx && vy == vy;
    playerX_ = vx;
    playerY_ = vy;
}

FrameInput TouchMapper::takeFrame() {
    FrameInput out;
    u32 held = 0;
    for (const Finger& f : fingers_) {
        if (!f.down || f.consumed) continue;
        if (f.button == static_cast<int>(TouchButton::Pause)) continue;
        if (f.button >= 0) held |= kButtonBits[f.button];
        if (settings_.autoFire) held |= ACT_FIRE;
    }
    if (dragFinger_ >= 0 && playerValid_ && !paused_) {
        if (!targetValid_) {
            targetX_ = playerX_;
            targetY_ = playerY_;
            targetValid_ = true;
            pendingDx_ = pendingDy_ = 0;
        }
        targetX_ += pendingDx_;
        targetY_ += pendingDy_;
        pendingDx_ = pendingDy_ = 0;
        // The target never runs far ahead of a helicopter held back by the screen edge or
        // the script's limits, so reversing the finger reverses the helicopter at once.
        const float lead = settings_.maxLead;
        targetX_ = std::min(std::max(targetX_, playerX_ - lead), playerX_ + lead);
        targetY_ = std::min(std::max(targetY_, playerY_ - lead), playerY_ + lead);
        targetX_ = std::min(std::max(targetX_, 0.0f), kVirtualW);
        targetY_ = std::min(std::max(targetY_, 0.0f), kVirtualH);
        float vx = 0, vy = 0;
        if (prevValid_) {
            vx = playerX_ - prevX_;
            vy = playerY_ - prevY_;
            // A respawn or a camera cut is not a velocity.
            if (std::fabs(vx) > 40.0f || std::fabs(vy) > 40.0f) vx = vy = 0;
        }
        float ex = targetX_ - (playerX_ + vx * settings_.lookAhead);
        float ey = targetY_ - (playerY_ + vy * settings_.lookAhead);
        const float dz = settings_.deadZone;
        if (ex > dz) held |= ACT_RIGHT;
        else if (ex < -dz) held |= ACT_LEFT;
        if (ey < -dz) held |= ACT_FORWARD; // up the screen
        else if (ey > dz) held |= ACT_BACKWARD;
    } else if (dragFinger_ >= 0 && !playerValid_) {
        // No helicopter to steer (dead, respawning): start again from wherever it appears.
        targetValid_ = false;
        pendingDx_ = pendingDy_ = 0;
    }
    prevValid_ = playerValid_;
    prevX_ = playerX_;
    prevY_ = playerY_;

    out.held[0] = held | latched_;
    out.confirm = confirmLatched_;
    out.pausePressed = pauseLatched_;
    latched_ = 0;
    confirmLatched_ = false;
    pauseLatched_ = false;
    return out;
}

} // namespace as3d
