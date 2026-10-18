// The console menu view: the first game's menus laid out in the 3DS screens' pixels (see
// as3d/console_menu.h and docs/3ds-ui-design.md "AirStrike 3D menus on the console (round 4,
// R4.5)" for the layouts in pixels).
#include "as3d/console_menu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "as2_screens.h"
#include "console_px.h"
#include "launcher_marquee.h"

namespace as3d::ui {

namespace {

// ---------------------------------------------------------------------------
// The AirStrike 3D skin (docs/3ds-ui-design.md "Per game": dark red bars, rust edge, the game's
// label pictures at 1/2). The sequels' skins replace these in the next package.
// ---------------------------------------------------------------------------
// Each game's look (docs/3ds-ui-design.md "Per game"): AirStrike 3D dark red bars with a rust
// edge and its label pictures at 1/2; AirStrike 2 grey chrome bars with a notched corner and green
// text on black; Gulf Thunder grey chrome on its green scan lines, white-green text, red titles.
struct ConsoleSkin {
    Color barFill, barFillFocus, barFillOff, barEdge, barEdgeFocus;
    Color label, labelFocus, labelOff, value, arrow, body, title, hintKey, hintLabel, greyText;
    Color ground;          // the bottom screen's ground (Opaque)
    bool scanLines;        // Gulf Thunder: its scan lines over the ground
    bool chamfer;          // the sequels: the bars' top-right corner notched
    bool labelPictures;    // the first game: its Button pictures at 1/2
};
constexpr ConsoleSkin kSkinAs3d{
    {0.21f, 0.035f, 0.02f, 0.92f}, {0.42f, 0.075f, 0.035f, 0.95f}, {0.10f, 0.02f, 0.015f, 0.85f},
    {0.55f, 0.16f, 0.06f, 1.0f}, {1.0f, 0.45f, 0.15f, 1.0f},
    {0.93f, 0.24f, 0.16f, 1.0f}, {1.0f, 0.62f, 0.42f, 1.0f}, {0.40f, 0.12f, 0.09f, 1.0f}, {0.96f, 0.84f, 0.70f, 1.0f},
    {0.95f, 0.38f, 0.12f, 1.0f}, {1.0f, 0.70f, 0.30f, 1.0f}, {1.0f, 0.30f, 0.18f, 1.0f}, {0.96f, 0.84f, 0.70f, 1.0f},
    {0.78f, 0.62f, 0.46f, 1.0f}, {0.55f, 0.55f, 0.55f, 1.0f},
    {0, 0, 0, 1}, false, false, true};
// AirStrike 2: the ink green (0xFF00B000) and the orange accent (0xFF00A0FF) of its menus.
constexpr ConsoleSkin kSkinAs2{
    {0.085f, 0.09f, 0.085f, 0.94f}, {0.16f, 0.27f, 0.16f, 0.96f}, {0.05f, 0.05f, 0.05f, 0.85f},
    {0.52f, 0.54f, 0.52f, 1.0f}, {0.62f, 0.95f, 0.55f, 1.0f},
    {0.10f, 0.80f, 0.10f, 1.0f}, {0.62f, 1.0f, 0.55f, 1.0f}, {0.25f, 0.30f, 0.25f, 1.0f}, {0.88f, 0.94f, 0.86f, 1.0f},
    {0.30f, 0.90f, 0.25f, 1.0f}, {0.10f, 0.80f, 0.10f, 1.0f}, {1.0f, 0.63f, 0.0f, 1.0f}, {0.88f, 0.94f, 0.86f, 1.0f},
    {0.58f, 0.66f, 0.56f, 1.0f}, {0.55f, 0.55f, 0.55f, 1.0f},
    {0, 0, 0, 1}, false, true, false};
// Gulf Thunder: light grey (0xFFBFBFBF) and red (0xFF0000FF) on green scan lines.
constexpr ConsoleSkin kSkinGulf{
    {0.07f, 0.11f, 0.07f, 0.92f}, {0.20f, 0.30f, 0.20f, 0.96f}, {0.04f, 0.06f, 0.04f, 0.85f},
    {0.60f, 0.62f, 0.60f, 1.0f}, {0.92f, 1.0f, 0.90f, 1.0f},
    {0.84f, 0.95f, 0.84f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.30f, 0.36f, 0.30f, 1.0f}, {0.92f, 0.96f, 0.90f, 1.0f},
    {0.85f, 0.95f, 0.85f, 1.0f}, {0.80f, 0.86f, 0.80f, 1.0f}, {1.0f, 0.15f, 0.10f, 1.0f}, {0.92f, 0.96f, 0.90f, 1.0f},
    {0.62f, 0.70f, 0.62f, 1.0f}, {0.55f, 0.55f, 0.55f, 1.0f},
    {0.0f, 0.03f, 0.0f, 1}, true, true, false};
const ConsoleSkin* g_skin = &kSkinAs3d;
const ConsoleSkin& sk() { return *g_skin; }
bool gulfGame(const Frontend& fe) {
    const Frontend::Facts f = fe.facts();
    return f.content && f.content->game && f.content->game->id == GameId::GulfThunder;
}
// Sets the skin of the front end being laid out or drawn (and the sequels' look of their tables).
void useSkin(const Frontend& fe) {
    g_skin = !fe.sequel() ? &kSkinAs3d : gulfGame(fe) ? &kSkinGulf : &kSkinAs2;
    if (fe.sequel()) SequelScreens::applyLook(fe);
}

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
    const Color fill = disabled ? sk().barFillOff : focused ? sk().barFillFocus : sk().barFill;
    const Color edge = focused ? sk().barEdgeFocus : sk().barEdge;
    const int t = focused ? 2 : 1;
    if (!sk().chamfer || b.w < 40 || b.h < 24) {
        p.rect(b, fill);
        p.frame(b, edge, t);
        return;
    }
    // The sequels' bar: the top-right corner notched by 8 px (their HUD boxes' and buttons' cut).
    const float c = 8, ft = static_cast<float>(t);
    p.rect(b.x, b.y + c, b.w, b.h - c, fill);
    for (int i = 0; i < static_cast<int>(c); ++i) p.rect(b.x, b.y + static_cast<float>(i), b.w - (c - static_cast<float>(i)), 1, fill);
    p.rect(b.x, b.y, b.w - c, ft, edge);
    p.rect(b.x, b.y + b.h - ft, b.w, ft, edge);
    p.rect(b.x, b.y + ft, ft, b.h - 2 * ft, edge);
    p.rect(b.x + b.w - ft, b.y + c, ft, b.h - c - ft, edge);
    for (int i = 0; i < static_cast<int>(c); ++i) p.rect(b.x + b.w - c + static_cast<float>(i), b.y + static_cast<float>(i), ft, ft, edge);
}
// The bottom screen's ground: the skin's colour (Gulf Thunder: its scan lines over it, one texel of
// lines_gulf.tga per pixel).
void drawGround(const Px& p) {
    p.rect(0, 0, kConsoleBottomW, kConsoleBottomH, sk().ground, Blend::Opaque);
    if (!sk().scanLines) return;
    if (const Texture2D* lines = p.a.textureRepeat("gfx\\logo\\lines_gulf.tga"))
        p.r.quadSpec(p.vx(0), p.vy(0), p.vw(kConsoleBottomW), p.vh(kConsoleBottomH), 0, 0, kConsoleBottomW / 4.0f,
                     kConsoleBottomH / 4.0f, lines, Color{}, Blend::Alpha);
}
// The focus markers either side of a centred label.
void drawMarkers(const Px& p, const RectF& b, float labelW) {
    // At the bar's ends (the mockups), when they clear the label by 8 px.
    const float lx = b.x + 6, rx = b.x + b.w - 18;
    if (lx + 12 + 8 > std::floor(b.x + b.w * 0.5f - labelW * 0.5f)) return;
    p.triangle({lx, b.y, 12, b.h}, true, sk().barEdgeFocus);
    p.triangle({rx, b.y, 12, b.h}, false, sk().barEdgeFocus);
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

// The sequels' captions keep their padding spaces (they set the desktop's button widths): trimmed.
std::string tidy(std::string s) {
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}
bool g_sequel = false; // the front end being laid out is a sequel's (their captions, not the first game's)

std::string itemLabel(const Texts& t, Screen s, const MenuItem& it) {
    if (g_sequel) return tidy(it.label);
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
    if (i >= 0) L.boxes.push_back(bar(i, {kMargin, kFooterTop, kBackW, kCornerH}, itemLabel(t, s, m.items[static_cast<size_t>(i)])));
}

// A spinner row's label: the first game's in capitals, the sequels' as they write it.
std::string spinLabel(const MenuItem& it) { return g_sequel ? stripColon(tidy(it.label)) : upper(stripColon(it.label)); }

void layoutStart(ConsoleLayout& L, const Menu& m, const Texts& t) {
    // The ids: the first game's list 3, spinners 4, 5 and the helicopter row 6; the sequels' list
    // 5 and spinners 3 (difficulty), 4 (game mode) (screens_as2_start.cpp).
    const int listId = g_sequel ? 5 : 3;
    const std::vector<int> rows = g_sequel ? std::vector<int>{3, 4} : std::vector<int>{4, 5, 6};
    // The mission list: three rows of 36 px with the scroll arrows right of them.
    const int li = indexOf(m, listId);
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
    for (int id : rows) {
        const int i = indexOf(m, id);
        if (i < 0) continue;
        const MenuItem& it = m.items[static_cast<size_t>(i)];
        const std::string label = !g_sequel && id == 6 ? conText(t, "start.heli", "HELICOPTER") : spinLabel(it);
        arrows = std::max(arrows, static_cast<float>(12 + measureTextPx(fm(), label, FontSize::L) + 8));
    }
    // The rows' labels at L, unless the values (at S) would not fit between the arrows then: the
    // labels at S and the arrows nearer (the sequels' "Game mode" with "Single Player").
    float arrowsAt = std::max(arrows, 120.0f);
    FontSize rowSize = FontSize::L;
    if (g_sequel) {
        float need = 0, labelsS = 0;
        for (int id : rows) {
            const int i = indexOf(m, id);
            if (i < 0) continue;
            const MenuItem& it = m.items[static_cast<size_t>(i)];
            for (const std::string& v : it.values) need = std::max(need, static_cast<float>(measureTextPx(fm(), upper(v), FontSize::S) + 4));
            labelsS = std::max(labelsS, static_cast<float>(12 + measureTextPx(fm(), spinLabel(it), FontSize::S) + 8));
        }
        const float zone = (312 + kMargin - 36) - (arrowsAt + 36);
        if (zone < need) {
            arrowsAt = std::floor(labelsS);
            rowSize = FontSize::S;
        }
    }
    for (int id : rows) {
        const int i = indexOf(m, id);
        if (i < 0) continue;
        const MenuItem& it = m.items[static_cast<size_t>(i)];
        const std::string label = !g_sequel && id == 6 ? conText(t, "start.heli", "HELICOPTER") : spinLabel(it);
        const RectF row{kMargin, y, 312, 36};
        L.boxes.push_back(bar(i, row, label, rowSize));
        spinnerRow(L, i, row, arrowsAt);
        y += 38;
        if (y + 36 > kFooterTop) break;
    }
    backButton(L, m, t, Screen::StartGame, 1);
    const int s = indexOf(m, 2);
    if (s >= 0) L.boxes.push_back(bar(s, {216, kFooterTop, 100, kCornerH}, itemLabel(t, Screen::StartGame, m.items[static_cast<size_t>(s)])));
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
    if (ap >= 0) L.boxes.push_back(bar(ap, {216, kFooterTop, 100, kCornerH}, itemLabel(t, Screen::Options, m.items[static_cast<size_t>(ap)])));
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
    // No footer: the HUD's pad reminder sits in the bottom-left corner; the top box says "A OK".
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

// ---------------------------------------------------------------------------
// The sequels' screens (R4.8; docs/3ds-ui-design.md "Sequels, selector, loading and name entry
// on the console"): the same boxes, footers and hit write-back with their own items and ids
// (screens_as2_*.cpp).
// ---------------------------------------------------------------------------
// Main menu: six buttons in two columns of three (Start Game, Top Scores, Options |
// Information, Credits, Quit: the pad's Up / Down walk the item order, a column at a time),
// 154 x 56 each; GAMES and the footer on the bottom row as the first game's.
void layoutSequelMain(ConsoleLayout& L, const Menu& m, const Texts& t) {
    int k = 0;
    for (int id : {1, 2, 3, 4, 7, 5}) {
        const int i = indexOf(m, id);
        if (i < 0) continue;
        const float x = static_cast<float>(kMargin + 158 * (k / 3)), y = static_cast<float>(kMargin + 64 * (k % 3));
        L.boxes.push_back(bar(i, {x, y, 154, 56}, tidy(m.items[static_cast<size_t>(i)].label)));
        ++k;
    }
    int hx = 8;
    const int g = indexOf(m, kChangeGameItem);
    if (g >= 0) {
        L.boxes.push_back(bar(g, {kMargin, kFooterTop, 84, kCornerH}, conText(t, "games", "GAMES")));
        hx = kMargin + 84 + 8;
    }
    footerHints(L, hx, {{HintKey::A, conText(t, "hint.select", "Select")}, {HintKey::DPad, conText(t, "hint.move", "Move")}});
}

// Helicopter selection: the model is on the top screen; here the previous / next arrows (the
// screen's own picture items 7 and 6) either side of the name and the speed and armour bars, the
// player spinner (two players), BACK and Start / Continue / Accept.
constexpr RectF kHeliPrev{kMargin, 26, 56, 104}, kHeliNext{320 - kMargin - 56, 26, 56, 104};
void layoutHeli(ConsoleLayout& L, const Menu& m, const Texts& t) {
    L.titleY = 6; // the screen's title: drawn by drawBottom (the game's caption)
    const int prev = indexOf(m, 7), next = indexOf(m, 6);
    if (prev >= 0) L.boxes.push_back(bar(prev, kHeliPrev));
    if (next >= 0) L.boxes.push_back(bar(next, kHeliNext));
    const int sp = indexOf(m, 5);
    if (sp >= 0) {
        const RectF row{kMargin, 150, 312, 36};
        L.boxes.push_back(bar(sp, row, spinLabel(m.items[static_cast<size_t>(sp)])));
        spinnerRow(L, sp, row, 150);
    }
    backButton(L, m, t, Screen::HeliSelect, 2);
    int go = indexOf(m, 1);
    if (go < 0) go = indexOf(m, 8); // Accept (opened from Mission Complete)
    const bool back = indexOf(m, 2) >= 0;
    if (go >= 0) L.boxes.push_back(bar(go, {204, kFooterTop, 112, kCornerH}, tidy(m.items[static_cast<size_t>(go)].label)));
    footerHints(L, back ? kMargin + kBackW + 8 : 8, {{HintKey::DPad, conText(t, "hint.choose", "Choose")}});
    L.hintRight = 204 - 4;
}

void layoutCredits(ConsoleLayout& L, const Menu& m, const Texts& t) {
    L.title = conText(t, "credits.title", "CREDITS");
    L.titleY = 48;
    L.subtitle = conText(t, "credits.sub", "The names are on the top screen");
    L.subtitleY = 112;
    backButton(L, m, t, Screen::Credits);
    footerHints(L, kMargin + kBackW + 8, {{HintKey::B, conText(t, "hint.back", "Back")}});
}

// In-game menu: Resume, Options, Restart, Quit (ids 1, 2, 4, 3).
void layoutSequelInGame(ConsoleLayout& L, const Menu& m, const Texts& t) {
    int row = 0;
    for (int id : {1, 2, 4, 3}) {
        const int i = indexOf(m, id);
        if (i < 0) continue;
        L.boxes.push_back(bar(i, {40, static_cast<float>(10 + 46 * row), 240, 40}, tidy(m.items[static_cast<size_t>(i)].label)));
        ++row;
    }
    footerHints(L, 8, {{HintKey::A, conText(t, "hint.select", "Select")}, {HintKey::B, conText(t, "hint.resume", "Resume")}});
}

// The portrait dialogue: its page on the top screen; here one NEXT (a key: Enter completes the
// page being typed, or turns it) over the dimmed HUD.
void layoutDialogue(ConsoleLayout& L, const Texts& t) {
    L.boxes.push_back(keyBox(keys::Enter, {80, 100, 160, 56}, conText(t, "dialogue.next", "NEXT")));
}

// Game over: QUIT and RESTART at once (the sequels' are usable at once), under GAME OVER.
void layoutSequelGameOver(ConsoleLayout& L, const Menu& m, const Texts& t) {
    L.title = conText(t, "gameover.title", "GAME OVER");
    L.titleY = 60;
    buttonRow(L, m, t, Screen::GameOver, {2, 1}, 128, 44, {138, 138});
    footerHints(L, 8, {{HintKey::A, conText(t, "hint.select", "Select")}, {HintKey::DPad, conText(t, "hint.move", "Move")}});
}

// Mission complete: Choose Helicopter across the screen, Restart / Quit / Next under it.
void layoutSequelComplete(ConsoleLayout& L, const Menu& m, const Texts& t) {
    L.title = conText(t, "complete.sub", "MISSION COMPLETE");
    L.titleY = 14;
    const int ch = indexOf(m, 4);
    if (ch >= 0) L.boxes.push_back(bar(ch, {kMargin, 48, 312, 44}, tidy(m.items[static_cast<size_t>(ch)].label)));
    buttonRow(L, m, t, Screen::MissionComplete, {2, 1, 3}, 108, 44, {102, 102, 102});
    footerHints(L, 8, {{HintKey::A, conText(t, "hint.select", "Select")}, {HintKey::DPad, conText(t, "hint.move", "Move")}});
}

// The sequels' intro (their logo pages, AirStrike 2's comic pages): the page on the top screen;
// here FASTER (any key: the page runs four times as fast) and SKIP (the comic's touch Skip: the
// intro ends).
void layoutIntro(ConsoleLayout& L, const Texts& t) {
    L.boxes.push_back(keyBox(keys::Enter, {8, 92, 148, 56}, conText(t, "intro.faster", "FASTER")));
    L.boxes.push_back(keyBox(keys::Escape, {164, 92, 148, 56}, conText(t, "intro.skip", "SKIP")));
    footerHints(L, 8, {{HintKey::A, conText(t, "hint.faster", "Faster")}, {HintKey::B, conText(t, "hint.skip", "Skip")}});
}

// The icons of an Information page's paragraphs (screens_main.cpp buildInformation).
struct PageIcon {
    int atlas = 0; // 0 weapons, 1 missiles, 2 items
    int index = 0;
};
std::vector<PageIcon> infoIcons(int page1) {
    std::vector<PageIcon> v;
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

// Information of the game on screen: the pages of its spinner, the texts page of the one shown
// (the sequels' spinner pages are their table's texts pages: Gulf Thunder has no page 4), the
// paragraphs' icons and the page as the top screen shows it.
int infoPageCount(const Frontend::Facts& f) { return f.sequel ? layout().infoPages : 10; }
int infoTextsPage(const Frontend::Facts& f) {
    if (!f.sequel) return f.infoPage + 1;
    const Layout& lay = layout();
    return lay.infoKey[std::clamp(f.infoPage, 0, lay.infoPages - 1)];
}
std::vector<PageIcon> pageIcons(const Frontend::Facts& f) {
    if (!f.sequel) return infoIcons(f.infoPage + 1);
    std::vector<PageIcon> v;
    for (const InfoIcon& ic : layout().infoIcons(infoTextsPage(f))) v.push_back({ic.kind, ic.slot});
    return v;
}
InfoPage infoPageOf(const Frontend::Facts& f);

int infoSheetCount(const Frontend::Facts& f) {
    if (!f.texts || !f.texts->installed()) return 1;
    const InfoPage p = infoPageOf(f);
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
        // The second row (2026-10-17): ZL and ZR on the shoulder plate, the circle pad (a ring
        // and its nub), the touch screen (a stylus).
        case HintKey::ZL: return {2, 17, 19, 15};
        case HintKey::ZR: return {23, 17, 19, 15};
        case HintKey::CPad: return {44, 17, 15, 15};
        case HintKey::Touch: return {61, 17, 15, 15};
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

// One Information page of the texts page `page` with its paragraphs' icons (the first game's or a
// sequel's table) and the credits' centred lines.
static InfoPage infoPageImpl(const Texts& texts, const FontMetrics& m, int page, int width, int height,
                             const std::vector<PageIcon>& icons, bool credits) {
    InfoPage out;
    char k[32];
    std::snprintf(k, sizeof k, "info.%d.title", page);
    out.title = texts.get(k);
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

InfoPage consoleInfoPage(const Texts& texts, const FontMetrics& m, int page, int width, int height) {
    return infoPageImpl(texts, m, page, width, height, infoIcons(page), page == 10);
}

namespace {
InfoPage infoPageOf(const Frontend::Facts& f) {
    return infoPageImpl(*f.texts, fm(), infoTextsPage(f), kInfoTextW, kInfoTextBottom - kInfoTextTop, pageIcons(f),
                        !f.sequel && f.infoPage + 1 == 10);
}
} // namespace

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
bool ConsoleMenuView::introShown(const Frontend& fe) {
    return fe.sequel() && fe.state() == FrontendState::Intro && fe.introPage() >= 0;
}

bool ConsoleMenuView::handles(const Frontend& fe) {
    if (fe.plain()) return false;
    if (introShown(fe)) return true; // R4.8: the sequels' logo and comic pages on the top screen
    if (!fe.menuOpen()) return false;
    switch (fe.topScreen()) {
        case Screen::MainMenu: case Screen::Exit: case Screen::StartGame: case Screen::TopScores: case Screen::NameEntry:
        case Screen::Options: case Screen::Information: case Screen::InGame: case Screen::GameOver:
        case Screen::MissionComplete: case Screen::GameComplete: case Screen::Hint:
            return true;
        // The sequels' own screens (R4.8).
        case Screen::HeliSelect: case Screen::Credits: case Screen::Dialogue: return fe.sequel();
        default: return false;
    }
}

ConsoleLayout ConsoleMenuView::layout(const Frontend& fe) const {
    ConsoleLayout L;
    if (!handles(fe)) return L;
    useSkin(fe);
    g_sequel = fe.sequel();
    const Frontend::Facts f = fe.facts();
    const Texts& t = *f.texts;
    if (introShown(fe)) {
        L.valid = true;
        L.intro = true;
        layoutIntro(L, t);
        applySizeRule(L, Menu{});
        return L;
    }
    const Menu* m = fe.menus().top();
    if (!m) return L;
    L.valid = true;
    L.screen = fe.topScreen();
    bool done = false; // the sequels' own layouts; the others are the first game's with their items
    if (g_sequel) {
        done = true;
        switch (L.screen) {
            case Screen::MainMenu: layoutSequelMain(L, *m, t); break;
            case Screen::HeliSelect: layoutHeli(L, *m, t); break;
            case Screen::Credits: layoutCredits(L, *m, t); break;
            case Screen::InGame: layoutSequelInGame(L, *m, t); break;
            case Screen::Dialogue: layoutDialogue(L, t); break;
            case Screen::GameOver: layoutSequelGameOver(L, *m, t); break;
            case Screen::MissionComplete: layoutSequelComplete(L, *m, t); break;
            default: done = false; break;
        }
    }
    if (!done) switch (L.screen) {
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
    const bool big = L.screen == Screen::GameOver || L.screen == Screen::GameComplete || L.screen == Screen::Exit ||
                     (g_sequel && L.screen == Screen::MissionComplete);
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
    if (introShown(fe)) {
        // B (Escape) is the comic's Skip: the pointer on the touch Skip and a click there.
        UiInput out;
        for (const UiEvent& e : in.events) {
            if (e.code == keys::Escape && e.type == UiEvent::Type::Press) {
                const RectF r = SequelScreens::touchSkipRect();
                out.move(r.x + r.w * 0.5f, r.y + r.h * 0.5f);
                out.key(keys::Mouse1);
                continue;
            }
            if (e.code == keys::Escape) continue;
            out.events.push_back(e);
        }
        in.events = std::move(out.events);
        return;
    }
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
    if (!L.valid || (!m && !L.intro)) return;
    if (L.intro) {
        // FASTER: a key; SKIP: the comic's touch Skip (the pointer on it and a click).
        if (!touching || wasTouching) return;
        const ConsoleBox* b = L.boxAt(static_cast<float>(px), static_cast<float>(py));
        if (!b || b->kind != ConsoleBoxKind::Key) return;
        if (b->arg == keys::Escape) {
            const RectF r = SequelScreens::touchSkipRect();
            out.move(r.x + r.w * 0.5f, r.y + r.h * 0.5f);
            out.key(keys::Mouse1);
        } else {
            out.key(b->arg);
        }
        return;
    }
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
    st.keyColor = sk().hintKey;
    st.labelColor = sk().hintLabel;
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
    p.textIn(r, b.label, size, disabled ? sk().labelOff : focused ? sk().labelFocus : sk().label);
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
    p.triangle(prev->r, false, sk().arrow);
    p.triangle(next->r, true, sk().arrow);
    const RectF zone{prev->r.x + prev->r.w, prev->r.y, next->r.x - prev->r.x - prev->r.w, prev->r.h};
    const FontSize size = L.valueSize; // one size for the screen's values (applySizeRule)
    const std::string v = fitLabel(fm(), upper(value), size, zone.w - 4);
    p.textIn(zone, v, size, focused ? Color{1, 1, 1, 1} : sk().value);
}

void drawSlider(const Px& p, const MenuItem& it, const RectF& row, bool focused) {
    const float y = std::floor(row.y + row.h * 0.5f) - 3;
    const float w = kSliderTo - kSliderFrom;
    const float range = static_cast<float>(it.max - it.min);
    const float f = range > 0 ? std::clamp((it.value - static_cast<float>(it.min)) / range, 0.0f, 1.0f) : 0.0f;
    const float fill = std::floor(w * f);
    p.rect(kSliderFrom, y, w, 6, Color{0.08f, 0.02f, 0.01f, 1});
    p.frame({kSliderFrom - 1, y - 1, w + 2, 8}, sk().barEdge);
    p.rect(kSliderFrom, y, fill, 6, focused ? sk().barEdgeFocus : sk().arrow);
    p.rect(kSliderFrom + fill - 4, std::floor(row.y + row.h * 0.5f) - 11, 9, 22, focused ? Color{1, 1, 1, 1} : sk().value);
}

void drawList(const Px& p, const ConsoleLayout& L, const MenuItem& list, int item, bool focused) {
    const int n = static_cast<int>(list.entries.size());
    for (const ConsoleBox& b : L.boxes) {
        if (b.item != item) continue;
        if (b.kind == ConsoleBoxKind::ListRow) {
            const bool enabled = list.entries[static_cast<size_t>(b.arg)].enabled;
            const bool sel = b.arg == list.selected;
            drawBar(p, b.r, sel && focused, !enabled);
            if (sel && !focused) p.frame(b.r, sk().barEdgeFocus, 1);
            p.textIn(b.r, b.label, b.size, !enabled ? sk().labelOff : sel ? sk().labelFocus : sk().label, Align::Left, 10);
        } else if (b.kind == ConsoleBoxKind::ListUp || b.kind == ConsoleBoxKind::ListDown) {
            drawBar(p, b.r, false, false);
            p.triangleUpDown(b.r, b.kind == ConsoleBoxKind::ListDown, sk().arrow);
        }
    }
    // The scroll position between the arrows.
    if (n > 0) {
        const float top = kMargin + 40, h = 112 - 80;
        const float y = top + std::floor(h * static_cast<float>(list.selected) / static_cast<float>(std::max(n - 1, 1)));
        p.rect(284, top, 28, h + 4, Color{0.08f, 0.02f, 0.01f, 1});
        p.rect(286, std::min(y, top + h - 2), 24, 6, sk().arrow);
    }
}

void drawEdit(const Px& p, const MenuItem& it, const RectF& r, bool focused, long long ms) {
    p.rect(r, Color{0.06f, 0.01f, 0.01f, 0.95f});
    p.frame(r, focused ? sk().barEdgeFocus : sk().barEdge, 1);
    const int ty = textTopIn(r.y, r.h, FontSize::L);
    const int w = measureTextPx(fm(), it.text, FontSize::L);
    p.text(r.x + 8, static_cast<float>(ty), it.text, FontSize::L, sk().value);
    if ((ms / 400) % 2 == 0) p.rect(r.x + 8 + static_cast<float>(w) + 1, r.y + 6, 2, r.h - 12, sk().value);
}

void drawGridCells(const Px& p, const ConsoleLayout& L, const MenuItem& g, int item, bool focused) {
    const int chosen = g.grid.choice ? g.grid.choice[0] : -1;
    for (const ConsoleBox& b : L.boxes) {
        if (b.item != item || b.kind != ConsoleBoxKind::GridCell) continue;
        const bool locked = g.grid.locked && g.grid.locked[b.arg];
        const bool sel = b.arg == chosen;
        p.rect(b.r, Color{0.05f, 0.01f, 0.01f, 0.9f});
        p.frame(b.r, sel ? (focused ? Color{1, 1, 1, 1} : sk().barEdgeFocus) : sk().barEdge, sel ? 2 : 1);
        const float size = 42;
        drawHeli(p, b.arg, std::floor(b.r.x + (b.r.w - size) * 0.5f + heliShift(b.arg, size)), std::floor(b.r.y + (b.r.h - size) * 0.5f), size,
                 locked, sel);
    }
}

// The helicopter selection's panel between its arrows: the screen's title, the helicopter's name
// and number, its speed and armour bars (the desktop's: speed / 1.5, health / 800), "not
// available" for a locked one.
void drawHeliPanel(const Px& p, const Frontend& fe, const Frontend::Facts& f, const ConsoleLayout& L) {
    const int h = SequelScreens::shownHeli(fe);
    const bool locked = SequelScreens::heliLocked(fe, h);
    p.text(160, static_cast<float>(L.titleY), fitLabel(fm(), SequelScreens::tr(fe, "title.heli"), FontSize::L, 312), FontSize::L,
           sk().title, Align::Center);
    const float x0 = kHeliPrev.x + kHeliPrev.w + 6, x1 = kHeliNext.x - 6, w = x1 - x0;
    const std::string name = SequelScreens::heliName(fe, h);
    const FontSize ns = measureTextPx(fm(), name, FontSize::L) <= w ? FontSize::L : FontSize::S;
    p.text(160, 32, fitLabel(fm(), name, ns, w), ns, locked ? sk().labelOff : sk().value, Align::Center);
    char line[64];
    std::snprintf(line, sizeof line, "%d / %d", h + 1, f.rules ? f.rules->helicopterCount : 1);
    p.text(160, 50, line, FontSize::S, sk().hintLabel, Align::Center);
    const FrontendContent::HeliInfo info = f.content ? f.content->heli[h] : FrontendContent::HeliInfo{};
    const float speed = info.known && info.hasSpeed ? info.speed : 1.0f;
    const float armor = info.known ? static_cast<float>(info.health) : 0.0f;
    const std::string sl = tidy(SequelScreens::tr(fe, "heli.speed")), al = tidy(SequelScreens::tr(fe, "heli.armor"));
    const float lw = static_cast<float>(std::max(measureTextPx(fm(), sl, FontSize::S), measureTextPx(fm(), al, FontSize::S)));
    const float bx = x0 + lw + 6, bw = std::floor(x1 - bx);
    const float fr[2] = {std::clamp(speed / 1.5f, 0.0f, 1.0f), std::clamp(armor / 800.0f, 0.0f, 1.0f)};
    for (int k = 0; k < 2; ++k) {
        const float y = 70.0f + 20.0f * static_cast<float>(k);
        p.text(x0, y, k == 0 ? sl : al, FontSize::S, sk().body);
        p.rect(bx, y + 2, bw, 8, Color{0, 0, 0, 0.8f});
        p.frame({bx - 1, y + 1, bw + 2, 10}, sk().barEdge);
        if (!locked) p.rect(bx, y + 2, std::floor(bw * fr[k]), 8, sk().arrow);
    }
    if (locked) p.text(160, 112, fitLabel(fm(), tidy(SequelScreens::tr(fe, "heli.na")), FontSize::L, w), FontSize::L,
                       Color{1, 0.2f, 0.15f, 1}, Align::Center);
}

} // namespace

void ConsoleMenuView::drawBottom(Renderer2D& r, const UiAssets& a, const Frontend& fe) const {
    const ConsoleLayout L = layout(fe);
    const Menu* m = fe.menus().top();
    if (!L.valid || (!m && !L.intro)) return;
    const Px p(r, a);
    const Frontend::Facts f = fe.facts();
    if (L.intro) {
        drawGround(p);
        p.text(160, 40, f.content && f.content->game ? f.content->game->title : "", FontSize::L, sk().title, Align::Center);
        p.text(160, 64, conText(*f.texts, "intro.top", "The story is on the top screen"), FontSize::S, sk().hintLabel, Align::Center);
        for (const ConsoleBox& b : L.boxes) {
            drawBar(p, b.r, false, false);
            p.textIn(b.r, b.label, b.size, sk().label);
        }
        drawFooter(p, L, keys_);
        return;
    }
    const float mt = fe.menus().menuTime();
    const long long ms = fe.menus().clockMs();
    // The tutorial hint and the sequels' dialogue leave the HUD under them, dimmed; every other
    // screen is on the game's ground.
    if (L.screen == Screen::Hint || L.screen == Screen::Dialogue) p.rect(0, 0, kConsoleBottomW, kConsoleBottomH, Color{0, 0, 0, 0.6f});
    else drawGround(p);
    if (L.screen == Screen::HeliSelect) drawHeliPanel(p, fe, f, L);
    if (L.screen == Screen::TopScores) {
        if (f.sequel || p.halfPicture("menu\\topscores_1.tga", {0, 0, 1, 1}, 160, static_cast<float>(L.titleY) + 8, 300, 40, Color{}, Blend::Alpha) == 0)
            p.text(160, static_cast<float>(L.titleY), L.title, FontSize::T, sk().title, Align::Center, false, TextEffect::None);
        const HighScore& best = f.profile->progress.scores[0];
        char line[192];
        std::snprintf(line, sizeof line, "%s  %lld", best.name.c_str(), static_cast<long long>(best.score));
        p.text(160, 80, conText(*f.texts, "scores.best", "BEST PILOT"), FontSize::S, sk().hintLabel, Align::Center);
        p.text(160, 96, line, FontSize::L, sk().value, Align::Center);
    } else if (!L.title.empty()) {
        p.text(160, static_cast<float>(L.titleY), L.title, L.titleSize, sk().title, Align::Center);
    }
    if (!L.subtitle.empty()) p.text(160, static_cast<float>(L.subtitleY), L.subtitle, FontSize::S, sk().hintLabel, Align::Center);
    if (L.screen == Screen::Information) {
        // The page dots (10 pages, the sequels' 7 or 8) and the sheet.
        const int pages = infoPageCount(f);
        const float x0 = 160 - (pages * 12 - 4) * 0.5f;
        for (int i = 0; i < pages; ++i)
            p.rect(std::floor(x0 + 12.0f * static_cast<float>(i)), 118, 8, 8, i == f.infoPage ? sk().barEdgeFocus : sk().barFill);
    }
    for (const ConsoleBox& b : L.boxes) {
        if (b.kind == ConsoleBoxKind::Key) {
            drawBar(p, b.r, false, false);
            p.textIn(b.r, b.label, b.size, sk().label);
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
                    p.textIn(b.r, b.label, b.size, focused ? sk().labelFocus : sk().label, Align::Left, 8);
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
                p.textIn(b.r, b.label, b.size, disabled ? sk().labelOff : focused ? sk().labelFocus : sk().label, Align::Left, 8);
                const std::string v = it.values.empty() ? std::string() : it.values[static_cast<size_t>(std::clamp(it.index, 0, static_cast<int>(it.values.size()) - 1))];
                drawSpinnerValue(p, L, b.item, v, focused);
                break;
            }
            case ItemType::Slider:
                drawBar(p, b.r, focused, disabled);
                p.textIn(b.r, b.label, b.size, disabled ? sk().labelOff : focused ? sk().labelFocus : sk().label, Align::Left, 8);
                drawSlider(p, it, b.r, focused);
                break;
            case ItemType::Edit: drawEdit(p, it, b.r, focused, ms); break;
            case ItemType::Picture: // the helicopter selection's arrows (7 previous, 6 next)
                drawBar(p, b.r, focused, disabled);
                p.triangle(b.r, it.id == 6, focused ? sk().barEdgeFocus : sk().arrow);
                break;
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
    p.rect(0, 34, kConsoleTopW, 1, sk().barEdge);
    if (g_sequel || header.empty() || !halfHeader(p, header, 200, 17)) // the sequels: their title in text
        p.text(200, static_cast<float>(textTopIn(0, 34, FontSize::L)), text, FontSize::L, sk().title, Align::Center);
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
            p.text(56, ry, row.k, FontSize::L, sk().body);
            p.text(344, ry, row.v, FontSize::L, sk().value, Align::Right);
        }
        ry += 22;
    }
}

void drawInfoTop(const Px& p, const Frontend::Facts& f, int sheet) {
    p.rect(0, 0, kConsoleTopW, kConsoleTopH, kDim);
    const Texts& t = *f.texts;
    if (!t.installed()) {
        p.text(12, 8, t.get("info.missing.title"), FontSize::L, sk().title);
        float y = 40;
        for (int i = 0; i < 3; i++) {
            for (const std::string& l : wrapText(fm(), t.get("info.missing." + std::to_string(i)), FontSize::S, kInfoTextW))
                p.text(12, y, l, FontSize::S, sk().body, Align::Left, true), y += 13;
        }
        return;
    }
    const InfoPage page = infoPageOf(f);
    const int n = static_cast<int>(page.sheets.size());
    const int s = std::clamp(sheet, 0, n - 1);
    std::string title = page.title;
    const size_t paren = title.find(" (");
    if (paren != std::string::npos) title.erase(paren); // "(Page 1 of 2)": the counter says it
    p.text(12, 5, title, FontSize::L, sk().title);
    char counter[64];
    const int pages = infoPageCount(f);
    if (n > 1) std::snprintf(counter, sizeof counter, "%d/%d (%d/%d)", f.infoPage + 1, pages, s + 1, n);
    else std::snprintf(counter, sizeof counter, "%d/%d", f.infoPage + 1, pages);
    p.text(388, 7, counter, FontSize::S, sk().hintLabel, Align::Right);
    p.rect(12, 22, 376, 1, sk().barEdge);
    const std::vector<PageIcon> icons = pageIcons(f);
    const GameId game = f.content && f.content->game ? f.content->game->id : GameId::AirStrike3D;
    const float x = icons.empty() ? 12.0f : 12.0f + kInfoIconIndent;
    float y = kInfoTextTop;
    for (const InfoLine& l : page.sheets[static_cast<size_t>(s)]) {
        y += static_cast<float>(l.gapBefore);
        if (l.icon >= 0 && l.icon < static_cast<int>(icons.size())) {
            const PageIcon& ic = icons[static_cast<size_t>(l.icon)];
            const Texture2D* atlas = ic.atlas == 0 ? &p.a.weapons : ic.atlas == 1 ? &p.a.missiles : &p.a.items;
            const SpecUv uv = ic.atlas == 0 ? weaponIconUv(ic.index, game)
                              : ic.atlas == 1 ? missileIconUv(ic.index, game) : powerupIconUv(ic.index, game);
            if (atlas->valid() && !uv.empty())
                p.r.quadSpec(p.vx(12), p.vy(y + 1), p.vw(33), p.vh(18), uv.s0, uv.t0, uv.s1, uv.t1, atlas, Color{}, Blend::Add);
        }
        if (l.centred) p.text(200, y, l.text, FontSize::S, sk().body, Align::Center, true);
        else p.text(x, y, l.text, FontSize::S, sk().body, Align::Left, true);
        y += static_cast<float>(fontLinePitchPx(FontSize::S));
    }
}

void drawScoresTop(const Px& p, const Frontend::Facts& f) {
    p.rect(0, 0, kConsoleTopW, kConsoleTopH, kDim);
    const Texts& t = *f.texts;
    p.text(200, 3, conText(t, "scores.title", "TOP SCORES"), FontSize::L, sk().title, Align::Center);
    const float y0 = 21;
    p.rect(8, y0, 384, 13, Color{0.38f, 0.0f, 0.0f, 0.6f});
    p.text(34, y0, t.get("scores.number"), FontSize::S, sk().body, Align::Right);
    p.text(44, y0, t.get("scores.name"), FontSize::S, sk().body);
    p.text(272, y0, t.get("scores.score"), FontSize::S, sk().body, Align::Right);
    p.text(284, y0, t.get("scores.rank"), FontSize::S, sk().body);
    for (int i = 0; i < kHighScoreCount; i++) {
        const HighScore& h = f.profile->progress.scores[i];
        const float y = y0 + 14 + 13.0f * static_cast<float>(i);
        const Color c = i == 0 ? sk().value : sk().body;
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
    p.text(56, y + 9, head, FontSize::L, sk().body);
    p.text(344, y + 9, number(f.endScore), FontSize::L, sk().value, Align::Right);
    if (f.endSlot >= 0) {
        char line[192];
        std::snprintf(line, sizeof line, "%s %d %s %d  -  %s", conText(t, "rankpos", "Place").c_str(), f.endSlot + 1,
                      conText(t, "of", "of").c_str(), kHighScoreCount, t.get("rank." + std::to_string(f.endRank)).c_str());
        p.text(200, y + 36, line, FontSize::S, sk().hintLabel, Align::Center);
    }
}


// The tutorial hint on the top screen (R4.7): the world dimmed 35 %, a box at the bottom of the
// screen sized to its text: TUTORIAL (L), the hint re-flowed at S to 356 px (the original's '^'
// breaks are its box's line ends, not paragraphs: joined with spaces), "A OK" at its lower right.
void drawHintTop(const Px& p, const Frontend::Facts& f, const KeyGlyphAtlas* keys, const std::string& title) {
    constexpr int kHintTextW = 356;
    p.rect(0, 0, kConsoleTopW, kConsoleTopH, Color{0, 0, 0, 0.35f});
    std::string joined = f.hintText;
    for (char& ch : joined)
        if (ch == '^' || ch == '\n') ch = ' ';
    std::vector<std::string> lines = wrapText(fm(), joined, FontSize::S, kHintTextW);
    const int maxLines = (kConsoleTopH - 8 - 44) / 13;
    if (static_cast<int>(lines.size()) > maxLines) lines.resize(static_cast<size_t>(maxLines));
    const float h = 28.0f + 13.0f * static_cast<float>(lines.size()) + 22.0f;
    const RectF box{14, std::floor(static_cast<float>(kConsoleTopH) - 8 - h), kConsoleTopW - 28, h};
    p.rect(box, Color{0, 0, 0, 0.78f});
    p.frame(box, sk().barEdge);
    p.text(box.x + 12, box.y + 6, fitLabel(fm(), title, FontSize::L, box.w - 24), FontSize::L, sk().title);
    float y = box.y + 28;
    for (const std::string& l : lines) {
        p.text(box.x + 12, y, l, FontSize::S, sk().body, Align::Left, true);
        y += 13;
    }
    KeyHintStyle st;
    st.size = FontSize::S;
    st.keyColor = sk().hintKey;
    st.labelColor = sk().hintKey;
    const std::string ok = f.texts ? conText(*f.texts, "hint.ok", "OK") : std::string("OK");
    const int w = hintWidth(fm(), {HintKey::A, ok});
    drawKeyHint(p.r, p.font, keys, static_cast<int>(box.x + box.w) - 12 - w, static_cast<int>(box.y + box.h) - 19, HintKey::A, ok, st);
}

// ---------------------------------------------------------------------------
// The sequels' top screens (R4.8)
// ---------------------------------------------------------------------------
// The intro page on the top screen: AirStrike 2's comic pages at 1:1 through a 400 x 240 window
// that pans along the panels and to each speech bubble as it appears (SequelScreens::comicWindow;
// before 2026-10-18 the pages were fitted whole, at 1/2 to 0.4, and their lettering unreadable),
// the logo pages (a picture centred on a coloured page) at 1/2.
bool drawIntroTop(const Px& p, const Frontend& fe, Renderer2D& scratch) {
    const auto* run = SequelScreens::introRun(fe);
    if (!run || run->index >= run->pages.size()) return false;
    const IntroPage& page = run->pages[run->index];
    Mapping m;
    struct Fit { float x0, y0, w, h; };
    if (page.comic) {
        float wx = 0, wy = 0;
        SequelScreens::comicWindow(page.comic, run->clock, static_cast<float>(kConsoleTopW), static_cast<float>(kConsoleTopH), wx, wy);
        m.scaleX = m.scaleY = 1.0f;
        m.offsetX = -wx;
        m.offsetY = -wy;
    } else {
        const Fit fit{0, 60, 800, 480};
        const float s = std::min(static_cast<float>(kConsoleTopW) / fit.w, static_cast<float>(kConsoleTopH) / fit.h);
        m.scaleX = m.scaleY = s;
        m.offsetX = std::floor((static_cast<float>(kConsoleTopW) - fit.w * s) * 0.5f - fit.x0 * s);
        m.offsetY = std::floor((static_cast<float>(kConsoleTopH) - fit.h * s) * 0.5f - fit.y0 * s);
    }
    scratch.beginMapped(kConsoleTopW, kConsoleTopH, m);
    if (page.comic) SequelScreens::drawComicPage(scratch, p.a, page.comic, run->clock);
    else const_cast<Frontend&>(fe).drawUnder(scratch, p.a); // the logo page (Frontend::drawIntro)
    p.r.appendMapped(scratch);
    return true;
}

// The main menu's title on the top screen, over the attract level: AirStrike 2's logo (with its
// clouds, glow and the spinning "2") and Gulf Thunder's emblem (tinted, with its clouds), each at
// exactly 1/2; the version and copyright band at the bottom as the first game's.
void drawSequelLogoTop(const Px& p, const Frontend& fe, const Frontend::Facts& f) {
    const auto* st = SequelScreens::state(fe);
    const float T = st ? st->logoClock : 0.0f;
    if (!gulfGame(fe)) {
        p.rect(0, 0, kConsoleTopW, 34, Color{0, 0, 0, 0.72f});
        p.rect(0, 34, kConsoleTopW, 1, sk().barEdge);
        as2::TitleLogoPictures pics;
        pics.glow = p.a.texture("gfx\\logo\\glow.tga");
        pics.two = p.a.texture("gfx\\logo\\two3.tga");
        pics.logo = p.a.texture("gfx\\logo\\logo.tga");
        pics.clouds = pics.logo ? p.a.textureRepeat("gfx\\logo\\clouds.tga") : nullptr;
        // logo.tga's letters are rows 46..93 of 128: at 1/2 they sit at y 2..26.
        const float k = 0.5f / p.m.scaleX;
        as2::titleLogoAt(p.r, pics, T, T, p.vx(50) - 124 * k, p.vy(-21), k, 1.0f);
    } else if (const Texture2D* logo = p.a.texture("gfx\\logo\\logo_gulf.tga")) {
        // logo_gulf.tga's emblem is rows 22..233 of 256: at 1/2 from y 2.
        p.rect(0, 0, kConsoleTopW, 112, Color{0, 0, 0, 0.35f});
        Quad q;
        q.x = p.vx(72);
        q.y = p.vy(-9);
        q.w = p.vw(256);
        q.h = p.vh(128);
        q.texture = logo;
        q.blend = Blend::Alpha;
        q.color = st ? Color{st->tint[0], st->tint[1], st->tint[2], st->tint[3]} : Color{};
        if (const Texture2D* clouds = p.a.textureRepeat("gfx\\logo\\clouds.tga")) {
            q.texture2 = clouds;
            q.s0b = 0.1f * T;
            q.s1b = 0.1f * T + 2.0f;
            q.t0b = 0;
            q.t1b = 1;
            q.combine2 = 2;
        }
        p.r.add(q);
    }
    const FrontendContent& c = *f.content;
    if (c.version.empty() && c.copyright.empty()) return;
    p.rect(0, 224, kConsoleTopW, 16, Color{0, 0, 0, 0.85f});
    p.text(6, 226, c.version, FontSize::S, sk().greyText);
    p.text(394, 226, c.copyright, FontSize::S, sk().greyText, Align::Right);
}

// Start Game: the chosen mission (L), the difficulty and the game mode (S) under the title.
void drawSequelStartTop(const Px& p, const Frontend& fe, const Menu& m) {
    titleBand(p, "", tidy(SequelScreens::tr(fe, "title.start_game")));
    p.rect(8, 44, 384, 76, Color{0, 0, 0, 0.72f});
    p.frame({8, 44, 384, 76}, sk().barEdge);
    float y = 52;
    for (const MenuItem& it : m.items) {
        if (it.id != 5 || it.type != ItemType::List || it.entries.empty()) continue;
        const std::string name = it.entries[static_cast<size_t>(std::clamp(it.selected, 0, static_cast<int>(it.entries.size()) - 1))].text;
        const FontSize ns = measureTextPx(fm(), name, FontSize::L) <= 368 ? FontSize::L : FontSize::S;
        p.text(200, y, fitLabel(fm(), name, ns, 368), ns, sk().value, Align::Center);
    }
    y += 24;
    for (int id : {3, 4}) {
        for (const MenuItem& it : m.items) {
            if (it.id != id || it.type != ItemType::Spinner || it.hidden() || it.values.empty()) continue;
            const std::string v = it.values[static_cast<size_t>(std::clamp(it.index, 0, static_cast<int>(it.values.size()) - 1))];
            p.text(200, y, stripColon(tidy(it.label)) + ": " + v, FontSize::S, sk().body, Align::Center);
            y += 16;
        }
    }
}

// Credits: the names centred at S over the dimmed attract level, a gap of 5 px where the texts
// leave a line out; more than the screen holds: in sheets that turn every 6 s.
void drawCreditsTop(const Px& p, const Frontend::Facts& f, float mt) {
    p.rect(0, 0, kConsoleTopW, kConsoleTopH, kDim);
    struct Line { std::string text; int gap; };
    std::vector<Line> lines;
    int last = -1;
    const Texts& t = *f.texts;
    for (int i = 0; i <= 30; ++i) {
        const std::string k = "credits." + std::to_string(i);
        if (!t.loaded(k)) continue;
        lines.push_back({t.get(k), lines.empty() ? 0 : (i - last > 1 ? 5 : 0)});
        last = i;
    }
    if (lines.empty()) {
        p.text(200, 100, t.get("info.missing.0"), FontSize::S, sk().body, Align::Center, true);
        return;
    }
    std::vector<std::vector<Line>> sheets(1);
    int used = 0;
    for (Line l : lines) {
        const int need = (sheets.back().empty() ? 0 : l.gap) + 13;
        if (!sheets.back().empty() && used + need > kConsoleTopH - 12) {
            sheets.emplace_back();
            used = 0;
        }
        if (sheets.back().empty()) l.gap = 0;
        used += l.gap + 13;
        sheets.back().push_back(l);
    }
    const size_t s = sheets.size() > 1 ? static_cast<size_t>(mt / 6.0f) % sheets.size() : 0;
    float y = std::floor((static_cast<float>(kConsoleTopH) - static_cast<float>(used)) * 0.5f);
    if (sheets.size() > 1) y = 6;
    for (const Line& l : sheets[s]) {
        y += static_cast<float>(l.gap);
        p.text(200, y, fitLabel(fm(), l.text, FontSize::S, 392), FontSize::S, sk().body, Align::Center, true);
        y += 13;
    }
}

// The portrait dialogue on the top screen: a box at the bottom, the speaker's portrait 1:1
// (portraits2.tga, 80 x 120) and the page re-flowed at S beside it, typed as the desktop types it.
void drawDialogueTop(const Px& p, const Frontend& fe) {
    const auto* st = SequelScreens::state(fe);
    p.rect(0, 0, kConsoleTopW, kConsoleTopH, Color{0, 0, 0, 0.3f});
    if (!st || st->fade < 1.0f || st->closing || st->page >= st->pages.size()) return;
    const auto& page = st->pages[st->page];
    const RectF box{8, kConsoleTopH - 8 - 132, kConsoleTopW - 16, 132};
    p.rect(box, Color{0, 0, 0, 0.8f});
    p.frame(box, sk().barEdge);
    if (const Texture2D* t = p.a.texture("gfx\\ui\\portraits2.tga")) {
        const SpecUv uv = as2::pieceUv(*t, as2::Piece{page.speaker == 1 ? 80.0f : 0.0f, 8, 80, 120});
        p.r.quadSpec(p.vx(box.x + 6), p.vy(box.y + 6), p.vw(80), p.vh(120), uv.s0, uv.t0, uv.s1, uv.t1, t, Color{}, Blend::Alpha);
    }
    std::string text = page.text;
    for (char& c : text)
        if (c == '\n') c = ' ';
    const float tx = box.x + 6 + 80 + 10, tw = box.x + box.w - 8 - tx;
    std::vector<std::string> lines = wrapText(fm(), text, FontSize::S, static_cast<int>(tw), false);
    if (lines.size() > 9) lines.resize(9);
    // The typed characters: the wrapped lines take the page's characters in order (a line break
    // stands for the space it replaced).
    int left = std::max(st->typed, 0);
    float y = box.y + 8;
    for (const std::string& l : lines) {
        if (left <= 0) break;
        const int n = std::min(left, static_cast<int>(l.size()));
        p.text(tx, y, l.substr(0, static_cast<size_t>(n)), FontSize::S, sk().value);
        left -= n + 1;
        y += 13;
    }
}

// Game over: the game's comic (the three columns of gameover_k_1 / _2 and the "GAME OVER"
// picture) at 1/2, fading in, the score panel over its foot.
void drawSequelGameOverTop(const Px& p, const Frontend::Facts& f, float mt) {
    const float fade = std::min(mt, 1.0f);
    p.rect(0, 0, kConsoleTopW, kConsoleTopH, Color{0, 0, 0, fade});
    char name[64];
    for (int k = 1; k <= 3; ++k)
        for (int row = 1; row <= 2; ++row) {
            std::snprintf(name, sizeof name, "gfx\\ui\\comix\\gameover_%d_%d.tga", k, row);
            if (const Texture2D* t = p.a.texture(name))
                p.r.quad(p.vx(8.0f + 128.0f * static_cast<float>(k - 1)), p.vy(row == 1 ? 0.0f : 128.0f), p.vw(128), p.vh(128), 0, 0, 1, 1,
                         t, Color{1, 1, 1, fade}, Blend::Alpha);
        }
    if (const Texture2D* g = p.a.texture("gfx\\ui\\comix\\gamov.tga"))
        p.r.quad(p.vx(72), p.vy(76), p.vw(256), p.vh(32), 0, 0, 1, 1, g, grey(fade), Blend::Add);
    if (mt >= 1.0f) highScorePanel(p, f, 172);
}

// Mission complete: the title (T, else L) outlined over the world dimmed 35 %, the statistics in L
// as they appear, the new helicopter's line.
void drawSequelCompleteTop(const Px& p, const Frontend& fe, const Frontend::Facts& f, float mt) {
    p.rect(0, 0, kConsoleTopW, kConsoleTopH, Color{0, 0, 0, 0.35f});
    const std::string title = tidy(SequelScreens::tr(fe, "title.mission_complete"));
    const FontSize ts = measureTextPx(fm(), title, FontSize::T) <= kConsoleTopW - 8 ? FontSize::T : FontSize::L;
    p.text(200, 14, title, ts, sk().title, Align::Center, false, TextEffect::Outline);
    if (fe.twoPlayers()) return;
    resultsPanel(p, f, 60, mt, false);
    const int mission = std::clamp(f.mission, 0, kMaxMissions - 1);
    const int heli = f.content ? f.content->enableHelic[mission] : -1;
    if (heli >= 0 && f.rules && heli < f.rules->helicopterCount && mt > 2.2f)
        p.text(200, 150, fitLabel(fm(), tidy(SequelScreens::tr(fe, "msg.new_heli")), FontSize::L, 384), FontSize::L, sk().value,
               Align::Center, false, TextEffect::Outline);
}

// Game complete: the eleven congratulation lines typed from 4 s at 8 characters a second.
void drawSequelGameCompleteTop(const Px& p, const Frontend::Facts& f, float mt) {
    p.rect(0, 0, kConsoleTopW, kConsoleTopH, Color{0, 0, 0, std::min(mt / 2.0f, 1.0f) * 0.85f});
    const Texts& t = *f.texts;
    std::vector<std::string> lines;
    for (int i = 0; i <= 10; i++) lines.push_back(t.loaded("congrats." + std::to_string(i)) ? t.get("congrats." + std::to_string(i)) : " ");
    if (!t.loaded("congrats.2")) {
        lines[0] = t.get("congrats.0");
        lines[2] = t.get("congrats.missing");
    }
    float start = 4.0f, y = 18;
    for (const std::string& l : lines) {
        const float dt = mt - start;
        start += static_cast<float>(l.size()) / 8.0f;
        const int k = dt < 0 ? 0 : std::min(static_cast<int>(8.0f * dt) + 1, static_cast<int>(l.size()));
        if (k > 0) {
            // Each line types out from its final left edge.
            const std::string full = fitLabel(fm(), l, FontSize::S, 392);
            const int w = measureTextPx(fm(), full, FontSize::S, true);
            p.text(std::floor(200.0f - static_cast<float>(w) * 0.5f), y, full.substr(0, static_cast<size_t>(k)), FontSize::S, sk().body,
                   Align::Left, true);
        }
        y += 18;
    }
}

} // namespace

bool ConsoleMenuView::drawTop(Renderer2D& r, const UiAssets& a, const Frontend& fe) const {
    if (!handles(fe)) return false;
    useSkin(fe);
    g_sequel = fe.sequel();
    const Px p(r, a);
    if (introShown(fe)) {
        if (!scratch_) scratch_.reset(new Renderer2D());
        return drawIntroTop(p, fe, *scratch_);
    }
    const Menu* m = fe.menus().top();
    if (!m) return false;
    const Frontend::Facts f = fe.facts();
    const Texts& t = *f.texts;
    const float mt = fe.menus().menuTime();
    if (f.sequel) {
        switch (fe.topScreen()) {
            case Screen::MainMenu: drawSequelLogoTop(p, fe, f); return true;
            case Screen::StartGame: drawSequelStartTop(p, fe, *m); return true;

            case Screen::HeliSelect: {
                // The model view is the frame's (on the top screen, over the selection's dot grid);
                // a locked helicopter has none: its "not available" over the dimmed attract level.
                const int h = SequelScreens::shownHeli(fe);
                if (!SequelScreens::heliLocked(fe, h)) return false;
                p.rect(0, 0, kConsoleTopW, kConsoleTopH, kDim);
                p.text(200, static_cast<float>(textTopIn(100, 40, FontSize::T)), fitLabel(fm(), tidy(SequelScreens::tr(fe, "heli.na")), FontSize::L, 384),
                       FontSize::L, Color{1, 0.2f, 0.15f, 1}, Align::Center, false, TextEffect::Outline);
                return true;
            }
            case Screen::Credits: drawCreditsTop(p, f, mt); return true;
            case Screen::Hint: drawHintTop(p, f, keys_, tidy(SequelScreens::tr(fe, "title.hint"))); return true;
            case Screen::Dialogue: drawDialogueTop(p, fe); return true;
            case Screen::GameOver: drawSequelGameOverTop(p, f, mt); return true;
            case Screen::MissionComplete: drawSequelCompleteTop(p, fe, f, mt); return true;
            case Screen::GameComplete: drawSequelGameCompleteTop(p, f, mt); return true;
            default: break; // Exit, Top Scores, Information, name entry: the first game's panels
        }
    }
    switch (fe.topScreen()) {
        case Screen::MainMenu: {
            const FrontendContent& c = *f.content;
            if (c.version.empty() && c.copyright.empty()) return false;
            p.rect(0, 224, kConsoleTopW, 16, Color{0, 0, 0, 0.85f});
            p.text(6, 226, c.version, FontSize::S, sk().greyText);
            p.text(394, 226, c.copyright, FontSize::S, sk().greyText, Align::Right);
            return true;
        }
        case Screen::Exit:
            p.rect(0, 92, kConsoleTopW, 56, Color{0, 0, 0, 0.75f});
            p.text(200, static_cast<float>(textTopIn(92, 56, FontSize::L)), t.get("label.exit"), FontSize::L, sk().body, Align::Center);
            return true;
        case Screen::Options: {
            titleBand(p, "menu\\optionsh", f.sequel ? tidy(SequelScreens::tr(fe, "title.options")) : conText(t, "options.title", "OPTIONS"));
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
                    p.text(200, 201, help, FontSize::S, sk().body, Align::Center);
                    const std::string how = it.type == ItemType::Slider ? conText(t, "help.slider", "Left / Right or drag the slider.")
                                            : it.type == ItemType::Spinner ? conText(t, "help.spinner", "Left / Right or tap the arrows.")
                                                                           : conText(t, "help.button", "A or tap.");
                    p.text(200, 218, how, FontSize::S, sk().hintLabel, Align::Center);
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
            p.frame({12, 52, 86, 86}, sk().barEdge);
            const int heli = f.helicopter;
            const bool locked = f.heliLocked && heli >= 0 && heli < f.rules->helicopterCount && f.heliLocked[heli];
            drawHeli(p, heli, 13 + heliShift(heli, 85), 53, 85, locked, true);
            std::string name;
            if (list && !list->entries.empty())
                name = list->entries[static_cast<size_t>(std::clamp(list->selected, 0, static_cast<int>(list->entries.size()) - 1))].text;
            const FontSize ns = measureTextPx(fm(), name, FontSize::L) <= 280 ? FontSize::L : FontSize::S;
            p.text(108, 56, name, ns, sk().value);
            char line[192];
            std::snprintf(line, sizeof line, "%s %s", t.get("label.difficulty").c_str(),
                          t.get("difficulty." + std::to_string(f.difficulty)).c_str());
            p.text(108, 80, line, FontSize::S, sk().body);
            std::snprintf(line, sizeof line, "%s %d %s %d", conText(t, "heli", "Helicopter").c_str(), heli + 1,
                          conText(t, "of", "of").c_str(), f.rules->helicopterCount);
            p.text(108, 96, line, FontSize::S, sk().body);
            int open = 0;
            for (int i = 0; i < f.rules->helicopterCount; ++i) open += f.heliLocked && !f.heliLocked[i] ? 1 : 0;
            std::snprintf(line, sizeof line, "%d %s %d %s", open, conText(t, "of", "of").c_str(), f.rules->helicopterCount,
                          conText(t, "helis_open", "helicopters open").c_str());
            p.text(108, 112, line, FontSize::S, sk().hintLabel);
            return true;
        }
        case Screen::TopScores: drawScoresTop(p, f); return true;
        case Screen::Information: drawInfoTop(p, f, infoSheetPage_ == f.infoPage ? infoSheet_ : 0); return true;
        case Screen::InGame: return false; // PAUSED (the host's top list: drawConsolePlayTop)
        case Screen::Hint:
            drawHintTop(p, f, keys_, f.texts ? conText(*f.texts, "hint.title", "TUTORIAL") : std::string("TUTORIAL"));
            return true;
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
            p.text(200, 50, conText(t, "name.top", "WELL DONE, PILOT"), FontSize::T, sk().title, Align::Center, false, TextEffect::Outline);
            highScorePanel(p, f, 110);
            p.text(200, 196, conText(t, "name.help", "Tap the keys, or Y for the keyboard."), FontSize::S, sk().hintLabel,
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
                if (k > 0) p.text(200, y, l.substr(0, static_cast<size_t>(k)), FontSize::S, sk().body, Align::Center);
                y += 15;
            }
            if (!fe.twoPlayers()) resultsPanel(p, f, 104, mt, true);
            return true;
        }
        default: break;
    }
    return false;
}

// ---------------------------------------------------------------------------
// The loading screen (R4.8)
// ---------------------------------------------------------------------------
namespace {

// The console's controls card (the default pad bindings, consoleKeyNames): a key picture or the
// key's name, then what it does.
struct ControlRow {
    bool picture;
    HintKey key;
    const char* name;  // without a picture
    const char* label;
};
constexpr ControlRow kControls[] = {
    {true, HintKey::CPad, "", "fly"},            {true, HintKey::A, "", "fire"},
    {true, HintKey::B, "", "missile"},           {true, HintKey::X, "", "power-up"},
    {true, HintKey::Y, "", "next weapon"},       {true, HintKey::R, "", "next missile"},
    {true, HintKey::ZR, "", "next power-up"},    {true, HintKey::Start, "", "pause"},
    {true, HintKey::Touch, "", "tiles: select, use"},
};

} // namespace

void drawConsoleLoading(Renderer2D& top, Renderer2D& bottom, const UiAssets& a, float progress, bool intermission,
                        const KeyGlyphAtlas* keys) {
    const LoadingInfo info = loadingInfo();
    g_skin = !info.sequel ? &kSkinAs3d : info.gulf ? &kSkinGulf : &kSkinAs2;
    const Px t(top, a);
    const Px b(bottom, a);
    t.rect(0, 0, kConsoleTopW, kConsoleTopH, Color{0, 0, 0, 1}, Blend::Opaque);
    b.rect(0, 0, kConsoleBottomW, kConsoleBottomH, Color{0, 0, 0, 1}, Blend::Opaque);
    if (intermission) return; // the attract level: black, as the desktop's
    const float p = std::clamp(progress, 0.0f, 1.0f);
    // Top: the picture at 1:1 (the first game's) or exactly 1/2 (the sequels' comics), the
    // mission's name and the bar under it.
    constexpr float kPicBottom = 206;
    if (!info.sequel) {
        if (const Texture2D* pic = a.texture("menu\\loading.tga")) {
            const float w = static_cast<float>(pic->width()), h = static_cast<float>(pic->height());
            const float rows = std::min(h, kPicBottom);
            const float cut = std::floor((h - rows) * 0.5f);
            const float x = std::floor((kConsoleTopW - w) * 0.5f);
            // quad(): t = 0 the top row.
            t.r.quad(t.vx(x), t.vy(0), t.vw(w), t.vh(rows), 0, cut / h, 1, (cut + rows) / h, pic, Color{}, Blend::Alpha);
        }
    } else {
        char name[64];
        const float half = 0.5f;
        if (!info.gulf) {
            // AirStrike 2: the comic of the mission's third of the campaign, three columns of 267
            // virtual pixels (as2/frontend.md 3.16) at 1/2: 400 x 165 from y 4.
            const int m = info.missionIndex;
            const int set = m <= 6 ? 1 : m <= 12 ? 2 : 3;
            for (int k = 1; k <= 3; k++) {
                const float x = std::floor(267.0f * half * static_cast<float>(k - 1));
                const float w = (k == 3 ? static_cast<float>(kConsoleTopW) : std::floor(267.0f * half * static_cast<float>(k))) - x;
                std::snprintf(name, sizeof name, "gfx\\ui\\comix\\loading%d_%d_1.tga", set, k);
                if (const Texture2D* c = a.texture(name)) t.r.quad(t.vx(x), t.vy(4), t.vw(w), t.vh(132), 0, 0, 1, 1, c, Color{}, Blend::Alpha);
                std::snprintf(name, sizeof name, "gfx\\ui\\comix\\loading%d_%d_2.tga", set, k);
                if (const Texture2D* c = a.texture(name)) t.r.quad(t.vx(x), t.vy(136), t.vw(w), t.vh(33), 0, 0, 1, 1, c, Color{}, Blend::Alpha);
            }
        } else {
            // Gulf Thunder: its one comic, two rows of four 256-texel tiles, at 1/2 (the first 400
            // pixels of it, its top 206 rows).
            for (int row = 0; row < 2; row++)
                for (int col = 0; col < 4; col++) {
                    const float x = 128.0f * static_cast<float>(col), y = 128.0f * static_cast<float>(row);
                    if (x >= kConsoleTopW || y >= kPicBottom) continue;
                    std::snprintf(name, sizeof name, "gfx\\ui\\comix\\loading1_%d_%d.tga", row, col);
                    const Texture2D* c = a.texture(name);
                    if (!c) continue;
                    const float h = std::min(128.0f, kPicBottom - y);
                    t.r.quad(t.vx(x), t.vy(y), t.vw(128), t.vh(h), 0, 0, 1, h / 128.0f, c, Color{1, 1, 1, 0.812f}, Blend::Alpha);
                }
        }
    }
    if (!info.name.empty()) {
        const FontSize ns = measureTextPx(fm(), info.name, FontSize::L, true) <= kConsoleTopW - 16 ? FontSize::L : FontSize::S;
        t.text(200, kPicBottom + 4, fitLabel(fm(), info.name, ns, kConsoleTopW - 16), ns, sk().value, Align::Center, true);
    }
    const float bx = 100, bw = 200, by = 228;
    t.rect(bx, by, bw, 6, Color{sk().barFill.r, sk().barFill.g, sk().barFill.b, 1});
    t.rect(bx, by, std::floor(bw * p), 6, sk().arrow);
    // Bottom: the controls card (the original's controls page names the keyboard's keys).
    b.text(160, 8, "CONTROLS", FontSize::L, sk().title, Align::Center);
    std::vector<std::string> labels;
    for (const ControlRow& c : kControls) labels.push_back(c.label);
    const FontSize ls = consoleGroupSize(fm(), labels, {kConsoleBottomW - 140.0f}, FontSize::L);
    KeyHintStyle st;
    st.size = FontSize::S;
    st.keyColor = sk().hintKey;
    st.labelColor = sk().hintKey;
    float y = 32;
    for (const ControlRow& c : kControls) {
        const RectF row{0, y, kConsoleBottomW, 20};
        if (c.picture) {
            const KeyGlyphAtlas::Rect kr = consoleKeyRect(c.key);
            const int kw = kr.empty() ? measureTextPx(fm(), hintKeyName(c.key), FontSize::S) + 4 : kr.w;
            drawKeyHint(b.r, b.font, keys, 120 - kw, static_cast<int>(y) + 2, c.key, "", st);
        } else {
            b.text(120, static_cast<float>(textTopIn(row.y, row.h, FontSize::L)), c.name, FontSize::L, sk().hintKey, Align::Right);
        }
        b.text(132, static_cast<float>(textTopIn(row.y, row.h, ls)), fitLabel(fm(), c.label, ls, kConsoleBottomW - 140.0f), ls,
               sk().body);
        y += 22;
    }
}


// ---------------------------------------------------------------------------
// The game selector (R4.8)
// ---------------------------------------------------------------------------
static std::string consoleCardTitle(const GameCard& card) {
    // "AirStrike II: Gulf Thunder" -> "Gulf Thunder": the part a card has room for.
    const size_t c = card.title.find(": ");
    return c == std::string::npos ? card.title : card.title.substr(c + 2);
}

// The card's name on one line when it fits `room` at L, else its two halves at the last space
// ("Gulf Thunder" at 93 px).
static std::vector<std::string> consoleCardTitleLines(const GameCard& card, float room) {
    const std::string t = consoleCardTitle(card);
    const size_t sp = t.rfind(' ');
    if (sp == std::string::npos || measureTextPx(fm(), t, FontSize::L) <= room) return {t};
    return {t.substr(0, sp), t.substr(sp + 1)};
}

ConsoleSelectorLayout consoleSelectorLayout(const GameSelector& sel) {
    ConsoleSelectorLayout L;
    const int n = std::min(static_cast<int>(sel.cards().size()), 3);
    L.count = n;
    if (n > 0) {
        const float w = std::floor((312.0f - 4.0f * static_cast<float>(n - 1)) / static_cast<float>(n));
        const float total = w * static_cast<float>(n) + 4.0f * static_cast<float>(n - 1);
        const float x0 = std::floor((kConsoleBottomW - total) * 0.5f);
        for (int i = 0; i < n; ++i) {
            L.cards[i] = {x0 + (w + 4.0f) * static_cast<float>(i), 22, w, 166};
            // The marquee: a box 4 px inside the card, 56 px high.
            L.marquee[i] = {L.cards[i].x + 4, L.cards[i].y + 6, w - 8, 56};
        }
    }
    L.exit = {kMargin, 198, 92, 38};
    L.play = {kConsoleBottomW - kMargin - 92, 198, 92, 38};
    // One size for the cards' titles and one for their lines (the size rule), L else S. A title
    // wider than its card at L goes on two lines (the owner, 2026-10-12: "You can write gulf
    // thunder on two lines if no space"): the size is chosen for its two parts.
    std::vector<std::string> titles, lines;
    const float room = n > 0 ? L.cards[0].w - 8 : 0;
    for (int i = 0; i < n; ++i) {
        for (const std::string& part : consoleCardTitleLines(sel.cards()[static_cast<size_t>(i)], room)) titles.push_back(part);
        lines.push_back(sel.cards()[static_cast<size_t>(i)].brief);
    }
    L.titleSize = consoleGroupSize(fm(), titles, {room}, FontSize::L);
    L.lineSize = consoleGroupSize(fm(), lines, {room}, FontSize::L);
    return L;
}

void consoleSelectorApplyHits(GameSelector& sel) {
    const ConsoleSelectorLayout L = consoleSelectorLayout(sel);
    Menu* m = sel.menus().top();
    if (!m) return;
    auto virt = [](const RectF& r) {
        return RectF{r.x * kConsoleVirtualPerPx, r.y * kConsoleVirtualPerPx, r.w * kConsoleVirtualPerPx, r.h * kConsoleVirtualPerPx};
    };
    // The cards are the first items, then Exit and Play (screens_launcher.cpp).
    for (size_t i = 0; i < m->items.size(); ++i) {
        MenuItem& it = m->items[i];
        if (static_cast<int>(i) < L.count) it.hit = virt(L.cards[i]);
        else if (it.id == 1) it.hit = virt(L.exit);
        else if (it.id == 2) it.hit = virt(L.play);
        else it.hit = {};
    }
}

void drawConsoleSelectorBottom(Renderer2D& r, const UiAssets& a, const GameSelector& sel, bool banner) {
    g_skin = &kSkinAs3d;
    const Px p(r, a);
    const ConsoleSelectorLayout L = consoleSelectorLayout(sel);
    const Menu* m = const_cast<GameSelector&>(sel).menus().top();
    const int focused = m ? m->focused : -1;
    const float clock = sel.clock();
    p.rect(0, 0, kConsoleBottomW, kConsoleBottomH, Color{0, 0, 0, 1}, Blend::Opaque);
    p.rect(0, 0, kConsoleBottomW, 20, Color{0.10f, 0.02f, 0.015f, 1});
    p.rect(0, 20, kConsoleBottomW, 1, sk().barEdge);
    p.text(160, static_cast<float>(textTopIn(0, 20, FontSize::L)), "CHOOSE A GAME", FontSize::L, Color{1.0f, 0.63f, 0.20f, 1}, Align::Center);
    const float pl = 0.5f + 0.5f * std::sin(clock * 6.0f);
    for (int i = 0; i < L.count; ++i) {
        const GameCard& card = sel.cards()[static_cast<size_t>(i)];
        const RectF& b = L.cards[i];
        const bool cur = i == sel.current();
        p.rect(b, cur ? Color{0.30f, 0.05f, 0.03f, 0.95f} : Color{0.10f, 0.02f, 0.015f, 0.9f});
        p.frame(b, cur ? Color{1.0f, 0.45f + 0.2f * pl, 0.15f, 1} : sk().barEdge, cur ? 2 : 1);
        // The marquee: the game's own animated title in the layout's box; the first game's 3D
        // banner by the app after this list (`banner`), else its name in large text.
        const RectF& mq = L.marquee[i];
        bool drawn = false;
        if (card.marquee == Marquee::Banner) drawn = banner;
        else if (card.marquee != Marquee::None)
            drawn = drawMarquee(r, card, RectF{p.vx(mq.x), p.vy(mq.y), p.vw(mq.w), p.vh(mq.h)}, clock, false);
        if (!drawn) {
            const size_t sp = card.title.rfind(' ');
            const std::string head = sp == std::string::npos ? card.title : card.title.substr(0, sp);
            const std::string tail = sp == std::string::npos ? std::string() : card.title.substr(sp + 1);
            const Color c = cur ? Color{1.0f, 0.55f, 0.20f, 1} : Color{0.85f, 0.35f, 0.15f, 1};
            p.text(mq.x + mq.w * 0.5f, mq.y + 4, fitLabel(fm(), head, FontSize::L, mq.w), FontSize::L, c, Align::Center);
            if (!tail.empty())
                p.text(mq.x + mq.w * 0.5f, mq.y + 20, fitLabel(fm(), tail, measureTextPx(fm(), tail, FontSize::T) <= mq.w ? FontSize::T : FontSize::L, mq.w),
                       measureTextPx(fm(), tail, FontSize::T) <= mq.w ? FontSize::T : FontSize::L, c, Align::Center);
        }
        float y = b.y + 72;
        const float room = b.w - 8;
        for (const std::string& part : consoleCardTitleLines(card, room)) {
            p.text(b.x + b.w * 0.5f, y, fitLabel(fm(), part, L.titleSize, room), L.titleSize,
                   cur ? Color{1, 0.95f, 0.85f, 1} : Color{1.0f, 0.55f, 0.30f, 1}, Align::Center);
            y += 16;
        }
        y += 4;
        if (!card.brief.empty())
            p.text(b.x + b.w * 0.5f, y, fitLabel(fm(), card.brief, L.lineSize, room), L.lineSize, sk().value, Align::Center);
        y += 20;
        if (!card.saveLines.empty() && card.missing)
            p.text(b.x + b.w * 0.5f, y, fitLabel(fm(), card.saveLines[0], FontSize::S, room), FontSize::S, Color{1, 0.35f, 0.3f, 1}, Align::Center);
        if (cur && !card.missing)
            p.text(b.x + b.w * 0.5f, b.y + b.h - 22, fitLabel(fm(), "TAP: PLAY", FontSize::S, room), FontSize::S,
                   Color{1.0f, 0.63f * (0.75f + 0.25f * pl), 0.2f, 1}, Align::Center);
    }
    const struct { const RectF* r; const char* label; int id; } buttons[] = {{&L.exit, "EXIT", 1}, {&L.play, "PLAY", 2}};
    for (const auto& bt : buttons) {
        bool foc = false;
        if (m && focused >= 0 && focused < static_cast<int>(m->items.size())) foc = m->items[static_cast<size_t>(focused)].id == bt.id;
        drawBar(p, *bt.r, foc, false);
        p.textIn(*bt.r, bt.label, FontSize::L, foc ? sk().labelFocus : sk().label);
    }
}

void drawConsoleSelectorTop(Renderer2D& r, const UiAssets& a, const GameSelector& sel, const KeyGlyphAtlas* keys,
                            const std::string& missing, bool banner) {
    g_skin = &kSkinAs3d;
    const Px p(r, a);
    // A dark red ground, darker towards the bottom.
    for (int k = 0; k < 8; ++k)
        p.rect(0, 30.0f * static_cast<float>(k), kConsoleTopW, 30, Color{0.16f - 0.016f * static_cast<float>(k), 0.04f - 0.004f * static_cast<float>(k), 0.02f, 1},
               Blend::Opaque);
    if (sel.cards().empty()) return;
    const GameCard& card = sel.cards()[static_cast<size_t>(std::clamp(sel.current(), 0, static_cast<int>(sel.cards().size()) - 1))];
    const float T = sel.clock();
    float y = 30;
    if (card.marquee == Marquee::TitleLogo && card.logo) {
        // AirStrike 2's logo at exactly 1/2, its letters at y 30..54.
        as2::TitleLogoPictures pics;
        pics.glow = card.glow;
        pics.two = card.two;
        pics.logo = card.logo;
        pics.clouds = card.clouds;
        const float k = 0.5f / p.m.scaleX;
        as2::titleLogoAt(p.r, pics, T, T, p.vx(50) - 124 * k, p.vy(7), k, 1.0f);
        y = 92;
    } else if (card.marquee == Marquee::Emblem && card.logo) {
        // Gulf Thunder's emblem at exactly 1/2 (its drawn part 152 x 106).
        Quad q;
        q.x = p.vx(72);
        q.y = p.vy(-9);
        q.w = p.vw(256);
        q.h = p.vh(128);
        q.texture = card.logo;
        q.blend = Blend::Alpha;
        if (card.clouds) {
            q.texture2 = card.clouds;
            q.s0b = 0.1f * T;
            q.s1b = 0.1f * T + 2.0f;
            q.t0b = 0;
            q.t1b = 1;
            q.combine2 = 2;
        }
        p.r.add(q);
        y = 104;
    } else if (card.marquee == Marquee::Banner && banner) {
        // AirStrike 3D's flaming title mesh, drawn by the app over the top 100 px (the desktop
        // selector's marquee), with its letterbox band: the text below it as under the logos.
        y = 104;
    }
    const FontSize ts = measureTextPx(fm(), card.title, FontSize::T) <= kConsoleTopW - 16 ? FontSize::T : FontSize::L;
    p.text(200, y, card.title, ts, Color{1.0f, 0.55f, 0.20f, 1}, Align::Center, false, TextEffect::Outline);
    y += ts == FontSize::T ? 30 : 20;
    if (!card.version.empty()) p.text(200, y, "Version " + card.version, FontSize::S, Color{0.92f, 0.86f, 0.80f, 1}, Align::Center);
    y += 20;
    if (!missing.empty()) {
        for (const std::string& l : wrapText(fm(), "Missing: " + missing, FontSize::S, 384, false)) {
            p.text(200, y, l, FontSize::S, Color{1, 0.4f, 0.3f, 1}, Align::Center);
            y += 13;
        }
    } else {
        for (const std::string& l : card.saveLines) {
            p.text(200, y, fitLabel(fm(), l, FontSize::L, 384), FontSize::L, Color{0.96f, 0.90f, 0.82f, 1}, Align::Center);
            y += 18;
        }
    }
    // The keys: A Play, B Exit, START + SELECT Quit (the owner's key pictures).
    std::vector<ConsoleHint> hints = {{HintKey::A, "Play"}, {HintKey::B, "Exit"}, {HintKey::Start, ""}, {HintKey::Select, "Quit"}};
    int w = consoleHintsWidth(fm(), hints);
    int x = static_cast<int>((kConsoleTopW - w) / 2);
    p.rect(0, 216, kConsoleTopW, 24, Color{0, 0, 0, 0.6f});
    KeyHintStyle st;
    st.size = FontSize::S;
    st.keyColor = sk().hintKey;
    st.labelColor = sk().hintLabel;
    for (const ConsoleHint& h : hints) {
        drawKeyHint(p.r, p.font, keys, x, 220, h.key, h.label, st);
        x += hintWidth(fm(), h) + 8;
    }
}


} // namespace as3d::ui
