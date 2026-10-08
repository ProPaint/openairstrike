// The music test of the 3DS port (M2.4), `mode=musictest` in sdmc:/3ds/openairstrike/run.ini.
// Decodes the game's modules through the real music path (Audio::playMusic, the mixer, the ndsp
// device's feeder on core 1) and logs, every second, the decoder's CPU share (time in the decoder
// per second of audio decoded), its longest decode of one mixer call, and the feeder's counters
// (fills, underruns, longest fill, feeder CPU). The verdict per pass applies the plan's rule:
// a decoder is usable when it takes at most 25 % of core 1 at 32728 Hz stereo.
//
// Modules: the menu's (the first attract level's) and mission 1's; when they are the same file,
// the largest module in music\ as the second (the heaviest case). `music_files=a,b` overrides.
// Each module plays pre-rendered (<dir>/<game>/music/<name>.ogg through tremor, when the file is
// there) and as a module (libopenmpt), `frames` displayed frames each (default 600 = 10 s) at
// the current clock; on a New 3DS all again with the speedup off (268 MHz, no L2: about an
// Old 3DS). Without a DSP
// (no dspfirm.cdc) the Null device: the test renders the same audio on the main thread as fast as
// it can, which still measures the decoder. Writes <dir>/musictest_log.txt (MUSICTEST lines).
#pragma once

#include <string>

namespace as3d {
class DefDatabase;
class Vfs;
} // namespace as3d

namespace musictest {

struct Options {
    int frames = 0;          // frames=N per pass (0: 600)
    int taps = 0;            // music_taps=N: interpolation filter length, 0 = libopenmpt's default (8)
    std::string files;       // music_files=a,b: game paths of the modules to play instead
    std::string game = "as3d"; // the pre-rendered files: <dir>/<game>/music/<name>.ogg
};

void run(as3d::Vfs& vfs, const as3d::DefDatabase& db, const std::string& dir, const Options& o);

} // namespace musictest
