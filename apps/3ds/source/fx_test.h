// The effects test of the 3DS port (M1.6), `mode=fxtest` in sdmc:/3ds/openairstrike/run.ini.
// Loads a mission (level=N, default 1), steps it `frames` frames with the bot pilot (frames=N,
// default 300, so explosions and smoke exist), then freezes the world and draws on the top
// screen, mono, from the world's camera, in phases of `hold` frames each (hold=N, default 90):
//   1 particles   the particle renderer alone (compare with as3d_game --headless --level 1 --bot
//                 --frames 300 --size 400x240 --screenshot-every 300 --render-only particles)
//   2 sprites     sprites and health bars alone (--render-only sprites)
//   3 all         particles, then sprites (--render-only particles,sprites)
//   4 stress      1536 synthetic particle quads (three emitters of 512: alpha, additive, filter)
//                 in front of the camera: arena use and C3D_GetDrawingTime at the cap
//   5 hbars       a synthetic health bar pair above the camera target at three fill fractions
//   6 eyes        phase 3 drawn as two stereo eyes in the left and right half of the screen
//                 (build once per frame, drawn per eye: the log shows the arena use of one build)
// Writes <dir>/fxtest_<n>.ppm (400x240, frame 30 of each phase) and <dir>/fxtest_log.txt with the
// counters (quads built and dropped, draw calls, arena bytes, textures, C3D_GetDrawingTime).
// The world is not stepped while drawing, so the images are reproducible.
#pragma once

#include <string>

namespace as3d {
class DefDatabase;
class Vfs;
struct GameProfile;
} // namespace as3d

namespace fxtest {

// Needs gfxInitDefault, consoleInit(GFX_BOTTOM) and C3D_Init.
void run(as3d::Vfs& vfs, const as3d::DefDatabase& db, const as3d::GameProfile& game, const std::string& dir,
         int level, int botFrames, int holdFrames);

} // namespace fxtest
