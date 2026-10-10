// The name entry from a system keyboard (the 3DS's software keyboard applet, docs/3ds-port.md
// "Packaging and launch"): the host asks whether the name entry is up (all three games: the
// high-score name of Screen::NameEntry, built by screens_scores.cpp and the sequels'
// SequelScreens::nameEntry, both with the edit field id 2), shows its keyboard, puts the result
// into the field with submitName and then sends Enter, which commits the name through the
// screen's own path (insert, save, Top Scores) as a typed name does. Header-only: no SDL, no 3DS.
#pragma once

#include <string>

#include "as3d/frontend.h"
#include "as3d/menu.h"
#include "as3d/ui.h"

namespace as3d_game {

// The edit field of the name entry when it is the top screen, else nullptr.
inline as3d::ui::MenuItem* nameEntryField(as3d::ui::Frontend& fe) {
    if (!fe.menuOpen() || fe.topScreen() != as3d::ui::Screen::NameEntry) return nullptr;
    as3d::ui::Menu* m = fe.menus().top();
    if (!m) return nullptr;
    for (as3d::ui::MenuItem& it : m->items)
        if (it.type == as3d::ui::ItemType::Edit && !it.disabled()) return &it;
    return nullptr;
}

// What the field keeps of `name` when it is typed into it character by character: the rule of
// MenuSystem::onChar (printable ASCII; a character goes in while the text is narrower than the
// field less 20 virtual pixels and shorter than 63 bytes). Other bytes (UTF-8 sequences) are dropped.
inline std::string acceptedName(const std::string& name, float fieldWidth) {
    std::string out;
    for (char ch : name) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c > 0x7F) continue;
        if (as3d::ui::measureText(as3d::ui::FontMetrics::original(), out) >= fieldWidth - 20 || out.size() >= 63) break;
        out.push_back(ch);
    }
    return out;
}

// Puts `name` (as acceptedName keeps it) into the name entry's field, cursor at its end, and gives
// the field the focus, so that the next Enter commits it. False when the name entry is not up.
inline bool submitName(as3d::ui::Frontend& fe, const std::string& name, std::string* kept = nullptr) {
    as3d::ui::MenuItem* field = nameEntryField(fe);
    if (!field) return false;
    field->text = acceptedName(name, field->w);
    field->cursor = static_cast<int>(field->text.size());
    as3d::ui::Menu* m = fe.menus().top();
    for (size_t i = 0; i < m->items.size(); ++i)
        if (&m->items[i] == field) fe.menus().focus(*m, static_cast<int>(i));
    if (kept) *kept = field->text;
    return true;
}

} // namespace as3d_game
