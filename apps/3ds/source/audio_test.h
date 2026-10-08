// The sound test of the 3DS port (M2.3), `mode=audiotest` in sdmc:/3ds/openairstrike/run.ini.
// Opens as3d::Audio on the platform device (the ndsp device; the Null device when ndspInit fails,
// e.g. without sdmc:/3ds/dspfirm.cdc), then, one frame per vblank:
//   frames   0..59  a generated 440 Hz tone (a WAV built in memory, mounted as audiotest\tone.wav)
//   frame   75      the first looping sound of mission 1's precache set (the rotor, if any), until
//                   the end
//   frames  90..    the second sound (a gun, if any) every 12 frames for 2 s
//   frame  240      the third sound, once
// and ends after `frames` frames (default 360). Every second, and at the end, it logs the device
// counters: fills (expected 32728 / 1024 = 32 per second), underruns, the peak sample of the
// buffers filled that second, the longest fill, the feeder's CPU share, active voices. Before the
// device opens it checks std::thread and std::mutex under libctru (two threads contending on one
// mutex). Writes <dir>/audiotest_log.txt; the bottom screen shows the same lines.
#pragma once

#include <string>

namespace as3d {
class DefDatabase;
class Vfs;
struct GameProfile;
} // namespace as3d

namespace audiotest {

// Needs gfxInitDefault and the bottom-screen console. `vfs` is mutable: the tone is mounted on it.
void run(as3d::Vfs& vfs, const as3d::DefDatabase& db, const as3d::GameProfile& game, const std::string& dir,
         int frames);

} // namespace audiotest
