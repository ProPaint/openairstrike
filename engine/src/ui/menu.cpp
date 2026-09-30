// Menu stack, focus, generic key handling and item adders (frontend.md 2.1 to 2.4).
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "as3d/menu.h"

namespace as3d::ui {

namespace {

const FontMetrics& fm() { return FontMetrics::original(); }

bool isNavKey(int code) {
    return code == keys::Up || code == keys::Down || code == keys::Tab || code == keys::StickUp ||
           code == keys::StickDown;
}

} // namespace

// ---------------------------------------------------------------------------
// Key names (frontend.md 3.7)
// ---------------------------------------------------------------------------
std::string keys::name(int code) {
    char buf[32];
    if ((code >= '0' && code <= '9') || (code >= 'A' && code <= 'Z')) return std::string(1, static_cast<char>(code));
    if (code >= 0x60 && code <= 0x69) { std::snprintf(buf, sizeof buf, "NumPad%d", code - 0x60); return buf; }
    if (code >= 0x70 && code <= 0x87) { std::snprintf(buf, sizeof buf, "F%d", code - 0x6F); return buf; }
    if (code >= 207 && code <= 238) { std::snprintf(buf, sizeof buf, "Joy%d", code - 206); return buf; }
    if (code >= 200 && code <= 202) { std::snprintf(buf, sizeof buf, "Mouse %d", code - 199); return buf; }
    if (code >= 203 && code <= 206) { std::snprintf(buf, sizeof buf, "joy%d", code - 202); return buf; }
    switch (code) {
        case 8: return "BACKSPACE";
        case 9: return "TAB";
        case 12: return "CLEAR";
        case 13: return "ENTER";
        case 16: return "SHIFT";
        case 17: return "CTRL";
        case 18: return "ALT";
        case 20: return "CAPS LOCK";
        case 32: return "SPACE";
        case 33: return "PGUP";
        case 34: return "PGDOWN";
        case 35: return "End";
        case 36: return "Home";
        case 37: return "Left";
        case 38: return "Up";
        case 39: return "Right";
        case 40: return "Down";
        case 45: return "INS";
        case 46: return "DEL";
        case 0x90: return "NumLock";
        case 0x91: return "Scroll Lock";
        case 0xBA: return ";";
        case 0xBB: return "=";
        case 0xBC: return ",";
        case 0xBD: return "-";
        case 0xBE: return ".";
        case 0xBF: return "/";
        case 0xC0: return "`";
        case 0xDB: return "[";
        case 0xDC: return "\\";
        case 0xDD: return "]";
        case 0xDE: return "'";
        case 241: return "joyLeft";
        case 242: return "joyRight";
        case 243: return "joyUp";
        case 244: return "joyDown";
        default: break;
    }
    std::snprintf(buf, sizeof buf, "0x%x", static_cast<unsigned>(code));
    return buf;
}

// ---------------------------------------------------------------------------
// Menu adders
// ---------------------------------------------------------------------------
MenuItem* Menu::find(int id) {
    for (MenuItem& it : items)
        if (it.id == id) return &it;
    return nullptr;
}

static MenuItem& pushItem(Menu& m, MenuItem it) {
    if (static_cast<int>(m.items.size()) >= Menu::kMaxItems) {
        AS3D_WARN("menu %s: more than %d items", m.name.c_str(), Menu::kMaxItems);
    }
    m.items.push_back(std::move(it));
    return m.items.back();
}

MenuItem& Menu::addText(int id, float x, float y, std::string label, u32 flags) {
    MenuItem it;
    it.type = ItemType::Text;
    it.id = id;
    it.flags = flags;
    it.x = x;
    it.y = y;
    const float w = 10.0f * static_cast<float>(label.size());
    float left = x;
    if (flags & itemflag::AlignRight) left = x - w;
    else if (flags & itemflag::AlignCenter) left = x - w * 0.5f;
    it.hit = {left, y, w, 16};
    it.label = std::move(label);
    return pushItem(*this, std::move(it));
}

MenuItem& Menu::addButton(int id, float x, float y, float w, float h, std::string texture, std::string highlight,
                          SpecUv uv, u32 flags) {
    MenuItem it;
    it.type = ItemType::Button;
    it.id = id;
    it.flags = flags;
    it.x = x;
    it.y = y;
    it.w = w;
    it.h = h;
    float left = x;
    if (flags & itemflag::AlignRight) left = x - w;
    else if (flags & itemflag::AlignCenter) left = x - w * 0.5f;
    it.hit = {left, y, w, h};
    it.texture = std::move(texture);
    it.highlight = std::move(highlight);
    it.uv = uv;
    return pushItem(*this, std::move(it));
}

MenuItem& Menu::addEdit(int id, float x, float y, float w) {
    MenuItem it;
    it.type = ItemType::Edit;
    it.id = id;
    it.x = x;
    it.y = y;
    it.w = w;
    it.h = 16;
    it.hit = {x, y, w, 16};
    return pushItem(*this, std::move(it));
}

MenuItem& Menu::addList(int id, float x, float y, float w, float h, std::vector<ListEntry> entries) {
    MenuItem it;
    it.type = ItemType::List;
    it.id = id;
    it.x = x;
    it.y = y;
    it.w = w;
    it.h = h;
    it.hit = {x, y, w, h};
    it.entries = std::move(entries);
    widgets::listFixSelection(it);
    return pushItem(*this, std::move(it));
}

MenuItem& Menu::addSpinner(int id, float x, float y, std::string label, std::vector<std::string> values, int index) {
    MenuItem it;
    it.type = ItemType::Spinner;
    it.id = id;
    it.x = x;
    it.y = y;
    float widest = 0;
    for (const std::string& v : values) widest = std::max(widest, measureText(fm(), v));
    const float lw = measureText(fm(), label);
    it.hit = {x - lw - 16, y, lw + 16 + 16 + widest, 16};
    it.label = std::move(label);
    it.values = std::move(values);
    it.index = index;
    return pushItem(*this, std::move(it));
}

MenuItem& Menu::addSlider(int id, float x, float y, std::string label, int min, int max, float value, float step) {
    MenuItem it;
    it.type = ItemType::Slider;
    it.id = id;
    it.x = x;
    it.y = y;
    const float left = x - 10.0f * static_cast<float>(label.size()) - 10.0f;
    it.hit = {left, y, x + 138 - left, 16};
    it.label = std::move(label);
    it.min = min;
    it.max = max;
    it.value = value;
    it.step = step == 0 ? 1 : step;
    return pushItem(*this, std::move(it));
}

MenuItem& Menu::addHeliGrid(int id, float x, float y, HeliGridRef ref) {
    MenuItem it;
    it.type = ItemType::HeliGrid;
    it.id = id;
    it.x = x;
    it.y = y;
    it.hit = {x, y, 352, 136};
    it.grid = ref;
    return pushItem(*this, std::move(it));
}

MenuItem& Menu::addCustom(int id, RectF hit, ItemDrawFn draw, ItemKeyFn onKey) {
    MenuItem it;
    it.type = ItemType::Custom;
    it.id = id;
    it.x = hit.x;
    it.y = hit.y;
    it.hit = hit;
    it.draw = std::move(draw);
    it.onKey = std::move(onKey);
    return pushItem(*this, std::move(it));
}

MenuItem& Menu::addTextButton(int id, RectF hit, std::string label, u32 flags) {
    MenuItem& it = addCustom(id, hit, [](MenuDrawContext& c, MenuItem& self, bool focused) {
        drawTextButton(c, self.hit, self.label, focused, self.disabled(), self.textScale);
    });
    it.label = std::move(label);
    it.flags = flags;
    return it;
}

MenuItem& Menu::addPicture(int id, float x, float y, float w, float h, std::string texture, SpecUv uv, u32 flags) {
    MenuItem it;
    it.type = ItemType::Picture;
    it.id = id;
    it.flags = flags;
    it.x = x;
    it.y = y;
    it.w = w;
    it.h = h;
    it.hit = {x, y, w, h};
    it.texture = std::move(texture);
    it.uv = uv;
    return pushItem(*this, std::move(it));
}

MenuItem& Menu::addSequelButton(int id, float x, float y, std::string label, u32 flags, float minWidth) {
    MenuItem it;
    it.type = ItemType::SequelButton;
    it.id = id;
    it.flags = flags | itemflag::NoHoverSound; // the initialiser sets it (as2/frontend.md 2.3)
    it.x = x;
    it.y = y;
    // The caption width skips the braces and counts the padding spaces (as2/frontend.md 2.5).
    it.captionW = std::max(measureText(fm(), label, 1.0f, true), minWidth);
    const float w = it.captionW + 46.0f;
    float left = x;
    if (flags & itemflag::AlignRight) left = x - w;
    else if (flags & itemflag::AlignCenter) left = x - w * 0.5f;
    it.hit = {left, y, w, 30};
    it.label = std::move(label);
    return pushItem(*this, std::move(it));
}

// ---------------------------------------------------------------------------
// MenuSystem
// ---------------------------------------------------------------------------
void MenuSystem::reset(Menu& m) {
    mt_ = 0;
    m.open = 0; // the sequels: a menu opens again whenever it becomes the top one
    m.focused = -1;
    m.hovered = -1;
    m.hoverTime = 0;
    for (MenuItem& it : m.items) it.flags &= ~itemflag::Focused;
    // Re-run the hover test at the current pointer; with touch there is no pointer between taps.
    if (!touchMode) {
        int idx = hitTest(m, px_, py_);
        m.hovered = idx;
        if (idx >= 0) focus(m, idx);
    }
}

Menu* MenuSystem::push(Menu menu) {
    if (depth() >= kMaxDepth) {
        AS3D_WARN("menu stack full, %s not shown", menu.name.c_str());
        return nullptr;
    }
    stack_.push_back(std::make_unique<Menu>(std::move(menu)));
    reset(*stack_.back());
    return stack_.back().get();
}

void MenuSystem::pop() {
    if (stack_.empty()) return;
    graveyard_.push_back(std::move(stack_.back()));
    stack_.pop_back();
    if (!stack_.empty()) reset(*stack_.back());
    else mt_ = 0;
}

void MenuSystem::clear() {
    while (!stack_.empty()) {
        graveyard_.push_back(std::move(stack_.back()));
        stack_.pop_back();
    }
    mt_ = 0;
}

Menu* MenuSystem::replaceAll(Menu menu) {
    clear();
    return push(std::move(menu));
}

void MenuSystem::collect() { graveyard_.clear(); }

std::vector<std::string> MenuSystem::takeSounds() {
    std::vector<std::string> out;
    out.swap(sounds_);
    return out;
}

void MenuSystem::setPointer(float x, float y) {
    px_ = x;
    py_ = y;
}

int MenuSystem::hitTest(const Menu& m, float x, float y) const {
    for (size_t i = 0; i < m.items.size(); i++) {
        const MenuItem& it = m.items[i];
        if (it.disabled()) continue;
        if (it.hit.contains(x, y)) return static_cast<int>(i);
    }
    return -1;
}

void MenuSystem::sendEvent(Menu& m, int index, int event) {
    if (index < 0 || index >= static_cast<int>(m.items.size())) return;
    MenuItem& it = m.items[static_cast<size_t>(index)];
    if (it.callback) it.callback(it, event);
    else if (m.onItem) m.onItem(m, it, event);
}

void MenuSystem::focus(Menu& m, int index) {
    if (index == m.focused) return;
    const int old = m.focused;
    if (old >= 0 && old < static_cast<int>(m.items.size())) m.items[static_cast<size_t>(old)].flags &= ~itemflag::Focused;
    m.focused = index;
    if (index >= 0) m.items[static_cast<size_t>(index)].flags |= itemflag::Focused;
    if (old >= 0) sendEvent(m, old, kFocusLost);
    if (index >= 0) sendEvent(m, index, kFocusGained);
}

void MenuSystem::focusFirst(Menu& m) {
    for (size_t i = 0; i < m.items.size(); i++)
        if (!m.items[i].disabled()) { focus(m, static_cast<int>(i)); return; }
}

void MenuSystem::focusStep(Menu& m, int dir) {
    const int n = static_cast<int>(m.items.size());
    if (n == 0) return;
    int i = m.focused;
    for (int k = 0; k < n; k++) {
        i = ((i + dir) % n + n) % n;
        if (!m.items[static_cast<size_t>(i)].disabled()) { focus(m, i); return; }
    }
}

void MenuSystem::activate(Menu& m, int index) {
    playSound("sounds\\menu1.wav");
    sendEvent(m, index, kActivate);
}

void MenuSystem::onPointerMove(float x, float y) {
    if (x != px_ || y != py_) pointerMovedThisFrame_ = true;
    px_ = x;
    py_ = y;
    Menu* m = top();
    if (!m) return;
    const int idx = hitTest(*m, x, y);
    if (idx != m->hovered) {
        m->hovered = idx;
        m->hoverTime = 0;
        if (idx >= 0 && !(m->items[static_cast<size_t>(idx)].flags & itemflag::NoHoverSound))
            playSound("sounds\\menu2.wav");
    }
    if (idx >= 0) focus(*m, idx);
}

void MenuSystem::pressKey(int code) { onPress(code); }

void MenuSystem::onPress(int code) {
    if (code == keys::Mouse1) leftHeld_ = true;
    Menu* m = top();
    if (!m) return;
    if (m->onKey && m->onKey(*m, code)) return;
    if (code == keys::Escape || code == keys::Mouse2) {
        if (!m->swallowBack) pop();
        return;
    }
    if (m->focused < 0) {
        if (isNavKey(code)) { focusFirst(*m); return; }
    } else if (!m->items[static_cast<size_t>(m->focused)].disabled()) {
        if (widgetKey(*m, m->focused, code)) return;
    }
    if (code == keys::Tab || code == keys::Down || code == keys::StickDown) { focusStep(*m, 1); return; }
    if (code == keys::Up || code == keys::StickUp) { focusStep(*m, -1); return; }
    if (code == keys::Enter) {
        if (m->focused >= 0) activate(*m, m->focused);
        return;
    }
    if (code == keys::Mouse1 || code == keys::Joy1) {
        const int idx = hitTest(*m, px_, py_);
        if (idx >= 0) activate(*m, idx);
    }
}

void MenuSystem::onRelease(int code) {
    if (code != keys::Mouse1) return;
    leftHeld_ = false;
    if (Menu* m = top())
        for (MenuItem& it : m->items) it.dragging = false;
}

void MenuSystem::onChar(int c) {
    Menu* m = top();
    if (!m) return;
    if (m->focused >= 0) {
        MenuItem& it = m->items[static_cast<size_t>(m->focused)];
        if (it.type == ItemType::Edit && !it.disabled()) {
            if (c < 0x20 || c > 0x7F) return;
            if (measureText(fm(), it.text) >= it.w - 20 || it.text.size() >= 63) return;
            const size_t pos = static_cast<size_t>(std::clamp(it.cursor, 0, static_cast<int>(it.text.size())));
            if (it.overwrite && pos < it.text.size()) it.text[pos] = static_cast<char>(c);
            else it.text.insert(it.text.begin() + static_cast<std::ptrdiff_t>(pos), static_cast<char>(c));
            it.cursor = static_cast<int>(pos) + 1;
            return;
        }
    }
    if (m->onChar) m->onChar(*m, static_cast<char>(c));
}

void MenuSystem::update(float dt, const UiInput& input) {
    collect();
    mt_ += dt;
    clockAcc_ += dt * 1000.0;
    clockMs_ = static_cast<long long>(clockAcc_);
    pointerMovedThisFrame_ = false;
    for (const UiEvent& e : input.events) {
        switch (e.type) {
            case UiEvent::Type::PointerMove: onPointerMove(e.x, e.y); break;
            case UiEvent::Type::Press: onPress(e.code); break;
            case UiEvent::Type::Release: onRelease(e.code); break;
            case UiEvent::Type::Char: onChar(e.code); break;
        }
    }
    Menu* m = top();
    if (!m) return;
    if (!pointerMovedThisFrame_) m->hoverTime += dt;
    // The sequels' menu opening (as2/frontend.md 2.1: 4 x dt twice per frame) and the text
    // buttons' slide (2.5: 4 x dt towards shown or hidden). Nothing reads them in the first
    // game's style.
    m->open = std::min(m->open + 8.0f * dt, 1.0f);
    for (MenuItem& it : m->items)
        if (it.type == ItemType::SequelButton)
            it.slide = std::clamp(it.slide + (it.hidden() ? -4.0f : 4.0f) * dt, 0.0f, 1.0f);
    // Slider drag: the click is re-sent every frame while the button is held and the slider
    // stays hovered (touch: while the finger is down, wherever it is).
    for (size_t i = 0; i < m->items.size(); i++) {
        MenuItem& it = m->items[i];
        if (it.type != ItemType::Slider || !it.dragging) continue;
        if (!leftHeld_) { it.dragging = false; continue; }
        if (!touchMode && m->hovered != static_cast<int>(i)) continue;
        const float before = it.value;
        widgets::sliderClick(it, px_);
        if (it.value != before) sendEvent(*m, static_cast<int>(i), kActivate);
        if (top() != m) return; // the callback changed the stack
    }
    if (m->onUpdate) m->onUpdate(*m, dt, mt_);
}

} // namespace as3d::ui
