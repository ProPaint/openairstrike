#include "touch_overlay.h"

#include <algorithm>
#include <cmath>
#include <string>

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
// ">>" in the unit square of `b` (two chevrons): "next".
void nextGlyph(ui::Renderer2D& r, const Box& b, Color c) {
    for (int k = 0; k < 2; ++k) {
        const float o = 0.22f * static_cast<float>(k);
        const float upper[] = {0.26f + o, 0.28f, 0.38f + o, 0.28f, 0.60f + o, 0.5f, 0.48f + o, 0.5f};
        const float lower[] = {0.48f + o, 0.5f, 0.60f + o, 0.5f, 0.38f + o, 0.72f, 0.26f + o, 0.72f};
        fillConvex(r, b, upper, 4, c);
        fillConvex(r, b, lower, 4, c);
    }
}

Color withAlpha(Color c, float a) {
    c.a *= a;
    return c;
}

// A HUD atlas icon (66 x 35 texels) centred at (x, y), `w` virtual pixels wide.
void atlasIcon(ui::Renderer2D& r, const Texture2D& tex, ui::SpecUv uv, float x, float y, float w, Color c, Blend b) {
    if (!tex.valid() || uv.empty()) return;
    const float h = w * 35.0f / 66.0f;
    r.quadSpec(x - w * 0.5f, y - h * 0.5f, w, h, uv.s0, uv.t0, uv.s1, uv.t1, &tex, c, b);
}

// A count in the game font, centred on x, digits `height` virtual pixels tall.
void countText(ui::Renderer2D& r, const ui::UiAssets& a, float x, float y, int n, float height, Color c) {
    const std::string s = std::to_string(std::max(n, 0));
    const float scale = height / 16.0f;
    const float w = ui::numberWidth(s, scale);
    // Dark backing so the additive digits read over bright ground.
    r.rect(x - w * 0.5f - scale * 3.0f, y - scale, w + scale * 6.0f, height + scale * 2.0f, Color{0, 0, 0, 0.35f * c.a});
    ui::drawNumber(r, a.uiFont(), x - w * 0.5f, y, s, scale, c);
}

const Color kOrange{1.0f, 0.63f, 0.0f, 1.0f};   // ui::orange()
const Color kCount{1.0f, 0.72f, 0.25f, 1.0f};
const Color kGreyIcon{0.5f, 0.5f, 0.52f, 1.0f};

} // namespace

void drawTouchControls(ui::Renderer2D& r, const TouchMapper& touch, const TouchOverlayState& st) {
    const TouchLayout& L = touch.layout();
    const ui::Mapping& m = r.mapping();
    const ui::HudPlayer* p = st.player;
    const ui::UiAssets* as = st.assets;
    for (int i = 0; i < kTouchButtonCount; ++i) {
        const TouchButton id = static_cast<TouchButton>(i);
        const bool held = touch.buttonHeld(id);
        const TouchCircle& c = L.circles[i];
        if (!(c.r > 0)) continue;
        // Held buttons are always at full opacity.
        const float a = held ? 1.0f : std::min(std::max(st.alpha, 0.0f), 1.0f);
        const float x = m.toVirtX(c.x), y = m.toVirtY(c.y);
        const float rad = c.r / m.scaleY; // virtual pixels, vertically
        const float px = 1.0f / m.scaleY; // one framebuffer pixel
        const bool main = id == TouchButton::Missile || id == TouchButton::PowerUp;
        // Soft shadow, body, rim, inner highlight.
        r.circle(x, y + rad * 0.04f, rad * 1.10f, Color{0, 0, 0, 0.30f * a}, Blend::Alpha, rad * 0.22f);
        const Color body = held ? Color{0.55f, 0.22f, 0.04f, 0.72f} : Color{0.05f, 0.05f, 0.07f, 0.55f};
        r.circle(x, y, rad, withAlpha(body, a));
        const Color rim = held ? Color{1.0f, 0.85f, 0.45f, 1.0f}
                               : main ? withAlpha(kOrange, 0.85f) : Color{0.85f, 0.86f, 0.9f, 0.7f};
        r.ring(x, y, rad, std::max(rad * 0.075f, 1.5f * px), withAlpha(rim, a), Blend::Alpha, px * 0.5f);
        r.ring(x, y, rad * 0.9f, std::max(rad * 0.025f, px), Color{1, 1, 1, 0.10f * a});
        if (held) r.ring(x, y, rad * 1.12f, rad * 0.14f, Color{1.0f, 0.55f, 0.1f, 0.55f}, Blend::Add, rad * 0.10f);

        const float iconW = rad * 1.35f;
        const Box box{x - rad, y - rad, 2 * rad, 2 * rad};
        const Color light = withAlpha(Color{0.92f, 0.93f, 0.97f, 1}, a);
        switch (id) {
            case TouchButton::Missile: {
                const int t = p ? std::min(std::max(p->missileSelected, 0), ui::kMissileTypes - 1) : 0;
                const int n = p ? p->missiles[t] : 1;
                const Color ic = n > 0 ? withAlpha(Color{}, a) : withAlpha(kGreyIcon, 0.4f * a);
                if (as && p && as->missiles.valid()) {
                    atlasIcon(r, as->missiles, ui::missileIconUv(t), x, y - rad * 0.16f, iconW, ic, Blend::Alpha);
                    countText(r, *as, x, y + rad * 0.22f, n, rad * 0.42f, withAlpha(n > 0 ? kCount : kGreyIcon, a));
                } else {
                    missileIcon(r, box, 0.25f, 0.15f, 0.75f, 0.85f, withAlpha(Color{0.95f, 0.35f, 0.15f, 1}, a));
                }
                break;
            }
            case TouchButton::PowerUp: {
                const int k = p ? std::min(std::max(p->powerupSelected, 0), ui::kPowerupSlots - 1) : 0;
                const int n = p ? p->powerups[k] : 1;
                const Color ic = n > 0 ? withAlpha(Color{}, a) : withAlpha(kGreyIcon, 0.4f * a);
                if (as && p && as->items.valid() && k < ui::kPowerupIconKinds) {
                    atlasIcon(r, as->items, ui::powerupIconUv(k), x, y - rad * 0.16f, iconW, ic,
                              k == 0 ? Blend::Add : Blend::Alpha);
                } else {
                    diamondIcon(r, box, 0.3f, 0.12f, 0.7f, 0.52f,
                                n > 0 ? withAlpha(Color{1.0f, 0.85f, 0.2f, 1}, a) : withAlpha(kGreyIcon, 0.4f * a));
                }
                if (as && p) countText(r, *as, x, y + rad * 0.22f, n, rad * 0.42f, withAlpha(n > 0 ? kCount : kGreyIcon, a));
                break;
            }
            case TouchButton::NextWeapon:
                if (as && p && as->weapons.valid() && !ui::weaponIconUv(p->weapon).empty()) {
                    atlasIcon(r, as->weapons, ui::weaponIconUv(p->weapon), x, y - rad * 0.2f, rad * 1.45f,
                              withAlpha(Color{}, 0.9f * a), Blend::Add);
                    nextGlyph(r, Box{x - rad * 0.45f, y + rad * 0.12f, rad * 0.9f, rad * 0.6f}, withAlpha(kOrange, a));
                } else {
                    bulletsIcon(r, box, 0.22f, 0.2f, 0.78f, 0.6f, light);
                    nextGlyph(r, Box{x - rad * 0.45f, y + rad * 0.15f, rad * 0.9f, rad * 0.6f}, withAlpha(kOrange, a));
                }
                break;
            case TouchButton::NextMissile:
            case TouchButton::NextPowerUp:
                nextGlyph(r, Box{x - rad * 0.5f, y - rad * 0.5f, rad * 1.0f, rad * 1.0f}, withAlpha(light, 0.9f));
                break;
            case TouchButton::Pause:
                fillRectU(r, box, 0.34f, 0.3f, 0.45f, 0.7f, light);
                fillRectU(r, box, 0.55f, 0.3f, 0.66f, 0.7f, light);
                break;
            default: break;
        }
    }
    // Where the drag is steering the helicopter.
    if (touch.dragging() && touch.targetValid()) {
        float x = touch.targetX(), y = touch.targetY();
        r.ring(x, y, 9.0f, 1.6f, Color{1, 1, 1, 0.45f});
        r.circle(x, y, 1.8f, Color{1, 1, 1, 0.45f});
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
