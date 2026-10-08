// The PICA environment map test of the 3DS port (M3.4), `mode=envtest` in
// sdmc:/3ds/openairstrike/run.ini. Loads mission 1 from the SD paks, precaches the level's closure
// (the meshes and skins the mission can draw: the memory numbers are the mission's), steps it
// `frames=N` bot frames (default 120, as `as3d_game --headless --level 1 --bot --frames 120`), then
// draws the frame's model records with the world camera on the top screen, mono, through
// pica::MeshPass (opaque list, then trans + effect), no terrain:
//   a  the player's helicopter and what hangs on it (the p_apache_shield case)
//   b  the environment-mapped records not on the player (the ENV_GLITTER help item)
//   c  every model record: compare with --render-only models
// With `only=f` (run it with frames=1400, as the M3.5 water test) the test instead starts the PICA
// WorldRenderer as the frame composer does (beginLevel, precache) and draws the full frame once:
//   f  every pass: compare with --render-only terrain,models,particles,sprites,shadows,marks,water,lightning
// Each holds `hold` frames (default 60). Files in <dir>:
//   envtest_log.txt   records (env ones listed: entity, mode, skin), memory (the env normal
//                     streams), per test draws / env draws / vertices / bytes / timing
//   envtest_<t>.ppm   the top target (400x240)
#pragma once

#include <string>

namespace envtest {

// Needs gfxInitDefault + C3D_Init. Reads the paks from <dir>/as3d/.
void run(const std::string& dir, int holdFrames, int botFrames, const std::string& only);

} // namespace envtest
