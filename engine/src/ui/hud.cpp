// In-game HUD and tutorial hint box drawing (frontend.md 4 and 3.15, answers to issue 060 in
// frontend.md 4.9; the sequels as2/frontend.md 4 and 3.15). What goes where is the game's
// HudLayout (hud_layouts.cpp); this file holds the drawing rules.
#include <algorithm>
#include <cmath>

#include "as3d/hud_layout.h"
#include "as3d/ui.h"

namespace as3d::ui {

namespace {

int ftol(float v) { return static_cast<int>(v); }

struct Hud {
    Renderer2D& r;
    const UiAssets& a;
    const HudLayout& L;

    void piece(const Texture2D& tex, float x, float y, float w, float h, SpecUv uv, bool mirror, Color c,
               Blend b) const {
        if (!tex.valid()) return;
        if (mirror) std::swap(uv.s0, uv.s1);
        r.quadSpec(x, y, w, h, uv.s0, uv.t0, uv.s1, uv.t1, &tex, c, b);
    }
    void draw(const HudPiece& p, float x, float y, bool mirror) const {
        piece(a.mainbar, x, y, p.w, p.h, p.uv, mirror, p.color, p.blend);
    }
    // `k` pieces side by side cut from one run of the atlas (the level pips): width k p.w,
    // growing leftwards from `x` when fromRight (mirrored).
    void run(const HudPiece& p, float x, float y, int k, bool fromRight) const {
        if (k <= 0 || p.w <= 0) return;
        const float w = p.w * static_cast<float>(k);
        SpecUv uv = p.uv;
        uv.s1 = uv.s0 + (p.uv.s1 - p.uv.s0) * static_cast<float>(k);
        piece(a.mainbar, fromRight ? x - w : x, y, w, p.h, uv, fromRight, p.color, p.blend);
    }

    // Colour of an icon: white, or dimmed when not selected (AS2: alpha 0.251 for ALPHA icons,
    // grey 0.251 for ADD ones; the timer power-ups are never dimmed).
    Color iconColor(const HudIcon& ic, bool selected) const {
        if (selected || ic.alwaysFull || L.dimUnselected >= 1.0f) return Color{};
        if (ic.blend == Blend::Add) return grey(L.dimUnselected);
        return Color{1, 1, 1, L.dimUnselected};
    }
    void icon(const Texture2D& atlas, const std::vector<HudIcon>& table, int i, float x, float y, bool selected) const {
        if (i < 0 || i >= static_cast<int>(table.size()) || table[static_cast<size_t>(i)].uv.empty()) return;
        const HudIcon& ic = table[static_cast<size_t>(i)];
        piece(atlas, x, y, L.iconW, L.iconH, ic.uv, false, iconColor(ic, selected), ic.blend);
    }

    void healthFill(const HudSide& s, const HudPlayer& p) const {
        const float scale = p.maxHealth > 0 ? p.maxHealth : kFullHealth;
        if (L.fillStyle == HudFill::Proportional) {
            const float f = std::clamp(p.health / scale, 0.0f, 1.0f);
            if (f <= 0) return;
            const float w = L.fillWidth * f;
            SpecUv uv{L.fill.uv.s0, L.fill.uv.t0, f * L.fillSpanTexels / L.barAtlasW, L.fill.uv.t1};
            piece(a.mainbar, s.fillFromRight ? s.fillX - w : s.fillX, s.fillY, w, L.fill.h, uv, s.fillFromRight,
                  L.fill.color, L.fill.blend);
            return;
        }
        // Segmented (as2 4.2): n = ftol(health / maximum x 42), clamped to 0..42 (the original lets
        // it go negative); the cap is always drawn.
        const int n = std::clamp(ftol(p.health / scale * static_cast<float>(L.fillSegments)), 0, L.fillSegments);
        const float w = L.fillCap + L.fillSegment * static_cast<float>(n);
        SpecUv uv = L.fill.uv;
        uv.s1 = uv.s0 + w / L.barAtlasW;
        piece(a.mainbar, s.fillFromRight ? s.fillX - w : s.fillX, s.fillY, w, L.fill.h, uv, s.fillFromRight,
              L.fill.color, L.fill.blend);
    }

    void weaponLevel(const HudSide& s, const HudPlayer& p) const {
        const int w = p.weapon;
        if (w < 0 || w >= static_cast<int>(L.weaponLevelMax.size()) || w >= kWeaponSlots) return;
        const int mx = L.weaponLevelMax[static_cast<size_t>(w)];
        run(L.levelMax, s.levelX, s.levelY, mx, s.levelFromRight);
        run(L.levelOn, s.levelX, s.levelY, std::clamp(p.upgrades[w], 0, mx), s.levelFromRight);
    }

    void count(float right, float y, int n, bool selected) const {
        std::string s = std::to_string(n);
        float x = static_cast<float>(ftol(right - 10.5f * static_cast<float>(s.size())));
        drawNumber(r, a.uiFont(), x, y, s, L.countScale, selected ? L.countSelected : L.countOther);
    }

    // One entry of a column with its frame top at F.
    void entry(const HudColumn& c, float F, const Texture2D& atlas, const std::vector<HudIcon>& table, int i, int n,
               int countMin, bool selected) const {
        if (c.drawFrames) {
            draw(L.box, c.frameX, F, c.mirror);
            if (selected && L.selection == HudSelection::DoubleFrame) draw(L.box, c.frameX, F, c.mirror);
        }
        if (!c.iconAfterCount) icon(atlas, table, i, c.iconX, F + c.iconDy, selected);
        if (selected && L.selection == HudSelection::LineAndAlpha && c.drawLine && L.selLine.w > 0)
            draw(L.selLine, c.lineX, F + c.lineDy, c.mirror);
        if (n >= countMin) count(c.countRight, F + c.countDy, n, selected);
        if (c.iconAfterCount) icon(atlas, table, i, c.iconX, F + c.iconDy, selected);
    }

    // Missile column, packed in type order; returns the number of entries drawn.
    int missiles(const HudSide& s, const HudPlayer& p) const {
        const HudColumn& c = s.missiles;
        int n = 0;
        for (int t = 0; t < kMissileTypes; t++) {
            if (p.missiles[t] == 0) continue;
            const float F = c.firstY + c.step * static_cast<float>(n++);
            entry(c, F, a.missiles, L.missiles, t, p.missiles[t], L.missileCountMin, t == p.missileSelected);
        }
        return n;
    }

    void powerups(const HudSide& s, const HudPlayer& p, int missileEntries) const {
        const HudColumn& c = s.powerups;
        const float first = c.followsMissiles
                                ? s.missiles.firstY + s.missiles.step * static_cast<float>(missileEntries)
                                : c.firstY;
        int n = 0;
        for (int k = 0; k < kPowerupSlots; k++) {
            if (p.powerups[k] == 0) continue;
            const float F = first + c.step * static_cast<float>(n++);
            entry(c, F, a.items, L.powerups, k, p.powerups[k], L.powerupCountMin, k == p.powerupSelected);
        }
    }

    void lives(const HudSide& s, int n) const {
        if (!a.life.valid()) return;
        const HudPiece& lp = L.life;
        for (int i = 0; i < std::min(n, L.lifeIconsMax); i++) {
            const float x = s.livesX + s.livesStep * static_cast<float>(i);
            r.quadSpec(x, s.livesY, lp.w, lp.h, lp.uv.s0, lp.uv.t0, lp.uv.s1, lp.uv.t1, &a.life, lp.color, lp.blend);
        }
    }

    void score(const HudSide& s, std::int64_t value) const {
        std::string str = std::to_string(value);
        const float x = s.scoreRightAligned ? s.scoreX - 14.0f * static_cast<float>(str.size()) : s.scoreX;
        drawNumber(r, a.uiFont(), x, s.scoreY, str, 1.0f, L.scoreColor);
    }

    void player(const HudSide& s, const HudPlayer& p) const {
        int missileEntries = 0;
        for (HudElement e : s.order) {
            switch (e) {
                case HudElement::Lives: lives(s, p.lives); break;
                case HudElement::HealthFrame: draw(L.barFrame, s.barX, s.barY, s.barMirror); break;
                case HudElement::HealthFill: healthFill(s, p); break;
                case HudElement::ScoreFrame:
                    if (s.scoreFrame) draw(L.scoreFrame, s.scoreFrameX, s.scoreFrameY, s.scoreFrameMirror);
                    break;
                case HudElement::Score: score(s, p.score); break;
                case HudElement::WeaponBox: draw(L.box, s.weaponBoxX, s.weaponBoxY, s.weaponBoxMirror); break;
                case HudElement::WeaponLevel: weaponLevel(s, p); break;
                case HudElement::WeaponIcon: icon(a.weapons, L.weapons, p.weapon, s.weaponIconX, s.weaponIconY, true); break;
                case HudElement::Missiles: missileEntries = missiles(s, p); break;
                case HudElement::Powerups: powerups(s, p, missileEntries); break;
            }
        }
    }
};

SpecUv iconUv(const std::vector<HudIcon>& table, int i) {
    return i >= 0 && i < static_cast<int>(table.size()) ? table[static_cast<size_t>(i)].uv : SpecUv{};
}

} // namespace

SpecUv weaponIconUv(int w, GameId game) { return iconUv(hudLayout(game).weapons, w); }
SpecUv missileIconUv(int type, GameId game) { return iconUv(hudLayout(game).missiles, type); }
SpecUv powerupIconUv(int kind, GameId game) { return iconUv(hudLayout(game).powerups, kind); }

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
    const HudLayout& L = a.hudLayout();
    Hud h{r, a, L};
    // Level-name typewriter first (frontend.md 4.1 order).
    Typewriter tw = typewriterText(s.levelName, s.levelTime);
    if (!tw.text.empty()) {
        // The full name is measured so that the text grows rightwards from its final left edge
        // (the sequels skip '{' and '}' in the width, as2/frontend.md 2.8).
        const float full = measureText(FontMetrics::original(), s.levelName, 1.0f, L.typewriterSkipsBraces);
        const float x = static_cast<float>(ftol(400.0f - full * 0.5f));
        TextStyle st;
        st.color = grey(tw.alpha);
        drawTextShadowed(r, a.uiFont(), x, 500, tw.text, st, tw.alpha);
    }
    if (s.playerCount >= 2) {
        h.player(L.twoPlayers[0], s.players[0]);
        h.player(L.twoPlayers[1], s.players[1]);
    } else {
        h.player(L.onePlayer, s.players[0]);
    }
    const float ma = messageAlpha(s.messageAge);
    if (ma > 0 && !s.message.empty()) {
        TextStyle st;
        st.align = Align::Center;
        st.color = grey(ma);
        drawTextShadowed(r, a.uiFont(), 400, 555, s.message, st, ma);
    }
    if (s.mouseCursor && L.mouseCursor && a.mcCursor.valid())
        r.quad(s.mouseX - 7, s.mouseY - 7, 16, 16, 0, 0, 1, 1, &a.mcCursor, Color{}, Blend::Add);
}

// ---------------------------------------------------------------------------
// Tutorial hint box
// ---------------------------------------------------------------------------

namespace {

constexpr const char* kSequelHintTitle = "Tutorial Tip"; // as2@0x48eeb0
constexpr const char* kSequelOkCaption = "  Ok  ";
constexpr float kSequelOpenSeconds = 0.125f;           // a menu's open value (as2/frontend.md 2.1)
const Color kSequelGreen{0.0f, 0xB0 / 255.0f, 0.0f, 1}; // 0xFF00B000

// The panel's static noise takes fresh offsets every frame; they come from a source of their
// own, never from the world's generator (issue 240 item 2).
float panelRandom() {
    static u32 state = 0x2545F491u;
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state >> 8) / 16777216.0f;
}

// A texel rectangle of interface.tga (256 x 256) at its size, white, ALPHA.
void panelPiece(Renderer2D& r, const Texture2D& t, float x, float y, float tx, float ty, float tw, float th) {
    const SpecUv uv = texelUv(tx, ty, tw, th, 256, 256);
    r.quadSpec(x, y, tw, th, uv.s0, uv.t0, uv.s1, uv.t1, &t, Color{}, Blend::Alpha);
}
// A piece repeated along x over `len`, in pieces of at most tw texels, the last one cut.
void panelRunX(Renderer2D& r, const Texture2D& t, float x, float y, float len, float tx, float ty, float tw, float th) {
    for (float o = 0; o < len; o += tw) panelPiece(r, t, x + o, y, tx, ty, std::min(tw, len - o), th);
}
// The vertical cable from y0 down to y1, in pieces of at most 90, cut from its top end.
void panelCable(Renderer2D& r, const Texture2D& t, float x, float y0, float y1) {
    for (float y = y0; y < y1; y += 90.0f) panelPiece(r, t, x, y, 0, 82, 9, std::min(90.0f, y1 - y));
}

// UI_DrawPanel (as2/frontend.md 3.1) at opening value f, titled when `title` is not empty.
void drawSequelPanel(Renderer2D& r, const UiAssets& a, float x, float y, float w, float h, float f,
                     std::string_view title) {
    f = std::clamp(f, 0.0f, 1.0f);
    const float yt = (y - 15.0f) - (1.0f - f) * y;
    const float yb = (y + h - 13.0f) + (1.0f - f) * (kVirtualHeight - y - h);
    if (a.panelNoise.valid()) {
        const float H = f * f * h + kVirtualHeight * f * (1.0f - f);
        const float r1 = panelRandom(), r2 = panelRandom();
        r.quadSpec(x, y + h * 0.5f - H * 0.5f, w, H, r1, r2, r1 + w / 256.0f, r2 + H / 256.0f, &a.panelNoise, Color{},
                   Blend::Filter);
    }
    if (!a.panel.valid()) return;
    const Texture2D& t = a.panel;
    const bool titled = !title.empty();
    if (titled) {
        panelPiece(r, t, x - 4, yt, 9, 9, 67, 28);
        panelRunX(r, t, x + 63, yt, w - 120, 76, 9, 111, 28);
        panelPiece(r, t, x + w - 57, yt, 187, 9, 61, 28);
    } else {
        panelPiece(r, t, x - 4, yt, 9, 9, 20, 28);
        panelRunX(r, t, x + 16, yt, w - 32, 76, 9, 111, 28);
        panelPiece(r, t, x + w - 16, yt, 228, 9, 20, 28);
    }
    panelPiece(r, t, x - 4, yb, 9, 42, 67, 31);
    panelRunX(r, t, x + 63, yb, w - 120, 76, 42, 111, 31);
    panelPiece(r, t, x + w - 57, yb, 187, 42, 61, 31);
    if (titled) {
        panelCable(r, t, x + 46, -40, yt - 25);
        panelCable(r, t, x + w - 50, -15, yt);
    }
    panelCable(r, t, x + 46, yb + 31, kVirtualHeight);
    panelCable(r, t, x + w - 50, yb + 31, kVirtualHeight);
    if (titled) {
        const float wt = measureText(FontMetrics::original(), title, 1.0f, true);
        panelPiece(r, t, x - 4, yt - 32, 105, 180, 65, 39);
        panelRunX(r, t, x + 61, yt - 32, wt, 170, 180, 64, 39);
        panelPiece(r, t, x + 61 + wt, yt - 32, 234, 180, 15, 39);
        TextStyle st;
        st.align = Align::Center;
        st.color = orange();
        drawText(r, a.uiFont(), x + 33 + wt * 0.5f, yt - 20, title, st);
    }
}

bool sequelPanel(const UiAssets& a) { return a.hudLayout().hint == HintStyle::SequelPanel && a.panel.valid(); }

} // namespace

HintLayout layoutHint(const FontMetrics& m, std::string_view text, HintStyle style) {
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
    L.box.h = std::max(160.0f, 18.0f * n + (style == HintStyle::SequelPanel ? 60.0f : 80.0f));
    L.box.x = static_cast<float>(ftol((kVirtualWidth - L.box.w) * 0.5f));
    L.box.y = static_cast<float>(ftol((kVirtualHeight - L.box.h) * 0.5f));
    if (style == HintStyle::SequelPanel) {
        L.textTop = L.box.y + (L.box.h - 18.0f * n) * 0.5f;
        const float bw = measureText(m, kSequelOkCaption, 1.0f, true) + 46.0f;
        L.okButton = {400.0f - bw * 0.5f, 520.0f, bw, 30.0f};
    } else {
        L.textTop = L.box.y + 20.0f;
        L.okButton = {350.0f, L.box.y + L.box.h - 60.0f, 100.0f, 64.0f};
    }
    return L;
}

bool drawHintPanel(Renderer2D& r, const UiAssets& a, const HintLayout& L, float u) {
    TextStyle st;
    st.align = Align::Center;
    st.markup = true;
    st.color = orange();
    if (sequelPanel(a)) {
        const float f = std::max(u, 0.0f) / kSequelOpenSeconds;
        drawSequelPanel(r, a, L.box.x, L.box.y, L.box.w, L.box.h, f, kSequelHintTitle);
        if (f < 1.0f) return false;
        float y = L.textTop;
        for (const std::string& line : L.lines) {
            drawText(r, a.uiFont(), 400, y, line, st);
            y += 18.0f;
        }
        return true;
    }
    if (u < kHintOpenSeconds) {
        const float f = std::max(u, 0.0f) / kHintOpenSeconds;
        const float x = static_cast<float>(ftol(400.0f - (400.0f - L.box.x) * f));
        r.rect(x, L.box.y, L.box.w * f, L.box.h, {0, 0, 0, static_cast<float>(ftol(80.0f * f)) / 255.0f}, Blend::Alpha);
        return false;
    }
    r.rect(L.box.x, L.box.y, L.box.w, L.box.h, packed(0x50000000u), Blend::Alpha);
    float y = L.textTop;
    for (const std::string& line : L.lines) {
        drawText(r, a.uiFont(), 400, y, line, st);
        y += 18.0f;
    }
    return true;
}

void drawHintOk(Renderer2D& r, const UiAssets& a, const HintLayout& L, bool focused, float mt) {
    const RectF& b = L.okButton;
    if (sequelPanel(a)) {
        // The text button (as2/frontend.md 2.5), fully slid in: frame, caption, rivets.
        const Texture2D& t = a.panel;
        const float w = b.w - 46.0f;
        panelPiece(r, t, b.x, b.y, 18, 79, 23, 30);
        panelRunX(r, t, b.x + 23, b.y, w, 41, 79, 40, 30);
        panelPiece(r, t, b.x + 23 + w, b.y, 80, 79, 23, 30);
        TextStyle st;
        st.align = Align::Center;
        st.color = focused ? orange() : kSequelGreen;
        drawText(r, a.uiFont(), b.x + 23 + w * 0.5f, b.y + 7, kSequelOkCaption, st);
        panelPiece(r, t, b.x + 25, b.y + 21, 0, 230, 26, 15);
        panelPiece(r, t, b.x + w - 6, b.y + 21, 0, 230, 26, 15);
        panelPiece(r, t, b.x + 25, b.y - 5, 0, 215, 26, 15);
        panelPiece(r, t, b.x + w - 6, b.y - 5, 0, 215, 26, 15);
        return;
    }
    if (const Texture2D* t = a.texture("menu\\apply_ok_1.tga"))
        r.quadSpec(b.x, b.y, b.w, b.h, 0.6094f, 0, 1, 1, t, Color{}, Blend::Alpha);
    if (focused)
        if (const Texture2D* t = a.texture("menu\\apply_ok_2.tga"))
            r.quadSpec(b.x, b.y, b.w, b.h, 0.6094f, 0, 1, 1, t, pulse(2, 0, mt), Blend::Add);
}

void drawHint(Renderer2D& r, const UiAssets& a, std::string_view text) {
    Font font = a.uiFont();
    if (!font.metrics) return;
    if (sequelPanel(a)) {
        // Items first, then the panel over them, as the original's screens (as2/frontend.md 3.1).
        HintLayout L = layoutHint(*font.metrics, text, HintStyle::SequelPanel);
        drawHintOk(r, a, L, false, 0);
        drawHintPanel(r, a, L, 1.0f);
        return;
    }
    HintLayout L = layoutHint(*font.metrics, text);
    drawHintPanel(r, a, L, 1.0f);
    drawHintOk(r, a, L, false, 0);
}

} // namespace as3d::ui
