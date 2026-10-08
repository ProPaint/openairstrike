// The PICA shadow test of the 3DS port (M3.1), `mode=shadowtest` in sdmc:/3ds/openairstrike/run.ini.
// Loads mission 1 from the SD paks, starts the PICA WorldRenderer as the frame composer does
// (beginLevel, then the precache with every shadow silhouette unless precache=0), steps the
// mission 120 frames with the bot pilot (as `as3d_game --headless --level 1 --bot --frames 120`),
// then freezes the world and draws the world camera on the top screen, mono:
//   t  terrain only (the base of a's darkened pixels): --render-only terrain
//   a  terrain and shadows only (no models, particles, sprites or dynamic lights): compare with
//      --render-only terrain,shadows
//   b  the full frame: compare with --render-only terrain,models,particles,sprites,shadows
// Each phase prepares and draws frames until no shadow is a blob any more (at once after the
// precache; with precache=0 one silhouette per frame is generated and drawn from the next frame:
// the late path), then holds `hold` frames (default 60). shadow_rotations=1 also precaches the 12
// rotations of every projected silhouette of the closure (shadow_pica.h). Files in <dir>:
//   shadowtest_log.txt      the precache numbers (silhouettes, bytes, time, sizes), the
//                           per-frame shadow counters (drawn, blobs, late, vertices, draws,
//                           arena), the prepare cost with and without shadows
//   shadowtest_<p>.ppm      the top target once every shadow is a silhouette (400x240)
//   shadowtest_<p>_late0.ppm, _late1.ppm  (precache=0 only) the first two frames: all blobs but
//                           one generated; then the first silhouette drawn
#pragma once

#include <string>

namespace shadowtest {

// Needs gfxInitDefault + C3D_Init. Reads the paks from <dir>/as3d/.
void run(const std::string& dir, int holdFrames, bool precache, bool allRotations);

} // namespace shadowtest
