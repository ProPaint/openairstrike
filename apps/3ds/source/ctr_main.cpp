// Entry point of the 3DS app: reads sdmc:/3ds/openairstrike/run.ini and starts what it asks for
// (run_config.h): the spike or the GPU probe (main.cpp, probe.cpp), or the game loop
// (ctr_loop.cpp) for mode=sim|bot|play, the latter two with the frame composer (ctr_frame.cpp),
// or the front end (ctr_menu_loop.cpp) for mode=menu and mode=play without level=.
#include <string>

#include "ctr_frame.h"
#include "ctr_loop.h"
#include "ctr_save.h"
#include "ctr_selector.h"
#include "run_config.h"
#include "spike.h"

int main() {
    ctrsave::installSleepGuard(); // no sleep mode while a save is being written
    ctrapp::CtrLoopOptions options;
    options.run = ctrapp::readRunConfig(std::string(ctrapp::kDataDir) + "/run.ini");
    if (!options.run.usesLoop()) return runSpike();
    // The front end without game= in run.ini: the game selector first (ctr_selector.h).
    if (options.run.menu() && !options.run.gameGiven) return ctrapp::runCtrFrontEnd(options.run);
    // bot, play and the front end draw through the frame composer; sim draws nothing.
    ctrapp::GameFrame frame(options.run);
    if (options.run.mode != ctrapp::Mode::Sim) options.hook = &frame;
    options.frame = &frame;
    return ctrapp::runCtrLoop(options);
}
