// The per-pass GPU timing of the PICA backend (3DS only; run.ini gpu_passes=, docs/3ds-port.md
// "Stereo GPU budget (round 4, R4.1)"): a diagnostic for the question "where do the 15.4 ms of a
// stereo frame go on the GPU".
//
// In a timed frame (every N-th displayed frame, never one whose state-check probes split the
// command list) every pass boundary submits the commands recorded since the last boundary and
// waits for the GX queue to drain: the time from the submission to the drain is that pass's GPU
// time (plus a fixed cost of the submission and the completion interrupt, measured each timed frame
// with a one-word list: `sync`, subtracted in the report). The display transfers that C3D_FrameEnd
// queues after the last pass are the next frame's C3D_GetDrawingTime (`tail`). The eye replay is off
// in a timed frame (the right eye is recorded pass by pass; the soak measured the replay's GPU cost
// equal to the recorded eye's). A timed frame takes the CPU's recording time plus the GPU's, so it
// misses its vblank: the pacer excuses nothing for it and a stage with gpu_passes on measures the
// passes, not the frame rate. Each split also flushes the GPU's framebuffer cache (citro3d
// C3Di_SplitFrame), which the untimed frame does once per target: the sum of the passes is a little
// above the frame's own GPU time.
//
// AS3D_GPU_PASS (every report, the averages over the timed frames since the last one, ms):
//     AS3D_GPU_PASS timed=30 every=10 sync=0.071 L: clear=0.41 terrain=2.10 ... R: ... B: clear=0.20 hud=0.31
//     touch=0.90 ... tail=0.85 sum=15.02
#pragma once

#include <string>

#include "pass_profile.h"

namespace as3d::pica {

// The screen the next passes draw on: 0 the left (or only) top eye, 1 the right eye, 2 the bottom.
constexpr int kGpuScreens = 3;

// Segments: the pass_profile.h passes (Clear .. Bottom2d), then these.
enum class GpuExtra : int {
    Wash = 0,  // the wash detector's depth copy behind the eye (ctr_wash.h)
    Other,     // whatever an eye or the bottom screen recorded outside the passes
    Count
};
constexpr int kGpuSegs = static_cast<int>(Pass::Bottom2d) + 1 + static_cast<int>(GpuExtra::Count);

// gpu_passes=N: every N-th displayed frame is timed (0 = off, the default).
void setGpuPassEvery(int frames);
int gpuPassEvery();
// pica::frameBegin (after the state check's frame start): decides whether this frame is timed and
// takes the last timed frame's tail.
void gpuPassFrameBegin();
// True in a timed frame (the eye replay stays off).
bool gpuPassTimedFrame();
// The screen of the passes that follow (the composer, before each target).
void gpuPassScreen(int screen);
// A boundary: the commands recorded since the last one are `p` (or `e`) of the current screen.
// Timed frames only; a no-op otherwise.
void gpuPassMark(Pass p);
void gpuPassMark(GpuExtra e);
// The report line's body (empty when no frame was timed since the last call); resets the sums.
std::string gpuPassReport();

} // namespace as3d::pica
