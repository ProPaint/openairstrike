// Entry point of the 3DS app: reads sdmc:/3ds/openairstrike/run.ini and starts what it asks for
// (run_config.h): the spike or the GPU probe (main.cpp, probe.cpp), or the game loop
// (ctr_loop.cpp) for mode=sim|bot|play, the latter two with the frame composer (ctr_frame.cpp),
// or the front end (ctr_menu_loop.cpp) for mode=menu and mode=play without level=. Without
// run.ini (an installed copy) the front end runs: mode=play, audio and music on.
//
// Where the data is: always kDataDir (sdmc:/3ds/openairstrike), whatever the launch. The Homebrew
// Launcher passes the .3dsx path as argv[0] (sdmc:/3ds/openairstrike/openairstrike.3dsx, or
// wherever the user put the file), 3dslink "3dslink:/<file>", a CIA installed in the HOME menu no
// argument at all; the path is only logged. The working directory is set to kDataDir, so that no
// relative path can end up elsewhere. When the data is missing or a pak does not open, the loops
// record why (ctr_fatal.h) and the bottom screen shows it with the expected SD layout.
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include "ctr_context.h"
#include "ctr_fatal.h"
#include "ctr_frame.h"
#include "ctr_loop.h"
#include "ctr_save.h"
#include "ctr_selector.h"
#include "mesh_renderer_pica.h"
#include "pica_frame.h"
#include "run_config.h"
#include "spike.h"
#include "water_renderer_pica.h"

namespace {
int run(ctrapp::CtrLoopOptions& options) {
    // The front end without game= in run.ini: the game selector first (ctr_selector.h).
    if (options.run.menu() && !options.run.gameGiven) return ctrapp::runCtrFrontEnd(options.run);
    // bot, play and the front end draw through the frame composer; sim draws nothing.
    ctrapp::GameFrame frame(options.run);
    if (options.run.mode != ctrapp::Mode::Sim) options.hook = &frame;
    options.frame = &frame;
    return ctrapp::runCtrLoop(options);
}
} // namespace

int main(int argc, char** argv) {
    ctrsave::installSleepGuard(); // no sleep mode while a save is being written
    mkdir(ctrapp::kDataDir, 0777); // a fresh card: the log, the saves and the caches go there
    chdir(ctrapp::kDataDir);
    ctrapp::CtrLoopOptions options;
    options.run = ctrapp::readRunConfig(std::string(ctrapp::kDataDir) + "/run.ini");
    options.run.launchedFrom = argc > 0 && argv && argv[0] && *argv[0] ? argv[0] : "none";
    // Hardware A/B switches (run.ini depthmap=, water=, water_tile=; the test modes too).
    as3d::pica::setWBufferAsZBuffer(options.run.depthMapZ);
    as3d::pica::setFogForcedOff(!options.run.fog);
    as3d::ctr::setTopColor565(options.run.top565); // run.ini top_format= (before the graphics context)
    as3d::pica::setMeshStateSort(options.run.meshSort);
    as3d::pica::setMeshMerge(options.run.meshMerge);
    as3d::pica::setWaterOptions(options.run.water, options.run.waterTile >= 0.0f ? options.run.waterTile
                                                                                : as3d::pica::kDefaultWaterTile);
    if (!options.run.usesLoop()) return runSpike();
    const int code = run(options);
    // Every graphics resource is released by now: the reason on the screens, then back to the launcher.
    if (ctrapp::hasFatal()) ctrapp::showFatal();
    return code;
}
