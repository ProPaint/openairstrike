// Touch events to FrameInput: relative drag steering, auto-fire, on-screen buttons
// (as3d/input.h; the choices are in docs/spec/issues/100-touch-controls.md and, for the round
// button layout, 140-android-polish.md).
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
constexpr float kPi = 3.14159265f;

// Physical sizes (millimetres) of the round buttons.
constexpr float kMainMm = 12.5f;     // missile and power-up
constexpr float kSatelliteMm = 9.0f; // next missile, next weapon, next power-up
constexpr float kPauseMm = 8.0f;
constexpr float kGapMm = 1.2f;       // between neighbouring buttons
constexpr float kEdgeMm = 2.0f;      // from the screen edges (and the cutout insets)
constexpr float kFieldGapMm = 0.8f;  // from the 4:3 field when the buttons sit in the bars
constexpr float kMinTouchMm = 9.0f;  // smallest touch area across
constexpr float kPhoneHeightMm = 68.0f; // assumed screen height when the density is unknown

// The action cluster relative to its corner: x to the left is negative, y up is positive
// (right-handed layout; the left-handed one is its mirror image). Button order as TouchButton.
struct Cluster {
    float x[5], y[5], r[5];
    float width = 0, height = 0;
};

// Missile in the corner, power-up straight above it; each has its "next" satellite at angle
// `theta` (degrees, 90 = straight up, larger = more towards the screen centre), next weapon
// above power-up. Neighbours keep `g` between their edges.
Cluster makeCluster(float D, float s, float g, float thetaDeg) {
    Cluster c;
    const float th = thetaDeg * kPi / 180.0f;
    const float dx = std::cos(th), dy = std::sin(th);
    const float rs = D * 0.5f + g + s * 0.5f; // main to satellite centre distance
    const int M = static_cast<int>(TouchButton::Missile), P = static_cast<int>(TouchButton::PowerUp),
              NM = static_cast<int>(TouchButton::NextMissile), NW = static_cast<int>(TouchButton::NextWeapon),
              NP = static_cast<int>(TouchButton::NextPowerUp);
    c.x[M] = -D * 0.5f;
    c.y[M] = D * 0.5f;
    c.r[M] = D * 0.5f;
    c.x[NM] = c.x[M] + rs * dx;
    c.y[NM] = c.y[M] + rs * dy;
    c.r[NM] = s * 0.5f;
    c.x[P] = -D * 0.5f;
    c.y[P] = c.y[M] + D + g;
    float ddx = c.x[NM] - c.x[P];
    if (std::fabs(ddx) < rs) c.y[P] = std::max(c.y[P], c.y[NM] + std::sqrt(rs * rs - ddx * ddx));
    c.r[P] = D * 0.5f;
    c.x[NP] = c.x[P] + rs * dx;
    c.y[NP] = c.y[P] + rs * dy;
    c.r[NP] = s * 0.5f;
    c.x[NW] = c.x[P];
    c.y[NW] = c.y[P] + rs;
    c.r[NW] = s * 0.5f;
    const float ss = s + g;
    ddx = c.x[NP] - c.x[NW];
    if (std::fabs(ddx) < ss) c.y[NW] = std::max(c.y[NW], c.y[NP] + std::sqrt(ss * ss - ddx * ddx));
    for (int i = 0; i < 5; ++i) {
        c.width = std::max(c.width, -c.x[i] + c.r[i]);
        c.height = std::max(c.height, c.y[i] + c.r[i]);
    }
    return c;
}

} // namespace

const char* touchButtonName(TouchButton b) {
    int i = static_cast<int>(b);
    return (i >= 0 && i < kTouchButtonCount) ? kButtonNames[i] : "?";
}

int TouchLayout::hitTest(float nx, float ny) const {
    const float px = nx * static_cast<float>(fbWidth), py = ny * static_cast<float>(fbHeight);
    int best = -1;
    float bestScore = 2.0f;
    for (int b = 0; b < kTouchButtonCount; ++b) {
        const TouchCircle& h = hit[b];
        if (!(h.r > 0) || !h.contains(px, py)) continue;
        const float score = std::sqrt((px - h.x) * (px - h.x) + (py - h.y) * (py - h.y)) / h.r;
        if (score < bestScore) {
            bestScore = score;
            best = b;
        }
    }
    return best;
}

bool TouchLayout::hits(TouchButton b, float nx, float ny) const { return hitTest(nx, ny) == static_cast<int>(b); }

float touchFadeAlpha(float t, float rest) {
    if (!(t > kTouchFadeHold)) return 1.0f;
    const float u = (t - kTouchFadeHold) / kTouchFadeTime;
    if (u >= 1.0f) return rest;
    const float e = u * u * (3.0f - 2.0f * u);
    return 1.0f + (rest - 1.0f) * e;
}

void TouchFade::update(float dt, bool buttonHeld) {
    if (buttonHeld) idle_ = 0;
    else if (dt > 0) idle_ = std::min(idle_ + dt, 1.0e4f);
}

TouchLayout computeTouchLayout(int fbWidth, int fbHeight, const SafeInsets& insets) {
    TouchLayoutOptions o;
    o.insets = insets;
    return computeTouchLayout(fbWidth, fbHeight, o);
}

TouchLayout computeTouchLayout(int fbWidth, int fbHeight, const TouchLayoutOptions& o) {
    TouchLayout L;
    L.fbWidth = std::max(fbWidth, 1);
    L.fbHeight = std::max(fbHeight, 1);
    L.leftHanded = o.leftHanded;
    L.screen4x3 = o.screen4x3;
    const float fw = static_cast<float>(L.fbWidth);
    const float fh = static_cast<float>(L.fbHeight);
    // The left-handed layout is computed as the right-handed one of the mirrored screen.
    const SafeInsets& in0 = o.insets;
    const float insL = static_cast<float>(o.leftHanded ? in0.right : in0.left);
    const float insR = static_cast<float>(o.leftHanded ? in0.left : in0.right);
    const float inL = std::min(std::max(insL, 0.0f), fw * 0.25f);
    const float inR = std::min(std::max(insR, 0.0f), fw * 0.25f);
    const float inT = std::min(std::max(static_cast<float>(in0.top), 0.0f), fh * 0.25f);
    const float inB = std::min(std::max(static_cast<float>(in0.bottom), 0.0f), fh * 0.25f);

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
    L.playField = {pfX / fw, pfY / fh, pfW / fw, pfH / fh};

    const bool dpiKnown = o.dpi >= 80.0f && o.dpi <= 1200.0f;
    const float mm = dpiKnown ? o.dpi / 25.4f : fh / kPhoneHeightMm;
    L.pixelsPerMm = mm;
    // Physical sizes, capped for screens that are small in pixels for their density.
    float D = std::min(kMainMm * mm, 0.20f * fh);
    float s = std::min(kSatelliteMm * mm, 0.15f * fh);
    float g = std::min(kGapMm * mm, 0.012f * fh);
    const float edge = std::min(kEdgeMm * mm, 0.03f * fh);
    const float pauseD = std::min(kPauseMm * mm, 0.12f * fh);

    // Outside: the right bar, from the field (plus a small gap) to the screen edge.
    Cluster c;
    float x1 = fw - inR - edge, y1 = fh - inB - edge;
    {
        const float availW = x1 - (pfX + pfW + kFieldGapMm * mm);
        const float availH = y1 - (inT + edge);
        // The widest arc that fits; then a straight column; then a narrower column.
        for (float th = 145.0f; th >= 90.0f && !L.outside; th -= 5.0f) {
            c = makeCluster(D, s, g, th);
            L.outside = c.width <= availW && c.height <= availH;
        }
        if (!L.outside && availW >= s) {
            float d2 = std::min(D, availW);
            c = makeCluster(d2, s, g, 90.0f);
            float k = c.height > availH ? availH / c.height : 1.0f;
            if (s * k >= kMinTouchMm * mm * 0.8f) {
                c = makeCluster(d2 * k, s * k, g * k, 90.0f);
                L.outside = c.width <= availW + 0.5f;
                if (L.outside) {
                    D = d2 * k;
                    s *= k;
                    g *= k;
                }
            }
        }
    }
    if (!L.outside) {
        // Inside the field, in its bottom corner, below the HUD's power-up column (right) or
        // missile column (left, where the lives also sit at the bottom).
        x1 = std::min(pfX + pfW, fw - inR) - edge;
        y1 = std::min(pfY + pfH, fh - inB) - edge;
        if (o.leftHanded) y1 = std::min(y1, pfY + pfH * (548.0f / kVirtualH));
        const float top = pfY + pfH * ((o.leftHanded ? 285.0f : 210.0f) / kVirtualH);
        D = std::min(D, 0.17f * fh);
        s = std::min(s, 0.125f * fh);
        c = makeCluster(D, s, g, 145.0f);
        const float availH = y1 - top;
        if (c.height > availH && availH > 0) {
            const float k = availH / c.height;
            c = makeCluster(D * k, s * k, g * k, 145.0f);
            g *= k;
        }
    }
    L.alpha = !L.outside ? 0.35f : o.screen4x3 ? 0.7f : 0.5f;

    for (int b = 0; b < 5; ++b) {
        TouchCircle& t = L.circles[b];
        t.x = x1 + c.x[b];
        t.y = y1 - c.y[b];
        t.r = c.r[b];
    }
    TouchCircle& p = L.circles[static_cast<int>(TouchButton::Pause)];
    p.r = pauseD * 0.5f;
    if (L.outside) {
        // The top corner on the other side, clear of cutouts.
        p.x = inL + edge + p.r;
        p.y = inT + edge + p.r;
    } else {
        // Top centre of the field, between the HUD's health and score bars.
        p.x = pfX + pfW * 0.5f;
        p.y = std::max(pfY, inT) + edge * 0.5f + p.r;
    }
    const float minHit = kMinTouchMm * mm * 0.5f;
    for (int b = 0; b < kTouchButtonCount; ++b) {
        TouchCircle& t = L.circles[b];
        if (o.leftHanded) t.x = fw - t.x;
        L.hit[b] = t;
        L.hit[b].r = std::max(t.r + g * 0.5f, minHit);
        L.buttons[b] = {(t.x - t.r) / fw, (t.y - t.r) / fh, 2 * t.r / fw, 2 * t.r / fh};
    }
    return L;
}

float touchSpeedFactor(int step) { return 1.0f + 0.25f * static_cast<float>(std::min(std::max(step, 0), 4)); }

void applyTouchSpeed(TouchSettings& s, int step) {
    const TouchSettings base;
    const float f = touchSpeedFactor(step);
    s.gain = base.gain * f;
    s.maxLead = base.maxLead * f;
}

TouchMapper::TouchMapper() { setScreen(800, 600); }

void TouchMapper::setScreen(int fbWidth, int fbHeight, const SafeInsets& insets) {
    layout_ = computeTouchLayout(fbWidth, fbHeight, insets);
}

void TouchMapper::setScreen(int fbWidth, int fbHeight, const TouchLayoutOptions& options) {
    layout_ = computeTouchLayout(fbWidth, fbHeight, options);
}

int TouchMapper::findFinger(long long id) const {
    for (int i = 0; i < kMaxFingers; ++i) {
        if (fingers_[i].down && fingers_[i].id == id) return i;
    }
    return -1;
}

int TouchMapper::hitButton(float x, float y) const { return layout_.hitTest(x, y); }

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
