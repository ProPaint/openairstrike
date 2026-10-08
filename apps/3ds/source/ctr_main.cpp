// Entry point of the 3DS app: reads sdmc:/3ds/openairstrike/run.ini and starts what it asks for
// (run_config.h): the spike or the GPU probe (main.cpp, probe.cpp), or the game loop
// (ctr_loop.cpp) for mode=sim|bot|play, the latter two with the frame composer (ctr_frame.cpp).
#include <string>

#include "ctr_frame.h"
#include "ctr_loop.h"
#include "run_config.h"
#include "spike.h"

int main() {
    ctrapp::CtrLoopOptions options;
    options.run = ctrapp::readRunConfig(std::string(ctrapp::kDataDir) + "/run.ini");
    if (!options.run.usesLoop()) return runSpike();
    // bot and play draw through the frame composer; sim draws nothing.
    ctrapp::GameFrame frame(options.run);
    if (options.run.mode != ctrapp::Mode::Sim) options.hook = &frame;
    return ctrapp::runCtrLoop(options);
}
