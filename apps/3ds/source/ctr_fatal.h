// The app's hard exit (docs/3ds-port.md "Packaging and launch"): when the game data is missing
// or a pak does not open, the loops record the reason with setFatal and return; main() then shows
// it on the bottom screen with the SD layout the app expects, after every graphics resource is
// gone, and waits for a key (or the HOME menu's Close) before it returns to the launcher.
#pragma once

#include <string>

namespace ctrapp {

// Records the message (the first one wins) and logs it as an error.
void setFatal(const std::string& what, const std::string& detail = std::string());
bool hasFatal();
// The text shown (for the log and the desktop-free check of its layout): what, detail, the layout.
std::string fatalText();
// gfxInitDefault + the libctru console on the bottom screen, the text, then A / B / START or the
// HOME menu's Close; gfxExit. No graphics context may be alive.
void showFatal();

} // namespace ctrapp
