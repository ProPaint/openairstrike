// The PICA mesh test of the 3DS port (M1.5), `mode=meshtest` in sdmc:/3ds/openairstrike/run.ini.
// Loads mission 1 from the SD paks (as the game loop), steps it 120 frames with the bot pilot
// (as `as3d_game --headless --level 1 --bot --frames 120`), then draws on the top screen, mono:
//   a  the player's helicopter and its attachments alone, lit with the level's sun and the
//      frame's dynamic lights through MeshRenderer (begin/submit/end), at a fixed camera;
//   b  every model record of that frame (the GL entity pass, no terrain, sprites or particles)
//      with the world's camera, through pica::MeshPass (opaque list, then trans + effect);
//   c  b with three synthetic point lights (red, green, blue) near the player.
// Each test stays on screen `hold` frames (default 300); then the next. Files in <dir>:
//   meshtest_log.txt          stats per test (records, draws, vertices, bytes, GPU ms), memory
//   meshtest_checks_<t>.txt   expected pixels ("name x y r g b tol"; tol < 0: the pixel must
//                             differ from (r, g, b) by more than -tol), for 3ds_picatest_check.py
//   meshtest_<t>.ppm          the top target read back after the test's first frame (400x240)
// "meshtest: showing <t>" in the log marks the moment a test is on screen (for screenshots).
// With hold=0 the three tests cycle until START; otherwise the app quits after one cycle.
#pragma once

#include <string>

namespace meshtest {

// Needs gfxInitDefault + C3D_Init. Reads the paks from <dir>/as3d/.
void run(const std::string& dir, int holdFrames);

} // namespace meshtest
