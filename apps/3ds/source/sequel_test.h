// The sequels' render test of the 3DS port (M5.1), `mode=sequeltest` in sdmc:/3ds/openairstrike/run.ini
// (`game=as2|gulf|as3d`, `level=N` default 1, `frames=N` default 600, `hold=N`, `precache=0`). Loads the
// mission from the game's SD paks (<dir>/<game>/), starts the PICA WorldRenderer as the frame composer
// does (beginLevel, then the precache), steps `frames` bot frames (the as3d_sim --bot pilot), logs the
// end state with the lines of as3d_sim (compare with `as3d_sim --game G --level N --frames F --bot`),
// then freezes the world and draws the world camera on the top screen, mono:
//   t  terrain only: --render-only terrain
//   w  terrain + water (the sequels' wave grid): --render-only terrain,water
//   f  the full frame: --render-only terrain,models,particles,sprites,shadows,marks,skid,water,lightning
//   then the mission steps 30 more bot frames and
//   w2 terrain + water again (the waves and the scroll moved: compare with --frames F+30)
// Each phase holds `hold` frames (default 60). Files in <dir>:
//   sequeltest_log.txt   the end state, memory (VRAM / linear textures, meshes, terrain, the water grid),
//                        per frame water stats (chunks, animated vertices, arena bytes, time)
//   sequeltest_<game>_<p>.ppm   the top target (400x240)
#pragma once

#include <string>

namespace sequeltest {

// Needs gfxInitDefault + C3D_Init.
void run(const std::string& dir, const std::string& game, int level, int holdFrames, int botFrames, bool precache);

} // namespace sequeltest
