// Front-end input by frame for `as3d_game --headless --ui-script FILE` and the flow tests:
// pointer taps, keys and text in the virtual 800x600 screen, plus named screenshots.
//
// One command per line, '#' starts a comment, blank lines are ignored; frames must not
// decrease. `frame` is the headless loop frame (the same clock as --input-script).
//
//     <frame> tap <x> <y>          pointer move, then Mouse1 press and release
//     <frame> move <x> <y>         pointer move
//     <frame> press <key>          key or button press (the menus act on presses)
//     <frame> release <key>
//     <frame> key <key>            press and release
//     <frame> text <characters>    typed characters (the rest of the line)
//     <frame> shot <name>          after this frame, writes <out-dir>/<name>.png
//
// <key> is a code of the original (frontend.md: Windows virtual keys, 200 = mouse 1, 201 =
// mouse 2) or one of the names Escape, Enter, Space, Backspace, Tab, Delete, Up, Down, Left,
// Right, PageUp, PageDown, Home, End, Pause, F1..F12, Mouse1, Mouse2, Mouse3, or a single
// letter or digit.
#pragma once

#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/menu.h"

namespace as3d_game {

class UiScript {
public:
    struct Command {
        as3d::u32 frame = 0;
        as3d::ui::UiEvent event; // unused for shots
        bool shot = false;
        std::string name;        // screenshot name
    };

    bool parse(const std::string& text, std::string* error);
    bool load(const std::string& path, std::string* error);

    // The events of `frame` (appended to `out`) and the screenshot names after it.
    void eventsAt(as3d::u32 frame, as3d::ui::UiInput& out, std::vector<std::string>* shots) const;
    as3d::u32 lastFrame() const { return commands_.empty() ? 0 : commands_.back().frame; }
    const std::vector<Command>& commands() const { return commands_; }

private:
    std::vector<Command> commands_;
};

// Key name or number to the original's key code; -1 if unknown.
int uiKeyCode(const std::string& name);

} // namespace as3d_game
