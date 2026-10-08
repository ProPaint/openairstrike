// The game selector of the 3DS app (M5.4) and the loop around the front end that it makes.
//
// run.ini without game= (or game=select) and mode=menu (or mode=play without level=):
//   1. detectInstalls: the games under sdmc:/3ds/openairstrike/{as3d,as2,gulf}/ (paks,
//      Settings.xml, texts file); every game with a file missing is logged ("selector: ...
//      missing ...") and shown on its card.
//   2. The selector is shown unless exactly one game is installed (and no other is part-
//      installed) and START is not held at boot: that game then starts directly. The selector is
//      ui::GameSelector (the desktop's, as3d/frontend.h) drawn on the bottom screen through
//      the PICA Renderer2D at the 320x240 mapping (4:3 screen mode); D-pad / A / B and the touch
//      screen as in the menus (CtrInput::uiInputs); the top screen shows the focused game's
//      title and version. Cards of games with missing files cannot be played. The marquees are
//      the logos of the sequels (their own pictures), the first game's banner mesh is its title
//      in text.
//   3. The choice goes to <userDataDir>/launcher.bin (writeLauncherChoice, atomic), which also
//      preselects the card next time.
//   4. runCtrMenuLoop runs the chosen game's front end with the same graphics context and
//      input; its main menu has "Change game" when more than one game is installed, which
//      comes back to step 2.
// With game=KEY in run.ini none of this happens (the old behaviour).
#pragma once

#include "run_config.h"

namespace ctrapp {

// Returns the process exit code.
int runCtrFrontEnd(const RunConfig& run);

} // namespace ctrapp
