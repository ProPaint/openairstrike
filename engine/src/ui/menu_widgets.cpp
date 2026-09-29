// Widget input and drawing (frontend.md 2.5 to 2.7, 3.1).
#include <algorithm>
#include <cmath>

#include "as3d/menu.h"

namespace as3d::ui {

namespace {

const FontMetrics& fm() { return FontMetrics::original(); }
int ftol(float v) { return static_cast<int>(v); }

const Color kGrey = grey(0x80 / 255.0f);
const Color kDark = grey(0x40 / 255.0f);
const u32 kFocusBox = 0x80000060u;

// Touch mode: taps left of this x (relative to the spinner's x) cycle backwards.
constexpr float kSpinnerPrevLeft = -30.0f, kSpinnerPrevRight = 8.0f;

void cycle(MenuItem& s, int dir) {
    const int n = static_cast<int>(s.values.size());
    if (n == 0) return;
    s.index = ((std::clamp(s.index, 0, n - 1) + dir) % n + n) % n;
}

float knobX(const MenuItem& s) {
    const float range = static_cast<float>(s.max - s.min);
    const float f = range != 0 ? (s.value - static_cast<float>(s.min)) * 128.0f / range : 0.0f;
    return s.x + static_cast<float>(ftol(f));
}

// Scroll bar geometry of a list (frontend.md 2.5).
struct ListZones {
    float barX;
    bool upArrow(const MenuItem& l, float px, float py) const { return px >= barX && py >= l.y && py < l.y + 26; }
    bool downArrow(const MenuItem& l, float px, float py) const {
        return px >= barX && py >= l.y + l.h - 26 && py < l.y + l.h;
    }
    bool track(const MenuItem& l, float px, float py) const { return px >= barX && py >= l.y && py < l.y + l.h; }
};

} // namespace

// ---------------------------------------------------------------------------
// Widget helpers
// ---------------------------------------------------------------------------
namespace widgets {

static void listFix(MenuItem& l, bool keepVisible) {
    const int n = static_cast<int>(l.entries.size());
    if (n == 0) { l.selected = 0; l.top = 0; return; }
    l.selected = std::clamp(l.selected, 0, n - 1);
    while (l.selected > 0 && !l.entries[static_cast<size_t>(l.selected)].enabled) l.selected--;
    if (!l.entries[static_cast<size_t>(l.selected)].enabled)
        for (int i = 0; i < n; i++)
            if (l.entries[static_cast<size_t>(i)].enabled) { l.selected = i; break; }
    const int vis = std::max(l.visibleRows(), 1);
    if (keepVisible) {
        if (l.selected < l.top) l.top = l.selected;
        if (l.selected >= l.top + vis) l.top = l.selected - vis + 1;
    }
    l.top = std::clamp(l.top, 0, std::max(0, n - vis));
}

void listFixSelection(MenuItem& l) { listFix(l, true); }

void sliderClick(MenuItem& s, float px) {
    if (px < s.x + 8) s.value = static_cast<float>(s.min);
    else if (px > s.x + 136) s.value = static_cast<float>(s.max);
    else s.value = static_cast<float>(s.min) + (px - s.x - 8) / 128.0f * static_cast<float>(s.max - s.min);
}

int gridCellAt(const MenuItem& g, float px, float py) {
    const int n = std::clamp(g.grid.count, 0, kHelicopters);
    for (int i = 0; i < n; i++) {
        const float cx = g.x + 72.0f * static_cast<float>(i % 5), cy = g.y + 72.0f * static_cast<float>(i / 5);
        if (px >= cx && px < cx + 64 && py >= cy && py < cy + 64) return i;
    }
    return -1;
}

bool gridClick(MenuItem& g, float px, float py) {
    const int cell = gridCellAt(g, px, py);
    if (cell < 0 || !g.grid.choice) return false;
    if (g.grid.locked && g.grid.locked[cell]) return false;
    const bool two = g.grid.twoPlayers && *g.grid.twoPlayers;
    if (!two) {
        g.grid.choice[0] = cell;
        return true;
    }
    int alt = g.grid.alternator ? *g.grid.alternator : 0;
    g.grid.choice[alt ? 1 : 0] = cell;
    if (g.grid.alternator) *g.grid.alternator = alt ? 0 : 1;
    return true;
}

void letterbox(MenuDrawContext& c) {
    // Bars extend to the framebuffer edges on wide screens (docs/spec/issues/091).
    const Mapping& m = c.r.mapping();
    const float l = std::min(m.left(), 0.0f), w = std::max(m.right(), 800.0f) - l;
    c.r.rect(l, std::min(m.top(), 0.0f), w, 100 - std::min(m.top(), 0.0f), {0, 0, 0, 1}, Blend::Opaque);
    c.r.rect(l, 500, w, std::max(m.bottom(), 600.0f) - 500, {0, 0, 0, 1}, Blend::Opaque);
    if (c.plain) {
        // Two rules where the first game has its corner ornament, at the same rows.
        const Color rule = packed(0xFF0030C0u);
        c.r.rect(l, 98, w, 2, rule, Blend::Opaque);
        c.r.rect(l, 500, w, 2, rule, Blend::Opaque);
        return;
    }
    if (const Texture2D* t = c.a.texture("menu\\corner.tga")) {
        c.r.quadSpec(l, 487, w, 16, 0.97f, 0, 0.99f, 0.97f, t, Color{}, Blend::Alpha);
        c.r.quadSpec(l, 97, w, 16, 0.99f, 0.97f, 0.97f, 0, t, Color{}, Blend::Alpha);
    }
}

void header(MenuDrawContext& c, std::string_view base, float x, float y, float w, float h) {
    static const char* suffix[3] = {"_2.tga", "_0.tga", "_1.tga"};
    static const Blend blend[3] = {Blend::Alpha, Blend::Add, Blend::Alpha};
    for (int i = 0; i < 3; i++)
        if (const Texture2D* t = c.a.texture(std::string(base) + suffix[i]))
            c.r.quadSpec(x, y, w, h, 0, 0, 1, 1, t, Color{}, blend[i]);
}

void plainTitle(MenuDrawContext& c, std::string_view title, float scale) {
    shadowedText(c, 400, 50.0f - 7.5f * scale, title, orange(), Align::Center, scale);
}

void shadowedText(MenuDrawContext& c, float x, float y, std::string_view s, Color col, Align al, float scale) {
    TextStyle st;
    st.color = col;
    st.align = al;
    st.scale = scale;
    drawTextShadowed(c.r, c.a.uiFont(), x, y, s, st);
}

void panel(MenuDrawContext& c, float x, float y, float w, float h) {
    c.r.rect(x, y, w, h, packed(0x50000000u), Blend::Alpha);
}

void text(MenuDrawContext& c, float x, float y, std::string_view s, Color col, Align al, bool markup) {
    TextStyle st;
    st.color = col;
    st.align = al;
    st.markup = markup;
    drawText(c.r, c.a.uiFont(), x, y, s, st);
}

} // namespace widgets

void drawTextButton(MenuDrawContext& c, const RectF& hit, std::string_view label, bool focused, bool disabled,
                    float textScale) {
    c.r.rect(hit.x, hit.y, hit.w, hit.h, packed(kFocusBox), Blend::Alpha);
    Color col = disabled ? kDark : (focused ? orange() : rust());
    if (focused && !disabled) {
        Color p = pulse(2, 0, c.mt);
        c.r.outline(hit.x, hit.y, hit.w, hit.h, {p.r, p.g * 0.63f, 0, 1}, Blend::Add);
    }
    if (textScale == 1) {
        widgets::text(c, hit.x + hit.w * 0.5f, hit.y + std::floor((hit.h - 15) * 0.5f), label, col, Align::Center);
        return;
    }
    TextStyle st;
    st.color = col;
    st.align = Align::Center;
    st.scale = textScale;
    drawText(c.r, c.a.uiFont(), hit.x + hit.w * 0.5f, hit.y + std::floor((hit.h - 15 * textScale) * 0.5f), label, st);
}

// ---------------------------------------------------------------------------
// Widget keys (frontend.md 2.4 step 3 and 2.5)
// ---------------------------------------------------------------------------
bool MenuSystem::widgetKey(Menu& m, int index, int code) {
    MenuItem& it = m.items[static_cast<size_t>(index)];
    const bool nav = code == keys::Up || code == keys::Down || code == keys::StickUp || code == keys::StickDown;
    switch (it.type) {
        case ItemType::Edit: {
            const int n = static_cast<int>(it.text.size());
            it.cursor = std::clamp(it.cursor, 0, n);
            switch (code) {
                case keys::Backspace:
                    if (it.cursor > 0) { it.text.erase(static_cast<size_t>(it.cursor - 1), 1); it.cursor--; }
                    return true;
                case keys::Delete:
                    if (it.cursor < n) it.text.erase(static_cast<size_t>(it.cursor), 1);
                    return true;
                case keys::Home: it.cursor = 0; return true;
                case keys::End: it.cursor = n; return true;
                case keys::Left: case keys::StickLeft: it.cursor = std::max(it.cursor - 1, 0); return true;
                case keys::Right: case keys::StickRight: it.cursor = std::min(it.cursor + 1, n); return true;
                case keys::Insert: it.overwrite = !it.overwrite; return true;
                case keys::Enter: sendEvent(m, index, kActivate); return true;
                case keys::Mouse1:
                    // Touch: tapping the field focuses it (and brings up the keyboard) rather
                    // than submitting it as a click does in the original.
                    return touchMode;
                default: return !plain && (nav || code == keys::Tab);
            }
        }
        case ItemType::Spinner: {
            int dir = 0;
            if (code == keys::Mouse1 && touchMode) {
                if (!it.hit.contains(px_, py_)) return true;
                dir = px_ >= it.x + kSpinnerPrevLeft && px_ < it.x + kSpinnerPrevRight ? -1 : 1;
            } else if (code == keys::Enter || code == keys::Right || code == keys::Mouse1 || code == keys::Joy1 ||
                       code == keys::WheelUp) {
                dir = 1;
            } else if (code == keys::Left || code == keys::WheelDown || code == keys::StickLeft) {
                dir = -1;
            }
            if (dir != 0) {
                cycle(it, dir);
                sendEvent(m, index, kActivate);
                return true;
            }
            // The first game's spinners keep Up, Down and Tab (mouse-driven menus); the plain
            // front end must be operable by keyboard alone, so they move the focus there.
            return !plain && (nav || code == keys::Tab);
        }
        case ItemType::Slider: {
            float before = it.value;
            if (code == keys::Enter || code == keys::Right || code == keys::StickRight) {
                it.value = std::min(it.value + it.step, static_cast<float>(it.max));
            } else if (code == keys::Left || code == keys::StickLeft) {
                it.value = std::max(it.value - it.step, static_cast<float>(it.min));
            } else if (code == keys::Mouse1) {
                if (touchMode && !it.hit.contains(px_, py_)) return true;
                const float kx = knobX(it);
                const bool onKnob = px_ >= kx && px_ < kx + 16 && py_ >= it.y - 10 && py_ < it.y + 22;
                if (onKnob || touchMode) it.dragging = true;
                widgets::sliderClick(it, px_);
                before = it.value + 1; // always report a click
            } else {
                return false;
            }
            if (it.value != before) sendEvent(m, index, kActivate);
            return true;
        }
        case ItemType::List: {
            const int n = static_cast<int>(it.entries.size());
            const int vis = std::max(it.visibleRows(), 1);
            const int before = it.selected, beforeTop = it.top;
            bool keepVisible = true;
            switch (code) {
                case keys::Up: case keys::StickUp: it.selected--; break;
                case keys::Down: case keys::StickDown: it.selected++; break;
                case keys::PageUp: it.selected -= vis; break;
                case keys::PageDown: it.selected += vis; break;
                case keys::Home: it.selected = 0; break;
                case keys::End: it.selected = n - 1; break;
                case keys::Mouse1: {
                    if (!it.hit.contains(px_, py_) && !(px_ >= it.x + it.w - 22 && px_ < it.x + it.w + 10 &&
                                                        py_ >= it.y && py_ < it.y + it.h))
                        return false;
                    ListZones z{it.x + it.w - 22};
                    if (z.upArrow(it, px_, py_)) {
                        it.selected--;
                    } else if (z.downArrow(it, px_, py_)) {
                        it.selected++;
                    } else if (z.track(it, px_, py_)) {
                        const float span = it.h - 68;
                        const int range = std::max(n - vis, 0);
                        if (span > 0 && range > 0)
                            it.top = std::clamp(static_cast<int>(std::lround((py_ - it.y - 28 - 12) / span * range)), 0, range);
                        keepVisible = false;
                    } else {
                        const int row = static_cast<int>(std::floor((py_ - it.y - 4) / 20));
                        const int e = it.top + row;
                        if (row >= 0 && row < vis && e < n && it.entries[static_cast<size_t>(e)].enabled) it.selected = e;
                    }
                    break;
                }
                default: return false;
            }
            if (n > 0) it.selected = std::clamp(it.selected, 0, n - 1);
            widgets::listFix(it, keepVisible);
            if (it.selected != before || it.top != beforeTop) sendEvent(m, index, kActivate);
            return true;
        }
        case ItemType::HeliGrid: {
            if (code == keys::Mouse1) {
                if (widgets::gridClick(it, px_, py_)) sendEvent(m, index, kActivate);
                return true;
            }
            return nav || code == keys::Enter || code == keys::Left || code == keys::Right;
        }
        case ItemType::Custom:
            return it.onKey ? it.onKey(it, code) : false;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
namespace {

void drawLabel(MenuDrawContext& c, MenuItem& it, bool focused) {
    Color col = it.disabled() ? kDark : (focused ? Color{} : kGrey);
    Align al = (it.flags & itemflag::AlignRight) ? Align::Right : (it.flags & itemflag::AlignCenter) ? Align::Center : Align::Left;
    widgets::text(c, it.x, it.y, it.label, col, al, (it.flags & itemflag::Markup) != 0);
}

void drawButton(MenuDrawContext& c, MenuItem& it, bool focused) {
    if (const Texture2D* t = c.a.texture(it.texture))
        c.r.quadSpec(it.hit.x, it.hit.y, it.w, it.h, it.uv.s0, it.uv.t0, it.uv.s1, it.uv.t1, t, Color{}, Blend::Alpha);
    if (focused && !it.highlight.empty())
        if (const Texture2D* t = c.a.texture(it.highlight))
            c.r.quadSpec(it.hit.x, it.hit.y, it.w, it.h, it.uv.s0, it.uv.t0, it.uv.s1, it.uv.t1, t, pulse(2, 0, c.mt), Blend::Add);
}

void drawEdit(MenuDrawContext& c, MenuItem& it, bool focused) {
    c.r.rect(it.x, it.y, it.w, 16, packed(kFocusBox), Blend::Alpha);
    widgets::text(c, it.x, it.y, it.text, Color{});
    if (focused && ((c.ms / 250) % 2) == 1) {
        const size_t cur = static_cast<size_t>(std::clamp(it.cursor, 0, static_cast<int>(it.text.size())));
        const float cx = it.x + measureText(fm(), std::string_view(it.text).substr(0, cur));
        drawNumber(c.r, c.a.uiFont(), cx, it.y, "_", 1.0f, Color{});
    }
}

void drawList(MenuDrawContext& c, MenuItem& it) {
    c.r.rect(it.x, it.y, it.w, it.h, {0, 0, 0, 0x50 / 255.0f}, Blend::Alpha);
    const int vis = std::max(it.visibleRows(), 1);
    const int n = static_cast<int>(it.entries.size());
    for (int k = 0; k < vis && it.top + k < n; k++) {
        const int e = it.top + k;
        const float ry = it.y + 4 + 20.0f * static_cast<float>(k);
        const ListEntry& le = it.entries[static_cast<size_t>(e)];
        Color col = le.enabled ? rust() : kGrey;
        if (e == it.selected) {
            c.r.rect(it.x + 4, ry, it.w - 30, 18, {0.376f, 0, 0, 0.5f}, Blend::Alpha);
            col = orange();
        }
        widgets::text(c, it.x + 10, ry, le.text, col);
    }
    const float bx = it.x + it.w - 22;
    const int range = std::max(n - vis, 1);
    const float thumbY = it.y + 28 + static_cast<float>(it.top) * (it.h - 68) / static_cast<float>(range);
    if (c.plain) {
        // Scroll bar drawn with rectangles: a track, a thumb and two filled triangles.
        const Color bar = rust();
        c.r.rect(bx + 2, it.y + 2, 18, it.h - 4, {0, 0, 0, 0.3f}, Blend::Alpha);
        c.r.outline(bx + 2, it.y + 2, 18, it.h - 4, {0.376f, 0, 0, 1}, Blend::Alpha);
        for (int k = 0; k < 9; k++) {
            const float half = 1.0f + 0.75f * static_cast<float>(k);
            c.r.rect(bx + 11 - half, it.y + 8 + static_cast<float>(k), 2 * half, 1, bar, Blend::Alpha);
            c.r.rect(bx + 11 - half, it.y + it.h - 8 - static_cast<float>(k) - 1, 2 * half, 1, bar, Blend::Alpha);
        }
        if (n > vis) {
            c.r.rect(bx + 4, thumbY, 14, 24, {0.376f, 0, 0, 1}, Blend::Alpha);
            c.r.outline(bx + 4, thumbY, 14, 24, orange(), Blend::Alpha);
        }
        return;
    }
    const Texture2D* s1 = c.a.texture("menu\\scroller_1.tga");
    struct Piece { float y, h, t0, t1; };
    const Piece up{it.y, 37, 0.711f, 1.0f}, thumb{thumbY, 24, 0.523f, 0.711f}, down{it.y + it.h - 32, 39, 0.219f, 0.523f};
    for (const Piece& p : {up, thumb, down})
        if (s1) c.r.quadSpec(bx, p.y, 32, p.h, 0, p.t0, 1, p.t1, s1, Color{}, Blend::Alpha);
}

void drawSpinner(MenuDrawContext& c, MenuItem& it, bool focused) {
    Color col = it.disabled() ? kDark : (focused ? orange() : rust());
    if (focused && !it.disabled())
        c.r.rect(it.hit.x - 2, it.hit.y - 1, it.hit.w + 4, it.hit.h + 1, packed(kFocusBox), Blend::Alpha);
    widgets::text(c, it.x - 10, it.y, it.label, col, Align::Right);
    const int n = static_cast<int>(it.values.size());
    std::string v = n > 0 ? it.values[static_cast<size_t>(std::clamp(it.index, 0, n - 1))] : std::string();
    widgets::text(c, it.x + 10, it.y, v, col);
    if (c.touchMode && !it.disabled()) {
        // Touch: arrows show where a tap cycles backwards and forwards.
        widgets::text(c, it.x - 3, it.y, "<", col);
        widgets::text(c, it.x + 16 + measureText(fm(), v), it.y, ">", col);
    }
}

void drawSlider(MenuDrawContext& c, MenuItem& it, bool focused) {
    Color col = it.disabled() ? kDark : (focused ? orange() : rust());
    widgets::text(c, it.x - 10, it.y, it.label, col, Align::Right);
    if (c.plain) {
        const float kx = knobX(it);
        c.r.rect(it.x + 8, it.y + 5, 128, 4, {0, 0, 0, 0.5f}, Blend::Alpha);
        c.r.rect(it.x + 8, it.y + 5, kx - it.x, 4, rust(), Blend::Alpha);
        c.r.outline(it.x + 8, it.y + 5, 128, 4, {0.376f, 0, 0, 1}, Blend::Alpha);
        c.r.rect(kx, it.y - 1, 16, 18, focused ? Color{0.376f, 0, 0, 1} : Color{0.188f, 0, 0, 1}, Blend::Alpha);
        c.r.outline(kx, it.y - 1, 16, 18, col, Blend::Alpha);
        return;
    }
    if (const Texture2D* t = c.a.texture("menu\\slider.tga"))
        c.r.quadSpec(it.x + 8, it.y - 2, 128, 16, 0, 0, 1, 1, t, Color{}, Blend::Alpha);
    const float kx = knobX(it);
    if (const Texture2D* t = c.a.texture("menu\\slidebutt_1.tga"))
        c.r.quadSpec(kx, it.y - 10, 16, 32, 0, 0, 1, 1, t, Color{}, Blend::Alpha);
    if (focused)
        if (const Texture2D* t = c.a.texture("menu\\slidebutt_2.tga"))
            c.r.quadSpec(kx, it.y - 10, 16, 32, 0, 0, 1, 1, t, pulse(2, 0, c.mt), Blend::Add);
}

void drawGrid(MenuDrawContext& c, MenuItem& it, float px, float py) {
    const HeliGridRef& g = it.grid;
    const int p1 = g.choice ? g.choice[0] : -1;
    const int p2 = g.choice ? g.choice[1] : -1;
    const bool two = g.twoPlayers && *g.twoPlayers;
    const int hoverCell = c.touchMode ? -1 : widgets::gridCellAt(it, px, py);
    const Texture2D* border = c.a.texture("menu\\icon_border_2.tga");
    auto cellPos = [&](int i, float& cx, float& cy) {
        cx = it.x + 72.0f * static_cast<float>(i % 5);
        cy = it.y + 72.0f * static_cast<float>(i / 5);
    };
    const int cells = std::clamp(g.count, 0, kHelicopters);
    for (int i = 0; i < cells; i++) {
        float cx, cy;
        cellPos(i, cx, cy);
        const bool locked = g.locked && g.locked[i];
        c.r.outline(cx, cy, 64, 64, {0.392f, 0, 0, 1}, Blend::Alpha);
        if (border) {
            if (i == p1 || (two && i == p2 && p2 != p1))
                c.r.quadSpec(cx - 4, cy - 4, 72, 72, 0, 0, 1, 1, border, Color{}, Blend::Add);
            if (i == hoverCell && !locked)
                c.r.quadSpec(cx - 4, cy - 4, 72, 72, 0, 0, 1, 1, border, pulse(2, 0, c.mt), Blend::Add);
        }
    }
    static const bool shifted[kHelicopters] = {true, true, true, false, false, true, false, true, true, false};
    const Texture2D* glow = c.a.texture("menu\\icons_2.tga");
    const Texture2D* body = c.a.texture("menu\\icons_1.tga");
    const Texture2D* ninth = c.a.texture("menu\\icons_3.tga");
    for (int i = 0; i < cells; i++) {
        float cx, cy;
        cellPos(i, cx, cy);
        const bool locked = g.locked && g.locked[i];
        const Color col = locked ? grey(0.251f) : Color{};
        const float ix = shifted[i] ? cx - 21 : cx;
        const int glowTimes = i == p1 ? 2 : 1;
        if (i < 9) {
            const float s0 = static_cast<float>(i % 3) * 0.332f, t0 = 0.668f - static_cast<float>(i / 3) * 0.332f;
            for (int k = 0; k < glowTimes && glow; k++)
                c.r.quadSpec(ix, cy, 85, 85, s0, t0, s0 + 0.332f, t0 + 0.332f, glow, col, Blend::Add);
            if (body) c.r.quadSpec(ix, cy, 85, 85, s0, t0, s0 + 0.332f, t0 + 0.332f, body, col, Blend::Alpha);
        } else if (ninth) {
            for (int k = 0; k < glowTimes; k++)
                c.r.quadSpec(ix, cy, 85, 85, 0.332f, 0.336f, 0.664f, 1.0f, ninth, col, Blend::Add);
            c.r.quadSpec(ix, cy, 85, 85, 0, 0.336f, 0.332f, 1.0f, ninth, col, Blend::Alpha);
        }
    }
    if (two) {
        TextStyle st;
        st.kind = FontKind::Alpha;
        st.color = packed(0xFF0000FFu);
        float cx, cy;
        if (p1 >= 0 && p1 < cells) { cellPos(p1, cx, cy); drawText(c.r, c.a.uiFont(), cx + 3, cy + 48, "P1", st); }
        if (p2 >= 0 && p2 < cells) { cellPos(p2, cx, cy); drawText(c.r, c.a.uiFont(), cx + 35, cy + 48, "P2", st); }
    }
}

} // namespace

void MenuSystem::drawBackground(Renderer2D& r, const UiAssets& a) {
    Menu* m = top();
    if (!m || !m->drawBack) return;
    MenuDrawContext c{r, a, *m, mt_, touchMode, clockMs_, plain};
    m->drawBack(c);
}

void MenuSystem::drawItems(Renderer2D& r, const UiAssets& a) {
    Menu* m = top();
    if (!m) return;
    MenuDrawContext c{r, a, *m, mt_, touchMode, clockMs_, plain};
    for (size_t i = 0; i < m->items.size(); i++) {
        MenuItem& it = m->items[i];
        if (it.hidden()) continue;
        const bool focused = static_cast<int>(i) == m->focused;
        switch (it.type) {
            case ItemType::Text: drawLabel(c, it, focused); break;
            case ItemType::Button: drawButton(c, it, focused); break;
            case ItemType::Edit: drawEdit(c, it, focused); break;
            case ItemType::List: {
                drawList(c, it);
                // Arrow highlight under the pointer (frontend.md 2.5).
                const Texture2D* s2 = plain ? nullptr : a.texture("menu\\scroller_2.tga");
                if (s2 && !touchMode) {
                    const float bx = it.x + it.w - 22;
                    if (px_ >= bx && px_ < bx + 32 && py_ >= it.y && py_ < it.y + 26)
                        r.quadSpec(bx, it.y, 32, 37, 0, 0.711f, 1, 1, s2, pulse(2, 0, mt_), Blend::Add);
                    else if (px_ >= bx && px_ < bx + 32 && py_ >= it.y + it.h - 26 && py_ < it.y + it.h)
                        r.quadSpec(bx, it.y + it.h - 32, 32, 39, 0, 0.219f, 1, 0.523f, s2, pulse(2, 0, mt_), Blend::Add);
                }
                break;
            }
            case ItemType::Spinner: drawSpinner(c, it, focused); break;
            case ItemType::Slider: drawSlider(c, it, focused); break;
            case ItemType::HeliGrid: drawGrid(c, it, px_, py_); break;
            case ItemType::Custom: if (it.draw) it.draw(c, it, focused); break;
        }
    }
    if (m->drawFront) m->drawFront(c);

    // Tooltip (frontend.md 2.6).
    if (showHints && !touchMode && m->hovered >= 0 && m->hoverTime > 1.0f) {
        const MenuItem& it = m->items[static_cast<size_t>(m->hovered)];
        if (!it.disabled() && !it.tooltip.empty()) {
            const float a8 = std::min((m->hoverTime - 1.0f) * 255.0f, 255.0f) / 255.0f;
            const float tw = measureText(fm(), it.tooltip);
            float x = px_ + 4;
            const float y = py_ + 12;
            if (x + tw + 8 > 800) x = 800 - tw - 8;
            r.rect(x, y, tw + 8, 20, {1, 1, 0.63f, a8}, Blend::Alpha);
            r.outline(x, y, tw + 8, 20, {0.251f, 0.251f, 0.251f, a8}, Blend::Alpha);
            TextStyle st;
            st.kind = FontKind::Alpha;
            st.color = {0, 0, 0, a8};
            drawText(r, a.uiFont(), x + 4, y + 2, it.tooltip, st);
        }
    }
    // Menu cursor (frontend.md 2.7), drawn last.
    if (drawCursor && !touchMode) {
        if (a.cursor1.valid()) r.quad(px_ - 4, py_ - 2, 32, 32, 0, 0, 1, 1, &a.cursor1, Color{}, Blend::Alpha);
        if (a.cursor2.valid()) r.quad(px_ - 4, py_ - 2, 32, 32, 0, 0, 1, 1, &a.cursor2, Color{}, Blend::Add);
    }
}

} // namespace as3d::ui
