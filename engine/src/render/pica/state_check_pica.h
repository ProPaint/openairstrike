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

} // namespace as3d::pica
