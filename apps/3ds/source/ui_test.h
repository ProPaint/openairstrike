// The 2D layer test of the 3DS port (M1.7), `mode=uitest` in sdmc:/3ds/openairstrike/run.ini.
// Loads a mission (level=N, default 1), steps it `frames` bot frames (default 120), then freezes
// the world and draws through Renderer2D's PICA flush, in phases of `hold` frames (default 30):
//   1 font    BOTTOM 320x240: a text block at the HUD scales, a score with the number routine, the
//             ASCII set 0x20..0x7F at scale 1.0 (12x6 px glyphs at the bottom screen's 0.4
//             mapping) in the additive font, a row in the alpha font; TOP 400x240: the same
//             block, once, to show both targets work
//   2 hud     BOTTOM: the mission's HUD (HudTarget::Bottom320x240, ui::drawHud) and the touch
//             controls band (ui::drawTouchControls), as `as3d_game --dual-screen` draws them;
//             compare with the bottom half of its screenshot
//   3 quads   BOTTOM: blend modes, five two-texture combine modes, a scissored fill and a disc,
//             with expected pixels in uitest_checks.txt (tools/3ds_picatest_check.py --ppm)
// Writes <dir>/uitest_<phase>.ppm (bottom, 320x240), <dir>/uitest_top_1.ppm (400x240),
// <dir>/uitest_checks.txt and <dir>/uitest_log.txt (draw stats, arena bytes, texture memory,
// C3D_GetDrawingTime). The console on the bottom screen is replaced by the test's picture.
#pragma once

#include <string>

namespace as3d {
class DefDatabase;
class Vfs;
struct GameProfile;
} // namespace as3d

namespace uitest {

// Needs gfxInitDefault and C3D_Init.
void run(as3d::Vfs& vfs, const as3d::DefDatabase& db, const as3d::GameProfile& game, const std::string& dir, int level,
         int botFrames, int holdFrames);

} // namespace uitest
