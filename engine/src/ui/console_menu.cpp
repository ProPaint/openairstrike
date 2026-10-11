// The console menu view: the first game's menus laid out in the 3DS screens' pixels (see
// as3d/console_menu.h and docs/3ds-ui-design.md "AirStrike 3D menus on the console (round 4,
// R4.5)" for the layouts in pixels).
#include "as3d/console_menu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "console_px.h"

namespace as3d::ui {

namespace {

// ---------------------------------------------------------------------------
// The AirStrike 3D skin (docs/3ds-ui-design.md "Per game": dark red bars, rust edge, the game's
// label pictures at 1/2). The sequels' skins replace these in the next package.
// ---------------------------------------------------------------------------
constexpr Color kBarFill{0.21f, 0.035f, 0.02f, 0.92f};
constexpr Color kBarFillFocus{0.42f, 0.075f, 0.035f, 0.95f};
constexpr Color kBarFillOff{0.10f, 0.02f, 0.015f, 0.85f};
constexpr Color kBarEdge{0.55f, 0.16f, 0.06f, 1.0f};
constexpr Color kBarEdgeFocus{1.0f, 0.45f, 0.15f, 1.0f};
constexpr Color kLabel{0.93f, 0.24f, 0.16f, 1.0f};
constexpr Color kLabelFocus{1.0f, 0.62f, 0.42f, 1.0f};
constexpr Color kLabelOff{0.40f, 0.12f, 0.09f, 1.0f};
constexpr Color kValue{0.96f, 0.84f, 0.70f, 1.0f};
constexpr Color kArrow{0.95f, 0.38f, 0.12f, 1.0f};
constexpr Color kBody{1.0f, 0.70f, 0.30f, 1.0f};   // reading text (additive, over a dark band)
constexpr Color kTitle{1.0f, 0.30f, 0.18f, 1.0f};
constexpr Color kHintKey{0.96f, 0.84f, 0.70f, 1.0f};
constexpr Color kHintLabel{0.78f, 0.62f, 0.46f, 1.0f};
constexpr Color kGreyText{0.55f, 0.55f, 0.55f, 1.0f};

constexpr int kMargin = 4;
constexpr int kFooterTop = 204;            // the bottom row: BACK / corner buttons and the footer
constexpr int kCornerH = 36;
constexpr int kBackW = 80;
constexpr int kInfoTextTop = 28, kInfoTextBottom = 236, kInfoTextW = 376, kInfoIconIndent = 44;

// Option item ids (screens_options.cpp).
constexpr int kOptBack = 1, kOptConfKeys = 2, kOptApply = 3, kOptBrightness = 24, kOptSfx = 25, kOptMusic = 26,
              kOptCamera = 28, kOptShowFps = 43;

const FontMetrics& fm() { return FontMetrics::original(); }

using consolepx::capOffset;
using consolepx::Px;
using consolepx::textTopIn;

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
std::string stripColon(std::string s) {
    while (!s.empty() && (s.back() == ':' || s.back() == ' ')) s.pop_back();
    return s;
}

// A short label (the console's texts table: "con.<key>" in the texts file, else the default).
std::string conText(const Texts& t, const std::string& key, const char* fallback) {
    const std::string k = "con." + key;
    return t.loaded(k) ? t.get(k) : std::string(fallback);
}

bool focusedItem(const Menu& m, int i) { return m.focused == i; }

void drawBar(const Px& p, const RectF& b, bool focused, bool disabled) {
    p.rect(b, disabled ? kBarFillOff : focused ? kBarFillFocus : kBarFill);
    p.frame(b, focused ? kBarEdgeFocus : kBarEdge, focused ? 2 : 1);
}
// The focus markers either side of a centred label.
void drawMarkers(const Px& p, const RectF& b, float labelW) {
    // At the bar's ends (the mockups), when they clear the label by 8 px.
    const float lx = b.x + 6, rx = b.x + b.w - 18;
    if (lx + 12 + 8 > std::floor(b.x + b.w * 0.5f - labelW * 0.5f)) return;
    p.triangle({lx, b.y, 12, b.h}, true, kBarEdgeFocus);
    p.triangle({rx, b.y, 12, b.h}, false, kBarEdgeFocus);
}

// The helicopter icons of menu\icons_1..3.tga (menu_widgets.cpp drawGrid), `size` pixels
// (85: 1:1, 42: 1/2), its 85-texel cell's left edge at x.
void drawHeli(const Px& p, int i, float x, float y, float size, bool locked, bool chosen) {
    const Color col = locked ? grey(0.251f) : Color{};
    const Texture2D* glow = p.a.texture("menu\\icons_2.tga");
    const Texture2D* body = p.a.texture("menu\\icons_1.tga");
    const Texture2D* ninth = p.a.texture("menu\\icons_3.tga");
    const int glowTimes = chosen ? 2 : 1;
    const float vx = p.vx(x), vy = p.vy(y), vw = p.vw(size), vh = p.vh(size);
    if (i < 9) {
        const float s0 = static_cast<float>(i % 3) * 0.332f, t0 = 0.668f - static_cast<float>(i / 3) * 0.332f;
        for (int k = 0; k < glowTimes && glow; k++) p.r.quadSpec(vx, vy, vw, vh, s0, t0, s0 + 0.332f, t0 + 0.332f, glow, col, Blend::Add);
        if (body) p.r.quadSpec(vx, vy, vw, vh, s0, t0, s0 + 0.332f, t0 + 0.332f, body, col, Blend::Alpha);
    } else if (ninth) {
        for (int k = 0; k < glowTimes; k++) p.r.quadSpec(vx, vy, vw, vh, 0.332f, 0.336f, 0.664f, 1.0f, ninth, col, Blend::Add);
        p.r.quadSpec(vx, vy, vw, vh, 0, 0.336f, 0.332f, 1.0f, ninth, col, Blend::Alpha);
    }
}
// The helicopter's body sits left or right of its atlas cell's centre (drawGrid's `shifted`):
// the offset of the picture's left edge from a centred one, at `size`.
float heliShift(int i, float size) {
    static const bool shifted[kHelicopters] = {true, true, true, false, false, true, false, true, true, false};
    const float d = 10.5f * size / 85.0f;
    return i >= 0 && i < kHelicopters && shifted[i] ? -d : d;
}

// The Button pictures' fallback labels (a picture that cannot be loaded) and the short labels.
std::string buttonLabel(const Texts& t, Screen s, int id) {
    switch (s) {
        case Screen::MainMenu:
            switch (id) {
                case 1: return conText(t, "main.start", "START GAME");
                case 2: return conText(t, "main.scores", "TOP SCORES");
                case 3: return conText(t, "main.options", "OPTIONS");
                case 4: return conText(t, "main.info", "INFORMATION");
                case 5: return conText(t, "main.exit", "EXIT");
                default: break;
            }
            break;
        case Screen::Exit: return id == 1 ? conText(t, "yes", "YES") : conText(t, "no", "NO");
        case Screen::StartGame: return id == 2 ? conText(t, "start", "START") : conText(t, "back", "BACK");
        case Screen::NameEntry: return conText(t, "ok", "OK");
        case Screen::Options:
            return id == kOptApply ? conText(t, "apply", "APPLY") : id == kOptConfKeys ? conText(t, "controls", "CONTROLS")
                                                                                  : conText(t, "back", "BACK");
        case Screen::InGame:
            return id == 1 ? conText(t, "resume", "RESUME GAME") : id == 2 ? conText(t, "main.options", "OPTIONS")
                                                                          : conText(t, "quit_game", "QUIT GAME");
        case Screen::GameOver: return id == 1 ? conText(t, "restart", "RESTART") : conText(t, "quit", "QUIT");
        case Screen::MissionComplete:
            return id == 2 ? conText(t, "restart", "RESTART") : id == 1 ? conText(t, "quit", "QUIT") : conText(t, "continue", "CONTINUE");
        case Screen::GameComplete: return conText(t, "continue", "CONTINUE");
        default: break;
    }
    return conText(t, "back", "BACK");
}

// Spinner and slider labels: short forms for 320 px.
std::string rowLabel(const Texts& t, const MenuItem& it) {
    switch (it.id) {
        case kOptBrightness: return conText(t, "opt.brightness", "BRIGHTNESS");
        case kOptSfx: return conText(t, "opt.sound", "SOUND");
        case kOptMusic: return conText(t, "opt.music", "MUSIC");
        case kOptCamera: return conText(t, "opt.camera", "CAMERA");
        case kOptShowFps: return conText(t, "opt.showfps", "SHOW FPS");
        default: break;
    }
    return upper(stripColon(it.label));
}

// What a footer hint costs: the key's width (its picture's, or its name in a frame), the gap,
// the label at S.
int hintWidth(const FontMetrics& m, const ConsoleHint& h) {
    const KeyGlyphAtlas::Rect r = consoleKeyRect(h.key);
    const int key = r.empty() ? measureTextPx(m, hintKeyName(h.key), FontSize::S) + 4 : r.w;
    return key + (h.label.empty() ? 0 : 4 + measureTextPx(m, h.label, FontSize::S));
}

void setHints(ConsoleLayout& L, int x, int y, std::vector<ConsoleHint> hints) {
    L.hints = std::move(hints);
    L.hintX = x;
    L.hintY = y;
    L.hintW = consoleHintsWidth(fm(), L.hints);
}
// Hints in the bottom row right of a corner button (the line centred on the row).
void footerHints(ConsoleLayout& L, int x, std::vector<ConsoleHint> hints) {
    setHints(L, x, kFooterTop + (kCornerH - fontLinePitchPx(FontSize::S)) / 2, std::move(hints));
}

ConsoleBox bar(int item, RectF r, std::string label = {}, FontSize size = FontSize::L) {
    ConsoleBox b;
    b.kind = ConsoleBoxKind::Bar;
    b.item = item;
    b.r = r;
    b.label = std::move(label);
    b.size = size;
    return b;
}
ConsoleBox sub(ConsoleBoxKind k, int item, RectF r, int arg = 0) {
    ConsoleBox b;
    b.kind = k;
    b.item = item;
    b.r = r;
    b.arg = arg;
    return b;
}
ConsoleBox keyBox(int code, RectF r, std::string label) {
    ConsoleBox b;
    b.kind = ConsoleBoxKind::Key;
    b.arg = code;
    b.r = r;
    b.label = std::move(label);
    return b;
}

int indexOf(const Menu& m, int id) {
    for (size_t i = 0; i < m.items.size(); ++i)
        if (m.items[i].id == id && !m.items[i].hidden()) return static_cast<int>(i);
    return -1;
}

std::string itemLabel(const Texts& t, Screen s, const MenuItem& it) {
    if (it.type == ItemType::Button) return buttonLabel(t, s, it.id);
    return it.label;
}

// A row of buttons across the screen at y (equal gaps of 4 px; widths in proportion to `weights`).
void buttonRow(ConsoleLayout& L, const Menu& m, const Texts& t, Screen s, const std::vector<int>& ids, float y, float h,
               const std::vector<float>& widths) {
    std::vector<int> shown;
    std::vector<float> w;
    for (size_t k = 0; k < ids.size(); ++k) {
        const int i = indexOf(m, ids[k]);
        if (i < 0) continue;
        shown.push_back(i);
        w.push_back(widths[k]);
    }
    if (shown.empty()) return;
    float total = 0;
    for (float x : w) total += x;
    total += 4.0f * static_cast<float>(shown.size() - 1);
    float x = std::floor((static_cast<float>(kConsoleBottomW) - total) * 0.5f);
    for (size_t k = 0; k < shown.size(); ++k) {
        const MenuItem& it = m.items[static_cast<size_t>(shown[k])];
        L.boxes.push_back(bar(shown[k], {x, y, w[k], h}, itemLabel(t, s, it)));
        x += w[k] + 4;
    }
}

// The spinner arrows and value of a row, after a label column ending at labelEnd.
void spinnerRow(ConsoleLayout& L, int item, const RectF& row, float arrowsFrom) {
    L.boxes.push_back(sub(ConsoleBoxKind::SpinPrev, item, {arrowsFrom, row.y, 36, row.h}));
    L.boxes.push_back(sub(ConsoleBoxKind::SpinNext, item, {row.x + row.w - 36, row.y, 36, row.h}));
}

constexpr float kSliderFrom = 168, kSliderTo = 304; // the track's ends (pixels, 136 px)

// ---------------------------------------------------------------------------
// Per-screen layouts
// ---------------------------------------------------------------------------
void layoutMain(ConsoleLayout& L, const Menu& m, const Texts& t) {
    int row = 0;
    for (int id : {1, 2, 3, 4, 5}) {
        const int i = indexOf(m, id);
        if (i < 0) continue;
        L.boxes.push_back(bar(i, {40, static_cast<float>(kMargin + 40 * row), 240, 36}, buttonLabel(t, Screen::MainMenu, id)));
        ++row;
    }
    int hx = 8;
    const int g = indexOf(m, kChangeGameItem);
    if (g >= 0) {
        L.boxes.push_back(bar(g, {kMargin, kFooterTop, 84, kCornerH}, conText(t, "games", "GAMES")));
        hx = kMargin + 84 + 8;
    }
    footerHints(L, hx, {{HintKey::A, conText(t, "hint.select", "Select")}, {HintKey::DPad, conText(t, "hint.move", "Move")}});
}

void layoutExit(ConsoleLayout& L, const Menu& m, const Texts& t) {
    L.title = conText(t, "exit.title", "EXIT");
    L.titleY = 40;
    buttonRow(L, m, t, Screen::Exit, {1, 2}, 84, 56, {138, 138});
    footerHints(L, 8, {{HintKey::A, conText(t, "hint.select", "Select")}, {HintKey::B, conText(t, "hint.back", "Back")}});
}

void backButton(ConsoleLayout& L, const Menu& m, const Texts& t, Screen s, int id = 1) {
    const int i = indexOf(m, id);
    if (i >= 0) L.boxes.push_back(bar(i, {kMargin, kFooterTop, kBackW, kCornerH}, buttonLabel(t, s, id)));
}

void layoutStart(ConsoleLayout& L, const Menu& m, const Texts& t) {
    // The mission list: three rows of 36 px with the scroll arrows right of them.
    const int li = indexOf(m, 3);
    if (li >= 0) {
        const MenuItem& list = m.items[static_cast<size_t>(li)];
        const int n = static_cast<int>(list.entries.size());
        const int vis = 3;
        const int first = std::clamp(list.selected - 1, 0, std::max(n - vis, 0));
        L.boxes.push_back(bar(li, {kMargin, kMargin, 312, 112}));
        for (int k = 0; k < vis && first + k < n; ++k) {
            ConsoleBox b = sub(ConsoleBoxKind::ListRow, li, {kMargin, static_cast<float>(kMargin + 38 * k), 272, 36}, first + k);
            b.label = list.entries[static_cast<size_t>(first + k)].text;
            L.boxes.push_back(b);
        }
        L.boxes.push_back(sub(ConsoleBoxKind::ListUp, li, {280, kMargin, 36, 36}));
        L.boxes.push_back(sub(ConsoleBoxKind::ListDown, li, {280, kMargin + 76, 36, 36}));
    }
    // Difficulty and helicopter: spinner rows.
    float y = 122;
    float arrows = 0;
    for (int id : {4, 5, 6}) {
        const int i = indexOf(m, id);
        if (i < 0) continue;
        const MenuItem& it = m.items[static_cast<size_t>(i)];
        const std::string label = id == 6 ? conText(t, "start.heli", "HELICOPTER") : upper(stripColon(it.label));
        arrows = std::max(arrows, static_cast<float>(12 + measureTextPx(fm(), label, FontSize::L) + 8));
    }
    for (int id : {4, 5, 6}) {
        const int i = indexOf(m, id);
        if (i < 0) continue;
        const MenuItem& it = m.items[static_cast<size_t>(i)];
        const std::string label = id == 6 ? conText(t, "start.heli", "HELICOPTER") : upper(stripColon(it.label));
        const RectF row{kMargin, y, 312, 36};
        L.boxes.push_back(bar(i, row, label));
        spinnerRow(L, i, row, std::max(arrows, 120.0f));
        y += 38;
        if (y + 36 > kFooterTop) break;
    }
    backButton(L, m, t, Screen::StartGame, 1);
    const int s = indexOf(m, 2);
    if (s >= 0) L.boxes.push_back(bar(s, {216, kFooterTop, 100, kCornerH}, buttonLabel(t, Screen::StartGame, 2)));
    footerHints(L, kMargin + kBackW + 8, {{HintKey::A, conText(t, "hint.ok", "OK")}});
    L.hintRight = 216 - 4;
}

void layoutScores(ConsoleLayout& L, const Menu& m, const Texts& t) {
    L.title = conText(t, "scores.title", "TOP SCORES");
    L.titleY = 48;
    L.subtitle = conText(t, "scores.sub", "The table is on the top screen");
    L.subtitleY = 112;
    backButton(L, m, t, Screen::TopScores);
    footerHints(L, kMargin + kBackW + 8, {{HintKey::B, conText(t, "hint.back", "Back")}});
}

void layoutOptions(ConsoleLayout& L, const Menu& m, const Texts& t) {
    std::vector<int> rows;
    for (size_t i = 0; i < m.items.size(); ++i) {
        const MenuItem& it = m.items[i];
        if (it.hidden() || it.id == kOptBack || it.id == kOptApply || it.id == kOptConfKeys) continue;
        if (it.type != ItemType::Spinner && it.type != ItemType::Slider) continue;
        rows.push_back(static_cast<int>(i));
    }
    const int n = static_cast<int>(rows.size());
    const int room = kFooterTop - kMargin - kMargin;
    const int pitch = n > 0 ? std::min(38, room / n) : 38;
    float arrows = 150;
    for (int i : rows) arrows = std::max(arrows, static_cast<float>(12 + measureTextPx(fm(), rowLabel(t, m.items[static_cast<size_t>(i)]), FontSize::L) + 8));
    for (int k = 0; k < n; ++k) {
        const int i = rows[static_cast<size_t>(k)];
        const MenuItem& it = m.items[static_cast<size_t>(i)];
        const RectF row{kMargin, static_cast<float>(kMargin + pitch * k), 312, static_cast<float>(pitch - 2)};
        L.boxes.push_back(bar(i, row, rowLabel(t, it)));
        if (it.type == ItemType::Slider) L.boxes.push_back(sub(ConsoleBoxKind::SliderTrack, i, {kSliderFrom - 8, row.y, kSliderTo - kSliderFrom + 16, row.h}));
        else spinnerRow(L, i, row, arrows);
    }
    backButton(L, m, t, Screen::Options, kOptBack);
    const int ap = indexOf(m, kOptApply);
    if (ap >= 0) L.boxes.push_back(bar(ap, {216, kFooterTop, 100, kCornerH}, buttonLabel(t, Screen::Options, kOptApply)));
    footerHints(L, kMargin + kBackW + 8, {{HintKey::DPad, conText(t, "hint.change", "Change")}, {HintKey::B, conText(t, "hint.back", "Back")}});
    if (ap >= 0) L.hintRight = 216 - 4;
}

void layoutInfo(ConsoleLayout& L, const Menu& m, const Texts& t, int page, int sheet, int sheets) {
    L.title = conText(t, "info.title", "INFORMATION");
    L.titleY = 6;
    char k[32];
    std::snprintf(k, sizeof k, "info.%d.title", page + 1);
    L.subtitle = t.loaded(k) ? t.get(k) : std::string();
    const size_t paren = L.subtitle.find(" (");
    if (paren != std::string::npos) L.subtitle.erase(paren); // the desktop's "(Page 1 of 2)": the sheets say it
    if (sheets > 1) {
        char part[48];
        std::snprintf(part, sizeof part, "  (%d/%d)", sheet + 1, sheets);
        L.subtitle += part;
    }
    L.subtitleY = 26;
    L.boxes.push_back(keyBox(keys::Left, {8, 48, 148, 56}, conText(t, "info.prev", "< PREV")));
    L.boxes.push_back(keyBox(keys::Right, {164, 48, 148, 56}, conText(t, "info.next", "NEXT >")));
    backButton(L, m, t, Screen::Information);
    footerHints(L, kMargin + kBackW + 8, {{HintKey::L, conText(t, "hint.prev", "Prev")}, {HintKey::R, conText(t, "hint.next", "Next")},
                                          {HintKey::B, conText(t, "hint.back", "Back")}});
}

void layoutInGame(ConsoleLayout& L, const Menu& m, const Texts& t) {
    int row = 0;
    for (int id : {1, 2, 3}) {
        const int i = indexOf(m, id);
        if (i < 0) continue;
        L.boxes.push_back(bar(i, {40, static_cast<float>(40 + 44 * row), 240, 36}, buttonLabel(t, Screen::InGame, id)));
        ++row;
    }
    footerHints(L, 8, {{HintKey::A, conText(t, "hint.select", "Select")}, {HintKey::B, conText(t, "hint.resume", "Resume")}});
}

// The tutorial hint (R4.7): its box is on the top screen; the bottom shows the OK (160 x 56, the
// item's own id 1) over the dimmed HUD.
void layoutHint(ConsoleLayout& L, const Menu& m, const Texts& t) {
    const int i = indexOf(m, 1);
    if (i >= 0) L.boxes.push_back(bar(i, {80, 100, 160, 56}, conText(t, "hint.ok", "OK")));
    footerHints(L, 8, {{HintKey::A, conText(t, "hint.ok", "OK")}});
}

void layoutGameOver(ConsoleLayout& L, const Menu& m, const Texts& t) {
    L.title = conText(t, "gameover.title", "GAME OVER");
    L.titleY = 60;
    buttonRow(L, m, t, Screen::GameOver, {1, 2}, 128, 44, {138, 138});
    if (!L.boxes.empty()) footerHints(L, 8, {{HintKey::A, conText(t, "hint.select", "Select")}, {HintKey::DPad, conText(t, "hint.move", "Move")}});
}

void layoutMissionComplete(ConsoleLayout& L, const Menu& m, const Texts& t) {
    const int g = indexOf(m, 7);
    if (g >= 0) {
        const MenuItem& grid = m.items[static_cast<size_t>(g)];
        const int cells = std::clamp(grid.grid.count, 0, kHelicopters);
        const float x0 = std::floor((static_cast<float>(kConsoleBottomW) - (5 * 54 - 4)) * 0.5f);
        L.boxes.push_back(bar(g, {x0 - 2, kMargin - 2, 5 * 54, 2 * 54}));
        for (int c = 0; c < cells; ++c)
            L.boxes.push_back(sub(ConsoleBoxKind::GridCell, g,
                                  {x0 + 54.0f * static_cast<float>(c % 5), kMargin + 54.0f * static_cast<float>(c / 5), 50, 50}, c));
    }
    buttonRow(L, m, t, Screen::MissionComplete, {2, 1, 3}, 152, 44, {108, 84, 112});
    footerHints(L, 8, {{HintKey::A, conText(t, "hint.select", "Select")}, {HintKey::DPad, conText(t, "hint.move", "Move")}});
}

void layoutGameComplete(ConsoleLayout& L, const Menu& m, const Texts& t) {
    L.title = conText(t, "complete.title", "CONGRATULATIONS");
    L.titleY = 60;
    buttonRow(L, m, t, Screen::GameComplete, {1}, 128, 44, {160});
    footerHints(L, 8, {{HintKey::A, conText(t, "hint.select", "Select")}});
}

void layoutNameEntry(ConsoleLayout& L, const Menu& m, const Texts& t) {
    L.title = conText(t, "name.title", "ENTER YOUR NAME");
    L.titleY = 3;
    const int e = indexOf(m, 2);
    if (e >= 0) L.boxes.push_back(bar(e, {kMargin, 22, 248, 32}));
    const int del = indexOf(m, 60);
    if (del >= 0) L.boxes.push_back(bar(del, {256, 22, 60, 32}, conText(t, "del", "DEL")));
    // The keys (ids 200 + character, four rows of ten in screens_scores.cpp), 29 x 30 at a
    // 31 x 32 pitch.
    int k = 0;
    for (size_t i = 0; i < m.items.size(); ++i) {
        const MenuItem& it = m.items[i];
        if (it.id < 200 || it.hidden()) continue;
        const int col = k % 10, row = k / 10;
        const char ch = static_cast<char>(it.id - 200);
        std::string label = ch == ' ' ? conText(t, "space", "SP") : std::string(1, ch);
        // L letters; a key whose label is wider than the key (SP) at S.
        const FontSize size = measureTextPx(fm(), label, FontSize::L) <= 29 - 4 ? FontSize::L : FontSize::S;
        L.boxes.push_back(bar(static_cast<int>(i), {6.0f + 31.0f * static_cast<float>(col), 58.0f + 32.0f * static_cast<float>(row), 29, 30}, label, size));
        L.boxes.back().group = ConsoleBox::kOwnSize;
        ++k;
    }
    const int ok = indexOf(m, 1);
    if (ok >= 0) L.boxes.push_back(bar(ok, {kMargin, 188, 312, 36}, buttonLabel(t, Screen::NameEntry, 1)));
    setHints(L, 8, 225, {{HintKey::A, conText(t, "hint.type", "Type")}, {HintKey::Y, conText(t, "hint.keyboard", "Keyboard")}});
}

// The icons of an Information page's paragraphs (screens_main.cpp buildInformation).
struct InfoIcon {
    int atlas = 0; // 0 weapons, 1 missiles, 2 items
    int index = 0;
};
std::vector<InfoIcon> infoIcons(int page1) {
    std::vector<InfoIcon> v;
    auto add = [&](int atlas, std::initializer_list<int> l) {
        for (int i : l) v.push_back({atlas, i});
    };
    if (page1 == 4) add(0, {0, 1, 2, 3});
    if (page1 == 5) add(0, {5, 6, 4, 8});
    if (page1 == 6) add(0, {7, 9});
    if (page1 == 7) add(1, {0, 1, 2, 3});
    if (page1 == 8) add(1, {4});
    if (page1 == 9) add(2, {3, 1, 2, 0});
    return v;
}

int infoSheetCount(const Frontend::Facts& f) {
    if (!f.texts || !f.texts->installed()) return 1;
    const InfoPage p = consoleInfoPage(*f.texts, fm(), f.infoPage + 1, kInfoTextW, kInfoTextBottom - kInfoTextTop);
    return std::max(1, static_cast<int>(p.sheets.size()));
}

} // namespace

// ---------------------------------------------------------------------------
// Public helpers
// ---------------------------------------------------------------------------
KeyGlyphAtlas::Rect consoleKeyRect(HintKey k) {
    // apps/3ds/assets/keys.tga (out/ui3ds/buttons/set_4.json).
    switch (k) {
        case HintKey::A: return {2, 2, 15, 15};
        case HintKey::B: return {19, 2, 15, 15};
        case HintKey::X: return {36, 2, 15, 15};
        case HintKey::Y: return {53, 2, 15, 15};
        case HintKey::L: return {70, 2, 19, 15};
        case HintKey::R: return {91, 2, 19, 15};
        case HintKey::Start: return {112, 3, 39, 13};
        case HintKey::Select: return {153, 3, 45, 13};
        case HintKey::DPad: return {200, 2, 15, 15};
        default: break;
    }
    return {};
}

int consoleHintsWidth(const FontMetrics& m, const std::vector<ConsoleHint>& hints) {
    int w = 0;
    for (size_t i = 0; i < hints.size(); ++i) w += hintWidth(m, hints[i]) + (i ? 8 : 0);
    return w;
}

const ConsoleBox* ConsoleLayout::boxAt(float px, float py) const {
    for (size_t i = boxes.size(); i-- > 0;)
        if (boxes[i].r.contains(px, py)) return &boxes[i];
    return nullptr;
}

const ConsoleBox* ConsoleLayout::barOf(int item) const {
    for (const ConsoleBox& b : boxes)
        if (b.kind == ConsoleBoxKind::Bar && b.item == item) return &b;
    return nullptr;
}

std::string consoleKeyNames(const std::string& text) {
    // Order matters: {X} (the desktop's next missile) becomes {R} before {Space} becomes {X}.
    static const struct { const char* from; const char* to; } kNames[] = {
        {"{X}", "{R}"}, {"{Z}", "{Y}"}, {"{Ctrl}", "{A}"}, {"{Shift}", "{B}"}, {"{Space}", "{X}"}};
    std::string s = text;
    std::vector<std::pair<size_t, size_t>> done; // replaced ranges, not replaced again
    for (const auto& n : kNames) {
        const size_t fl = std::strlen(n.from), tl = std::strlen(n.to);
        size_t pos = 0;
        while ((pos = s.find(n.from, pos)) != std::string::npos) {
            bool taken = false;
            for (const auto& d : done)
                if (pos >= d.first && pos < d.first + d.second) taken = true;
            if (taken) {
                pos += fl;
                continue;
            }
            s.replace(pos, fl, n.to);
            for (auto& d : done)
                if (d.first > pos) d.first = d.first + tl - fl;
            size_t end = pos + tl;
            if (s.compare(end, 4, " key") == 0) {
                s.replace(end, 4, " button");
                for (auto& d : done)
                    if (d.first > end) d.first += 3;
                end += 7;
            }
            done.push_back({pos, end - pos});
            pos = end;
        }
    }
    return s;
}

std::vector<std::string> wrapText(const FontMetrics& m, std::string_view text, FontSize size, int width, bool markup) {
    std::vector<std::string> out;
    std::string line;
    bool openAtLineStart = false; // the line began inside braces (a "{" was prefixed)
    bool open = false;             // inside braces at the end of `line`
    const int space = glyphAdvancePx(m, size, ' ');
    int lineW = 0;
    size_t pos = 0;
    auto flush = [&] {
        if (line.empty()) return;
        if (markup && open) line += '}';
        out.push_back(line);
        line.clear();
        lineW = 0;
        openAtLineStart = open;
        if (markup && open) line = "{";
    };
    while (pos < text.size()) {
        while (pos < text.size() && text[pos] == ' ') ++pos;
        if (pos >= text.size()) break;
        size_t end = text.find(' ', pos);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view word = text.substr(pos, end - pos);
        pos = end;
        const int ww = measureTextPx(m, word, size, markup);
        const bool lineHasWords = lineW > 0;
        if (lineHasWords && lineW + space + ww > width) flush();
        if (lineW > 0) {
            line += ' ';
            lineW += space;
        }
        line.append(word.data(), word.size());
        lineW += ww;
        if (markup)
            for (char c : word) {
                if (c == '{') open = true;
                if (c == '}') open = false;
            }
    }
    (void)openAtLineStart;
    if (!line.empty() && line != "{") {
        if (markup && open) line += '}';
        out.push_back(line);
    }
    return out;
}

InfoPage consoleInfoPage(const Texts& texts, const FontMetrics& m, int page, int width, int height) {
    InfoPage out;
    char k[32];
    std::snprintf(k, sizeof k, "info.%d.title", page);
    out.title = texts.get(k);
    const bool credits = page == 10;
    const std::vector<InfoIcon> icons = infoIcons(page);
    const int textW = icons.empty() ? width : width - kInfoIconIndent;
    const int pitch = fontLinePitchPx(FontSize::S);
    constexpr int kParaGap = 5;
    // Paragraphs: runs of consecutive line numbers; a line wholly in braces is a heading of its own.
    struct Para {
        std::string text;
        bool heading = false;
        int gapLines = 0; // missing line numbers before it
    };
    std::vector<Para> paras;
    int last = -2;
    for (int line = 0; line < 32; ++line) {
        std::snprintf(k, sizeof k, "info.%d.%d", page, line);
        if (!texts.loaded(k)) continue;
        std::string s = texts.get(k);
        while (!s.empty() && s.front() == ' ') s.erase(s.begin());
        while (!s.empty() && s.back() == ' ') s.pop_back();
        const bool heading = s.size() >= 2 && s.front() == '{' && s.back() == '}' && s.find('}') == s.size() - 1;
        const bool joins = !credits && line == last + 1 && !paras.empty() && !paras.back().heading && !heading;
        if (joins) {
            paras.back().text += ' ';
            paras.back().text += s;
        } else {
            Para p;
            p.text = s;
            p.heading = heading;
            p.gapLines = last < 0 ? 0 : line - last - 1;
            paras.push_back(p);
        }
        last = line;
    }
    // Lines, then sheets. The icon goes with each heading on icon pages (one per paragraph group).
    std::vector<InfoLine> lines;
    int group = -1;
    for (const Para& p : paras) {
        const bool newGroup = p.gapLines > 0 || lines.empty();
        if (newGroup) ++group;
        const std::vector<std::string> wrapped = credits ? std::vector<std::string>{p.text}
                                                         : wrapText(m, p.text, FontSize::S, textW, true);
        for (size_t i = 0; i < wrapped.size(); ++i) {
            InfoLine l;
            l.text = wrapped[i];
            l.centred = credits;
            if (i == 0 && newGroup) {
                l.gapBefore = lines.empty() ? 0 : (credits ? 4 * p.gapLines : kParaGap);
                if (!icons.empty() && group < static_cast<int>(icons.size())) l.icon = group;
            }
            lines.push_back(l);
        }
    }
    std::vector<InfoLine> sheet;
    int used = 0;
    for (InfoLine l : lines) {
        const int need = (sheet.empty() ? 0 : l.gapBefore) + pitch;
        if (!sheet.empty() && used + need > height) {
            out.sheets.push_back(sheet);
            sheet.clear();
            used = 0;
        }
        if (sheet.empty()) l.gapBefore = 0;
        used += (sheet.empty() ? 0 : l.gapBefore) + pitch;
        sheet.push_back(l);
    }
    if (!sheet.empty() || out.sheets.empty()) out.sheets.push_back(sheet);
    return out;
}

FontSize consoleGroupSize(const FontMetrics& m, const std::vector<std::string>& texts, const std::vector<float>& room,
                          FontSize from) {
    const FontSize order[3] = {FontSize::T, FontSize::L, FontSize::S};
    bool started = false;
    for (FontSize sz : order) {
        if (sz == from || (from == FontSize::Auto && sz == FontSize::L)) started = true;
        if (!started) continue;
        bool fits = true;
        for (size_t i = 0; i < texts.size() && fits; ++i)
            fits = static_cast<float>(measureTextPx(m, texts[i], sz, true)) <= (i < room.size() ? room[i] : room.back());
        if (fits) return sz;
    }
    return FontSize::S;
}

std::string fitLabel(const FontMetrics& m, const std::string& text, FontSize size, float room) {
    if (static_cast<float>(measureTextPx(m, text, size, true)) <= room) return text;
    std::string out = text;
    const int dots = measureTextPx(m, "...", size);
    while (!out.empty() && static_cast<float>(measureTextPx(m, out, size, true) + dots) > room) out.pop_back();
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out + "...";
}

namespace {

// The room a box's label has: a list row's text starts 10 px in (6 kept at the right); a row with
// spinner arrows or a slider track starts its label 8 px in and ends it 4 px before them; a
// centred label keeps 4 px either side.
float labelRoom(const ConsoleLayout& L, const ConsoleBox& b) {
    if (b.kind == ConsoleBoxKind::ListRow) return b.r.w - 16;
    float end = b.r.x + b.r.w;
    bool row = false;
    for (const ConsoleBox& o : L.boxes) {
        if (o.item != b.item || b.item < 0 || o.kind == ConsoleBoxKind::Bar || o.kind == ConsoleBoxKind::Key) continue;
        end = std::min(end, o.r.x);
        row = true;
    }
    return row ? end - 4 - b.r.x - 8 : b.r.w - 8;
}

// The size rule over a laid-out screen (ConsoleBox::group): each group's size, the labels that
// do not fit even at S cut, and the spinner values' size.
void applySizeRule(ConsoleLayout& L, const Menu& m) {
    const FontMetrics& f = fm();
    std::vector<bool> done(L.boxes.size(), false);
    for (size_t i = 0; i < L.boxes.size(); ++i) {
        ConsoleBox& b = L.boxes[i];
        if (done[i] || b.label.empty() || (b.kind != ConsoleBoxKind::Bar && b.kind != ConsoleBoxKind::Key &&
                                           b.kind != ConsoleBoxKind::ListRow))
            continue;
        std::vector<size_t> members;
        for (size_t j = i; j < L.boxes.size(); ++j) {
            const ConsoleBox& o = L.boxes[j];
            if (done[j] || o.label.empty()) continue;
            bool same = j == i;
            if (!same && b.group != ConsoleBox::kOwnSize && o.group != ConsoleBox::kOwnSize) {
                if (b.kind == ConsoleBoxKind::ListRow) same = o.kind == ConsoleBoxKind::ListRow && o.item == b.item;
                else if (b.group >= 0) same = o.group == b.group;
                else same = o.group < 0 && o.kind == b.kind && o.r.w == b.r.w && o.r.h == b.r.h;
            }
            if (same) members.push_back(j);
        }
        std::vector<std::string> texts;
        std::vector<float> room;
        for (size_t j : members) {
            texts.push_back(L.boxes[j].label);
            room.push_back(labelRoom(L, L.boxes[j]));
        }
        // A list's every entry (the rows scroll: the size must not change as they do).
        if (b.kind == ConsoleBoxKind::ListRow && b.item >= 0 && b.item < static_cast<int>(m.items.size()))
            for (const ListEntry& e : m.items[static_cast<size_t>(b.item)].entries) {
                texts.push_back(e.text);
                room.push_back(labelRoom(L, b));
            }
        const FontSize sz = consoleGroupSize(f, texts, room, b.size);
        for (size_t j : members) {
            ConsoleBox& o = L.boxes[j];
            o.size = sz;
            o.label = fitLabel(f, o.label, sz, labelRoom(L, o));
            done[j] = true;
        }
    }
    // The spinner values: every value of every spinner of the screen.
    std::vector<std::string> values;
    std::vector<float> room;
    for (const ConsoleBox& b : L.boxes) {
        if (b.kind != ConsoleBoxKind::SpinPrev) continue;
        const ConsoleBox* next = nullptr;
        for (const ConsoleBox& o : L.boxes)
            if (o.item == b.item && o.kind == ConsoleBoxKind::SpinNext) next = &o;
        if (!next || b.item < 0 || b.item >= static_cast<int>(m.items.size())) continue;
        const float w = next->r.x - b.r.x - b.r.w - 4;
        const MenuItem& it = m.items[static_cast<size_t>(b.item)];
        for (const std::string& v : it.values) {
            values.push_back(upper(v));
            room.push_back(w);
        }
        if (it.type == ItemType::HeliGrid) { // "10 OF 10"
            values.push_back("10 OF 10");
            room.push_back(w);
        }
    }
    if (!values.empty()) L.valueSize = consoleGroupSize(f, values, room, FontSize::L);
}

} // namespace

// ---------------------------------------------------------------------------
// The view
// ---------------------------------------------------------------------------
bool ConsoleMenuView::handles(const Frontend& fe) {
    if (fe.sequel() || fe.plain() || !fe.menuOpen()) return false;
    switch (fe.topScreen()) {
        case Screen::MainMenu: case Screen::Exit: case Screen::StartGame: case Screen::TopScores: case Screen::NameEntry:
        case Screen::Options: case Screen::Information: case Screen::InGame: case Screen::GameOver:
        case Screen::MissionComplete: case Screen::GameComplete: case Screen::Hint:
            return true;
        default: return false;
    }
}

ConsoleLayout ConsoleMenuView::layout(const Frontend& fe) const {
    ConsoleLayout L;
    if (!handles(fe)) return L;
    const Menu* m = fe.menus().top();
    if (!m) return L;
    const Frontend::Facts f = fe.facts();
    const Texts& t = *f.texts;
    L.valid = true;
    L.screen = fe.topScreen();
    switch (L.screen) {
        case Screen::MainMenu: layoutMain(L, *m, t); break;
        case Screen::Exit: layoutExit(L, *m, t); break;
        case Screen::StartGame: layoutStart(L, *m, t); break;
        case Screen::TopScores: layoutScores(L, *m, t); break;
        case Screen::Options: layoutOptions(L, *m, t); break;
        case Screen::Information: {
            const int sheets = infoSheetCount(f);
            const int sheet = infoSheetPage_ == f.infoPage ? std::clamp(infoSheet_, 0, sheets - 1) : 0;
            layoutInfo(L, *m, t, f.infoPage, sheet, sheets);
            break;
        }
        case Screen::InGame: layoutInGame(L, *m, t); break;
        case Screen::Hint: layoutHint(L, *m, t); break;
        case Screen::GameOver: layoutGameOver(L, *m, t); break;
        case Screen::MissionComplete: layoutMissionComplete(L, *m, t); break;
        case Screen::GameComplete: layoutGameComplete(L, *m, t); break;
        case Screen::NameEntry: layoutNameEntry(L, *m, t); break;
        default: L.valid = false; break;
    }
    if (L.valid) applySizeRule(L, *m);
    // Titles: T where it fits the screen (the end screens' and the exit's), else L.
    const bool big = L.screen == Screen::GameOver || L.screen == Screen::GameComplete || L.screen == Screen::Exit;
    L.titleSize = big && measureTextPx(fm(), L.title, FontSize::T) <= kConsoleBottomW - 16 ? FontSize::T : FontSize::L;
    return L;
}

void ConsoleMenuView::applyHits(Frontend& fe) {
    if (!handles(fe)) return;
    const ConsoleLayout L = layout(fe);
    Menu* m = fe.menus().top();
    if (!m || !L.valid) return;
    for (size_t i = 0; i < m->items.size(); ++i) {
        MenuItem& it = m->items[i];
        const ConsoleBox* b = L.barOf(static_cast<int>(i));
        RectF r;
        if (b) r = b->r;
        // Information's page spinner: the NEXT button (a tap there is the next page).
        if (!b && L.screen == Screen::Information && it.type == ItemType::Spinner)
            for (const ConsoleBox& k : L.boxes)
                if (k.kind == ConsoleBoxKind::Key && k.arg == keys::Right) r = k.r;
        it.hit = {r.x * kConsoleVirtualPerPx, r.y * kConsoleVirtualPerPx, r.w * kConsoleVirtualPerPx, r.h * kConsoleVirtualPerPx};
    }
    // Information: the sheet follows the page (a page left backwards opens at its last sheet).
    if (L.screen == Screen::Information) {
        const Frontend::Facts f = fe.facts();
        if (infoSheetPage_ != f.infoPage) {
            infoSheet_ = infoBackward_ ? infoSheetCount(f) - 1 : 0;
            infoSheetPage_ = f.infoPage;
        }
        infoBackward_ = false;
    } else {
        infoSheetPage_ = -1;
    }
}

void ConsoleMenuView::filterKeys(const Frontend& fe, UiInput& in) {
    if (!handles(fe) || fe.topScreen() != Screen::Information) return;
    const Frontend::Facts f = fe.facts();
    const int sheets = infoSheetCount(f);
    if (infoSheetPage_ != f.infoPage) {
        infoSheetPage_ = f.infoPage;
        infoSheet_ = 0;
    }
    std::vector<UiEvent> kept;
    std::vector<int> dropRelease;
    for (const UiEvent& e : in.events) {
        const bool back = e.code == keys::Left || e.code == keys::PageUp;
        const bool fwd = e.code == keys::Right || e.code == keys::PageDown;
        if (e.type == UiEvent::Type::Press && (back || fwd)) {
            if (back && infoSheet_ > 0) {
                --infoSheet_;
                dropRelease.push_back(e.code);
                continue;
            }
            if (fwd && infoSheet_ < sheets - 1) {
                ++infoSheet_;
                dropRelease.push_back(e.code);
                continue;
            }
            infoBackward_ = back;
        }
        if (e.type == UiEvent::Type::Release) {
            auto it = std::find(dropRelease.begin(), dropRelease.end(), e.code);
            if (it != dropRelease.end()) {
                dropRelease.erase(it);
                continue;
            }
        }
        kept.push_back(e);
    }
    in.events = std::move(kept);
}

void ConsoleMenuView::touchInput(const Frontend& fe, bool touching, bool wasTouching, int px, int py, UiInput& out) {
    if (!handles(fe)) {
        pressKind_ = -1;
        return;
    }
    const Menu* m = fe.menus().top();
    const ConsoleLayout L = layout(fe);
    if (!m || !L.valid) return;
    auto pointAt = [&](int item) {
        if (item < 0 || item >= static_cast<int>(m->items.size())) return;
        const RectF& h = m->items[static_cast<size_t>(item)].hit;
        out.move(h.x + h.w * 0.5f, h.y + h.h * 0.5f);
    };
    auto steps = [&](int n) {
        for (int k = 0; k < std::abs(n); ++k) out.key(n > 0 ? keys::Right : keys::Left);
    };
    auto sliderSteps = [&](const MenuItem& it, const ConsoleBox& b) {
        (void)b;
        const float f = std::clamp((static_cast<float>(px) - kSliderFrom) / (kSliderTo - kSliderFrom), 0.0f, 1.0f);
        const float step = it.step != 0 ? it.step : 1.0f;
        const float target = static_cast<float>(it.min) + std::round(f * static_cast<float>(it.max - it.min) / step) * step;
        const float from = pressKind_ == static_cast<int>(ConsoleBoxKind::SliderTrack) && touching && wasTouching ? lastSliderValue_ : it.value;
        steps(static_cast<int>(std::lround((target - from) / step)));
        lastSliderValue_ = target;
    };
    if (touching && !wasTouching) {
        const ConsoleBox* b = L.boxAt(static_cast<float>(px), static_cast<float>(py));
        pressKind_ = b ? static_cast<int>(b->kind) : -1;
        pressItem_ = b ? b->item : -1;
        pressMouse_ = false;
        if (!b) return;
        const MenuItem* it = b->item >= 0 ? &m->items[static_cast<size_t>(b->item)] : nullptr;
        if (it && it->disabled()) return;
        switch (b->kind) {
            case ConsoleBoxKind::Bar:
                pointAt(b->item);
                if (!it) break;
                if (it->type == ItemType::Spinner) out.key(keys::Right);
                else if (it->type == ItemType::HeliGrid && L.screen == Screen::StartGame) out.key(keys::Right);
                else if (it->type != ItemType::Slider && it->type != ItemType::List && it->type != ItemType::HeliGrid) {
                    out.press(keys::Mouse1);
                    pressMouse_ = true;
                }
                break;
            case ConsoleBoxKind::SpinPrev:
            case ConsoleBoxKind::SpinNext:
                pointAt(b->item);
                out.key(b->kind == ConsoleBoxKind::SpinPrev ? keys::Left : keys::Right);
                break;
            case ConsoleBoxKind::SliderTrack:
                pointAt(b->item);
                if (it) sliderSteps(*it, *b);
                break;
            case ConsoleBoxKind::ListRow:
                if (it && b->arg >= 0 && b->arg < static_cast<int>(it->entries.size()) && it->entries[static_cast<size_t>(b->arg)].enabled) {
                    pointAt(b->item);
                    steps(b->arg - it->selected);
                }
                break;
            case ConsoleBoxKind::ListUp:
            case ConsoleBoxKind::ListDown:
                pointAt(b->item);
                out.key(b->kind == ConsoleBoxKind::ListUp ? keys::Left : keys::Right);
                break;
            case ConsoleBoxKind::GridCell: {
                if (!it || !it->grid.choice) break;
                const HeliGridRef& g = it->grid;
                const int n = std::clamp(g.count, 0, kHelicopters);
                if (b->arg >= n || (g.locked && g.locked[b->arg])) break;
                // The steps of padKey's walk (the next unlocked helicopter each) up to the cell.
                int c = std::clamp(g.choice[0], 0, std::max(n - 1, 0)), k = 0;
                while (c != b->arg && k < n) {
                    c = (c + 1) % n;
                    if (g.locked && g.locked[c]) continue;
                    ++k;
                }
                pointAt(b->item);
                if (c == b->arg) steps(k);
                break;
            }
            case ConsoleBoxKind::Key: out.key(b->arg); break;
        }
    } else if (touching && wasTouching) {
        if (pressKind_ == static_cast<int>(ConsoleBoxKind::SliderTrack) && pressItem_ >= 0 &&
            pressItem_ < static_cast<int>(m->items.size())) {
            const MenuItem& it = m->items[static_cast<size_t>(pressItem_)];
            if (const ConsoleBox* b = L.barOf(pressItem_)) sliderSteps(it, *b);
        }
    } else if (!touching && wasTouching) {
        if (pressMouse_) out.release(keys::Mouse1);
        pressKind_ = -1;
        pressItem_ = -1;
        pressMouse_ = false;
    }
}

// ---------------------------------------------------------------------------
// Drawing: the bottom screen
// ---------------------------------------------------------------------------
namespace {

void drawFooter(const Px& p, const ConsoleLayout& L, const KeyGlyphAtlas* keys) {
    int x = L.hintX;
    KeyHintStyle st;
    st.size = FontSize::S;
    st.keyColor = kHintKey;
    st.labelColor = kHintLabel;
    for (const ConsoleHint& h : L.hints) {
        drawKeyHint(p.r, p.font, keys, x, L.hintY, h.key, h.label, st);
        x += hintWidth(fm(), h) + 8;
    }
}

void drawLabelOrPicture(const Px& p, const MenuItem& it, const ConsoleBox& b, bool focused, bool disabled, float mt) {
    const RectF& r = b.r;
    if (it.type == ItemType::Button && !it.texture.empty()) {
        const Color c = disabled ? grey(0.35f) : Color{};
        const int w = p.halfPicture(it.texture, it.uv, r.x + r.w * 0.5f, r.y + r.h * 0.5f, r.w - 8, r.h - 2, c, Blend::Alpha);
        if (w > 0) {
            if (focused && !it.highlight.empty())
                p.halfPicture(it.highlight, it.uv, r.x + r.w * 0.5f, r.y + r.h * 0.5f, r.w - 8, r.h - 2, pulse(2, 0, mt), Blend::Add);
            if (focused) drawMarkers(p, r, static_cast<float>(w));
            return;
        }
    }
    const FontSize size = b.size; // the size rule's (applySizeRule)
    p.textIn(r, b.label, size, disabled ? kLabelOff : focused ? kLabelFocus : kLabel);
    if (focused && r.w >= 160) drawMarkers(p, r, static_cast<float>(measureTextPx(fm(), b.label, size)));
}

void drawSpinnerValue(const Px& p, const ConsoleLayout& L, int item, const std::string& value, bool focused) {
    const ConsoleBox* prev = nullptr;
    const ConsoleBox* next = nullptr;
    for (const ConsoleBox& b : L.boxes) {
        if (b.item != item) continue;
        if (b.kind == ConsoleBoxKind::SpinPrev) prev = &b;
        if (b.kind == ConsoleBoxKind::SpinNext) next = &b;
    }
    if (!prev || !next) return;
    p.triangle(prev->r, false, kArrow);
    p.triangle(next->r, true, kArrow);
    const RectF zone{prev->r.x + prev->r.w, prev->r.y, next->r.x - prev->r.x - prev->r.w, prev->r.h};
    const FontSize size = L.valueSize; // one size for the screen's values (applySizeRule)
    const std::string v = fitLabel(fm(), upper(value), size, zone.w - 4);
    p.textIn(zone, v, size, focused ? Color{1, 1, 1, 1} : kValue);
}

void drawSlider(const Px& p, const MenuItem& it, const RectF& row, bool focused) {
    const float y = std::floor(row.y + row.h * 0.5f) - 3;
    const float w = kSliderTo - kSliderFrom;
    const float range = static_cast<float>(it.max - it.min);
    const float f = range > 0 ? std::clamp((it.value - static_cast<float>(it.min)) / range, 0.0f, 1.0f) : 0.0f;
    const float fill = std::floor(w * f);
    p.rect(kSliderFrom, y, w, 6, Color{0.08f, 0.02f, 0.01f, 1});
    p.frame({kSliderFrom - 1, y - 1, w + 2, 8}, kBarEdge);
    p.rect(kSliderFrom, y, fill, 6, focused ? kBarEdgeFocus : kArrow);
    p.rect(kSliderFrom + fill - 4, std::floor(row.y + row.h * 0.5f) - 11, 9, 22, focused ? Color{1, 1, 1, 1} : kValue);
}

void drawList(const Px& p, const ConsoleLayout& L, const MenuItem& list, int item, bool focused) {
    const int n = static_cast<int>(list.entries.size());
    for (const ConsoleBox& b : L.boxes) {
        if (b.item != item) continue;
        if (b.kind == ConsoleBoxKind::ListRow) {
            const bool enabled = list.entries[static_cast<size_t>(b.arg)].enabled;
            const bool sel = b.arg == list.selected;
            drawBar(p, b.r, sel && focused, !enabled);
            if (sel && !focused) p.frame(b.r, kBarEdgeFocus, 1);
            p.textIn(b.r, b.label, b.size, !enabled ? kLabelOff : sel ? kLabelFocus : kLabel, Align::Left, 10);
        } else if (b.kind == ConsoleBoxKind::ListUp || b.kind == ConsoleBoxKind::ListDown) {
            drawBar(p, b.r, false, false);
            p.triangleUpDown(b.r, b.kind == ConsoleBoxKind::ListDown, kArrow);
        }
    }
    // The scroll position between the arrows.
    if (n > 0) {
        const float top = kMargin + 40, h = 112 - 80;
        const float y = top + std::floor(h * static_cast<float>(list.selected) / static_cast<float>(std::max(n - 1, 1)));
        p.rect(284, top, 28, h + 4, Color{0.08f, 0.02f, 0.01f, 1});
        p.rect(286, std::min(y, top + h - 2), 24, 6, kArrow);
    }
}

void drawEdit(const Px& p, const MenuItem& it, const RectF& r, bool focused, long long ms) {
    p.rect(r, Color{0.06f, 0.01f, 0.01f, 0.95f});
    p.frame(r, focused ? kBarEdgeFocus : kBarEdge, 1);
    const int ty = textTopIn(r.y, r.h, FontSize::L);
    const int w = measureTextPx(fm(), it.text, FontSize::L);
    p.text(r.x + 8, static_cast<float>(ty), it.text, FontSize::L, kValue);
    if ((ms / 400) % 2 == 0) p.rect(r.x + 8 + static_cast<float>(w) + 1, r.y + 6, 2, r.h - 12, kValue);
}

void drawGridCells(const Px& p, const ConsoleLayout& L, const MenuItem& g, int item, bool focused) {
    const int chosen = g.grid.choice ? g.grid.choice[0] : -1;
    for (const ConsoleBox& b : L.boxes) {
        if (b.item != item || b.kind != ConsoleBoxKind::GridCell) continue;
        const bool locked = g.grid.locked && g.grid.locked[b.arg];
        const bool sel = b.arg == chosen;
        p.rect(b.r, Color{0.05f, 0.01f, 0.01f, 0.9f});
        p.frame(b.r, sel ? (focused ? Color{1, 1, 1, 1} : kBarEdgeFocus) : kBarEdge, sel ? 2 : 1);
        const float size = 42;
        drawHeli(p, b.arg, std::floor(b.r.x + (b.r.w - size) * 0.5f + heliShift(b.arg, size)), std::floor(b.r.y + (b.r.h - size) * 0.5f), size,
                 locked, sel);
    }
}

} // namespace

void ConsoleMenuView::drawBottom(Renderer2D& r, const UiAssets& a, const Frontend& fe) const {
    const ConsoleLayout L = layout(fe);
    const Menu* m = fe.menus().top();
    if (!L.valid || !m) return;
    const Px p(r, a);
    const float mt = fe.menus().menuTime();
    const long long ms = fe.menus().clockMs();
    const Frontend::Facts f = fe.facts();
    // The tutorial hint leaves the HUD under it, dimmed; every other screen is on black.
    if (L.screen == Screen::Hint) p.rect(0, 0, kConsoleBottomW, kConsoleBottomH, Color{0, 0, 0, 0.6f});
    else p.rect(0, 0, kConsoleBottomW, kConsoleBottomH, Color{0, 0, 0, 1}, Blend::Opaque);
    if (L.screen == Screen::TopScores) {
        if (p.halfPicture("menu\\topscores_1.tga", {0, 0, 1, 1}, 160, static_cast<float>(L.titleY) + 8, 300, 40, Color{}, Blend::Alpha) == 0)
            p.text(160, static_cast<float>(L.titleY), L.title, FontSize::T, kTitle, Align::Center, false, TextEffect::None);
        const HighScore& best = f.profile->progress.scores[0];
        char line[192];
        std::snprintf(line, sizeof line, "%s  %lld", best.name.c_str(), static_cast<long long>(best.score));
        p.text(160, 80, conText(*f.texts, "scores.best", "BEST PILOT"), FontSize::S, kHintLabel, Align::Center);
        p.text(160, 96, line, FontSize::L, kValue, Align::Center);
    } else if (!L.title.empty()) {
        p.text(160, static_cast<float>(L.titleY), L.title, L.titleSize, kTitle, Align::Center);
    }
    if (!L.subtitle.empty()) p.text(160, static_cast<float>(L.subtitleY), L.subtitle, FontSize::S, kHintLabel, Align::Center);
    if (L.screen == Screen::Information) {
        // The page dots (10 pages) and the sheet.
        const int pages = 10;
        const float x0 = 160 - (pages * 12 - 4) * 0.5f;
        for (int i = 0; i < pages; ++i)
            p.rect(std::floor(x0 + 12.0f * static_cast<float>(i)), 118, 8, 8, i == f.infoPage ? kBarEdgeFocus : kBarFill);
    }
    for (const ConsoleBox& b : L.boxes) {
        if (b.kind == ConsoleBoxKind::Key) {
            drawBar(p, b.r, false, false);
            p.textIn(b.r, b.label, b.size, kLabel);
            continue;
        }
        if (b.kind != ConsoleBoxKind::Bar || b.item < 0) continue;
        const MenuItem& it = m->items[static_cast<size_t>(b.item)];
        const bool focused = focusedItem(*m, b.item);
        const bool disabled = it.disabled();
        switch (it.type) {
            case ItemType::List: drawList(p, L, it, b.item, focused); break;
            case ItemType::HeliGrid:
                if (L.screen == Screen::StartGame) {
                    drawBar(p, b.r, focused, disabled);
                    p.textIn(b.r, b.label, b.size, focused ? kLabelFocus : kLabel, Align::Left, 8);
                    char v[96];
                    const int c = it.grid.choice ? it.grid.choice[0] : 0;
                    std::snprintf(v, sizeof v, "%d %s %d", c + 1, conText(*f.texts, "of", "OF").c_str(), std::clamp(it.grid.count, 0, kHelicopters));
                    drawSpinnerValue(p, L, b.item, v, focused);
                } else {
                    drawGridCells(p, L, it, b.item, focused);
                }
                break;
            case ItemType::Spinner: {
                drawBar(p, b.r, focused, disabled);
                p.textIn(b.r, b.label, b.size, disabled ? kLabelOff : focused ? kLabelFocus : kLabel, Align::Left, 8);
                const std::string v = it.values.empty() ? std::string() : it.values[static_cast<size_t>(std::clamp(it.index, 0, static_cast<int>(it.values.size()) - 1))];
                drawSpinnerValue(p, L, b.item, v, focused);
                break;
            }
            case ItemType::Slider:
                drawBar(p, b.r, focused, disabled);
                p.textIn(b.r, b.label, b.size, disabled ? kLabelOff : focused ? kLabelFocus : kLabel, Align::Left, 8);
                drawSlider(p, it, b.r, focused);
                break;
            case ItemType::Edit: drawEdit(p, it, b.r, focused, ms); break;
            default:
                drawBar(p, b.r, focused, disabled);
                drawLabelOrPicture(p, it, b, focused, disabled, mt);
                break;
        }
    }
    drawFooter(p, L, keys_);
}

// ---------------------------------------------------------------------------
// Drawing: the top screen
// ---------------------------------------------------------------------------
namespace {

constexpr Color kDim{0, 0, 0, 0.72f};

// The three-layer header picture (widgets::header: `_2` alpha, `_0` add, `_1` alpha) at 1/2,
// centred at (cx, cy).
bool halfHeader(const Px& p, const std::string& base, float cx, float cy) {
    bool any = false;
    any |= p.halfPicture(base + "_2.tga", {0, 0, 1, 1}, cx, cy, 400, 64, Color{}, Blend::Alpha) > 0;
    any |= p.halfPicture(base + "_0.tga", {0, 0, 1, 1}, cx, cy, 400, 64, Color{}, Blend::Add) > 0;
    any |= p.halfPicture(base + "_1.tga", {0, 0, 1, 1}, cx, cy, 400, 64, Color{}, Blend::Alpha) > 0;
    return any;
}

void titleBand(const Px& p, const std::string& header, const std::string& text) {
    p.rect(0, 0, kConsoleTopW, 34, Color{0, 0, 0, 0.88f});
    p.rect(0, 34, kConsoleTopW, 1, kBarEdge);
    if (header.empty() || !halfHeader(p, header, 200, 17))
        p.text(200, static_cast<float>(textTopIn(0, 34, FontSize::L)), text, FontSize::L, kTitle, Align::Center);
}

std::string number(std::int64_t v) {
    char b[32];
    std::snprintf(b, sizeof b, "%lld", static_cast<long long>(v));
    return b;
}

void resultsPanel(const Px& p, const Frontend::Facts& f, float y, float mt, bool withScore) {
    const Texts& t = *f.texts;
    const LevelPlayerResult& r = f.report.players[0];
    const LevelTotals& tot = f.report.totals;
    struct Row { std::string k, v; float at; };
    std::vector<Row> rows;
    rows.push_back({stripColon(t.get("stat.enemies")), tot.enemyTotal > 0 ? std::to_string(r.kills * 100 / tot.enemyTotal) + " %" : "-", 1.0f});
    rows.push_back({stripColon(t.get("stat.stars")), std::to_string(static_cast<int>(r.stars)) + " / " + std::to_string(tot.starTotal), 1.4f});
    rows.push_back({stripColon(t.get("stat.rank")), t.get("rank." + std::to_string(f.missionRank)), 1.8f});
    if (withScore) rows.push_back({conText(t, "score", "Score"), number(f.endScore), 2.2f});
    const float h = 12 + 22.0f * static_cast<float>(rows.size());
    p.rect(40, y, 320, h, Color{0, 0, 0, std::min(mt * 2.0f, 1.0f) * 0.7f});
    float ry = y + 8;
    for (const Row& row : rows) {
        if (mt >= row.at) {
            p.text(56, ry, row.k, FontSize::L, kBody);
            p.text(344, ry, row.v, FontSize::L, kValue, Align::Right);
        }
        ry += 22;
    }
}

void drawInfoTop(const Px& p, const Frontend::Facts& f, int sheet) {
    p.rect(0, 0, kConsoleTopW, kConsoleTopH, kDim);
    const Texts& t = *f.texts;
    if (!t.installed()) {
        p.text(12, 8, t.get("info.missing.title"), FontSize::L, kTitle);
        float y = 40;
        for (int i = 0; i < 3; i++) {
            for (const std::string& l : wrapText(fm(), t.get("info.missing." + std::to_string(i)), FontSize::S, kInfoTextW))
                p.text(12, y, l, FontSize::S, kBody, Align::Left, true), y += 13;
        }
        return;
    }
    const InfoPage page = consoleInfoPage(t, fm(), f.infoPage + 1, kInfoTextW, kInfoTextBottom - kInfoTextTop);
    const int n = static_cast<int>(page.sheets.size());
    const int s = std::clamp(sheet, 0, n - 1);
    std::string title = page.title;
    const size_t paren = title.find(" (");
    if (paren != std::string::npos) title.erase(paren); // "(Page 1 of 2)": the counter says it
    p.text(12, 5, title, FontSize::L, kTitle);
    char counter[64];
    if (n > 1) std::snprintf(counter, sizeof counter, "%d/10 (%d/%d)", f.infoPage + 1, s + 1, n);
    else std::snprintf(counter, sizeof counter, "%d/10", f.infoPage + 1);
    p.text(388, 7, counter, FontSize::S, kHintLabel, Align::Right);
    p.rect(12, 22, 376, 1, kBarEdge);
    const std::vector<InfoIcon> icons = infoIcons(f.infoPage + 1);
    const float x = icons.empty() ? 12.0f : 12.0f + kInfoIconIndent;
    float y = kInfoTextTop;
    for (const InfoLine& l : page.sheets[static_cast<size_t>(s)]) {
        y += static_cast<float>(l.gapBefore);
        if (l.icon >= 0 && l.icon < static_cast<int>(icons.size())) {
            const InfoIcon& ic = icons[static_cast<size_t>(l.icon)];
            const Texture2D* atlas = ic.atlas == 0 ? &p.a.weapons : ic.atlas == 1 ? &p.a.missiles : &p.a.items;
            const SpecUv uv = ic.atlas == 0 ? weaponIconUv(ic.index) : ic.atlas == 1 ? missileIconUv(ic.index) : powerupIconUv(ic.index);
            if (atlas->valid()) p.r.quadSpec(p.vx(12), p.vy(y + 1), p.vw(33), p.vh(18), uv.s0, uv.t0, uv.s1, uv.t1, atlas, Color{}, Blend::Add);
        }
        if (l.centred) p.text(200, y, l.text, FontSize::S, kBody, Align::Center, true);
        else p.text(x, y, l.text, FontSize::S, kBody, Align::Left, true);
        y += static_cast<float>(fontLinePitchPx(FontSize::S));
    }
}

void drawScoresTop(const Px& p, const Frontend::Facts& f) {
    p.rect(0, 0, kConsoleTopW, kConsoleTopH, kDim);
    const Texts& t = *f.texts;
    p.text(200, 3, conText(t, "scores.title", "TOP SCORES"), FontSize::L, kTitle, Align::Center);
    const float y0 = 21;
    p.rect(8, y0, 384, 13, Color{0.38f, 0.0f, 0.0f, 0.6f});
    p.text(34, y0, t.get("scores.number"), FontSize::S, kBody, Align::Right);
    p.text(44, y0, t.get("scores.name"), FontSize::S, kBody);
    p.text(272, y0, t.get("scores.score"), FontSize::S, kBody, Align::Right);
    p.text(284, y0, t.get("scores.rank"), FontSize::S, kBody);
    for (int i = 0; i < kHighScoreCount; i++) {
        const HighScore& h = f.profile->progress.scores[i];
        const float y = y0 + 14 + 13.0f * static_cast<float>(i);
        const Color c = i == 0 ? kValue : kBody;
        p.text(34, y, std::to_string(i + 1), FontSize::S, c, Align::Right);
        p.text(44, y, h.name, FontSize::S, c);
        p.text(272, y, number(h.score), FontSize::S, c, Align::Right);
        p.text(284, y, t.get("rank." + std::to_string(h.rank)), FontSize::S, c);
    }
}

void highScorePanel(const Px& p, const Frontend::Facts& f, float y) {
    const Texts& t = *f.texts;
    p.rect(40, y, 320, f.endSlot >= 0 ? 62 : 40, Color{0, 0, 0, 0.7f});
    const std::string head = f.endSlot >= 0 ? conText(t, "highscore", "NEW HIGH SCORE") : conText(t, "score", "SCORE");
    p.text(56, y + 9, head, FontSize::L, kBody);
    p.text(344, y + 9, number(f.endScore), FontSize::L, kValue, Align::Right);
    if (f.endSlot >= 0) {
        char line[192];
        std::snprintf(line, sizeof line, "%s %d %s %d  -  %s", conText(t, "rankpos", "Place").c_str(), f.endSlot + 1,
                      conText(t, "of", "of").c_str(), kHighScoreCount, t.get("rank." + std::to_string(f.endRank)).c_str());
        p.text(200, y + 36, line, FontSize::S, kHintLabel, Align::Center);
    }
}


// The tutorial hint on the top screen (R4.7): the world dimmed 35 %, a box at the bottom of the
// screen sized to its text: TUTORIAL (L), the hint re-flowed at S to 356 px (the original's '^'
// breaks are its box's line ends, not paragraphs: joined with spaces), "A OK" at its lower right.
void drawHintTop(const Px& p, const Frontend::Facts& f, const KeyGlyphAtlas* keys) {
    constexpr int kHintTextW = 356;
    p.rect(0, 0, kConsoleTopW, kConsoleTopH, Color{0, 0, 0, 0.35f});
    std::string joined = f.hintText;
    for (char& ch : joined)
        if (ch == '^') ch = ' ';
    std::vector<std::string> lines = wrapText(fm(), joined, FontSize::S, kHintTextW);
    const int maxLines = (kConsoleTopH - 8 - 44) / 13;
    if (static_cast<int>(lines.size()) > maxLines) lines.resize(static_cast<size_t>(maxLines));
    const float h = 28.0f + 13.0f * static_cast<float>(lines.size()) + 22.0f;
    const RectF box{14, std::floor(static_cast<float>(kConsoleTopH) - 8 - h), kConsoleTopW - 28, h};
    p.rect(box, Color{0, 0, 0, 0.78f});
    p.frame(box, kBarEdge);
    p.text(box.x + 12, box.y + 6, f.texts ? conText(*f.texts, "hint.title", "TUTORIAL") : std::string("TUTORIAL"), FontSize::L, kTitle);
    float y = box.y + 28;
    for (const std::string& l : lines) {
        p.text(box.x + 12, y, l, FontSize::S, kBody, Align::Left, true);
        y += 13;
    }
    KeyHintStyle st;
    st.size = FontSize::S;
    st.keyColor = kHintKey;
    st.labelColor = kHintKey;
    const std::string ok = f.texts ? conText(*f.texts, "hint.ok", "OK") : std::string("OK");
    const int w = hintWidth(fm(), {HintKey::A, ok});
    drawKeyHint(p.r, p.font, keys, static_cast<int>(box.x + box.w) - 12 - w, static_cast<int>(box.y + box.h) - 19, HintKey::A, ok, st);
}

} // namespace

bool ConsoleMenuView::drawTop(Renderer2D& r, const UiAssets& a, const Frontend& fe) const {
    if (!handles(fe)) return false;
    const Menu* m = fe.menus().top();
    if (!m) return false;
    const Px p(r, a);
    const Frontend::Facts f = fe.facts();
    const Texts& t = *f.texts;
    const float mt = fe.menus().menuTime();
    switch (fe.topScreen()) {
        case Screen::MainMenu: {
            const FrontendContent& c = *f.content;
            if (c.version.empty() && c.copyright.empty()) return false;
            p.rect(0, 224, kConsoleTopW, 16, Color{0, 0, 0, 0.85f});
            p.text(6, 226, c.version, FontSize::S, kGreyText);
            p.text(394, 226, c.copyright, FontSize::S, kGreyText, Align::Right);
            return true;
        }
        case Screen::Exit:
            p.rect(0, 92, kConsoleTopW, 56, Color{0, 0, 0, 0.75f});
            p.text(200, static_cast<float>(textTopIn(92, 56, FontSize::L)), t.get("label.exit"), FontSize::L, kBody, Align::Center);
            return true;
        case Screen::Options: {
            titleBand(p, "menu\\optionsh", conText(t, "options.title", "OPTIONS"));
            if (m->focused >= 0) {
                const MenuItem& it = m->items[static_cast<size_t>(m->focused)];
                std::string help;
                switch (it.id) {
                    case kOptBrightness: help = conText(t, "help.brightness", "Brightness of the top screen."); break;
                    case kOptSfx: help = conText(t, "help.sound", "Volume of the sound effects."); break;
                    case kOptMusic: help = conText(t, "help.music", "Volume of the music."); break;
                    case kOptCamera: help = conText(t, "help.camera", "The camera's pitch over the helicopter."); break;
                    case kOptShowFps: help = conText(t, "help.showfps", "Frames per second in the corner."); break;
                    case kOptBack: help = conText(t, "help.back", "Back to the menu; the settings are saved."); break;
                    default: break;
                }
                if (!help.empty()) {
                    p.rect(0, 196, kConsoleTopW, 44, Color{0, 0, 0, 0.72f});
                    p.text(200, 201, help, FontSize::S, kBody, Align::Center);
                    const std::string how = it.type == ItemType::Slider ? conText(t, "help.slider", "Left / Right or drag the slider.")
                                            : it.type == ItemType::Spinner ? conText(t, "help.spinner", "Left / Right or tap the arrows.")
                                                                           : conText(t, "help.button", "A or tap.");
                    p.text(200, 218, how, FontSize::S, kHintLabel, Align::Center);
                }
            }
            return true;
        }
        case Screen::StartGame: {
            titleBand(p, "menu\\starth", conText(t, "start.title", "START GAME"));
            const MenuItem* list = nullptr;
            for (const MenuItem& it : m->items)
                if (it.type == ItemType::List) list = &it;
            p.rect(8, 44, 384, 101, Color{0, 0, 0, 0.72f});
            p.frame({12, 52, 86, 86}, kBarEdge);
            const int heli = f.helicopter;
            const bool locked = f.heliLocked && heli >= 0 && heli < f.rules->helicopterCount && f.heliLocked[heli];
            drawHeli(p, heli, 13 + heliShift(heli, 85), 53, 85, locked, true);
            std::string name;
            if (list && !list->entries.empty())
                name = list->entries[static_cast<size_t>(std::clamp(list->selected, 0, static_cast<int>(list->entries.size()) - 1))].text;
            const FontSize ns = measureTextPx(fm(), name, FontSize::L) <= 280 ? FontSize::L : FontSize::S;
            p.text(108, 56, name, ns, kValue);
            char line[192];
            std::snprintf(line, sizeof line, "%s %s", t.get("label.difficulty").c_str(),
                          t.get("difficulty." + std::to_string(f.difficulty)).c_str());
            p.text(108, 80, line, FontSize::S, kBody);
            std::snprintf(line, sizeof line, "%s %d %s %d", conText(t, "heli", "Helicopter").c_str(), heli + 1,
                          conText(t, "of", "of").c_str(), f.rules->helicopterCount);
            p.text(108, 96, line, FontSize::S, kBody);
            int open = 0;
            for (int i = 0; i < f.rules->helicopterCount; ++i) open += f.heliLocked && !f.heliLocked[i] ? 1 : 0;
            std::snprintf(line, sizeof line, "%d %s %d %s", open, conText(t, "of", "of").c_str(), f.rules->helicopterCount,
                          conText(t, "helis_open", "helicopters open").c_str());
            p.text(108, 112, line, FontSize::S, kHintLabel);
            return true;
        }
        case Screen::TopScores: drawScoresTop(p, f); return true;
        case Screen::Information: drawInfoTop(p, f, infoSheetPage_ == f.infoPage ? infoSheet_ : 0); return true;
        case Screen::InGame: return false; // PAUSED (the host's top list: drawConsolePlayTop)
        case Screen::Hint: drawHintTop(p, f, keys_); return true;
        case Screen::GameOver: {
            p.rect(0, 0, kConsoleTopW, kConsoleTopH, Color{0, 0, 0, std::min(mt, 1.0f) * 0.45f});
            const float alpha = mt < 2.0f ? 0.5f * mt : 1.0f;
            const float cx = 200, cy = 70;
            // gameover_3.tga (its letters with alpha) fading in, then pulsing additively over itself
            // (the desktop's gameover_0 is a picture without alpha: a black box over the world here).
            if (const Texture2D* t3 = a.texture("menu\\gameover_3.tga")) {
                p.r.quadSpec(p.vx(cx - 128), p.vy(cy - 32), p.vw(256), p.vh(64), 0, 0, 1, 1, t3, {1, 1, 1, std::min(alpha, 1.0f)}, Blend::Alpha);
                const Color g = pulse(0.25f, 3.14159265f / 2, mt);
                p.r.quadSpec(p.vx(cx - 128), p.vy(cy - 32), p.vw(256), p.vh(64), 0, 0, 1, 1, t3, {g.r * 0.5f, g.g * 0.5f, g.b * 0.5f, 1}, Blend::Add);
            }
            if (mt >= 1.0f) highScorePanel(p, f, 130);
            return true;
        }
        case Screen::NameEntry:
            p.rect(0, 0, kConsoleTopW, kConsoleTopH, Color{0, 0, 0, 0.45f});
            p.text(200, 50, conText(t, "name.top", "WELL DONE, PILOT"), FontSize::T, kTitle, Align::Center, false, TextEffect::Outline);
            highScorePanel(p, f, 110);
            p.text(200, 196, conText(t, "name.help", "Tap the keys, or Y for the keyboard."), FontSize::S, kHintLabel,
                   Align::Center, false, TextEffect::Outline);
            return true;
        case Screen::MissionComplete: {
            p.rect(0, 0, kConsoleTopW, kConsoleTopH, Color{0, 0, 0, 0.35f});
            // menu\miscompl_1.tga: "MISSION" (the lower half) and "COMPLETE" (the upper), at 1/2,
            // with miscompl_2.tga pulsing over them.
            for (int layer = 0; layer < 2; layer++) {
                const std::string path = layer == 0 ? "menu\\miscompl_1.tga" : "menu\\miscompl_2.tga";
                const Color col = layer == 0 ? Color{} : pulse(0.25f, 0, mt);
                const Blend b = layer == 0 ? Blend::Alpha : Blend::Add;
                p.halfPicture(path, {0, 0.5f, 0.765625f, 1}, 200 - 64 - 6, 34, 200, 40, col, b);
                p.halfPicture(path, {0, 0, 1, 0.5f}, 200 + 49 + 6, 34, 200, 40, col, b);
            }
            if (fe.twoPlayers()) return true;
            resultsPanel(p, f, 72, mt, true);
            return true;
        }
        case Screen::GameComplete: {
            p.rect(0, 0, kConsoleTopW, kConsoleTopH, Color{0, 0, 0, 0.55f});
            std::vector<std::string> lines;
            for (int i = 0; i < 5; i++) lines.push_back(t.get("congrats." + std::to_string(i)));
            if (!t.loaded("congrats.2")) lines[2] = t.get("congrats.missing");
            float start = 1.0f, y = 14;
            for (const std::string& l : lines) {
                const float dt = mt - start;
                start += static_cast<float>(l.size()) / 8.0f;
                const int k = dt < 0 ? 0 : std::min(static_cast<int>(8.0f * dt) + 1, static_cast<int>(l.size()));
                if (k > 0) p.text(200, y, l.substr(0, static_cast<size_t>(k)), FontSize::S, kBody, Align::Center);
                y += 15;
            }
            if (!fe.twoPlayers()) resultsPanel(p, f, 104, mt, true);
            return true;
        }
        default: break;
    }
    return false;
}

} // namespace as3d::ui
