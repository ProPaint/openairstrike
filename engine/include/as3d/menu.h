// Menu system: menu stack, items, widgets, focus, tooltips and the menu cursor.
// Spec: docs/spec/frontend.md 2 (authority) with the touch notes of section 7.
//
// Input arrives as a plain UiInput (an ordered list of pointer moves, key or button presses and
// releases, and typed characters) in virtual 800x600 coordinates, so the whole system runs
// headless in tests and the same code serves mouse and touch: a tap is a pointer move followed
// by a press and release of Mouse1. Sounds are reported by name (takeSounds), never played
// here. Drawing goes through Renderer2D and UiAssets (ui.h).
//
// Touch mode (MenuSystem::touchMode) keeps the original behaviour and adds only what a device
// without keyboard, mouse buttons or hover needs; every addition is listed at the item it
// changes and in docs/spec/issues/090-touch-mode-additions.md.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "as3d/ui.h"

namespace as3d::ui {

// Key codes: Windows virtual keys, extended as in engine-behaviour.md 7.2.
namespace keys {
constexpr int Backspace = 8, Tab = 9, Enter = 13, Shift = 16, Ctrl = 17, Pause = 19, Escape = 27,
              Space = 32, PageUp = 33, PageDown = 34, End = 35, Home = 36, Left = 37, Up = 38,
              Right = 39, Down = 40, Insert = 45, Delete = 46, F5 = 0x74, F6 = 0x75, F7 = 0x76,
              F8 = 0x77, F9 = 0x78, F12 = 0x7B;
constexpr int Mouse1 = 200, Mouse2 = 201, Mouse3 = 202, Joy1 = 203, WheelDown = 239, WheelUp = 240,
              StickLeft = 241, StickRight = 242, StickUp = 243, StickDown = 244;
// Display name of a key code for the controls screen (frontend.md 3.7).
std::string name(int code);
} // namespace keys

struct UiEvent {
    enum class Type { PointerMove, Press, Release, Char };
    Type type = Type::PointerMove;
    int code = 0;       // key code for Press/Release, character for Char
    float x = 0, y = 0; // virtual coordinates for PointerMove
};

struct UiInput {
    std::vector<UiEvent> events; // in the order they happened

    UiInput& move(float x, float y) { events.push_back({UiEvent::Type::PointerMove, 0, x, y}); return *this; }
    UiInput& press(int code) { events.push_back({UiEvent::Type::Press, code, 0, 0}); return *this; }
    UiInput& release(int code) { events.push_back({UiEvent::Type::Release, code, 0, 0}); return *this; }
    UiInput& key(int code) { return press(code).release(code); }
    UiInput& tap(float x, float y) { return move(x, y).press(keys::Mouse1).release(keys::Mouse1); }
    UiInput& text(std::string_view s) {
        for (char c : s) events.push_back({UiEvent::Type::Char, static_cast<unsigned char>(c), 0, 0});
        return *this;
    }
};

// Item types (frontend.md 2.2). Custom is ours: a screen-drawn row with its own hit rectangle
// (controls rows, touch-mode text buttons). Picture and SequelButton are the sequels' types 2
// and 9 (as2/frontend.md 2.2, 2.5): an image without highlight, and the text button with its
// slide-in, drawn only by a MenuSystem in the sequels' style.
enum class ItemType { Text = 0, Button = 1, Edit = 2, List = 4, Spinner = 5, Slider = 6, HeliGrid = 7, Custom = 100,
                      Picture = 101, SequelButton = 102 };

namespace itemflag {
constexpr u32 Focused = 1, Disabled = 2, Hidden = 4, AlignRight = 0x2000, AlignCenter = 0x4000,
              Markup = 0x10000, NoHoverSound = 0x20000;
}

// Item events (frontend.md 2.2).
enum ItemEvent { kActivate = 1, kFocusGained = 2, kFocusLost = 3 };

struct ListEntry {
    std::string text;
    bool enabled = true;
};

// Helicopter choices shared with the front end: written immediately on a click (frontend.md
// 2.5), so they survive Back.
struct HeliGridRef {
    int* choice = nullptr;         // [2]: player 1 and player 2
    int* alternator = nullptr;     // which player the next click is for (two players)
    const bool* locked = nullptr;  // [count]
    const bool* twoPlayers = nullptr;
    int count = 10;                // helicopters of the game: cells drawn and clickable
};
// Cells the 5 by 2 grid and its icon tables (menu\\icons_1..3.tga) have. A game with fewer
// helicopters shows its first cells; one with more shows no more than these.
// HOOK: a game with its own icon sheet and grid layout replaces cellPos and the icon tables in
// menu_widgets.cpp per game (HeliGridRef::count is already the game's).
constexpr int kHelicopters = 10;

struct Menu;
struct MenuItem;
class MenuSystem;

struct MenuDrawContext {
    Renderer2D& r;
    const UiAssets& a;
    Menu& menu;
    float mt;        // menu time
    bool touchMode;
    long long ms;    // wall-clock milliseconds for blinking cursors
    bool plain = false; // MenuSystem::plain: draw without the first game's menu textures
    bool sequel = false; // MenuSystem::sequel: the sequels' widgets (as2/frontend.md 2.5)
    float px = -100, py = -100; // the pointer (the sequels' scroll boxes light up under it)
};

using ItemDrawFn = std::function<void(MenuDrawContext&, MenuItem&, bool focused)>;
using ItemKeyFn = std::function<bool(MenuItem&, int key)>; // true = consumed

struct MenuItem {
    ItemType type = ItemType::Text;
    int id = 0;
    u32 flags = 0;
    std::string label;
    std::string tooltip;
    float x = 0, y = 0;
    RectF hit;
    std::function<void(MenuItem&, int event)> callback; // else Menu::onItem

    // Button
    float w = 0, h = 0;
    std::string texture, highlight; // game paths
    SpecUv uv{0, 0, 1, 1};

    // Edit field
    std::string text;
    int cursor = 0;
    bool overwrite = false;

    // List
    std::vector<ListEntry> entries;
    int selected = 0, top = 0;

    // Spinner
    std::vector<std::string> values;
    int index = 0;

    // Slider
    int min = 0, max = 10;
    float value = 0, step = 1;
    bool dragging = false;

    // Helicopter grid
    HeliGridRef grid;

    // Custom
    ItemDrawFn draw;
    ItemKeyFn onKey;
    float textScale = 1; // text buttons: font scale of the caption

    // SequelButton: the caption width it is drawn with (max(caption, minimum width) = W; the
    // frame is W + 46 wide) and its slide value a, 0..1 (as2/frontend.md 2.5).
    float captionW = 0;
    float slide = 0;

    bool disabled() const { return (flags & itemflag::Disabled) != 0; }
    bool hidden() const { return (flags & itemflag::Hidden) != 0; }
    void setEnabled(bool on) { flags = on ? flags & ~itemflag::Disabled : flags | itemflag::Disabled; }
    void setShown(bool on) {
        flags = on ? flags & ~(itemflag::Disabled | itemflag::Hidden) : flags | itemflag::Disabled | itemflag::Hidden;
    }
    int visibleRows() const { return static_cast<int>((h - 4) / 20); }
};

struct Menu {
    static constexpr int kMaxItems = 64;
    std::string name;
    std::vector<MenuItem> items;
    int focused = -1;
    int hovered = -1;
    float hoverTime = 0;
    bool swallowBack = false; // Esc and right click do nothing (main menu, end screens, name entry)
    int tag = -1;             // free for the owner (the front end stores its Screen here)
    // The sequels' open value f (as2/frontend.md 2.1): 0 whenever the menu becomes the top one,
    // then raised by 8 x dt per update up to 1 (0.125 s). Only the sequels' style reads it.
    float open = 0;
    // The sequels' text buttons of this menu: the frame's extra width beyond the caption and its
    // height (AirStrike 2: 46 and 30; Gulf Thunder: 0 and 37, gulf/frontend.delta.md 2.2).
    float buttonMargin = 46, buttonHeight = 30;

    std::function<void(MenuDrawContext&)> drawBack;   // before the items (frames, texts)
    std::function<void(MenuDrawContext&)> drawFront;  // after the items
    std::function<bool(Menu&, int key)> onKey;         // runs before the generic handler; true = consumed
    std::function<void(Menu&, MenuItem&, int event)> onItem;
    std::function<void(Menu&, float dt, float mt)> onUpdate;
    std::function<void(Menu&, char c)> onChar;         // typed characters not taken by an edit field

    MenuItem* find(int id);
    // Adders (frontend.md 2.2 hit rectangles). The returned reference is valid until the next add.
    MenuItem& addText(int id, float x, float y, std::string label, u32 flags = 0);
    MenuItem& addButton(int id, float x, float y, float w, float h, std::string texture, std::string highlight,
                        SpecUv uv = {0, 0, 1, 1}, u32 flags = 0);
    MenuItem& addEdit(int id, float x, float y, float w);
    MenuItem& addList(int id, float x, float y, float w, float h, std::vector<ListEntry> entries);
    MenuItem& addSpinner(int id, float x, float y, std::string label, std::vector<std::string> values, int index);
    MenuItem& addSlider(int id, float x, float y, std::string label, int min, int max, float value, float step = 1);
    MenuItem& addHeliGrid(int id, float x, float y, HeliGridRef ref);
    MenuItem& addCustom(int id, RectF hit, ItemDrawFn draw, ItemKeyFn onKey = {});
    // Ours (touch mode and on-screen keyboards): a text on a dark-red box, rust, orange with a
    // pulse when focused, in the style of the focused spinner.
    MenuItem& addTextButton(int id, RectF hit, std::string label, u32 flags = 0);
    // The sequels (as2/frontend.md 2.2, 2.5). A picture: `texture` with `uv` over (x, y, w, h).
    MenuItem& addPicture(int id, float x, float y, float w, float h, std::string texture, SpecUv uv, u32 flags = 0);
    // A text button: W = max(caption width, minWidth), hit rectangle (left, y, W + margin, height)
    // (margin 46 and height 30; Menu::buttonMargin, buttonHeight) with left = x, x - w / 2
    // (AlignCenter) or x - w (AlignRight). No hover sound.
    MenuItem& addSequelButton(int id, float x, float y, std::string label, u32 flags = 0, float minWidth = 0);
};

// Draws a text button as addTextButton makes them (for screens that draw their own).
void drawTextButton(MenuDrawContext& c, const RectF& hit, std::string_view label, bool focused, bool disabled,
                    float textScale = 1);

class MenuSystem {
public:
    static constexpr int kMaxDepth = 16;

    bool touchMode = false;      // see the file comment
    bool showHints = false;      // ShowHints: tooltips
    bool drawCursor = true;      // false with UseSystemMouse or on touch devices
    // Ours (docs/spec/as2/issues/260): the plain front end of the sequels draws the list and
    // slider widgets and the letterbox frame with rectangles and lines, so that it needs none
    // of the first game's menu textures (a sequel ships only menu\\cursor_1.tga and cursor_2.tga).
    bool plain = false;
    // The sequels' menus (as2/frontend.md 2): their widget drawing (green, orange, the
    // interface.tga pieces), widgets hidden until the menu is open, text buttons and pictures.
    bool sequel = false;

    // Pushes a menu (built right before, frontend.md 2.1). Resets the menu time, clears the new
    // menu's focus and re-runs the hover test. Beyond 16 menus the push is refused.
    Menu* push(Menu menu);
    void pop();
    void clear();
    // Replaces the whole stack by one menu (M_ShowMainMenu).
    Menu* replaceAll(Menu menu);

    bool empty() const { return stack_.empty(); }
    int depth() const { return static_cast<int>(stack_.size()); }
    Menu* top() { return stack_.empty() ? nullptr : stack_.back().get(); }
    const Menu* top() const { return stack_.empty() ? nullptr : stack_.back().get(); }
    Menu* at(int i) { return stack_[static_cast<size_t>(i)].get(); }
    float menuTime() const { return mt_; }

    // Advances the menu time and hover time, then routes the events to the top menu.
    void update(float dt, const UiInput& input);
    // Routes one key press to the top menu (as update would).
    void pressKey(int code);

    // Background of the top menu (frames, texts), then items, front layer, tooltip and cursor.
    // The two halves exist so a 3D pass (the main menu's banner) can go between them.
    void drawBackground(Renderer2D& r, const UiAssets& a);
    void drawItems(Renderer2D& r, const UiAssets& a);
    void draw(Renderer2D& r, const UiAssets& a) { drawBackground(r, a); drawItems(r, a); }

    float pointerX() const { return px_; }
    float pointerY() const { return py_; }
    void setPointer(float x, float y);
    bool leftHeld() const { return leftHeld_; }

    // Sound cues since the last call ("sounds\\menu1.wav", "sounds\\menu2.wav", ...).
    std::vector<std::string> takeSounds();
    void playSound(std::string name) { sounds_.push_back(std::move(name)); }
    long long clockMs() const { return clockMs_; }

    // Sends an event to an item (its callback, else the menu's onItem).
    void sendEvent(Menu& m, int index, int event);
    // Focus handling used by the pointer and keyboard paths.
    void focus(Menu& m, int index);
    int hitTest(const Menu& m, float x, float y) const;

private:
    void onPointerMove(float x, float y);
    void onPress(int code);
    void onRelease(int code);
    void onChar(int c);
    bool widgetKey(Menu& m, int index, int code);
    void focusFirst(Menu& m);
    void focusStep(Menu& m, int dir);
    void activate(Menu& m, int index);
    void reset(Menu& m);
    void collect();

    std::vector<std::unique_ptr<Menu>> stack_;
    std::vector<std::unique_ptr<Menu>> graveyard_; // popped menus live until the next update
    float mt_ = 0;
    float px_ = 400, py_ = 300;
    bool leftHeld_ = false;
    bool pointerMovedThisFrame_ = false;
    long long clockMs_ = 0;
    double clockAcc_ = 0;
    std::vector<std::string> sounds_;
};

// Widget helpers shared with the screens.
namespace widgets {
// Scroll so that the selection is visible and step back over disabled entries.
void listFixSelection(MenuItem& list);
// Sets a slider from a pointer x (frontend.md 2.5).
void sliderClick(MenuItem& s, float px);
// Two-player-aware click on the helicopter grid; returns true if a choice changed.
bool gridClick(MenuItem& g, float px, float py);
int gridCellAt(const MenuItem& g, float px, float py); // -1 outside the cells
// Letterbox bars and rules shared by most screens (frontend.md 3.1). Plain: the same bars with
// two drawn rules in place of the corner ornament.
void letterbox(MenuDrawContext& c);
// Plain screens: a title in the top bar (scaled game font, orange, black shadow), centred.
void plainTitle(MenuDrawContext& c, std::string_view title, float scale = 2.0f);
// Plain screens: text with the black shadow of the original's alpha font.
void shadowedText(MenuDrawContext& c, float x, float y, std::string_view s, Color col, Align al = Align::Left,
                  float scale = 1.0f);
// Three-layer header `<base>_2` ALPHA, `_0` ADD, `_1` ALPHA over the same rectangle.
void header(MenuDrawContext& c, std::string_view base, float x, float y, float w, float h);
// Panel fill 0x50000000, blend ALPHA.
void panel(MenuDrawContext& c, float x, float y, float w, float h);
// Additive text helper.
void text(MenuDrawContext& c, float x, float y, std::string_view s, Color col, Align al = Align::Left,
          bool markup = false);
} // namespace widgets

} // namespace as3d::ui
