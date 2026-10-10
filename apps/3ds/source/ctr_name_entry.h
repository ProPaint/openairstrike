// The name entry through the system's software keyboard (libctru swkbd, docs/3ds-port.md
// "Packaging and launch"). All three games ask the pilot's name on Screen::NameEntry (the high
// score); when it opens, the front end loop shows the keyboard applet (one line, the profile's 31
// characters at most, a filter that refuses what the score table's field cannot hold), puts the
// result into the field (apps/game/name_entry.h submitName) and sends Enter: the screen's own
// commit. Cancel or an applet failure leaves the screen's touch keyboard (taps, the D-pad over
// the sequels' keys, A on the field commits); Y opens the applet again.
#pragma once

#include <string>

namespace ctrapp {

enum class NameKeyboard { Ok, Cancel, Failed };

// Shows the applet with `current` as its text; `fieldWidth` is the edit field's width (virtual
// pixels, MenuItem::w). On Ok, `*name` is what the player confirmed. Must be called between frames
// (no C3D frame open). `*detail`: the applet's result code for the log.
NameKeyboard askName(const std::string& current, float fieldWidth, std::string* name, int* detail);

} // namespace ctrapp
