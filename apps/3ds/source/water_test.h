// The PICA water and lightning test of the 3DS port (M3.5, M3.3), `mode=watertest` in
// sdmc:/3ds/openairstrike/run.ini. Loads mission 1 from the SD paks, starts the PICA WorldRenderer as the
// frame composer does (beginLevel, then the precache), steps the mission `frames=N` frames with the bot
// pilot (default 1400), then freezes the world and draws the world camera on the top screen, mono:
//   t  terrain only: --render-only terrain
//   w  terrain + water: compare with --render-only terrain,water
//   l  terrain + a synthetic lightning bolt set (no desktop reference: mission 1 has no bolt with the bot)
//   f  the full frame: compare with --render-only terrain,models,particles,sprites,shadows,marks,water,lightning
//   then the mission steps 30 more bot frames and
//   w2 terrain + water again (the scroll moved: compare with --frames N+30)
// Each phase holds `hold` frames (default 60). Files in <dir>:
//   watertest_log.txt   the precache numbers, per frame water / lightning stats and the scroll
//   watertest_<p>.ppm   the top target (400x240)
#pragma once

#include <string>

namespace watertest {

// Needs gfxInitDefault + C3D_Init. Reads the paks from <dir>/as3d/.
void run(const std::string& dir, int holdFrames, int botFrames, bool precache);

} // namespace watertest
