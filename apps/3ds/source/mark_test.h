// The PICA ground-mark test of the 3DS port (M3.2), `mode=markstest` in sdmc:/3ds/openairstrike/run.ini.
// Loads mission 1 from the SD paks, starts the PICA WorldRenderer as the frame composer does
// (beginLevel, then the precache), steps the mission `frames=N` frames with the bot pilot (default
// 1400: a scorch mark lies near the start, as the desktop sprite pass showed in M1.6), then freezes
// the world and draws the world camera on the top screen, mono:
//   t  terrain only (the base of m's darkened pixels): --render-only terrain
//   m  terrain, ground marks and skid trails: compare with --render-only terrain,marks,skid
//   f  the full frame: compare with --render-only terrain,models,particles,sprites,shadows,marks,skid
// Each phase holds `hold` frames (default 60). Files in <dir>:
//   markstest_log.txt   the precache numbers, per frame marks / skid / vertices / draws / arena /
//                       texture bytes / build time, the prepare cost with and without marks
//   markstest_<p>.ppm   the top target (400x240)
#pragma once

#include <string>

namespace markstest {

// Needs gfxInitDefault + C3D_Init. Reads the paks from <dir>/as3d/.
void run(const std::string& dir, int holdFrames, int botFrames, bool precache);

} // namespace markstest
