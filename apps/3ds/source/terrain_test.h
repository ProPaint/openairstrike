// The terrain test of the 3DS port (M1.4), `mode=terraintest` in sdmc:/3ds/openairstrike/run.ini.
// Loads a mission (level=N, default 1), steps it 120 frames with the bot pilot, builds the
// engine's TerrainRenderer (engine/src/render/pica/terrain_renderer_pica.cpp) and draws the
// terrain alone on the top screen, mono, from the world's camera, in six phases of `hold`
// frames each (hold=N, default 300), the bottom screen naming the phase:
//   1 plain     the terrain as the world camera sees it (compare with
//               as3d_game --headless --level 1 --bot --frames 120 --size 400x240
//               --screenshot-every 120 --render-only terrain)
//   2 crater    plus a synthetic crater (heights lowered in a 7x7-vertex rectangle around the
//               ground point a bit below the screen centre) applied through
//               TerrainRenderer::update (copy-on-write chunks)
//   3 lights    plus three point lights (red left, green right, blue further) around that point
//   4 grazing   a low camera looking along the map at the first tile overlay cell past row 8,
//               for the overlays' depth bias
//   5 grazing-notiles  the same without the overlay pass (diff with 4: where the tiles are)
//   6 grazing-nobias   the same with a zero overlay depth bias (diff with 4: what the bias does)
// Then it keeps phase 6 until START (or quits after one more period with hold=N). Three 6x6
// corner markers (as pica_test) locate the top screen in an emulator screenshot. Writes <dir>/terraintest_log.txt: chunk, vertex and byte
// counts, texture placement, draw calls and C3D_GetDrawingTime per phase.
#pragma once

#include <string>

namespace as3d {
class DefDatabase;
class Vfs;
struct GameProfile;
} // namespace as3d

namespace terraintest {

// Needs gfxInitDefault, consoleInit(GFX_BOTTOM) and C3D_Init.
void run(as3d::Vfs& vfs, const as3d::DefDatabase& db, const as3d::GameProfile& game, const std::string& dir,
         int level, int holdFrames);

} // namespace terraintest
