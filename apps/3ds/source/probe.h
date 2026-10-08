// The M1.0 GPU probe of the 3DS port (docs/3ds-port.md): draws mission 1's terrain and
// helicopter in eight variants of texture memory/format, render targets and blending and logs
// what the PICA200 spends on each. Selected by `mode=probe` in sdmc:/3ds/openairstrike/run.ini.
#pragma once

#include <string>

namespace as3d {
class DefDatabase;
class Vfs;
struct GameProfile;
} // namespace as3d

namespace probe {

struct RunConfig {
    bool probe = false; // mode=probe (default and anything else: the spike)
    bool picatest = false; // mode=picatest: the PICA core test (pica_test.h), no game data needed
    bool terraintest = false; // mode=terraintest: the terrain test (terrain_test.h)
    bool fxtest = false;      // mode=fxtest: the effects test (fx_test.h)
    bool uitest = false;      // mode=uitest: the 2D layer test (ui_test.h)
    int botFrames = 0;  // frames=N: the effects test's bot frames before drawing (0: 300)
    bool meshtest = false; // mode=meshtest: the PICA mesh test (mesh_test.h), mission 1 from the SD paks
    int level = 1;      // level=N: the terrain test's mission
    int holdFrames = 0; // hold=N: keep drawing N frames after each variant (screenshots under Azahar);
                        // the terrain test's frames per phase
    std::string only;   // only=CH: run just these variants (default: all)
};

// Reads run.ini ("key=value" lines, '#' comments); an absent file gives the defaults.
RunConfig readRunConfig(const std::string& path);

// Runs every variant unattended, prints the table on the bottom screen and appends it to
// `logPath`. `vramAtStart` is vramSpaceFree() taken before anything was initialised.
// Returns when the user presses START (or the app is asked to exit).
void run(as3d::Vfs& vfs, const as3d::DefDatabase& db, const as3d::GameProfile& game, const std::string& logPath,
         unsigned vramAtStart, const RunConfig& cfg);

} // namespace probe
