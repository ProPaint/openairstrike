#include "touch_overlay.h"

#include <algorithm>
#include <cmath>

namespace as3d_game {

using namespace as3d;
using ui::Blend;
using ui::Color;

namespace {

// A screen rectangle in virtual pixels; icons are drawn in its unit square.
struct Box {
    float x, y, w, h;
    float px(float u) const { return x + u * w; }
    float py(float v) const { return y + v * h; }
};

Box toVirtual(const ui::Renderer2D& r, const TouchRect& t) {
    const ui::Mapping& m = r.mapping();
    float x0 = m.toVirtX(t.x * static_cast<float>(m.fbWidth));
    float y0 = m.toVirtY(t.y * static_cast<float>(m.fbHeight));
    float x1 = m.toVirtX((t.x + t.w) * static_cast<float>(m.fbWidth));
    float y1 = m.toVirtY((t.y + t.h) * static_cast<float>(m.fbHeight));
    return {x0, y0, x1 - x0, y1 - y0};
}

// Fills a convex polygon (unit-square coordinates of `b`) with horizontal strips.
void fillConvex(ui::Renderer2D& r, const Box& b, const float* uv, int n, Color c) {
    float ys[8], xs[8];
    float top = 1e9f, bottom = -1e9f;
    for (int i = 0; i < n; ++i) {
        xs[i] = b.px(uv[2 * i]);
        ys[i] = b.py(uv[2 * i + 1]);
        top = std::min(top, ys[i]);
        bottom = std::max(bottom, ys[i]);
    }
    // About one strip per two framebuffer pixels, so edges look smooth at any resolution.
    int strips = static_cast<int>((bottom - top) * r.mapping().scaleY * 0.5f);
    strips = std::min(std::max(strips, 6), 160);
    float step = (bottom - top) / static_cast<float>(strips);
    if (step <= 0) return;
    for (int s = 0; s < strips; ++s) {
        float yc = top + (s + 0.5f) * step;
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < n; ++i) {
            int j = (i + 1) % n;
            float y0 = ys[i], y1 = ys[j];
            if ((yc < std::min(y0, y1)) || (yc > std::max(y0, y1)) || y0 == y1) continue;
            float x = xs[i] + (xs[j] - xs[i]) * (yc - y0) / (y1 - y0);
            lo = std::min(lo, x);
            hi = std::max(hi, x);
        }
        if (hi > lo) r.rect(lo, top + s * step, hi - lo, step, c, Blend::Alpha);
    }
}

void fillRectU(ui::Renderer2D& r, const Box& b, float u0, float v0, float u1, float v1, Color c) {
    r.rect(b.px(u0), b.py(v0), (u1 - u0) * b.w, (v1 - v0) * b.h, c, Blend::Alpha);
}

// A missile pointing up, in the sub-box (u0, v0)-(u1, v1).
void missileIcon(ui::Renderer2D& r, const Box& outer, float u0, float v0, float u1, float v1, Color c) {
    Box b{outer.px(u0), outer.py(v0), (u1 - u0) * outer.w, (v1 - v0) * outer.h};
    const float nose[] = {0.38f, 0.32f, 0.5f, 0.08f, 0.62f, 0.32f};
    fillConvex(r, b, nose, 3, c);
    fillRectU(r, b, 0.38f, 0.32f, 0.62f, 0.8f, c);
    const float finL[] = {0.38f, 0.58f, 0.38f, 0.86f, 0.2f, 0.92f};
    const float finR[] = {0.62f, 0.58f, 0.8f, 0.92f, 0.62f, 0.86f};
    fillConvex(r, b, finL, 3, c);
    fillConvex(r, b, finR, 3, c);
}

void diamondIcon(ui::Renderer2D& r, const Box& outer, float u0, float v0, float u1, float v1, Color c) {
    Box b{outer.px(u0), outer.py(v0), (u1 - u0) * outer.w, (v1 - v0) * outer.h};
    const float d[] = {0.5f, 0.08f, 0.9f, 0.5f, 0.5f, 0.92f, 0.1f, 0.5f};
    fillConvex(r, b, d, 4, c);
    Color inner{c.r * 0.5f, c.g * 0.5f, c.b * 0.2f, c.a};
    const float e[] = {0.5f, 0.32f, 0.68f, 0.5f, 0.5f, 0.68f, 0.32f, 0.5f};
    fillConvex(r, b, e, 4, inner);
}

void bulletsIcon(ui::Renderer2D& r, const Box& outer, float u0, float v0, float u1, float v1, Color c) {
    Box b{outer.px(u0), outer.py(v0), (u1 - u0) * outer.w, (v1 - v0) * outer.h};
    for (int k = 0; k < 3; ++k) {
        float x = 0.14f + 0.28f * k;
        const float tip[] = {x, 0.3f, x + 0.08f, 0.12f, x + 0.16f, 0.3f};
        fillConvex(r, b, tip, 3, c);
        fillRectU(r, b, x, 0.3f, x + 0.16f, 0.86f, c);
    }
}

// ">" on the right side of the button: "next".
void chevron(ui::Renderer2D& r, const Box& b, Color c) {
    const float upper[] = {0.64f, 0.3f, 0.74f, 0.3f, 0.92f, 0.5f, 0.82f, 0.5f};
    const float lower[] = {0.82f, 0.5f, 0.92f, 0.5f, 0.74f, 0.7f, 0.64f, 0.7f};
    fillConvex(r, b, upper, 4, c);
    fillConvex(r, b, lower, 4, c);
}

Color withAlpha(Color c, float a) {
    c.a *= a;
    return c;
}

} // namespace

void drawTouchControls(ui::Renderer2D& r, const TouchMapper& touch) {
    const TouchLayout& L = touch.layout();
    const float a = L.alpha;
    for (int i = 0; i < kTouchButtonCount; ++i) {
        TouchButton id = static_cast<TouchButton>(i);
        bool held = touch.buttonHeld(id);
        Box b = toVirtual(r, L.buttons[i]);
        Color bg = held ? Color{0.45f, 0.45f, 0.5f, 0.75f} : Color{0.08f, 0.08f, 0.1f, 0.55f};
        r.rect(b.x, b.y, b.w, b.h, withAlpha(bg, a), Blend::Alpha);
        Color edge = withAlpha(Color{0.85f, 0.85f, 0.85f, held ? 1.0f : 0.7f}, a);
        r.rect(b.x, b.y, b.w, b.h * 0.03f, edge);
        r.rect(b.x, b.y + b.h * 0.97f, b.w, b.h * 0.03f, edge);
        r.rect(b.x, b.y, b.w * 0.03f, b.h, edge);
        r.rect(b.x + b.w * 0.97f, b.y, b.w * 0.03f, b.h, edge);
        const Color red = withAlpha(Color{0.95f, 0.35f, 0.15f, 1}, a);
        const Color yellow = withAlpha(Color{1.0f, 0.85f, 0.2f, 1}, a);
        const Color grey = withAlpha(Color{0.85f, 0.88f, 0.95f, 1}, a);
        switch (id) {
            case TouchButton::Missile: missileIcon(r, b, 0.15f, 0.1f, 0.85f, 0.9f, red); break;
            case TouchButton::PowerUp: diamondIcon(r, b, 0.15f, 0.15f, 0.85f, 0.85f, yellow); break;
            case TouchButton::NextMissile:
                missileIcon(r, b, 0.05f, 0.15f, 0.6f, 0.85f, red);
                chevron(r, b, grey);
                break;
            case TouchButton::NextWeapon:
                bulletsIcon(r, b, 0.08f, 0.2f, 0.62f, 0.8f, grey);
                chevron(r, b, grey);
                break;
            case TouchButton::NextPowerUp:
                diamondIcon(r, b, 0.08f, 0.22f, 0.6f, 0.78f, yellow);
                chevron(r, b, grey);
                break;
            case TouchButton::Pause:
                fillRectU(r, b, 0.28f, 0.22f, 0.43f, 0.78f, grey);
                fillRectU(r, b, 0.57f, 0.22f, 0.72f, 0.78f, grey);
                break;
            default: break;
        }
    }
    // Where the drag is steering the helicopter.
    if (touch.dragging() && touch.targetValid()) {
        float x = touch.targetX(), y = touch.targetY();
        Color c{1, 1, 1, 0.45f};
        r.rect(x - 10, y - 1, 20, 2, c);
        r.rect(x - 1, y - 10, 2, 20, c);
    }
}

void drawPauseOverlay(ui::Renderer2D& r, bool touch) {
    r.fullscreen(Color{0, 0, 0, 0.5f});
    Box b{340, 220, 120, 160};
    const float play[] = {0.1f, 0.05f, 0.95f, 0.5f, 0.1f, 0.95f};
    fillConvex(r, b, play, 3, Color{1, 1, 1, 0.85f});
    (void)touch;
}

void drawLoadingScreen(ui::Renderer2D& r, float progress) {
    progress = std::min(std::max(progress, 0.0f), 1.0f);
    r.fullscreen(Color{0, 0, 0, 1});
    Color frame{0.6f, 0.6f, 0.6f, 1};
    r.rect(248, 288, 304, 2, frame);
    r.rect(248, 310, 304, 2, frame);
    r.rect(248, 288, 2, 24, frame);
    r.rect(550, 288, 2, 24, frame);
    r.rect(252, 292, 296 * progress, 16, Color{0.82f, 0.25f, 0.0f, 1});
}

} // namespace as3d_game
