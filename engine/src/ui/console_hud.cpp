// The first game's in-play screens on the 3DS: the pad reminder and the pressed tile over the
// console HUD, and the top screen in play (see as3d/console_hud.h and docs/3ds-ui-design.md "In
// play on the console (round 4, R4.7)").
#include "as3d/console_hud.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "as3d/hud_layout.h"
#include "console_px.h"

namespace as3d::ui {

namespace {

using consolepx::Px;
using consolepx::textTopIn;

constexpr Color kKeyColor{0.96f, 0.84f, 0.70f, 1.0f};
constexpr Color kKeyLabel{0.78f, 0.62f, 0.46f, 1.0f};
constexpr Color kHeadColor{0.96f, 0.84f, 0.70f, 1.0f};
constexpr float kVirtualPerPx = 2.5f; // the bottom screen's 0.4 mapping, inverted
constexpr int kKeyPitch = 18;         // the reminder's rows (the key pictures are 15 px high)
constexpr int kTopW = 400;            // the top screen

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
std::string trim(std::string s) {
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

} // namespace

const ConsoleHudKey* consoleHudKeys(int* count) {
    static const ConsoleHudKey keys[] = {
        {HintKey::B, "Missile"}, {HintKey::X, "Power-up"}, {HintKey::Y, "Weapon"}, {HintKey::R, "Next missile"}};
    if (count) *count = static_cast<int>(sizeof keys / sizeof keys[0]);
    return keys;
}

void drawConsoleHudExtras(Renderer2D& r, const UiAssets& a, const KeyGlyphAtlas* keys, const RectF* pressed) {
    const HudLayout& L = a.hudLayout();
    const Px p(r, a);
    if (pressed && pressed->w > 0) {
        p.rect(*pressed, Color{1, 0.85f, 0.6f, 0.18f}, Blend::Add);
        p.frame(*pressed, Color{1, 0.9f, 0.65f, 1});
    }
    if (L.keysArea.w <= 0 || !p.font.metrics) return;
    const int x = static_cast<int>(std::lround(L.keysArea.x / kVirtualPerPx));
    int y = static_cast<int>(std::lround(L.keysArea.y / kVirtualPerPx));
    KeyHintStyle st;
    st.size = FontSize::S;
    st.keyColor = kKeyColor;
    st.labelColor = kKeyLabel;
    int n = 0;
    const ConsoleHudKey* k = consoleHudKeys(&n);
    for (int i = 0; i < n; ++i, y += kKeyPitch) drawKeyHint(r, p.font, keys, x + 2, y + 2, k[i].key, k[i].label, st);
}

ConsoleLevelTitle consoleLevelTitle(const FontMetrics& m, const std::string& name) {
    ConsoleLevelTitle t;
    const size_t c = name.find(':');
    if (c == std::string::npos) {
        t.title = upper(trim(name));
    } else {
        t.head = upper(trim(name.substr(0, c)));
        t.title = upper(trim(name.substr(c + 1)));
    }
    if (measureTextPx(m, t.title, FontSize::T, true) > kTopW - 8) t.titleSize = FontSize::L;
    return t;
}

bool consolePlayTopVisible(const ConsolePlayTop& s) {
    if (s.paused) return true;
    if (!typewriterText(s.levelName, s.levelTime).text.empty()) return true;
    return messageAlpha(s.messageAge) > 0 && !s.message.empty();
}

bool drawConsolePlayTop(Renderer2D& r, const UiAssets& a, const ConsolePlayTop& s) {
    const Px p(r, a);
    if (!p.font.metrics) return false;
    const FontMetrics& m = *p.font.metrics;
    const float W = static_cast<float>(kTopW);
    bool drew = false;
    if (s.paused) {
        p.rect(0, 0, W, 240, Color{0, 0, 0, 0.45f});
        p.text(W * 0.5f, static_cast<float>(textTopIn(96, 40, FontSize::T)), "PAUSED", FontSize::T, orange(), Align::Center,
               false, TextEffect::Outline);
        drew = true;
    } else {
        const Typewriter tw = typewriterText(s.levelName, s.levelTime);
        if (!tw.text.empty()) {
            // The typed part of the name, split as the whole name is (head over title); each line
            // grows rightwards from the left edge of its full text, centred on the screen.
            const ConsoleLevelTitle full = consoleLevelTitle(m, s.levelName);
            const bool split = !full.head.empty();
            std::string head, title;
            const size_t colon = tw.text.find(':');
            if (!split) {
                title = upper(trim(tw.text));
            } else {
                head = upper(trim(tw.text.substr(0, colon)));
                if (colon != std::string::npos) title = upper(trim(tw.text.substr(colon + 1)));
            }
            const float y0 = 14;
            if (split) {
                const int w = measureTextPx(m, full.head, FontSize::L, true);
                p.text(std::floor((W - static_cast<float>(w)) * 0.5f), y0, head, FontSize::L,
                       Color{kHeadColor.r, kHeadColor.g, kHeadColor.b, tw.alpha}, Align::Left, true, TextEffect::Outline);
            }
            const int tw2 = measureTextPx(m, full.title, full.titleSize, true);
            const Color oc = orange();
            p.text(std::floor((W - static_cast<float>(tw2)) * 0.5f), y0 + (split ? 16.0f : 0.0f), title, full.titleSize,
                   Color{oc.r, oc.g, oc.b, tw.alpha}, Align::Left, true, TextEffect::Outline);
            drew = true;
        }
    }
    const float ma = messageAlpha(s.messageAge);
    if (ma > 0 && !s.message.empty()) {
        p.text(W * 0.5f, 206, s.message, FontSize::S, Color{1, 1, 1, ma}, Align::Center, false, TextEffect::Outline);
        drew = true;
    }
    return drew;
}

} // namespace as3d::ui
