#include "touch_overlay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "as3d/player_select.h"

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

enum class ItemKind { Weapon, Missile, Powerup };

// The face of an item button: the item's icon as large as the round button allows, its count
// on a dark backing below, and for a "next" button (`next`) a small double arrow badge. A next
// button that would change nothing (`active` false) is dimmed to the arrow alone. `n` is the
// count to show (`counted`); an item that is not owned (n = 0) is greyed, as on the HUD.
void itemFace(ui::Renderer2D& r, const ui::UiAssets* as, const Box& box, float x, float y, float rad, float a,
              ItemKind kind, int idx, int n, bool counted, bool next, bool active = true) {
    if (next && !active) {
        nextGlyph(r, Box{x - rad * 0.5f, y - rad * 0.5f, rad, rad}, withAlpha(Color{0.75f, 0.76f, 0.8f, 1}, 0.32f * a));
        return;
    }
    const bool owned = !counted || n > 0;
    const Color ic = owned ? withAlpha(Color{}, a) : withAlpha(kGreyIcon, 0.4f * a);
    // Vertical layout: the icon above centre, the count row (or the badge) below.
    const float iconY = y - rad * (next ? 0.24f : 0.22f);
    const float iconW = rad * (next ? 1.75f : 1.85f);
    const Texture2D* tex = nullptr;
    ui::SpecUv uv;
    Blend blend = Blend::Alpha;
    if (as) {
        switch (kind) {
            case ItemKind::Weapon: tex = &as->weapons; uv = ui::weaponIconUv(idx); blend = Blend::Add; break;
            case ItemKind::Missile: tex = &as->missiles; uv = ui::missileIconUv(idx); break;
            case ItemKind::Powerup:
                tex = &as->items;
                uv = idx < ui::kPowerupIconKinds ? ui::powerupIconUv(idx) : ui::SpecUv{};
                if (idx == 0) blend = Blend::Add;
                break;
        }
    }
    if (tex && tex->valid() && !uv.empty()) {
        atlasIcon(r, *tex, uv, x, iconY, iconW, ic, blend);
    } else {
        // No picture in the atlas (or no atlas): a drawn symbol.
        const Box ib{x - rad, iconY - rad * 0.5f, 2 * rad, rad};
        switch (kind) {
            case ItemKind::Weapon: bulletsIcon(r, ib, 0.2f, 0.0f, 0.8f, 1.0f, withAlpha(Color{0.92f, 0.93f, 0.97f, 1}, a)); break;
            case ItemKind::Missile: missileIcon(r, ib, 0.3f, -0.4f, 0.7f, 1.4f, withAlpha(Color{0.95f, 0.35f, 0.15f, 1}, a)); break;
            case ItemKind::Powerup: diamondIcon(r, ib, 0.3f, -0.2f, 0.7f, 1.2f, withAlpha(Color{1.0f, 0.85f, 0.2f, 1}, owned ? a : 0.4f * a)); break;
        }
    }
    const float rowY = y + rad * 0.22f;
    const float rowH = rad * (next ? 0.46f : 0.44f);
    if (next) {
        // Count at the left, arrow badge at the right; the arrow alone is centred.
        if (counted && as) {
            countText(r, *as, x - rad * 0.3f, rowY, n, rowH, withAlpha(owned ? kCount : kGreyIcon, a));
            nextGlyph(r, Box{x + rad * 0.0f, rowY - rad * 0.08f, rad * 0.7f, rad * 0.6f}, withAlpha(kOrange, a));
        } else {
            nextGlyph(r, Box{x - rad * 0.35f, rowY - rad * 0.08f, rad * 0.7f, rad * 0.6f}, withAlpha(kOrange, a));
        }
    } else if (counted && as) {
        countText(r, *as, x, rowY, n, rowH, withAlpha(owned ? kCount : kGreyIcon, a));
    }
}

} // namespace

void drawTouchControls(ui::Renderer2D& r, const TouchMapper& touch, const TouchOverlayState& st) {
    const TouchLayout& L = touch.layout();
    const ui::Mapping& m = r.mapping();
    const ui::HudPlayer* p = st.player;
    const ui::UiAssets* as = st.assets;
    // The game's own cycling rules: the sequels' 9 weapon slots and power-up skip mask.
    const GameRules& rules = st.rules ? *st.rules : defaultGameRules();
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

        const Box box{x - rad, y - rad, 2 * rad, 2 * rad};
        const Color light = withAlpha(Color{0.92f, 0.93f, 0.97f, 1}, a);
        switch (id) {
            case TouchButton::Missile:
                if (p) {
                    const int t = std::min(std::max(p->missileSelected, 0), ui::kMissileTypes - 1);
                    itemFace(r, as, box, x, y, rad, a, ItemKind::Missile, t, p->missiles[t], true, false);
                } else {
                    missileIcon(r, box, 0.25f, 0.15f, 0.75f, 0.85f, withAlpha(Color{0.95f, 0.35f, 0.15f, 1}, a));
                }
                break;
            case TouchButton::PowerUp:
                if (p) {
                    const int k = std::min(std::max(p->powerupSelected, 0), ui::kPowerupSlots - 1);
                    itemFace(r, as, box, x, y, rad, a, ItemKind::Powerup, k, p->powerups[k], true, false);
                } else {
                    diamondIcon(r, box, 0.3f, 0.12f, 0.7f, 0.52f, withAlpha(Color{1.0f, 0.85f, 0.2f, 1}, a));
                }
                break;
            case TouchButton::NextWeapon:
                if (p) {
                    // The big icon is the weapon one press selects; no count (a weapon is a level).
                    const int nw = nextWeaponIndex(rules, p->upgrades, p->weapon);
                    itemFace(r, as, box, x, y, rad, a, ItemKind::Weapon, nw, 0, false, true, nw != p->weapon);
                } else {
                    bulletsIcon(r, box, 0.22f, 0.2f, 0.78f, 0.6f, light);
                    nextGlyph(r, Box{x - rad * 0.45f, y + rad * 0.15f, rad * 0.9f, rad * 0.6f}, withAlpha(kOrange, a));
                }
                break;
            case TouchButton::NextMissile:
                if (p) {
                    const int nm = nextMissileType(p->missiles, p->missileSelected);
                    itemFace(r, as, box, x, y, rad, a, ItemKind::Missile, nm, p->missiles[nm], true, true,
                             nm != p->missileSelected);
                } else {
                    nextGlyph(r, Box{x - rad * 0.5f, y - rad * 0.5f, rad * 1.0f, rad * 1.0f}, withAlpha(light, 0.9f));
                }
                break;
            case TouchButton::NextPowerUp:
                if (p) {
                    const int nk = nextPowerupSlot(rules, p->powerups, p->powerupSelected);
                    itemFace(r, as, box, x, y, rad, a, ItemKind::Powerup, nk, p->powerups[nk], true, true,
                             nk != p->powerupSelected);
                } else {
                    nextGlyph(r, Box{x - rad * 0.5f, y - rad * 0.5f, rad * 1.0f, rad * 1.0f}, withAlpha(light, 0.9f));
                }
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

void drawFpsCounter(ui::Renderer2D& r, const ui::UiAssets& a, const FpsCounter& fps, const TouchLayout* touch,
                    const SafeInsets& in) {
    if (!a.fontLoaded) return;
    const ui::Mapping& m = r.mapping();
    const float fw = static_cast<float>(m.fbWidth);
    char line1[32], line2[48];
    if (fps.valid()) {
        std::snprintf(line1, sizeof line1, "%d FPS", static_cast<int>(fps.fps() + 0.5));
        std::snprintf(line2, sizeof line2, "worst %d ms  drop %d", static_cast<int>(fps.worstMs() + 0.5), fps.dropped());
    } else {
        std::snprintf(line1, sizeof line1, "-- FPS");
        line2[0] = 0;
    }
    const float s1 = 0.7f, s2 = 0.5f;            // text scales (virtual pixels)
    const float h1 = 15.0f * s1, h2 = 15.0f * s2;
    const ui::FontMetrics& fm = ui::FontMetrics::original();
    const float w = std::max(ui::measureText(fm, line1, s1), ui::measureText(fm, line2, s2));
    const float pad = 3.0f;
    const float margin = 6.0f;
    // Anchor: a top corner in virtual pixels, text right- or left-aligned from it.
    float x = 0, y = 0;
    ui::Align align = ui::Align::Right;
    const float topInset = m.toVirtY(static_cast<float>(in.top)) - m.toVirtY(0);
    if (touch && touch->outside) {
        const TouchCircle& p = touch->circles[static_cast<int>(TouchButton::Pause)];
        const bool pauseLeft = p.x < fw * 0.5f;
        y = m.top() + topInset + margin;
        if (pauseLeft) {
            x = m.toVirtX(fw - static_cast<float>(in.right)) - margin;
        } else {
            x = m.toVirtX(static_cast<float>(in.left)) + margin;
            align = ui::Align::Left;
        }
    } else if (touch) {
        // Beside the pause button at the top centre of the field.
        const TouchCircle& p = touch->circles[static_cast<int>(TouchButton::Pause)];
        x = m.toVirtX(p.x + p.r) + margin;
        y = m.toVirtY(p.y - p.r);
        align = ui::Align::Left;
    } else if (m.left() < -margin * 2) {
        x = m.right() - margin; // the screen's top right corner, outside the field
        y = m.top() + margin;
    } else {
        x = 400.0f + w * 0.5f; // top centre, between the health and score bars
        y = 3.0f;
    }
    const float bx = align == ui::Align::Right ? x - w - pad : x - pad;
    const float bh = h1 + (line2[0] ? h2 + 2.0f : 0.0f) + 2 * pad;
    r.rect(bx, y - pad, w + 2 * pad, bh, Color{0, 0, 0, 0.45f});
    ui::TextStyle st;
    st.scale = s1;
    st.align = align;
    st.color = Color{1.0f, 0.63f, 0.0f, 1.0f};
    ui::drawText(r, a.uiFont(), x, y, line1, st);
    if (line2[0]) {
        st.scale = s2;
        st.color = Color{0.75f, 0.75f, 0.78f, 1.0f};
        ui::drawText(r, a.uiFont(), x, y + h1 + 2.0f, line2, st);
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
