// The sequels' menu drawing (docs/spec/as2/frontend.md 2.5, 2.9, 3.1): atlas pieces, the
// panel, the title logo, the text button and the widgets of a MenuSystem in their style.
#include <algorithm>
#include <cmath>

#include "as2_draw.h"

namespace as3d::ui {

namespace as2 {

namespace {

const FontMetrics& fm() { return FontMetrics::original(); }
int ftol(float v) { return static_cast<int>(v); }

} // namespace

const Texture2D* interfaceAtlas(const UiAssets& a) {
    if (a.panel.valid()) return &a.panel;
    return a.texture("gfx\\ui\\interface.tga");
}

const Texture2D* snow(const UiAssets& a) {
    if (a.panelNoise.valid()) return &a.panelNoise;
    return a.textureRepeat("gfx\\ui\\snow.tga");
}

SpecUv pieceUv(const Texture2D& t, const Piece& p) {
    const float aw = t.width() > 0 ? static_cast<float>(t.width()) : 256.0f;
    const float ah = t.height() > 0 ? static_cast<float>(t.height()) : 256.0f;
    return texelUv(p.x, p.y, p.w, p.h, aw, ah);
}

void piece(Renderer2D& r, const Texture2D& t, float x, float y, const Piece& p, Color c, Blend b, float w, float h) {
    Piece q = p;
    if (w >= 0) q.w = std::min(w, p.w);
    if (h >= 0) q.h = std::min(h, p.h);
    if (q.w <= 0 || q.h <= 0) return;
    const SpecUv uv = pieceUv(t, q);
    r.quadSpec(x, y, q.w, q.h, uv.s0, uv.t0, uv.s1, uv.t1, &t, c, b);
}

void pieceRunX(Renderer2D& r, const Texture2D& t, float x, float y, float len, const Piece& p, Color c) {
    for (float o = 0; o < len; o += p.w) piece(r, t, x + o, y, p, c, Blend::Alpha, len - o);
}

void picStretched(Renderer2D& r, const Texture2D& t, float x, float y, float w, float h, Color c, Blend b) {
    const float tw = static_cast<float>(std::max(t.width(), 1)), th = static_cast<float>(std::max(t.height(), 1));
    const float du = 0.5f / tw, dv = 0.5f / th;
    r.quad(x, y, w, h, du, dv, 1.0f - du, 1.0f - dv, &t, c, b);
}

void pic(Renderer2D& r, const Texture2D& t, float x, float y, Color c, Blend b) {
    picStretched(r, t, x, y, static_cast<float>(t.width()), static_cast<float>(t.height()), c, b);
}

float noiseRandom() {
    static u32 state = 0x6C078965u;
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state >> 8) / 16777216.0f;
}

void text(Renderer2D& r, const UiAssets& a, float x, float y, std::string_view s, Color c, Align al, bool markup) {
    TextStyle st;
    st.color = c;
    st.align = al;
    st.markup = markup;
    drawText(r, a.uiFont(), x, y, s, st);
}

namespace {

// The vertical cable from y0 down to y1, in pieces of at most 90 cut from its top end.
void cable(Renderer2D& r, const Texture2D& t, float x, float y0, float y1) {
    constexpr Piece kCable{0, 82, 9, 90};
    for (float y = y0; y < y1; y += 90.0f) piece(r, t, x, y, kCable, Color{}, Blend::Alpha, -1, y1 - y);
}

} // namespace

void panel(Renderer2D& r, const UiAssets& a, float x, float y, float w, float h, float f, std::string_view title) {
    f = std::clamp(f, 0.0f, 1.0f);
    const float yt = (y - 15.0f) - (1.0f - f) * y;
    const float yb = (y + h - 13.0f) + (1.0f - f) * (kVirtualHeight - y - h);
    // 1. The static fill: the scene multiplied by fresh noise every frame.
    if (const Texture2D* n = snow(a)) {
        const float H = f * f * h + kVirtualHeight * f * (1.0f - f);
        const float r1 = noiseRandom(), r2 = noiseRandom();
        if (H > 0) r.quadSpec(x, y + h * 0.5f - H * 0.5f, w, H, r1, r2, r1 + w / 256.0f, r2 + H / 256.0f, n, Color{}, Blend::Filter);
    }
    const Texture2D* t = interfaceAtlas(a);
    if (!t) return;
    const bool titled = !title.empty();
    // 2. Top bar, 3. bottom bar.
    if (titled) {
        piece(r, *t, x - 4, yt, {9, 9, 67, 28});
        pieceRunX(r, *t, x + 63, yt, w - 120, {76, 9, 111, 28});
        piece(r, *t, x + w - 57, yt, {187, 9, 61, 28});
    } else {
        piece(r, *t, x - 4, yt, {9, 9, 20, 28});
        pieceRunX(r, *t, x + 16, yt, w - 32, {76, 9, 111, 28});
        piece(r, *t, x + w - 16, yt, {228, 9, 20, 28});
    }
    piece(r, *t, x - 4, yb, {9, 42, 67, 31});
    pieceRunX(r, *t, x + 63, yb, w - 120, {76, 42, 111, 31});
    piece(r, *t, x + w - 57, yb, {187, 42, 61, 31});
    // 4. Cables.
    if (titled) {
        cable(r, *t, x + 46, -40, yt - 25);
        cable(r, *t, x + w - 50, -15, yt);
    }
    cable(r, *t, x + 46, yb + 31, kVirtualHeight);
    cable(r, *t, x + w - 50, yb + 31, kVirtualHeight);
    // 5. The title tab.
    if (titled) {
        const float wt = measureText(fm(), title, 1.0f, true);
        piece(r, *t, x - 4, yt - 32, {105, 180, 65, 39});
        {
            constexpr Piece kTitleBody{170, 180, 64, 39};
            for (float o = 0; o < wt; o += kTitleBody.w) piece(r, *t, x + 61 + o, yt - 32, kTitleBody, Color{}, Blend::Alpha, wt - o);
        }
        piece(r, *t, x + 61 + wt, yt - 32, {234, 180, 15, 39});
        text(r, a, x + 33 + wt * 0.5f, yt - 20, title, orange(), Align::Center);
    }
}

void titleLogoAt(Renderer2D& r, const TitleLogoPictures& p, float T, float cloudT, float ox, float oy, float k, float alpha) {
    const Color tint{1, 1, 1, alpha};
    if (p.glow)
        picStretched(r, *p.glow, ox + 124 * k, oy, static_cast<float>(p.glow->width()) * k, static_cast<float>(p.glow->height()) * k,
                     tint, Blend::Add);
    if (p.two) {
        const float g = 15.0f + 15.0f * std::sin(T + 0.2f);
        Quad q;
        q.x = ox + (580 - g) * k;
        q.y = oy + (-g) * k;
        q.w = q.h = (128 + 2 * g) * k;
        q.texture = p.two;
        q.color = tint;
        q.blend = Blend::Alpha;
        q.rotation = 20.0f + 15.0f * std::sin(2.0f * T);
        r.add(q);
    }
    if (p.logo) {
        Quad q;
        q.x = ox + 124 * k;
        q.y = oy;
        q.w = 512 * k;
        q.h = 128 * k;
        q.texture = p.logo;
        q.color = tint;
        q.blend = Blend::Alpha;
        if (p.clouds) {
            // The letters show the clouds scrolling left, 0.05 texture widths per second.
            q.texture2 = p.clouds;
            q.s0b = 0.1f * cloudT;
            q.s1b = 0.1f * cloudT + 2.0f;
            q.t0b = 0;
            q.t1b = 1;
            q.combine2 = 2;
        }
        r.add(q);
    }
}

void titleLogo(Renderer2D& r, const UiAssets& a, float T) {
    TitleLogoPictures p;
    p.glow = a.texture("gfx\\logo\\glow.tga");
    p.two = a.texture("gfx\\logo\\two3.tga");
    p.logo = a.texture("gfx\\logo\\logo.tga");
    // The clouds are only asked for with the logo, as before.
    p.clouds = p.logo ? a.textureRepeat("gfx\\logo\\clouds.tga") : nullptr;
    titleLogoAt(r, p, T, T, 0, 0, 1, 1);
}

void textButton(Renderer2D& r, const UiAssets& a, float left, float yd, float W, std::string_view caption, Color col) {
    if (const Texture2D* t = interfaceAtlas(a)) {
        piece(r, *t, left, yd, kButtonLeft);
        pieceRunX(r, *t, left + 23, yd, W, kButtonBody);
        piece(r, *t, left + 23 + W, yd, kButtonRight);
    }
    text(r, a, left + 23 + W * 0.5f, yd + 7, caption, col, Align::Center);
    if (const Texture2D* t = interfaceAtlas(a)) {
        piece(r, *t, left + 25, yd + 21, kRivetLow);
        piece(r, *t, left + W - 6, yd + 21, kRivetLow);
        piece(r, *t, left + 25, yd - 5, kRivetHigh);
        piece(r, *t, left + W - 6, yd - 5, kRivetHigh);
    }
}

} // namespace as2

// ---------------------------------------------------------------------------
// Widgets (as2/frontend.md 2.5)
// ---------------------------------------------------------------------------
namespace {

using namespace as2;

Color itemColor(const MenuItem& it, bool focused, Color disabled = disabledGrey()) {
    if (it.disabled()) return disabled;
    return focused ? orange() : green();
}

Align alignOf(const MenuItem& it) {
    return (it.flags & itemflag::AlignRight) ? Align::Right : (it.flags & itemflag::AlignCenter) ? Align::Center : Align::Left;
}

void drawList(MenuDrawContext& c, MenuItem& it) {
    c.r.outline(it.x, it.y, it.w, it.h, greenOutline(), Blend::Alpha);
    const int vis = std::max(it.visibleRows(), 1);
    const int n = static_cast<int>(it.entries.size());
    for (int k = 0; k < vis && it.top + k < n; k++) {
        const int e = it.top + k;
        const float ry = it.y + 4 + 20.0f * static_cast<float>(k);
        const ListEntry& le = it.entries[static_cast<size_t>(e)];
        Color col = le.enabled ? green() : listGrey();
        if (e == it.selected) {
            c.r.rect(it.x + 4, ry - 1, it.w - 25, 18, darkGreenBox(), Blend::Alpha);
            col = orange();
        }
        text(c.r, c.a, it.x + 10, ry, le.text, col);
    }
    const Texture2D* t = interfaceAtlas(c.a);
    if (!t) return;
    const float bx = it.x + it.w - 18;
    // A scroll box under the pointer is orange (seen in the original; not on touch).
    auto box = [&](float y) {
        return !c.touchMode && c.px >= bx && c.px < bx + 15 && c.py >= y && c.py < y + 15 ? orange() : green();
    };
    piece(c.r, *t, bx, it.y + 3, kScrollUp, box(it.y + 3));
    for (float y = it.y + 21; y < it.y + it.h - 21; y += kScrollTrack.h)
        piece(c.r, *t, bx, y, kScrollTrack, green(), Blend::Alpha, -1, it.y + it.h - 21 - y);
    // Thumb travel: proportional to the first visible row (GUESS, as2/frontend.md 10.1 item 6).
    const int range = std::max(n - vis, 0);
    const float travel = it.h - 42 - kScrollThumb.h;
    const float thumbY = it.y + 21 + (range > 0 ? travel * static_cast<float>(it.top) / static_cast<float>(range) : 0.0f);
    piece(c.r, *t, bx, thumbY, kScrollThumb, green());
    piece(c.r, *t, bx, it.y + it.h - 19, kScrollDown, box(it.y + it.h - 19));
}

void drawSpinner(MenuDrawContext& c, MenuItem& it, bool focused) {
    const Color col = itemColor(it, focused);
    text(c.r, c.a, it.x - 10, it.y, it.label, col, Align::Right);
    const int n = static_cast<int>(it.values.size());
    const std::string v = n > 0 ? it.values[static_cast<size_t>(std::clamp(it.index, 0, n - 1))] : std::string();
    text(c.r, c.a, it.x + 10, it.y, v, col);
    if (c.touchMode && !it.disabled() && n > 1) {
        // Touch: where a tap cycles backwards and forwards (docs/spec/issues/090).
        text(c.r, c.a, it.x - 3, it.y, "<", col);
        text(c.r, c.a, it.x + 16 + measureText(FontMetrics::original(), v), it.y, ">", col);
    }
}

void drawSlider(MenuDrawContext& c, MenuItem& it, bool focused) {
    const Color col = itemColor(it, focused);
    text(c.r, c.a, it.x - 10, it.y, it.label, col, Align::Right);
    const Texture2D* t = interfaceAtlas(c.a);
    if (!t) return;
    piece(c.r, *t, it.x + 8, it.y + 3, kSliderBar, col);
    const float range = static_cast<float>(it.max - it.min);
    const float off = range != 0 ? static_cast<float>(static_cast<int>((it.value - static_cast<float>(it.min)) * 128.0f / range)) : 0.0f;
    piece(c.r, *t, it.x + 8 + off - 3, it.y + 1, kSliderKnob, col);
}

void drawEdit(MenuDrawContext& c, MenuItem& it, bool focused) {
    c.r.rect(it.x - 2, it.y, it.w + 4, 16, darkGreenBox(), Blend::Alpha);
    text(c.r, c.a, it.x, it.y, it.text, Color{});
    if (focused && ((c.ms / 250) % 2) == 1) {
        const size_t cur = static_cast<size_t>(std::clamp(it.cursor, 0, static_cast<int>(it.text.size())));
        const float cx = it.x + measureText(FontMetrics::original(), std::string_view(it.text).substr(0, cur));
        drawNumber(c.r, c.a.uiFont(), cx, it.y, "_", 1.0f, Color{});
    }
}

} // namespace

void drawSequelItem(MenuDrawContext& c, MenuItem& it, bool focused) {
    const bool open = c.menu.open >= 1.0f;
    switch (it.type) {
        case ItemType::SequelButton: {
            // Slides up from the bottom edge; skipped once fully out (issue 240 item 3).
            if (it.slide <= 0) return;
            const float yd = kVirtualHeight - (kVirtualHeight - it.y) * it.slide;
            Color col = it.disabled() ? disabledGrey() : focused ? orange() : green();
            if ((it.flags & itemflag::Markup) && !it.disabled()) col = red();
            textButton(c.r, c.a, it.hit.x, yd, it.captionW, it.label, col);
            return;
        }
        case ItemType::Picture: {
            if (it.hidden()) return;
            if (const Texture2D* t = c.a.texture(it.texture))
                c.r.quadSpec(it.x, it.y, it.w, it.h, it.uv.s0, it.uv.t0, it.uv.s1, it.uv.t1, t,
                             focused && !it.disabled() ? orange() : green(), Blend::Alpha);
            return;
        }
        default: break;
    }
    // Text, edit, list, spinner, slider and the screens' own rows: once the menu is open.
    if (it.hidden() || !open) return;
    switch (it.type) {
        case ItemType::Text:
            text(c.r, c.a, it.x, it.y, it.label, itemColor(it, focused), alignOf(it), (it.flags & itemflag::Markup) != 0);
            break;
        case ItemType::Edit: drawEdit(c, it, focused); break;
        case ItemType::List: drawList(c, it); break;
        case ItemType::Spinner: drawSpinner(c, it, focused); break;
        case ItemType::Slider: drawSlider(c, it, focused); break;
        case ItemType::Custom: if (it.draw) it.draw(c, it, focused); break;
        default: break; // image buttons and the helicopter grid: no sequel screen has them
    }
}

} // namespace as3d::ui
