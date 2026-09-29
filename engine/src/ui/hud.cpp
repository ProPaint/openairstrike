// In-game HUD and tutorial hint box. Layout: engine-behaviour.md 11.2 and 11.3. The two-player
// layout, stars, upgrades and boss bar are our own design (docs/spec/issues/060-hud-gaps.md).
#include <algorithm>
#include <cmath>

#include "as3d/ui.h"

namespace as3d::ui {

namespace {

const Color kScoreColor{0.75f, 0.19f, 0.0f, 1};
const Color kMissileSelected{0.82f, 0.25f, 0.0f, 1};
const Color kMissileIdle{0.5f, 0.125f, 0.0f, 1};

// Draws in "column space" (the left-hand layout); mirror flips x around the 800 wide field.
struct Panel {
    Renderer2D& r;
    const UiAssets& a;
    bool mirror;

    float mx(float x, float w) const { return mirror ? kVirtualWidth - x - w : x; }

    // Pixel rectangle (px0,py0)-(px1,py1) of `tex` drawn at (x, y) with size w x h.
    void pix(const Texture2D& tex, float x, float y, float w, float h, float px0, float py0, float px1, float py1,
             Color c, Blend b, bool flipUv = false) const {
        if (!tex.valid()) return;
        float tw = static_cast<float>(tex.width()), th = static_cast<float>(tex.height());
        float s0 = px0 / tw, s1 = px1 / tw, t0 = py0 / th, t1 = py1 / th;
        if (mirror && flipUv) std::swap(s0, s1);
        r.quad(mx(x, w), y, w, h, s0, t0, s1, t1, &tex, c, b);
    }

    void text(float x, float y, float w, std::string_view s, Align align, float scale, Color c,
              bool flipAlign = true) const {
        // x, w describe the anchor box in column space; align is in column space too.
        Align al = align;
        float ax;
        if (mirror) {
            float bx = mx(x, w);
            ax = align == Align::Left ? bx + w : (align == Align::Right ? bx : bx + w * 0.5f);
            if (flipAlign) {
                al = align == Align::Left ? Align::Right : (align == Align::Right ? Align::Left : Align::Center);
            } else {
                ax = align == Align::Left ? bx : (align == Align::Right ? bx + w : bx + w * 0.5f);
            }
        } else {
            ax = align == Align::Left ? x : (align == Align::Right ? x + w : x + w * 0.5f);
        }
        TextStyle st;
        st.scale = scale;
        st.color = c;
        st.align = al;
        drawText(r, a.uiFont(), ax, y, s, st);
    }

    void healthBar(float x, float y, float health, float width = 180.0f) const {
        pix(a.mainbar, x, y, width, 21, 0, 0, 180, 21, grey(0.5f), Blend::Add, true);
        float f = std::clamp(health / kFullHealth, 0.0f, 1.0f);
        if (f <= 0) return;
        pix(a.mainbar, x + 2, y, f * (width - 8), 21, 0, 21, f * 174.0f, 42, Color{}, Blend::Add, true);
    }

    // Count in the bottom-right corner of a frame, on a dark backing so it reads over the icon.
    void count(float x, float y, int n, Color c) const {
        std::string s = std::to_string(n);
        float w = measureText(FontMetrics::original(), s, 0.75f);
        r.rect(mx(x, 70) + 64 - w - 2, y + 24, w + 4, 13, {0, 0, 0, 0.6f}, Blend::Alpha);
        TextStyle st;
        st.scale = 0.75f;
        st.color = c;
        st.align = Align::Right;
        drawText(r, a.uiFont(), mx(x, 70) + 64, y + 25, s, st);
    }

    void frame(float x, float y, Color c) const { pix(a.mainbar, x, y, 70, 39, 0, 42, 70, 81, c, Blend::Add, true); }

    void weaponBox(float x, float y, const HudPlayer& p) const {
        frame(x, y, grey(0.5f));
        if (p.weapon >= 0 && p.weapon < 12) {
            float cx = static_cast<float>(p.weapon % 4) * 64.0f, cy = static_cast<float>(p.weapon / 4) * 34.0f;
            pix(a.weapons, x + 1, y + 3, 66, 35, cx, cy, cx + 64, cy + 34, Color{}, Blend::Add);
        }
        for (int i = 0; i < std::min(p.weaponLevel, 8); i++)
            r.rect(mx(x + 4 + 7.0f * static_cast<float>(i), 5), y + 4, 5, 3, {1.0f, 0.85f, 0.1f, 1}, Blend::Add);
    }

    void missileFrames(float x, float y0, const HudPlayer& p) const {
        int k = 0;
        for (int t = 0; t < kMissileTypes; t++) {
            if (p.missiles[t] < 0) continue;
            float y = y0 + 41.0f * static_cast<float>(k++);
            bool sel = t == p.missileSelected;
            frame(x, y, grey(0.5f));
            if (sel) frame(x, y, grey(0.5f));
            float cx = static_cast<float>(t % 4) * 64.0f, cy = static_cast<float>(t / 4) * 34.0f;
            pix(a.missiles, x + 1, y + 2, 66, 35, cx, cy, cx + 64, cy + 34, Color{}, Blend::Alpha);
            count(x, y, p.missiles[t], sel ? kMissileSelected : kMissileIdle);
        }
    }

    void powerups(float x, float y0, const HudPlayer& p) const {
        int k = 0;
        for (int t = 0; t < kPowerupKinds; t++) {
            if (p.powerups[t] <= 0) continue;
            float y = y0 + 41.0f * static_cast<float>(k++);
            frame(x, y, grey(0.5f));
            float cx = static_cast<float>(t) * 64.0f;
            pix(a.items, x + 1, y + 2, 66, 35, cx, 0, cx + 64, 34, Color{}, Blend::Alpha);
            if (p.powerups[t] > 1)
                count(x, y, p.powerups[t], kMissileSelected);
        }
    }

    void lives(int n) const {
        for (int i = 0; i < std::min(n, 5); i++) pix(a.life, 15 + 32.0f * static_cast<float>(i), 555, 32, 32, 0, 0, 32, 32, grey(0.63f), Blend::Add);
    }

    void stars(float x, float y, float w, int n, Align al) const {
        if (n <= 0) return;
        text(x, y, w, "STARS " + std::to_string(n), al, 0.75f, {0.9f, 0.75f, 0.1f, 1});
    }
};

void drawOnePlayer(Renderer2D& r, const UiAssets& a, const HudPlayer& p) {
    Panel left{r, a, false}, right{r, a, true};
    left.healthBar(10, 10, p.health);
    left.weaponBox(10, 32, p);
    left.missileFrames(10, 73, p);
    left.lives(p.lives);
    // Score bar: the health bar frame mirrored.
    right.pix(a.mainbar, 10, 10, 180, 21, 0, 0, 180, 21, grey(0.5f), Blend::Add, true);
    left.text(630, 12, 150, std::to_string(p.score), Align::Left, 1.0f, kScoreColor);
    right.powerups(10, 32, p);
    left.stars(0, 562, 788, p.stars, Align::Right);
}

void drawTwoPlayers(Renderer2D& r, const UiAssets& a, const HudPlayer& p, int side) {
    Panel c{r, a, side == 1};
    c.healthBar(10, 10, p.health);
    c.text(12, 33, 176, std::to_string(p.score), Align::Left, 0.75f, kScoreColor);
    c.weaponBox(10, 48, p);
    c.missileFrames(10, 89, p);
    c.powerups(86, 48, p);
    c.lives(p.lives);
    c.stars(12, 532, 176, p.stars, Align::Left);
}

} // namespace

Typewriter typewriterText(std::string_view name, float t) {
    Typewriter out;
    t -= 1.0f;
    if (t < 0 || name.empty()) return out;
    const float n = static_cast<float>(name.size());
    size_t shown = static_cast<size_t>(std::min(std::floor(t * 8.0f) + 1.0f, n));
    float tFull = n / 8.0f;
    float since = t - tFull;
    float alpha = since <= 3.0f ? 1.0f : 1.0f - (since - 3.0f);
    if (alpha <= 0) return out;
    out.text = std::string(name.substr(0, shown));
    out.alpha = std::min(alpha, 1.0f);
    return out;
}

float messageAlpha(float age) {
    if (age < 0 || age >= 3.0f) return 0;
    return age <= 2.0f ? 1.0f : 3.0f - age;
}

void drawHud(Renderer2D& r, const UiAssets& a, const HudState& s) {
    if (s.playerCount >= 2) {
        drawTwoPlayers(r, a, s.players[0], 0);
        drawTwoPlayers(r, a, s.players[1], 1);
    } else {
        drawOnePlayer(r, a, s.players[0]);
    }
    if (s.bossHealth >= 0) {
        Panel c{r, a, false};
        c.healthBar(250, 10, s.bossHealth * kFullHealth, 300);
    }
    Typewriter tw = typewriterText(s.levelName, s.levelTime);
    if (!tw.text.empty()) {
        TextStyle st;
        st.color = {1, 1, 1, tw.alpha};
        // The full name is measured so that the text does not shift while it is typed.
        float full = measureText(FontMetrics::original(), s.levelName);
        st.align = Align::Left;
        drawText(r, a.uiFont(), 400.0f - full * 0.5f, 500, tw.text, st);
    }
    float ma = messageAlpha(s.messageAge);
    if (ma > 0 && !s.message.empty()) {
        TextStyle st;
        st.align = Align::Center;
        st.color = {1, 0.85f, 0.3f, ma};
        drawText(r, a.uiFont(), 400, 110, s.message, st);
    }
}

static std::vector<std::string> splitHintLines(std::string_view text) {
    std::vector<std::string> out;
    size_t i = 0;
    while (true) {
        size_t j = text.find('^', i);
        if (j == std::string_view::npos) j = text.size();
        out.emplace_back(text.substr(i, j - i));
        if (j >= text.size()) break;
        i = j + 1;
    }
    return out;
}

HintLayout layoutHint(const FontMetrics& m, std::string_view text, float maxLineWidth) {
    HintLayout L;
    for (const std::string& para : splitHintLines(text)) {
        if (measureText(m, para, 1.0f, true) <= maxLineWidth) {
            L.lines.push_back(para);
        } else {
            for (std::string& w : wrapText(m, para, maxLineWidth)) L.lines.push_back(std::move(w));
        }
        if (L.lines.size() >= 16) { L.lines.resize(16); break; }
    }
    float widest = 0;
    for (const std::string& l : L.lines) widest = std::max(widest, measureText(m, l, 1.0f, true));
    const float n = static_cast<float>(L.lines.size());
    L.box.w = std::max(360.0f, widest + 40.0f);
    L.box.h = std::max(160.0f, 18.0f * n + 80.0f);
    L.box.x = std::floor((kVirtualWidth - L.box.w) * 0.5f);
    L.box.y = std::floor((kVirtualHeight - L.box.h) * 0.5f);
    L.textTop = L.box.y + 20.0f;
    L.okButton = {L.box.x + std::floor((L.box.w - 80.0f) * 0.5f), L.box.y + L.box.h - 44.0f, 80.0f, 28.0f};
    return L;
}

void drawHint(Renderer2D& r, const UiAssets& a, std::string_view text) {
    Font font = a.uiFont();
    if (!font.metrics) return;
    HintLayout L = layoutHint(*font.metrics, text);
    r.fullscreen({0, 0, 0, 0.45f}, Blend::Alpha);
    r.rect(L.box.x, L.box.y, L.box.w, L.box.h, {0.04f, 0.04f, 0.06f, 0.92f}, Blend::Alpha);
    const Color orange{0.82f, 0.3f, 0.0f, 1};
    r.outline(L.box.x, L.box.y, L.box.w, L.box.h, orange, Blend::Alpha);
    r.outline(L.box.x + 2, L.box.y + 2, L.box.w - 4, L.box.h - 4, {0.4f, 0.15f, 0.0f, 1}, Blend::Alpha);
    TextStyle st;
    st.align = Align::Center;
    st.markup = true;
    st.color = {0.85f, 0.85f, 0.85f, 1};
    float y = L.textTop;
    for (const std::string& line : L.lines) {
        drawText(r, font, L.box.x + L.box.w * 0.5f, y, line, st);
        y += 18.0f;
    }
    r.rect(L.okButton.x, L.okButton.y, L.okButton.w, L.okButton.h, {0.3f, 0.1f, 0.0f, 0.9f}, Blend::Alpha);
    r.outline(L.okButton.x, L.okButton.y, L.okButton.w, L.okButton.h, orange, Blend::Alpha);
    TextStyle ok;
    ok.align = Align::Center;
    ok.color = {1, 1, 1, 1};
    drawText(r, font, L.okButton.x + L.okButton.w * 0.5f, L.okButton.y + 7, "OK", ok);
}

} // namespace as3d::ui
