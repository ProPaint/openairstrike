// run.ini mode=soak (docs/3ds-port.md "Soak mode and the log push (round 2, P7)"): the console
// tests itself in one launch. A sequence of bot stages (soak_report.h SoakStage; default: every
// installed game, missions 1 and 2, each in mono and in stereo with the slider forced to 0.5), each
// one runCtrLoop in mode=bot on the same screens (a loading screen between them) with the weapon
// tour, perf=1, state_check>=1 and the periodic GPU probe (probe_every, default 30), soak_frames
// steps each (default 1800). Per stage the log gets AS3D_SOAK_STAGE (everything the PC's end-state
// comparison needs: game, level, frames, weapons), the loop's own lines (the config line, AS3D_PACE,
// PERF_SUMMARY, AS3D_PASS_MS, AS3D_LAZY_LOADS, the end state "level N: F frames, map_pos ...") and
// AS3D_SOAK_RESULT. At the end soak.txt (the verdict, soak_report.h soakVerdict); then the app
// returns to the launcher by itself.
//
// sdmc:/3ds/openairstrike/soak.running holds the stage being run; a launch that finds it (the last
// one died inside that stage) reports it: takeSoakCrashMarker in main().
#pragma once

#include <string>

#include "run_config.h"

namespace ctrapp {

// Runs every stage; returns 0, or 1 when the screens cannot be created. `previousCrash`: the
// marker's text left by the last launch ("" when it ended cleanly).
int runSoak(const RunConfig& base, const std::string& previousCrash);

// The soak.running marker left by the previous launch: its text (the stage), and the marker is
// removed; "" when there was none.
std::string takeSoakCrashMarker();

} // namespace ctrapp
