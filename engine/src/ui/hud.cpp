// In-game HUD and tutorial hint box drawing (frontend.md 4 and 3.15; answers to issue 060 in
// frontend.md 4.9).
#include <algorithm>
#include <cmath>

#include "as3d/ui.h"

namespace as3d::ui {

namespace {

// frontend.md 4.1 colours.
const Color kFrameGrey = grey(0x80 / 255.0f);
const Color kLifeGrey = grey(0xA0 / 255.0f);
const Color kScoreColor{0.753f, 0.188f, 0.0f, 1};
const Color kCountSelected{0.816f, 0.251f, 0.0f, 1};
const Color kCountOther{0.502f, 0.031f, 0.0f, 1};

// Pieces of mainbar.tga in spec UVs (frontend.md 4.1).
constexpr SpecUv kBarFrame{0.0f, 0.8359f, 0.7031f, 1.0f};
constexpr SpecUv kBoxFrame{0.0f, 0.375f, 0.2734f, 0.6719f};
constexpr float kFillS0 = 0.0078f, kFillT0 = 0.6719f, kFillT1 = 0.8359f;

int ftol(float v) { return static_cast<int>(v); }

struct Hud {
    Renderer2D& r;
    const UiAssets& a;

    void piece(const Texture2D& tex, float x, float y, float w, float h, SpecUv uv, bool mirror, Color c,
               Blend b) const {
        if (!tex.valid()) return;
        if (mirror) std::swap(uv.s0, uv.s1);
        r.quadSpec(x, y, w, h, uv.s0, uv.t0, uv.s1, uv.t1, &tex, c, b);
    }
    void barFrame(float x, float y, bool mirror) const { piece(a.mainbar, x, y, 180, 21, kBarFrame, mirror, kFrameGrey, Blend::Add); }
    void boxFrame(float x, float y, bool mirror) const { piece(a.mainbar, x, y, 70, 39, kBoxFrame, mirror, kFrameGrey, Blend::Add); }

    // Health fill: from the left at (12, y), or growing from the right edge 788 (mirrored).
    void fill(float y, float health, bool fromRight) const {
        const float f = std::clamp(health / kFullHealth, 0.0f, 1.0f);
        if (f <= 0) return;
        const float w = 172.0f * f;
        SpecUv uv{kFillS0, kFillT0, f * 174.0f / 256.0f, kFillT1};
        piece(a.mainbar, fromRight ? 788.0f - w : 12.0f, y, w, 21, uv, fromRight, Color{}, Blend::Add);
    }

    void weapon(float boxX, float boxY, bool mirrorBox, int w) const {
        boxFrame(boxX, boxY, mirrorBox);
        SpecUv uv = weaponIconUv(w);
        if (!uv.empty()) piece(a.weapons, boxX + 1, boxY + 3, 66, 35, uv, false, Color{}, Blend::Add);
    }

    void count(float right, float y, int n, bool selected) const {
        std::string s = std::to_string(n);
        float x = static_cast<float>(ftol(right - 10.5f * static_cast<float>(s.size())));
        drawNumber(r, a.uiFont(), x, y, s, 0.75f, selected ? kCountSelected : kCountOther);
    }

    // Missile column: frames at (frameX, y0 + 41 n), packed in type order.
    void missiles(float frameX, float y0, bool mirror, float countRight, const HudPlayer& p) const {
        int n = 0;
        for (int t = 0; t < kMissileTypes; t++) {
            if (p.missiles[t] == 0) continue;
            const float y = y0 + 41.0f * static_cast<float>(n++);
            const bool sel = t == p.missileSelected;
            boxFrame(frameX, y, mirror);
            if (sel) boxFrame(frameX, y, mirror);
            piece(a.missiles, frameX + 1, y + 3, 66, 35, missileIconUv(t), false, Color{}, Blend::Alpha);
            count(countRight, y + 3, p.missiles[t], sel);
        }
    }

    // Power-up column: frames at (frameX, frameY0 + 41 n), icons at (iconX, frameY0 + 3 + 41 n).
    void powerups(float frameX, float frameY0, float iconX, bool mirror, float countRight, const HudPlayer& p) const {
        int n = 0;
        for (int k = 0; k < kPowerupSlots; k++) {
            if (p.powerups[k] == 0) continue;
            const float y = frameY0 + 41.0f * static_cast<float>(n++);
            const bool sel = k == p.powerupSelected;
            boxFrame(frameX, y, mirror);
            if (sel) boxFrame(frameX, y, mirror);
            if (k < kPowerupIconKinds)
                piece(a.items, iconX, y + 3, 66, 35, powerupIconUv(k), false, Color{}, k == 0 ? Blend::Add : Blend::Alpha);
            if (p.powerups[k] > 1) count(countRight, y + 3, p.powerups[k], sel);
        }
    }

    void lives(int n, bool fromRight) const {
        if (!a.life.valid()) return;
        for (int i = 0; i < std::min(n, 5); i++) {
            const float x = fromRight ? 753.0f - 32.0f * static_cast<float>(i) : 15.0f + 32.0f * static_cast<float>(i);
            r.quad(x, 555, 32, 32, 0, 0, 1, 1, &a.life, kLifeGrey, Blend::Add);
        }
    }
};

void drawOnePlayer(const Hud& h, const HudPlayer& p) {
    h.barFrame(10, 10, false);
    h.fill(10, p.health, false);
    h.weapon(10, 32, false, p.weapon);
    h.missiles(10, 73, false, 76, p);
    h.barFrame(610, 10, true);
    drawNumber(h.r, h.a.uiFont(), 630, 12, std::to_string(p.score), 1.0f, kScoreColor);
    h.powerups(720, 32, 721, true, 786, p);
    h.lives(p.lives, false);
}

void drawTwoPlayers(const Hud& h, const HudPlayer& p1, const HudPlayer& p2) {
    // Player 1, left column (frontend.md 4.3).
    h.lives(p1.lives, false);
    h.barFrame(10, 10, false);
    h.fill(10, p1.health, false);
    h.barFrame(10, 32, false);
    {
        std::string s = std::to_string(p1.score);
        drawNumber(h.r, h.a.uiFont(), 170.0f - 14.0f * static_cast<float>(s.size()), 34, s, 1.0f, kScoreColor);
    }
    h.weapon(10, 54, true, p1.weapon);
    h.missiles(10, 95, true, 76, p1);
    h.powerups(82, 54, 86, true, 148, p1);
    // Player 2, right column.
    h.lives(p2.lives, true);
    h.barFrame(610, 10, true);
    h.fill(10, p2.health, true);
    h.barFrame(610, 32, true);
    drawNumber(h.r, h.a.uiFont(), 630, 34, std::to_string(p2.score), 1.0f, kScoreColor);
    h.weapon(720, 54, false, p2.weapon);
    h.missiles(720, 95, false, 786, p2);
    h.powerups(648, 54, 649, true, 714, p2);
}

} // namespace

SpecUv weaponIconUv(int w) {
    // Table 0x457ae0 (frontend.md 4.4); entries 10..19 are zero (no picture).
    static const SpecUv t[10] = {
        {0.0f, 0.727f, 0.258f, 1.0f},     {0.258f, 0.727f, 0.516f, 1.0f},   {0.516f, 0.727f, 0.774f, 1.0f},
        {0.774f, 0.727f, 0.998f, 1.0f},   {0.0f, 0.18f, 0.258f, 0.453f},    {0.516f, 0.453f, 0.774f, 0.727f},
        {0.774f, 0.453f, 0.998f, 0.727f}, {0.0f, 0.453f, 0.258f, 0.727f},   {0.258f, 0.453f, 0.516f, 0.727f},
        {0.258f, 0.18f, 0.516f, 0.453f},
    };
    return w >= 0 && w < 10 ? t[w] : SpecUv{};
}

SpecUv missileIconUv(int type) {
    static const SpecUv t[kMissileTypes] = {
        {0.0f, 0.727f, 0.258f, 1.0f},   {0.516f, 0.727f, 0.774f, 1.0f}, {0.258f, 0.727f, 0.516f, 1.0f},
        {0.774f, 0.727f, 0.998f, 1.0f}, {0.0f, 0.501f, 0.258f, 0.727f},
    };
    return type >= 0 && type < kMissileTypes ? t[type] : SpecUv{};
}

SpecUv powerupIconUv(int kind) {
    static const SpecUv t[kPowerupIconKinds] = {
        {0.0f, 0.453f, 0.258f, 0.727f},
        {0.774f, 0.727f, 0.998f, 1.0f},
        {0.258f, 0.727f, 0.516f, 1.0f},
        {0.516f, 0.727f, 0.774f, 1.0f},
    };
    return kind >= 0 && kind < kPowerupIconKinds ? t[kind] : SpecUv{};
}

Typewriter typewriterText(std::string_view name, float clock) {
    Typewriter out;
    const float tau = clock - 1.0f;
    if (tau < 0 || name.empty()) return out;
    const float d = static_cast<float>(name.size()) / 8.0f;
    if (tau > d + 4.0f) return out;
    int shown = static_cast<int>(name.size());
    if (tau < d) shown = std::min(ftol(8.0f * tau) + 1, shown);
    float alpha = 1.0f;
    if (tau > d + 3.0f) alpha = 1.0f - (tau - d - 3.0f);
    if (alpha <= 0) return out;
    out.text = std::string(name.substr(0, static_cast<size_t>(shown)));
    out.alpha = std::min(alpha, 1.0f);
    out.shown = shown;
    return out;
}

bool typewriterTypes(std::string_view name, float tPrev, float tNow) {
    const float d = static_cast<float>(name.size()) / 8.0f;
    auto shownAt = [&](float clock) {
        const float tau = clock - 1.0f;
        if (tau < 0 || name.empty()) return 0;
        if (tau >= d) return static_cast<int>(name.size());
        return std::min(ftol(8.0f * tau) + 1, static_cast<int>(name.size()));
    };
    const int a = shownAt(tPrev), b = shownAt(tNow);
    if (b == a || b <= 1) return false;
    return name[static_cast<size_t>(b - 1)] != ' ';
}

float messageAlpha(float age) {
    if (age < 0 || age >= 3.0f) return 0;
    return age <= 2.0f ? 1.0f : 3.0f - age;
}

void drawHud(Renderer2D& r, const UiAssets& a, const HudState& s) {
    Hud h{r, a};
    // Level-name typewriter first (frontend.md 4.1 order).
    Typewriter tw = typewriterText(s.levelName, s.levelTime);
    if (!tw.text.empty()) {
        // The full name is measured so that the text grows rightwards from its final left edge.
        const float full = measureText(FontMetrics::original(), s.levelName);
        const float x = static_cast<float>(ftol(400.0f - full * 0.5f));
        TextStyle st;
        st.color = grey(tw.alpha);
        drawTextShadowed(r, a.uiFont(), x, 500, tw.text, st, tw.alpha);
    }
    if (s.playerCount >= 2) drawTwoPlayers(h, s.players[0], s.players[1]);
    else drawOnePlayer(h, s.players[0]);
    const float ma = messageAlpha(s.messageAge);
    if (ma > 0 && !s.message.empty()) {
        TextStyle st;
        st.align = Align::Center;
        st.color = grey(ma);
        drawTextShadowed(r, a.uiFont(), 400, 555, s.message, st, ma);
    }
    if (s.mouseCursor && a.mcCursor.valid())
        r.quad(s.mouseX - 7, s.mouseY - 7, 16, 16, 0, 0, 1, 1, &a.mcCursor, Color{}, Blend::Add);
}

// ---------------------------------------------------------------------------
// Tutorial hint box
// ---------------------------------------------------------------------------

HintLayout layoutHint(const FontMetrics& m, std::string_view text) {
    HintLayout L;
    size_t i = 0;
    while (L.lines.size() < 16) {
        size_t j = text.find('^', i);
        if (j == std::string_view::npos) j = text.size();
        std::string line(text.substr(i, j - i));
        if (line.size() > 63) line.resize(63); // the original overflows a 64-byte buffer
        L.lines.push_back(std::move(line));
        if (j >= text.size()) break;
        i = j + 1;
    }
    float widest = 0;
    for (const std::string& l : L.lines) widest = std::max(widest, measureText(m, l, 1.0f, true));
    const float n = static_cast<float>(L.lines.size());
    L.box.w = std::max(360.0f, widest + 40.0f);
    L.box.h = std::max(160.0f, 18.0f * n + 80.0f);
    L.box.x = static_cast<float>(ftol((kVirtualWidth - L.box.w) * 0.5f));
    L.box.y = static_cast<float>(ftol((kVirtualHeight - L.box.h) * 0.5f));
    L.textTop = L.box.y + 20.0f;
    L.okButton = {350.0f, L.box.y + L.box.h - 60.0f, 100.0f, 64.0f};
    return L;
}

bool drawHintPanel(Renderer2D& r, const UiAssets& a, const HintLayout& L, float u) {
    if (u < kHintOpenSeconds) {
        const float f = std::max(u, 0.0f) / kHintOpenSeconds;
        const float x = static_cast<float>(ftol(400.0f - (400.0f - L.box.x) * f));
        r.rect(x, L.box.y, L.box.w * f, L.box.h, {0, 0, 0, static_cast<float>(ftol(80.0f * f)) / 255.0f}, Blend::Alpha);
        return false;
    }
    r.rect(L.box.x, L.box.y, L.box.w, L.box.h, packed(0x50000000u), Blend::Alpha);
    TextStyle st;
    st.align = Align::Center;
    st.markup = true;
    st.color = orange();
    float y = L.textTop;
    for (const std::string& line : L.lines) {
        drawText(r, a.uiFont(), 400, y, line, st);
        y += 18.0f;
    }
    return true;
}

void drawHintOk(Renderer2D& r, const UiAssets& a, const HintLayout& L, bool focused, float mt) {
    const RectF& b = L.okButton;
    if (const Texture2D* t = a.texture("menu\\apply_ok_1.tga"))
        r.quadSpec(b.x, b.y, b.w, b.h, 0.6094f, 0, 1, 1, t, Color{}, Blend::Alpha);
    if (focused)
        if (const Texture2D* t = a.texture("menu\\apply_ok_2.tga"))
            r.quadSpec(b.x, b.y, b.w, b.h, 0.6094f, 0, 1, 1, t, pulse(2, 0, mt), Blend::Add);
}

void drawHint(Renderer2D& r, const UiAssets& a, std::string_view text) {
    Font font = a.uiFont();
    if (!font.metrics) return;
    HintLayout L = layoutHint(*font.metrics, text);
    drawHintPanel(r, a, L, 1.0f);
    drawHintOk(r, a, L, false, 0);
}

} // namespace as3d::ui
