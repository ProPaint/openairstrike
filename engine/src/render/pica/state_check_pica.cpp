// The render-state self-check (state_check_pica.h).
#include "state_check_pica.h"

#include <3ds.h>
#include <citro3d.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "as3d/core.h"
#include "pica_frame.h"
#include "texture_budget.h"
#include "water_renderer_pica.h"


namespace as3d::pica {
namespace {

// A mirror of citro3d 1.7.1's internal C3D_Effect and C3D_Context (source/internal.h of the
// devkitARM image's citro3d-1.7.1-2, the version tools/3ds_build.sh pins). Read only; the layout
// is checked once at start (&fb == C3D_GetFrameBuf()), and the context is not read when it fails.
struct C3dEffectMirror {
    u32 fragOpMode;
    u32 fragOpShadow;
    u32 zScale, zOffset;
    GPU_CULLMODE cullMode;
    bool zBuffer, earlyDepth;
    GPU_EARLYDEPTHFUNC earlyDepthFunc;
    u32 earlyDepthRef;
    u32 alphaTest;
    u32 stencilMode, stencilOp;
    u32 depthTest;
    u32 blendClr;
    u32 alphaBlend;
    GPU_LOGICOP clrLogicOp;
};

struct C3dContextMirror {
    gxCmdQueue_s gxQueue;
    u32* cmdBuf;
    size_t cmdBufSize;
    float cmdBufUsage;
    u32 flags;
    shaderProgram_s* program;
    C3D_AttrInfo attrInfo;
    C3D_BufInfo bufInfo;
    C3dEffectMirror effect;
    C3D_LightEnv* lightEnv;
    u32 texConfig;
    u32 texShadow;
    C3D_Tex* tex[3];
    C3D_TexEnv texEnv[6];
    u32 texEnvBuf, texEnvBufClr;
    u32 fogClr;
    C3D_FogLut* fogLut;
    u16 gasAttn, gasAccMax;
    u32 gasLightXY, gasLightZ, gasLightZColor;
    u32 gasDeltaZ : 24;
    u32 gasFlags : 8;
    C3D_GasLut* gasLut;
    C3D_ProcTex* procTex;
    C3D_ProcTexLut* procTexLut[3];
    C3D_ProcTexColorLut* procTexColorLut;
    C3D_FrameBuf fb;
    u32 viewport[5];
    u32 scissor[3];
};
constexpr u32 kFlagActive = 1u << 0;
constexpr u32 kFlagEffect = 1u << 4;

const C3dContextMirror* ctxMirror() { return static_cast<const C3dContextMirror*>(detail::c3dContextAddress()); }
gxCmdQueue_s* gxQueue() { return static_cast<gxCmdQueue_s*>(detail::c3dContextAddress()); }

struct State {
    int level = 0;
    bool ctxOk = false;
    u64 t0 = 0;
    unsigned frames = 0;         // frameEnd calls since setStateCheck
    unsigned mismatches = 0, gpuMismatches = 0, logged = 0, gpuLogged = 0;
    unsigned lastLogFrame = 0;   // the rate limit after the first 64 lines
    unsigned checks = 0, probes = 0;
    unsigned busyFrames = 0;
    u32 lastStat[4] = {0, 0, 0, 0};
    bool firstRegs[4] = {true, true, true, true};
    int fullFrames = 0;          // after a mark: frames left with full logging and GPU read-back
    int marks = 0;
    unsigned queueMax = 0;       // most GX queue entries seen in a frame (citro3d's queue holds 32)
    float cmdMax = 0.0f;
    // The terrain's depth map of the current target (the water's must equal it).
    const void* terrainTarget = nullptr;
    float terrainScale = 0.0f, terrainOffset = 0.0f;
    bool terrainZ = false;
    float waterScale = 0.0f;
    // Counts by field for the periodic line.
    std::string fieldsSeen;
};

State& st() {
    static State s;
    return s;
}

const char* funcName(int f) {
    static const char* names[] = {"NEVER", "ALWAYS", "EQUAL", "NOTEQUAL", "LESS", "LEQUAL", "GREATER", "GEQUAL"};
    return f >= 0 && f < 8 ? names[f] : "?";
}

bool shouldLog(State& s) {
    if (s.fullFrames > 0) return true;
    if (s.logged < 64) return true;
    if (s.frames - s.lastLogFrame >= 60) return true;
    return false;
}

void noteField(State& s, const char* pass, const char* field) {
    std::string key = std::string(pass) + "." + field;
    if (s.fieldsSeen.find(key) == std::string::npos && s.fieldsSeen.size() < 400)
        s.fieldsSeen += (s.fieldsSeen.empty() ? "" : ",") + key;
}

void mismatch(State& s, const char* pass, const char* layer, const char* field, const std::string& expected,
              const std::string& actual) {
    ++s.mismatches;
    noteField(s, pass, field);
    if (!shouldLog(s)) return;
    ++s.logged;
    s.lastLogFrame = s.frames;
    AS3D_INFO("AS3D_STATE_MISMATCH pass=%s layer=%s field=%s expected=%s actual=%s frame=%u target=%p", pass, layer,
              field, expected.c_str(), actual.c_str(), static_cast<unsigned>(frameSerial()),
              static_cast<const void*>(currentTarget().target));
}

std::string hex(u32 v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08lx", static_cast<unsigned long>(v));
    return b;
}
std::string num(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.6g", v);
    return b;
}
std::string yes(bool v) { return v ? "on" : "off"; }

u32 depthReg(bool on, int func, int mask) {
    return (on ? 1u : 0u) | (static_cast<u32>(func & 7) << 4) | (static_cast<u32>(mask) << 8);
}
u32 blendReg(int src, int dst) {
    return GPU_BLEND_ADD | (GPU_BLEND_ADD << 8) | (static_cast<u32>(src) << 16) | (static_cast<u32>(dst) << 20) |
           (static_cast<u32>(src) << 24) | (static_cast<u32>(dst) << 28);
}

// GPU registers through GSP (PICA internal registers at 0x1EF01000 + 4 * id; GSP's offsets are
// from 0x1EB00000).
bool readRegs(u32 firstId, u32* out, u32 count) {
    return R_SUCCEEDED(GSPGPU_ReadHWRegs(0x401000u + firstId * 4u, out, static_cast<u8>(count * 4u)));
}

} // namespace

void setStateCheck(int level) {
    State& s = st();
    s = State();
    s.level = level < 0 ? 0 : (level > 2 ? 2 : level);
    s.t0 = svcGetSystemTick();
    if (!s.level) return;
    const C3dContextMirror* m = ctxMirror();
    C3D_FrameBuf* fb = C3D_GetFrameBuf(); // marks the frame buffer dirty: re-emitted with the next draw
    s.ctxOk = fb && static_cast<const void*>(&m->fb) == static_cast<const void*>(fb) && (m->flags & kFlagActive) &&
              m->gxQueue.maxEntries == 32;
    AS3D_INFO("AS3D_STATE_CHECK level=%d citro3d_context=%s (mirror of citro3d 1.7.1 internal.h)", s.level,
              s.ctxOk ? "read" : "layout check failed, not read");
}

int stateCheck() { return st().level; }

void expectDepthMap(PassExpect& e, float nearPlane, float offset) {
    e.zBuffer = depthMapping() == FogDepthMap::ZBuffer;
    e.zScale = e.zBuffer ? -1.0f : -1.0f / (nearPlane > 0.0f ? nearPlane : 1.0f);
    e.zOffset = offset;
}

void checkPassState(const PassExpect& e) {
    State& s = st();
    if (!s.level) return;
    ++s.checks;
    const char* p = e.pass;
    const int mask = e.depthTest ? e.writeMask : GPU_WRITE_COLOR;
    const int func = e.depthTest ? e.depthFunc : GPU_ALWAYS;
    // Layer 1: what the renderer issued (shadow copies).
    const detail::C3dShadow& sh = detail::c3dShadow();
    if (!sh.depthValid) mismatch(s, p, "shadow", "depth", "issued", "unset");
    else {
        if (sh.depthOn != e.depthTest) mismatch(s, p, "shadow", "depth_test", yes(e.depthTest), yes(sh.depthOn));
        if (sh.depthFunc != func) mismatch(s, p, "shadow", "depth_func", funcName(func), funcName(sh.depthFunc));
        if (sh.depthMask != mask) mismatch(s, p, "shadow", "write_mask", hex(mask), hex(sh.depthMask));
    }
    if (!sh.mapValid) mismatch(s, p, "shadow", "depthmap", "issued", "unset");
    else {
        if (sh.mapZ != e.zBuffer) mismatch(s, p, "shadow", "depthmap_mode", e.zBuffer ? "z" : "w", sh.mapZ ? "z" : "w");
        if (sh.mapScale != e.zScale) mismatch(s, p, "shadow", "depthmap_scale", num(e.zScale), num(sh.mapScale));
        if (sh.mapOffset != e.zOffset) mismatch(s, p, "shadow", "depthmap_offset", num(e.zOffset), num(sh.mapOffset));
    }
    if (!sh.blendValid) mismatch(s, p, "shadow", "blend", "issued", "unset");
    else if (sh.blend[2] != e.src || sh.blend[3] != e.dst)
        mismatch(s, p, "shadow", "blend", hex(blendReg(e.src, e.dst)), hex(blendReg(sh.blend[2], sh.blend[3])));
    if (!sh.cullValid) mismatch(s, p, "shadow", "cull", "issued", "unset");
    else if (sh.cull != e.cull) mismatch(s, p, "shadow", "cull", num(e.cull), num(sh.cull));
    if (sh.alphaValid && sh.alphaOn != e.alphaTest)
        mismatch(s, p, "shadow", "alpha_test", yes(e.alphaTest), yes(sh.alphaOn));
    if (e.tex0 && boundTexture(0) != e.tex0) mismatch(s, p, "shadow", "texture0", hex(reinterpret_cast<u32>(e.tex0)), hex(reinterpret_cast<u32>(boundTexture(0))));
    if (e.tex1 && boundTexture(1) != e.tex1) mismatch(s, p, "shadow", "texture1", hex(reinterpret_cast<u32>(e.tex1)), hex(reinterpret_cast<u32>(boundTexture(1))));

    // Layer 2: citro3d's context (what its next draw emits).
    const FrameTarget& t = currentTarget();
    if (s.ctxOk) {
        const C3dContextMirror* m = ctxMirror();
        const C3dEffectMirror& ef = m->effect;
        const u32 wantDepth = depthReg(e.depthTest, func, mask);
        if ((ef.depthTest & 0x1F71u) != (wantDepth & 0x1F71u)) {
            if ((ef.depthTest & 1u) != (wantDepth & 1u)) mismatch(s, p, "c3d", "depth_test", yes(e.depthTest), yes(ef.depthTest & 1u));
            if (((ef.depthTest >> 4) & 7u) != ((wantDepth >> 4) & 7u))
                mismatch(s, p, "c3d", "depth_func", funcName(func), funcName((ef.depthTest >> 4) & 7u));
            if (((ef.depthTest >> 8) & 0x1Fu) != ((wantDepth >> 8) & 0x1Fu))
                mismatch(s, p, "c3d", "write_mask", hex(mask), hex((ef.depthTest >> 8) & 0x1Fu));
        }
        if (ef.zBuffer != e.zBuffer) mismatch(s, p, "c3d", "depthmap_mode", e.zBuffer ? "z" : "w", ef.zBuffer ? "z" : "w");
        if (ef.zScale != f32tof24(e.zScale)) mismatch(s, p, "c3d", "depthmap_scale", hex(f32tof24(e.zScale)), hex(ef.zScale));
        if (ef.zOffset != f32tof24(e.zOffset)) mismatch(s, p, "c3d", "depthmap_offset", hex(f32tof24(e.zOffset)), hex(ef.zOffset));
        if (ef.alphaBlend != blendReg(e.src, e.dst)) mismatch(s, p, "c3d", "blend", hex(blendReg(e.src, e.dst)), hex(ef.alphaBlend));
        if ((ef.fragOpMode & 0xFF00u) != 0x0100u) mismatch(s, p, "c3d", "blend_mode", hex(0x0100u), hex(ef.fragOpMode & 0xFF00u));
        if ((ef.cullMode & 3) != (e.cull & 3)) mismatch(s, p, "c3d", "cull", num(e.cull), num(ef.cullMode));
        if (((ef.alphaTest & 1u) != 0) != e.alphaTest) mismatch(s, p, "c3d", "alpha_test", yes(e.alphaTest), yes(ef.alphaTest & 1u));
        if (ef.stencilMode & 1u) mismatch(s, p, "c3d", "stencil", "off", hex(ef.stencilMode));
        if (ef.earlyDepth) mismatch(s, p, "c3d", "early_depth", "off", "on");
        if (e.tex0 && m->tex[0] != e.tex0) mismatch(s, p, "c3d", "texture0", hex(reinterpret_cast<u32>(e.tex0)), hex(reinterpret_cast<u32>(m->tex[0])));
        if (e.tex1 && m->tex[1] != e.tex1) mismatch(s, p, "c3d", "texture1", hex(reinterpret_cast<u32>(e.tex1)), hex(reinterpret_cast<u32>(m->tex[1])));
        if (t.target) {
            const C3D_FrameBuf& want = t.target->frameBuf;
            if (m->fb.colorBuf != want.colorBuf) mismatch(s, p, "c3d", "target_color", hex(reinterpret_cast<u32>(want.colorBuf)), hex(reinterpret_cast<u32>(m->fb.colorBuf)));
            if (m->fb.depthBuf != want.depthBuf) mismatch(s, p, "c3d", "target_depth", hex(reinterpret_cast<u32>(want.depthBuf)), hex(reinterpret_cast<u32>(m->fb.depthBuf)));
            if (m->fb.depthMask != want.depthMask || m->fb.colorMask != want.colorMask)
                mismatch(s, p, "c3d", "target_masks", hex(want.colorMask | (want.depthMask << 4)), hex(m->fb.colorMask | (m->fb.depthMask << 4)));
            if (!want.depthBuf && e.depthTest) mismatch(s, p, "c3d", "target_depth", "a depth buffer", "none");
        }
    }
    // The water's depth map against the terrain's of the same target.
    if (std::strcmp(p, "terrain") == 0) {
        s.terrainTarget = t.target;
        s.terrainScale = e.zScale;
        s.terrainOffset = e.zOffset;
        s.terrainZ = e.zBuffer;
    } else if (std::strncmp(p, "water", 5) == 0) {
        s.waterScale = e.zScale;
        if (s.terrainTarget == t.target && (s.terrainScale != e.zScale || s.terrainZ != e.zBuffer))
            mismatch(s, p, "expect", "depthmap_vs_terrain", num(s.terrainScale), num(e.zScale));
    }
    if (s.fullFrames > 0 && s.ctxOk) {
        const C3dContextMirror* m = ctxMirror();
        AS3D_INFO("AS3D_STATE_FULL pass=%s frame=%u target=%p depth=0x%lx map=%s/0x%06lx/0x%06lx blend=0x%08lx cull=%d "
                  "alpha=0x%lx tex0=%p tex1=%p fb=%p/%p masks=%x/%x flags=0x%08lx queue=%u/%u/%u",
                  p, static_cast<unsigned>(frameSerial()), static_cast<const void*>(t.target),
                  static_cast<unsigned long>(m->effect.depthTest), m->effect.zBuffer ? "z" : "w",
                  static_cast<unsigned long>(m->effect.zScale), static_cast<unsigned long>(m->effect.zOffset),
                  static_cast<unsigned long>(m->effect.alphaBlend), static_cast<int>(m->effect.cullMode),
                  static_cast<unsigned long>(m->effect.alphaTest), static_cast<const void*>(m->tex[0]),
                  static_cast<const void*>(m->tex[1]), m->fb.colorBuf, m->fb.depthBuf, m->fb.colorMask, m->fb.depthMask,
                  static_cast<unsigned long>(m->flags), m->gxQueue.numEntries, m->gxQueue.curEntry, m->gxQueue.lastEntry);
    }
}

void probeGpuState(const char* pass) {
    State& s = st();
    if (!s.level || !s.ctxOk || !(s.level >= 2 || s.fullFrames > 0)) return;
    ++s.probes;
    const C3dContextMirror* m = ctxMirror();
    const bool effectDirty = (m->flags & kFlagEffect) != 0;
    // The state the pass's draws used (nothing changed it after the last draw).
    const C3dEffectMirror ef = m->effect;
    const C3D_FrameBuf fb = m->fb;
    const C3D_Tex* tex0 = m->tex[0];
    // Everything recorded so far to the GPU, then wait for it.
    splitCommandList();
    gxCmdQueueRun(gxQueue());
    gxCmdQueueWait(gxQueue(), -1);
    u32 r40[16] = {}, r6d[1] = {}, r80[6] = {}, r100[30] = {};
    const bool ok = readRegs(0x40, r40, 16) && readRegs(0x6D, r6d, 1) && readRegs(0x80, r80, 6) && readRegs(0x100, r100, 30);
    const int slot = std::strncmp(pass, "water", 5) == 0 ? 1 : 0;
    if (s.firstRegs[slot] || s.fullFrames > 0) {
        s.firstRegs[slot] = false;
        AS3D_INFO("AS3D_GPU_REGS pass=%s frame=%u read=%d dirty=%d cull=0x%lx map_scale=0x%lx map_offset=0x%lx map_enable=0x%lx "
                  "tex_config=0x%lx tex0=0x%lx color_op=0x%lx blend=0x%lx alpha=0x%lx stencil=0x%lx depth_mask=0x%lx "
                  "color_write=0x%lx depth_write=0x%lx depth_loc=0x%lx color_loc=0x%lx",
                  pass, static_cast<unsigned>(frameSerial()), ok ? 1 : 0, effectDirty ? 1 : 0,
                  static_cast<unsigned long>(r40[0]), static_cast<unsigned long>(r40[0xD]),
                  static_cast<unsigned long>(r40[0xE]), static_cast<unsigned long>(r6d[0]),
                  static_cast<unsigned long>(r80[0]), static_cast<unsigned long>(r80[5]),
                  static_cast<unsigned long>(r100[0]), static_cast<unsigned long>(r100[1]),
                  static_cast<unsigned long>(r100[4]), static_cast<unsigned long>(r100[5]),
                  static_cast<unsigned long>(r100[7]), static_cast<unsigned long>(r100[0x13]),
                  static_cast<unsigned long>(r100[0x15]), static_cast<unsigned long>(r100[0x1C]),
                  static_cast<unsigned long>(r100[0x1D]));
    }
    if (!ok) return;
    auto gpuMismatch = [&](const char* field, u32 expected, u32 actual) {
        ++s.gpuMismatches;
        noteField(s, pass, field);
        if (s.gpuLogged >= 64 && s.fullFrames <= 0 && s.frames - s.lastLogFrame < 60) return;
        ++s.gpuLogged;
        s.lastLogFrame = s.frames;
        AS3D_INFO("AS3D_GPU_MISMATCH pass=%s field=%s expected=0x%08lx actual=0x%08lx frame=%u target=%p dirty=%d", pass,
                  field, static_cast<unsigned long>(expected), static_cast<unsigned long>(actual),
                  static_cast<unsigned>(frameSerial()), static_cast<const void*>(currentTarget().target),
                  effectDirty ? 1 : 0);
    };
    if ((r6d[0] & 1u) != (ef.zBuffer ? 1u : 0u)) gpuMismatch("depthmap_mode", ef.zBuffer ? 1u : 0u, r6d[0]);
    if ((r40[0xD] & 0xFFFFFFu) != (ef.zScale & 0xFFFFFFu)) gpuMismatch("depthmap_scale", ef.zScale, r40[0xD]);
    if ((r40[0xE] & 0xFFFFFFu) != (ef.zOffset & 0xFFFFFFu)) gpuMismatch("depthmap_offset", ef.zOffset, r40[0xE]);
    if ((r40[0] & 3u) != (static_cast<u32>(ef.cullMode) & 3u)) gpuMismatch("cull", ef.cullMode, r40[0]);
    if ((r100[7] & 0x1F71u) != (ef.depthTest & 0x1F71u)) gpuMismatch("depth_test", ef.depthTest, r100[7]);
    if (r100[1] != ef.alphaBlend) gpuMismatch("blend", ef.alphaBlend, r100[1]);
    if ((r100[4] & 1u) != (ef.alphaTest & 1u)) gpuMismatch("alpha_test", ef.alphaTest, r100[4]);
    if ((r100[5] & 1u) != (ef.stencilMode & 1u)) gpuMismatch("stencil", ef.stencilMode, r100[5]);
    const u32 wantDepthWrite = fb.depthBuf ? fb.depthMask : 0u;
    if ((r100[0x15] & 0xFu) != (wantDepthWrite & 0xFu)) gpuMismatch("depth_write", wantDepthWrite, r100[0x15]);
    if (fb.depthBuf && (r100[0x1C] & 0x0FFFFFFFu) != (osConvertVirtToPhys(fb.depthBuf) >> 3))
        gpuMismatch("depth_loc", osConvertVirtToPhys(fb.depthBuf) >> 3, r100[0x1C]);
    if (fb.colorBuf && (r100[0x1D] & 0x0FFFFFFFu) != (osConvertVirtToPhys(fb.colorBuf) >> 3))
        gpuMismatch("color_loc", osConvertVirtToPhys(fb.colorBuf) >> 3, r100[0x1D]);
    if (tex0 && tex0->data && (r80[5] & 0x0FFFFFFFu) != (osConvertVirtToPhys(tex0->data) >> 3))
        gpuMismatch("texture0_addr", osConvertVirtToPhys(tex0->data) >> 3, r80[5]);
}

void stateCheckFrameBegin() {
    State& s = st();
    if (!s.level) return;
    // The GPU after the fence: the memory fill engines (PSC0/PSC1 control, bit 0 busy) and the
    // display transfer engine (PPF, bit 0 busy) must be idle once the GX queue drained.
    u32 ext[12] = {};
    u32 ppf[1] = {};
    const bool ok = R_SUCCEEDED(GSPGPU_ReadHWRegs(0x400010u, ext, 0x30)) && R_SUCCEEDED(GSPGPU_ReadHWRegs(0x400C18u, ppf, 4));
    if (!ok) return;
    const u32 psc0 = ext[3], psc1 = ext[7], stat34 = ext[9];
    s.lastStat[0] = psc0;
    s.lastStat[1] = psc1;
    s.lastStat[2] = ppf[0];
    s.lastStat[3] = stat34;
    if ((psc0 & 1u) || (psc1 & 1u) || (ppf[0] & 1u)) {
        ++s.busyFrames;
        if (s.busyFrames <= 20 || s.fullFrames > 0)
            AS3D_INFO("AS3D_GPU_BUSY frame=%u psc0=0x%08lx psc1=0x%08lx ppf=0x%08lx stat34=0x%08lx (after the GX queue drained)",
                      static_cast<unsigned>(frameSerial()), static_cast<unsigned long>(psc0),
                      static_cast<unsigned long>(psc1), static_cast<unsigned long>(ppf[0]),
                      static_cast<unsigned long>(stat34));
    }
}

void stateCheckFrameEnd() {
    State& s = st();
    if (!s.level) return;
    ++s.frames;
    if (s.fullFrames > 0) --s.fullFrames;
    const gxCmdQueue_s& q = *gxQueue();
    if (q.numEntries > s.queueMax) s.queueMax = q.numEntries;
    if (C3D_GetCmdBufUsage() > s.cmdMax) s.cmdMax = C3D_GetCmdBufUsage();
    if (s.frames % 60 != 0) return;
    const WaterPicaStats& w = waterPicaStats();
    const FenceStats& f = fenceStats();
    const TextureInFrameStats& tf = textureInFrameStats();
    const LinearArena& a = frameArena();
    const double t = static_cast<double>(svcGetSystemTick() - s.t0) / SYSCLOCK_ARM11;
    AS3D_INFO("AS3D_STATE frame=%u t=%.1f water=%s planeZ=%.2f quads=%d tiles=%d near_tiles=%d split=%d max_span=%.2f "
              "min_w=%.2f buf=%d/%u/%u fallbacks=%u near_terrain=%.4f near_water=%.4f arena=%d/%uKB fence=%u/%u busy=%u "
              "hazards=%u deferred=%u/%u tex_in_frame=%u/%u cmdbuf=%.3f queue_max=%u checks=%u probes=%u "
              "mismatches=%u gpu_mismatches=%u gpu_busy=%u psc=%lx/%lx ppf=%lx fields=%s",
              static_cast<unsigned>(frameSerial()), t, !w.active ? "off" : (w.grid ? "grid" : "plane"),
              static_cast<double>(w.planeZ), w.grid ? w.chunksVisible : w.quadsDrawn,
              w.grid ? w.trianglesDrawn : w.tilesDrawn, w.nearTiles, w.splitTiles, static_cast<double>(w.maxSpan),
              static_cast<double>(w.minCutW), w.bufHalf, static_cast<unsigned>(w.bufOffset),
              static_cast<unsigned>(w.bufBytes), w.bufFallbacks,
              static_cast<double>(s.terrainScale != 0.0f ? -1.0f / s.terrainScale : 0.0f),
              static_cast<double>(s.waterScale != 0.0f ? -1.0f / s.waterScale : 0.0f), a.currentHalf(),
              static_cast<unsigned>(a.used() / 1024), static_cast<unsigned>(gpuCompletedSerial()), static_cast<unsigned>(frameSerial()),
              f.queueBusyAtBegin, f.halfHazards + w.bufHazards, static_cast<unsigned>(deferredFreeCount()), f.deferredMax,
              tf.creates, tf.destroys, static_cast<double>(s.cmdMax), s.queueMax, s.checks, s.probes, s.mismatches,
              s.gpuMismatches, s.busyFrames, static_cast<unsigned long>(s.lastStat[0]),
              static_cast<unsigned long>(s.lastStat[1]), static_cast<unsigned long>(s.lastStat[2]),
              s.fieldsSeen.empty() ? "none" : s.fieldsSeen.c_str());
    s.cmdMax = 0.0f;
    s.queueMax = 0;
}

int stateCheckMark(unsigned displayedFrame, unsigned step) {
    State& s = st();
    ++s.marks;
    s.fullFrames = 3;
    const WaterPicaStats& w = waterPicaStats();
    AS3D_INFO("AS3D_WASH_MARK n=%d frame=%u step=%u serial=%u t=%.1f mismatches=%u gpu_mismatches=%u water=%s planeZ=%.2f "
              "tiles=%d near_tiles=%d max_span=%.2f tex_in_frame=%u",
              s.marks, displayedFrame, step, static_cast<unsigned>(frameSerial()),
              static_cast<double>(svcGetSystemTick() - s.t0) / SYSCLOCK_ARM11, s.mismatches, s.gpuMismatches,
              !w.active ? "off" : (w.grid ? "grid" : "plane"), static_cast<double>(w.planeZ), w.tilesDrawn, w.nearTiles,
              static_cast<double>(w.maxSpan), textureInFrameStats().creates);
    return s.marks;
}

} // namespace as3d::pica
