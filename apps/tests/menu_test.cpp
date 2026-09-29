// Menu system tests from synthetic input (frontend.md 2): stack, focus, sounds, generic keys
// and every widget. CPU only.
#include "doctest.h"

#include <algorithm>

#include "as3d/menu.h"

using namespace as3d;
using namespace as3d::ui;

namespace {

int count(const std::vector<std::string>& v, const char* s) {
    return static_cast<int>(std::count(v.begin(), v.end(), std::string(s)));
}

struct Recorder {
    std::vector<std::pair<int, int>> events; // (id, event)
    void attach(Menu& m) {
        m.onItem = [this](Menu&, MenuItem& it, int ev) { events.push_back({it.id, ev}); };
    }
    int activations(int id) const {
        int n = 0;
        for (auto& e : events) n += e.first == id && e.second == kActivate;
        return n;
    }
};

Menu threeButtons(Recorder& rec) {
    Menu m;
    m.name = "test";
    m.addButton(1, 100, 100, 100, 30, "", "");
    m.addButton(2, 100, 140, 100, 30, "", "", {0, 0, 1, 1}, itemflag::Disabled);
    m.addButton(3, 100, 180, 100, 30, "", "", {0, 0, 1, 1}, itemflag::NoHoverSound);
    rec.attach(m);
    return m;
}

} // namespace

TEST_CASE("menu stack: push, pop, depth limit, menu time") {
    MenuSystem ms;
    Recorder rec;
    CHECK(ms.empty());
    ms.push(threeButtons(rec));
    ms.update(0.5f, {});
    CHECK(ms.menuTime() == doctest::Approx(0.5f));
    ms.push(threeButtons(rec));
    CHECK(ms.menuTime() == 0.0f); // pushing restarts the clock
    ms.update(0.25f, {});
    ms.pop();
    CHECK(ms.menuTime() == 0.0f); // so does popping
    CHECK(ms.depth() == 1);
    for (int i = 0; i < 20; i++) ms.push(threeButtons(rec));
    CHECK(ms.depth() == MenuSystem::kMaxDepth);
    ms.replaceAll(threeButtons(rec));
    CHECK(ms.depth() == 1);
}

TEST_CASE("menu focus follows the pointer, with hover and activate sounds") {
    MenuSystem ms;
    Recorder rec;
    ms.setPointer(0, 0);
    ms.push(threeButtons(rec));
    Menu* m = ms.top();
    CHECK(m->focused == -1);
    ms.update(0.016f, UiInput().move(150, 110));
    CHECK(m->focused == 0);
    CHECK(count(ms.takeSounds(), "sounds\\menu2.wav") == 1);
    // Empty space keeps the focus.
    ms.update(0.016f, UiInput().move(10, 10));
    CHECK(m->focused == 0);
    // Disabled items are skipped by the hit test.
    ms.update(0.016f, UiInput().move(150, 150));
    CHECK(m->focused == 0);
    // No hover sound for flag 0x20000.
    ms.takeSounds();
    ms.update(0.016f, UiInput().move(150, 190));
    CHECK(m->focused == 2);
    CHECK(ms.takeSounds().empty());
    // Focus events: 3 to the old, 2 to the new.
    bool lost = false, gained = false;
    for (auto& e : rec.events) {
        lost |= e.first == 1 && e.second == kFocusLost;
        gained |= e.first == 3 && e.second == kFocusGained;
    }
    CHECK(lost);
    CHECK(gained);
    // Click activates the item under the pointer, with menu1.wav.
    ms.update(0.016f, UiInput().press(keys::Mouse1).release(keys::Mouse1));
    CHECK(rec.activations(3) == 1);
    CHECK(count(ms.takeSounds(), "sounds\\menu1.wav") == 1);
    // A click over empty space activates nothing.
    ms.update(0.016f, UiInput().tap(5, 5));
    CHECK(rec.activations(3) == 1);
    // Enter activates the focused item.
    ms.update(0.016f, UiInput().key(keys::Enter));
    CHECK(rec.activations(3) == 2);
}

TEST_CASE("menu generic keys: navigation wraps and skips disabled, Esc pops unless swallowed") {
    MenuSystem ms;
    Recorder rec;
    ms.setPointer(0, 0);
    ms.push(threeButtons(rec));
    Menu* m = ms.top();
    ms.update(0, UiInput().key(keys::Down)); // nothing focused: focus the first
    CHECK(m->focused == 0);
    ms.update(0, UiInput().key(keys::Down)); // skips the disabled one
    CHECK(m->focused == 2);
    ms.update(0, UiInput().key(keys::Tab)); // wraps
    CHECK(m->focused == 0);
    ms.update(0, UiInput().key(keys::Up));
    CHECK(m->focused == 2);
    ms.update(0, UiInput().key(keys::Escape));
    CHECK(ms.empty());

    Menu sw = threeButtons(rec);
    sw.swallowBack = true;
    ms.push(std::move(sw));
    ms.update(0, UiInput().key(keys::Escape).key(keys::Mouse2));
    CHECK(ms.depth() == 1);
    // A screen key handler runs first.
    ms.top()->onKey = [](Menu&, int code) { return code == keys::Down; };
    ms.update(0, UiInput().key(keys::Down));
    CHECK(ms.top()->focused == -1);
}

TEST_CASE("menu edit field: typing, editing keys, width limit, Enter") {
    MenuSystem ms;
    Recorder rec;
    Menu m;
    m.addEdit(2, 275, 285, 250);
    rec.attach(m);
    ms.setPointer(0, 0);
    ms.push(std::move(m));
    MenuItem& e = ms.top()->items[0];
    ms.update(0, UiInput().key(keys::Tab).text("Pilot"));
    CHECK(e.text == "Pilot");
    ms.update(0, UiInput().key(keys::Backspace).key(keys::Home).key(keys::Delete));
    CHECK(e.text == "ilo");
    ms.update(0, UiInput().key(keys::Insert).text("X"));
    CHECK(e.text == "Xlo");
    ms.update(0, UiInput().key(keys::End).text("\x01"));
    CHECK(e.text == "Xlo"); // control characters refused
    // Navigation keys are consumed by the field.
    ms.update(0, UiInput().key(keys::Down));
    CHECK(ms.top()->focused == 0);
    // Width limit: w - 20.
    ms.update(0, UiInput().text(std::string(80, 'W')));
    CHECK(measureText(FontMetrics::original(), e.text) < 250 - 20 + 23);
    CHECK(e.text.size() < 63);
    ms.update(0, UiInput().key(keys::Enter));
    CHECK(rec.activations(2) == 1);
}

TEST_CASE("menu list: disabled entries cannot be selected; keys, clicks, arrows, track") {
    MenuSystem ms;
    Recorder rec;
    Menu m;
    std::vector<ListEntry> entries;
    for (int i = 0; i < 20; i++) entries.push_back({"Mission " + std::to_string(i + 1), i < 3 || i == 10});
    m.addList(1, 295, 160, 420, 106, entries);
    rec.attach(m);
    ms.setPointer(0, 0);
    ms.push(std::move(m));
    MenuItem& l = ms.top()->items[0];
    CHECK(l.visibleRows() == 5);
    ms.update(0, UiInput().key(keys::Tab));
    ms.update(0, UiInput().key(keys::Down).key(keys::Down));
    CHECK(l.selected == 2);
    ms.update(0, UiInput().key(keys::Down)); // 3 disabled: stays
    CHECK(l.selected == 2);
    ms.update(0, UiInput().key(keys::End)); // 19 disabled: steps back to 10
    CHECK(l.selected == 10);
    CHECK(l.top <= 10);
    CHECK(l.top + 5 > 10);
    ms.update(0, UiInput().key(keys::Home));
    CHECK(l.selected == 0);
    CHECK(l.top == 0);
    // Click on row 1 selects it; on a disabled row does nothing.
    ms.update(0, UiInput().tap(320, 160 + 4 + 20 + 5));
    CHECK(l.selected == 1);
    ms.update(0, UiInput().tap(320, 160 + 4 + 60 + 5)); // entry 3, disabled
    CHECK(l.selected == 1);
    // Down arrow zone steps by one.
    ms.update(0, UiInput().tap(295 + 420 - 10, 160 + 106 - 10));
    CHECK(l.selected == 2);
    // Track sets the first visible row without moving the selection.
    ms.update(0, UiInput().tap(295 + 420 - 10, 160 + 106 - 40));
    CHECK(l.top > 0);
    CHECK(l.selected == 2);
    // Tab leaves the list.
    ms.update(0, UiInput().key(keys::PageDown));
    CHECK(l.selected == 2); // 7 is disabled, back to 2
}

TEST_CASE("menu spinner: forward on click and Enter, back on Left, wrapping; touch zones") {
    MenuSystem ms;
    Recorder rec;
    Menu m;
    m.addSpinner(5, 400, 200, "Difficulty:", {"A", "B", "C"}, 1);
    m.addButton(9, 600, 450, 100, 50, "", "");
    rec.attach(m);
    ms.setPointer(0, 0);
    ms.push(std::move(m));
    MenuItem& s = ms.top()->items[0];
    CHECK(s.hit.x < 400 - 10);
    ms.update(0, UiInput().tap(420, 205));
    CHECK(s.index == 2);
    ms.update(0, UiInput().key(keys::Enter));
    CHECK(s.index == 0);
    ms.update(0, UiInput().key(keys::Left));
    CHECK(s.index == 2);
    ms.update(0, UiInput().key(keys::WheelDown));
    CHECK(s.index == 1);
    CHECK(rec.activations(5) == 4);
    // Desktop quirk kept: a click on empty space advances the focused spinner.
    ms.update(0, UiInput().tap(20, 580));
    CHECK(s.index == 2);
    // Touch: taps outside do nothing; the zone left of the value cycles backwards.
    ms.touchMode = true;
    ms.update(0, UiInput().tap(20, 580));
    CHECK(s.index == 2);
    ms.update(0, UiInput().tap(398, 205));
    CHECK(s.index == 1);
    ms.update(0, UiInput().tap(425, 205));
    CHECK(s.index == 2);
    // Navigation keys are consumed by the spinner.
    ms.update(0, UiInput().key(keys::Down));
    CHECK(ms.top()->focused == 0);
}

TEST_CASE("menu slider: keys, click positions, drag") {
    MenuSystem ms;
    Recorder rec;
    Menu m;
    m.addSlider(6, 408, 280, "Sound Volume:", 0, 10, 5);
    rec.attach(m);
    ms.setPointer(0, 0);
    ms.push(std::move(m));
    MenuItem& s = ms.top()->items[0];
    CHECK(s.hit.x == doctest::Approx(408 - 130 - 10));
    CHECK(s.hit.x + s.hit.w == doctest::Approx(408 + 138));
    ms.update(0, UiInput().move(420, 285).key(keys::Right));
    CHECK(s.value == doctest::Approx(6));
    ms.update(0, UiInput().key(keys::Left).key(keys::Left));
    CHECK(s.value == doctest::Approx(4));
    ms.update(0, UiInput().tap(410, 285)); // left of x + 8
    CHECK(s.value == 0.0f);
    ms.update(0, UiInput().tap(408 + 8 + 64, 285)); // middle of the bar
    CHECK(s.value == doctest::Approx(5));
    ms.update(0, UiInput().tap(545, 285)); // right of x + 136
    CHECK(s.value == 10.0f);
    // Drag from the knob: held button, pointer moves along the row.
    const float knob = 408 + 128; // value 10
    ms.update(0, UiInput().move(knob + 4, 285).press(keys::Mouse1));
    CHECK(s.dragging);
    ms.update(0.016f, UiInput().move(408 + 8 + 32, 285));
    CHECK(s.value == doctest::Approx(2.5f));
    ms.update(0.016f, UiInput().release(keys::Mouse1));
    CHECK_FALSE(s.dragging);
    ms.update(0.016f, UiInput().move(408 + 8 + 96, 285));
    CHECK(s.value == doctest::Approx(2.5f));
    // Slider passes navigation keys on.
    ms.update(0, UiInput().key(keys::Down));
    CHECK(ms.top()->focused == 0); // only item: wraps to itself
}

TEST_CASE("menu helicopter grid: one player, two players alternate, locked cells ignored") {
    MenuSystem ms;
    int choice[2] = {1, 0};
    int alt = 0;
    bool locked[kHelicopters] = {false, false, true, true, true, true, true, true, true, true};
    bool two = false;
    Menu m;
    m.addHeliGrid(7, 224, 304, {choice, &alt, locked, &two});
    ms.setPointer(0, 0);
    ms.push(std::move(m));
    ms.update(0, UiInput().tap(224 + 10, 304 + 10)); // cell 0
    CHECK(choice[0] == 0);
    ms.update(0, UiInput().tap(224 + 72 * 2 + 10, 304 + 10)); // cell 2 locked
    CHECK(choice[0] == 0);
    ms.update(0, UiInput().tap(224 + 66, 304 + 10)); // gap between cells
    CHECK(choice[0] == 0);
    ms.update(0, UiInput().key(keys::Enter).key(keys::Right)); // keys do nothing
    CHECK(choice[0] == 0);
    two = true;
    locked[5] = false;
    ms.update(0, UiInput().tap(224 + 72 + 10, 304 + 10)); // cell 1 -> player 1
    CHECK(choice[0] == 1);
    ms.update(0, UiInput().tap(224 + 10, 304 + 72 + 10)); // cell 5 -> player 2
    CHECK(choice[1] == 5);
    CHECK(alt == 0);
    CHECK(widgets::gridCellAt(ms.top()->items[0], 224 + 72 * 4 + 63, 304 + 72 + 63) == 9);
}

TEST_CASE("menu tooltip after one second with ShowHints, cursor last") {
    MenuSystem ms;
    ms.showHints = true;
    Menu m;
    m.addSpinner(1, 400, 160, "Resolution:", {"800 x 600"}, 0).tooltip = "Changes screen resolution.";
    ms.setPointer(0, 0);
    ms.push(std::move(m));
    UiAssets assets; // no textures: only untextured quads appear
    Renderer2D r;
    ms.update(0.1f, UiInput().move(420, 165));
    r.begin(800, 600);
    ms.draw(r, assets);
    const size_t base = r.quads().size();
    ms.update(1.5f, {});
    r.begin(800, 600);
    ms.draw(r, assets);
    CHECK(r.quads().size() > base); // box and outline
    ms.touchMode = true; // no hover on touch: no tooltip
    r.begin(800, 600);
    ms.draw(r, assets);
    CHECK(r.quads().size() == base);
}

TEST_CASE("menu key names") {
    CHECK(keys::name('A') == "A");
    CHECK(keys::name('7') == "7");
    CHECK(keys::name(0x61) == "NumPad1");
    CHECK(keys::name(0x70) == "F1");
    CHECK(keys::name(211) == "Joy5");
    CHECK(keys::name(203) == "joy1");
    CHECK(keys::name(200) == "Mouse 1");
    CHECK(keys::name(17) == "CTRL");
    CHECK(keys::name(243) == "joyUp");
    CHECK(keys::name(239) == "0xef");
}
