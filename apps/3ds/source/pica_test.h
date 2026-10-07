// The PICA core test of the 3DS port (M1.3), `mode=picatest` in sdmc:/3ds/openairstrike/run.ini.
// Draws known patterns on the top screen through engine/src/render/pica (texture formats,
// blend modes, depth, cull, viewport, scissor, the terrain and mesh shaders, the fog table,
// the stereo projection) and writes:
//   <dir>/picatest_log.txt     texture byte accounting, eye-projection matrix comparison
//   <dir>/picatest_checks.txt  "name x y r g b tol" per line: the expected colour at a pixel
//                              of the upright 400x240 top screen (origin top-left)
// tools/3ds_picatest_check.py compares an Azahar screenshot with the checks file.
// Owns its own citro3d target (the real targets belong to platform/ctr, M1.2).
#pragma once

#include <string>

namespace picatest {

// Runs until START or `holdFrames` frames (0: until START). Needs gfxInitDefault + C3D_Init.
void run(const std::string& dir, int holdFrames);

} // namespace picatest
