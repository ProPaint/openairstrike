// The render-state self-check of the PICA backend (3DS only; run.ini state_check=, docs/3ds-port.md
// "Frame synchronisation"): the instrument for the water wash seen in play on the console (the
// water covers the terrain and the roads, never the models, a few seconds at a time).
//
// state_check=1: right before the terrain's base draws, its tile overlays and the water's draw,
// the state the pass expects (depth test, function and write mask, depth map scale and offset,
// blend, cull, alpha test, the bound textures, the current target) is compared with two layers:
// the renderer's shadow copies (render_state_pica.cpp, `layer=shadow`) and citro3d's own context
// (the values its next draw emits, `layer=c3d`; the context is read through a mirror of citro3d
// 1.7.1's internal.h, checked against C3D_GetFrameBuf at start). A difference logs
//     AS3D_STATE_MISMATCH pass=water layer=c3d field=depth_func expected=GEQUAL actual=ALWAYS frame=N eye=E
// (the first 64, then at most one a second; every one is counted). The water's depth map is also
// compared with the terrain's of the same eye (`field=depthmap_vs_terrain`). At every frameBegin
// the GPU's idle status is read back (memory fill and display transfer engines; AS3D_GPU_BUSY when
// one is still busy after the GX queue drained). Every 60 frames one AS3D_STATE line: the water
// (plane z, quads, tiles, tiles cut at the near plane, the largest tile span, its vertex buffer's
// half and offset), the near planes of the terrain and the water, the arena, the GPU fence, the
// textures created inside frames, the command buffer and GX queue use, the mismatch counts.
//
// state_check=2: also right after the terrain's base draws and the water's draw, the command list
// recorded so far is submitted and waited for, and the GPU's registers (GSPGPU_ReadHWRegs) are
// compared with citro3d's context: AS3D_GPU_MISMATCH pass= field= expected= actual= frame=. The
// first read-back of each pass logs every register (AS3D_GPU_REGS). Level 2 stalls the CPU twice
// per eye: the frame rate drops, the GPU's work does not change.
//
// The L button with state_check on (stateCheckMark): AS3D_WASH_MARK, the screens are dumped
// (`mark<N>_*.ppm`, ctr_frame.cpp), and the next 3 frames log every check in full
// (AS3D_STATE_FULL) and read the GPU back as level 2 does.
#pragma once

#include <citro3d.h>

#include <cstdint>

namespace as3d::pica {

void setStateCheck(int level);
int stateCheck();

struct PassExpect {
    const char* pass = "";
    bool depthTest = true;
    GPU_TESTFUNC depthFunc = GPU_GEQUAL;
    GPU_WRITEMASK writeMask = GPU_WRITE_ALL;
    bool zBuffer = false; // the depth map: Z-buffer (true) or W-buffer
    float zScale = 0.0f, zOffset = 0.0f;
    GPU_BLENDFACTOR src = GPU_ONE, dst = GPU_ZERO;
    GPU_CULLMODE cull = GPU_CULL_NONE;
    bool alphaTest = false;
    const C3D_Tex* tex0 = nullptr; // null: not checked
    const C3D_Tex* tex1 = nullptr;
};
// The depth map setDepthMapping(depthMapping(), nearPlane) issues, plus `offset`.
void expectDepthMap(PassExpect& e, float nearPlane, float offset);
// Before the pass's draw, its state applied (no-op when off).
void checkPassState(const PassExpect& e);
// Right after the pass's draw (state_check=2 or a mark; no-op otherwise).
void probeGpuState(const char* pass);
// pica_frame.cpp: after the fence of frameBegin, and at frameEnd.
void stateCheckFrameBegin();
void stateCheckFrameEnd();
// The L button: logs AS3D_WASH_MARK and checks the next frames in full. Returns the mark's number.
int stateCheckMark(unsigned displayedFrame, unsigned step);

// run.ini probe_every=N (mode=soak's default 30; docs/3ds-port.md "Soak mode and the log push"):
// every N frames the terrain pass's GPU read-back of state_check=2 runs (probeGpuState("terrain"):
// one GPU stall per eye), so the water wash's signature (AS3D_GPU_MISMATCH pass=terrain) is caught
// without anyone pressing L. A GPU mismatch found by any probe asks for an automatic mark (the
// screens dumped as for L, at most kMaxAutoMarks per setStateCheck): stateCheckTakeAutoMark.
// 0 = off. Call after setStateCheck (which resets it).
void setProbeEvery(int frames);
constexpr int kMaxAutoMarks = 3;
// True once when a probe found a GPU mismatch and an automatic mark is due (the caller dumps the
// screens and calls stateCheckMark, as for L).
bool stateCheckTakeAutoMark();
// run.ini probe_inject=N: the N-th probe of each level start reports a made-up terrain mismatch
// (field=inject), the self-test of the soak's mark, verdict and push path on a GPU that never
// loses its state (Azahar). 0 = never (default).
void setProbeInject(int nthProbe);
// Probes run since the start (the pacer excuses a frame that stalled for one).
unsigned stateCheckProbes();

// The counts since setStateCheck (the soak's per-stage verdict).
struct StateCheckCounts {
    unsigned checks = 0, probes = 0;
    unsigned mismatches = 0;           // AS3D_STATE_MISMATCH (shadow / c3d layers)
    unsigned gpuMismatches = 0;        // AS3D_GPU_MISMATCH, all passes
    unsigned terrainGpuMismatches = 0; // of those, pass=terrain
    unsigned primeGpuMismatches = 0;   // pass=terrain_prime (the terrain's priming draw: its first state block)
    // The wash detector (apps/3ds/source/ctr_wash.cpp, run.ini wash_check): eye-frames judged, the
    // samples judged, washed frames and episodes (runs of consecutive washed frames).
    unsigned washEvals = 0, washJudged = 0, washFrames = 0, washEpisodes = 0, washLongest = 0;
    unsigned washMaxSamples = 0, washFirstFrame = 0, washMarks = 0;
    double washMs = 0.0, washMaxMs = 0.0; // the CPU's judging time: total and longest
    unsigned busyFrames = 0;           // AS3D_GPU_BUSY
    unsigned marks = 0, autoMarks = 0;
    double probeMs = 0.0, probeMaxMs = 0.0; // the probes' stalls (submit, wait, read-back): total and longest
    bool contextRead = false;          // citro3d's context mirror passed its layout check (probes need it)
};
StateCheckCounts stateCheckCounts();

// The wash detector (run.ini wash_check, docs/3ds-port.md "Wash detector"): one eye-frame judged
// in `ms` of CPU over `judged` samples; an episode of `frames` washed frames from `startFrame`
// ended, `maxWashed` samples at most.
void stateCheckNoteWashEval(double ms, int judged);
void stateCheckNoteWashEpisode(unsigned startFrame, unsigned frames, int maxWashed);
// The register read-back of every probed pass during the next `frames` frames (the current one
// included when called before its passes), as after a mark.
void stateCheckArmProbe(int frames);
bool stateCheckWashMarkDue();
// The periodic probe alternates between the terrain's priming draw (pass=terrain_prime, its first
// state block) and the end of its base draws (pass=terrain).

} // namespace as3d::pica
