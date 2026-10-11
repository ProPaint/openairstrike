// The console menu view (docs/3ds-ui-design.md "AirStrike 3D menus on the console (round 4,
// R4.5)"): the front end's menus drawn in the 3DS screens' own pixels instead of a 0.4 copy of the
// 800x600 virtual screen. The model stays the Frontend's (its menus, items, focus, values and
// callbacks); this view only
//   - lays the top menu out per screen on the 320x240 bottom screen (ConsoleLayout: full-width
//     bars of at least 36 px, their sub-targets, a footer of pad hints),
//   - writes each item's hit rectangle back (the bar in virtual units, 2.5 x the pixels), so the
//     menu system's pointer and pad paths, the AS3D_MENU log and run.ini ui_taps follow the layout,
//   - turns a touch on the bottom screen into the menu system's input (a bar: a click on its
//     item; a spinner arrow: Left / Right; a slider: the steps to the touched value; a list row
//     or a helicopter cell: the steps to it), and
//   - draws the bottom screen's layout and the top screen's reading panel (titles, the
//     Information pages re-flowed, the mission and helicopter, the scores table, results and
//     the high score) over the attract level or the world.
// Every coordinate here is in screen pixels; the drawing converts them through the Renderer2D's
// mapping, so it works with begin() (the 0.4 mapping of the 3DS lists) and beginPixels() alike.
//
// Covered: the first game's screens (FrontendStyle::Original): main menu, exit, start game, top
// scores, name entry, options, information, in-game menu, game over, mission complete, game
// complete, the tutorial hint (R4.7: its box on the top screen, its OK on the bottom one over the
// dimmed HUD); R4.8: the sequels' screens (FrontendStyle::SequelMenus) in their own skins (main
// menu in two columns with their logo on the top screen, exit, start game, helicopter selection
// with the model on the top screen, top scores, options, Information, credits, name entry,
// in-game menu, hint, portrait dialogue, game over, mission and game complete) and their intro
// pages (on the top screen, FASTER / SKIP on the bottom), the game selector and the loading
// screen. Not covered (the legacy drawing stays): Configure controls (not offered on the
// console) and the plain front end.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "as3d/frontend.h"
#include "as3d/menu.h"
#include "as3d/ui.h"

namespace as3d::ui {

constexpr int kConsoleBottomW = 320, kConsoleBottomH = 240;
constexpr int kConsoleTopW = 400, kConsoleTopH = 240;
constexpr int kConsoleMinBar = 36; // a thumb: 7 mm at the 3DS's 132 dpi
constexpr int kConsoleMinKey = 20; // the on-screen keyboard's keys
constexpr float kConsoleVirtualPerPx = 2.5f; // the bottom screen's 0.4 mapping, inverted

// What a box of the layout is.
enum class ConsoleBoxKind : u8 {
    Bar,         // an item's bar: drawn, a tap clicks the item
    SpinPrev,    // a spinner's (or the helicopter choice's) previous / next arrow: Left / Right
    SpinNext,
    SliderTrack, // a slider's track: the value under the finger
    ListRow,     // a list row: arg = the entry
    ListUp,      // a list's scroll arrows: the selection one up / down
    ListDown,
    GridCell,    // a helicopter cell: arg = the helicopter
    Key,         // a pad key (no item): arg = the key code (keys::Left, keys::Right, ...)
};

struct ConsoleBox {
    ConsoleBoxKind kind = ConsoleBoxKind::Bar;
    RectF r;               // bottom-screen pixels
    int item = -1;         // index in Menu::items (-1 for Key)
    int arg = 0;
    std::string label;     // the text drawn on a Bar or Key (a Button with a picture: empty)
    FontSize size = FontSize::L;
    // The size rule (docs/3ds-ui-design.md "Owner decisions", R4.7): the labels of a group share
    // one size, the largest at which the group's longest label fits its room; a label that does
    // not fit even at S is cut with "...". Groups: the rows of one list (every entry, not only the
    // visible ones), and the labelled boxes of the same kind and the same width and height (a
    // column of bars, a row of buttons, the option rows). kOwnSize: the box is sized alone (the
    // keyboard's keys: key caps, not a list).
    static constexpr int kOwnSize = -2;
    int group = -1;        // -1: the automatic groups above; kOwnSize: alone
};

struct ConsoleHint {
    HintKey key = HintKey::A;
    std::string label;
};

struct ConsoleLayout {
    bool valid = false;
    Screen screen = Screen::MainMenu;
    bool intro = false;              // the sequels' intro pages (no menu: `screen` unused)
    std::vector<ConsoleBox> boxes;   // drawn in this order; a touch takes the last box under it
    std::vector<ConsoleHint> hints;  // the footer, drawn at (hintX, hintY), S text
    int hintX = 0, hintY = 0;
    int hintW = 0;                   // the footer's width in pixels (with the set's key widths)
    int hintRight = kConsoleBottomW; // the footer's room ends here
    // The bottom screen's own texts: a title (L, centred at titleY) and a sub-title (S).
    std::string title, subtitle;
    int titleY = 0, subtitleY = 0;
    FontSize titleSize = FontSize::L;
    // The spinner values of the screen (between their arrows), one size for all of them and all
    // their values (the size rule): L, else S, else cut with "...".
    FontSize valueSize = FontSize::L;

    const ConsoleBox* boxAt(float px, float py) const;
    const ConsoleBox* barOf(int item) const;
};

// The owner's key pictures (docs/3ds-ui-design.md "Owner decisions": set 4, "HUD plate"): the
// rectangles of apps/3ds/assets/keys.tga (217 x 19, original art of this project). The width of
// each key is what the footers are laid out with, with or without the picture loaded.
KeyGlyphAtlas::Rect consoleKeyRect(HintKey k);
// The width a footer takes (key pictures, gaps, S labels, 8 px between hints).
int consoleHintsWidth(const FontMetrics& m, const std::vector<ConsoleHint>& hints);

// The size rule (see ConsoleBox::group): the largest of `from` and the smaller sizes at which
// every text fits `room` pixels; S when none does (the texts are then cut: fitLabel).
FontSize consoleGroupSize(const FontMetrics& m, const std::vector<std::string>& texts, const std::vector<float>& room,
                          FontSize from = FontSize::L);
// `text` at `size` cut to `room` pixels with "..." when it does not fit (unchanged when it does).
std::string fitLabel(const FontMetrics& m, const std::string& text, FontSize size, float room);

// The tutorial hints' key names as the console's buttons: "{Ctrl}" -> "{A}" etc. (the default
// pad bindings: A fire, B missile, X power-up, Y next weapon, R next missile, ZR next power-up)
// and "key" -> "button" after them.
std::string consoleKeyNames(const std::string& text);

// One Information page as the top screen shows it: the page's lines joined into paragraphs and
// wrapped at S to `width` pixels, then cut into sheets of `linesPerSheet` lines (a page longer
// than the screen is read in sheets: L / R and the arrows step through them).
struct InfoLine {
    std::string text;   // with the page's {markup}; braces balanced per line
    bool centred = false;
    int icon = -1;      // the paragraph's icon index (first line only), -1 none
    int gapBefore = 0;  // pixels before this line (paragraph spacing)
};
struct InfoPage {
    std::string title;
    std::vector<std::vector<InfoLine>> sheets;
};
InfoPage consoleInfoPage(const Texts& texts, const FontMetrics& m, int page, int width, int height);
// Words of `text` wrapped at `size` into lines of at most `width` pixels (markup braces kept
// balanced on every line).
std::vector<std::string> wrapText(const FontMetrics& m, std::string_view text, FontSize size, int width,
                                  bool markup = true);

// The loading screen on the console (R4.8, docs/3ds-ui-design.md "Sequels, selector, loading and
// name entry on the console"): on the top screen the game's loading picture 1:1 (the first game's
// menu loading.tga) or its comic at exactly 1/2 (the sequels'), the mission's name and a 6-px bar;
// on the bottom screen the console's controls card (the key pictures of `keys`, or their names).
// An intermission (the attract level) is black on both. The look follows loadingInfo().
void drawConsoleLoading(Renderer2D& top, Renderer2D& bottom, const UiAssets& a, float progress, bool intermission,
                        const KeyGlyphAtlas* keys);

// The game selector on the console (R4.8): GameSelector's model (its cards, focus, Exit and Play)
// in the bottom screen's pixels: "CHOOSE A GAME", up to three cards of 101 x 166 (the game's
// marquee, its title and its line at one size for all the cards, "TAP TO PLAY" on the current one),
// EXIT and PLAY 92 x 38 in the bottom corners; on the top screen the current game's logo at 1/2,
// its name (T), version, save lines (or its missing files) and the keys.
struct ConsoleSelectorLayout {
    int count = 0;
    RectF cards[3];
    RectF exit, play;
    FontSize titleSize = FontSize::L, lineSize = FontSize::L;
};
ConsoleSelectorLayout consoleSelectorLayout(const GameSelector& sel);
// Writes the items' hit rectangles (virtual units, 2.5 x the pixels): taps through the bottom
// screen's 0.4 mapping land on the drawn cards and buttons.
void consoleSelectorApplyHits(GameSelector& sel);
void drawConsoleSelectorBottom(Renderer2D& r, const UiAssets& a, const GameSelector& sel);
void drawConsoleSelectorTop(Renderer2D& r, const UiAssets& a, const GameSelector& sel, const KeyGlyphAtlas* keys,
                            const std::string& missing);

class ConsoleMenuView {
public:
    // True when this view draws the front end's top menu (else the host keeps Frontend::drawUnder
    // / drawOver and the linear touch mapping).
    static bool handles(const Frontend& fe);
    // The sequels' intro pages are up (their logo pages and AirStrike 2's comic): the page on the
    // top screen, FASTER and SKIP on the bottom one (R4.8).
    static bool introShown(const Frontend& fe);

    // The layout of the top menu (invalid when not handled).
    ConsoleLayout layout(const Frontend& fe) const;
    // Writes every item's hit rectangle (virtual units) from the layout: the item's bar; items
    // without one get an empty rectangle. Call after every change of the menu stack (before and
    // after Frontend::update, before drawing). It also keeps the Information sheet with its page.
    void applyHits(Frontend& fe);

    // The front end's input for one frame of the touch screen: `touching` now and `wasTouching`
    // the frame before, (px, py) in bottom-screen pixels. Appends to `out`.
    void touchInput(const Frontend& fe, bool touching, bool wasTouching, int px, int py, UiInput& out);
    // Keys the view takes before the menu: on Information, Left / Right / PageUp / PageDown step
    // through the page's sheets before the page itself. Rewrites `in`.
    void filterKeys(const Frontend& fe, UiInput& in);

    // Draws the bottom screen's layout (the list begun by the host, any mapping).
    void drawBottom(Renderer2D& r, const UiAssets& a, const Frontend& fe) const;
    // Draws the top screen's panel for the menu (400 x 240 pixels of the list's mapping); false
    // when the screen has nothing to show there.
    bool drawTop(Renderer2D& r, const UiAssets& a, const Frontend& fe) const;

    // The key pictures (KeyGlyphAtlas with consoleKeyRect's rectangles); null: the keys' names.
    void setKeyGlyphs(const KeyGlyphAtlas* atlas) { keys_ = atlas; }
    const KeyGlyphAtlas* keyGlyphs() const { return keys_; }

    int infoSheet() const { return infoSheet_; }

private:
    const KeyGlyphAtlas* keys_ = nullptr;
    // A list for pictures of the virtual screen drawn at another scale (the intro pages), appended
    // to the top screen's list (Renderer2D::appendMapped); never flushed itself.
    mutable std::unique_ptr<Renderer2D> scratch_;
    // The touch in progress: the box it went down on (kind, item, arg), -1 none.
    int pressKind_ = -1, pressItem_ = -1;
    bool pressMouse_ = false; // Mouse1 was pressed for it (released with the finger)
    float lastSliderValue_ = 0;
    // Information: the sheet of the page shown, the page it belongs to, and whether the last
    // page change was backwards (the new page then opens at its last sheet).
    int infoSheet_ = 0, infoSheetPage_ = -1;
    bool infoBackward_ = false;
};

} // namespace as3d::ui
