// The render-state self-check (state_check_pica.h).
#include <cstddef>
#include "state_check_pica.h"

#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "as3d/core.h"
#include "pass_profile.h"
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

// citro3d 1.7.1's C3D_TexBind(unit, NULL) reads the texture's type for units 1 and 2 before it
// accepts the pointer (texture.c: "if (unitId > 0 && C3D_TexGetType(tex) == GPU_TEX_CUBE_MAP)"), so a
// null bind of those units is a data abort on the console (the fifth console soak, 2026-10-11:
// terrain_far_tev's unbind of unit 1 at the first frame; Azahar reads 0 from an unmapped address
// and never faulted). The disassembly of C3D_TexBind: the slot is written at context + (70 + unit) * 4
// and the flag is bit 23 + unit of the flags word at context + 32; C3Di_UpdateContext then disables
// the unit whose slot is null. The same two writes through the mirror.
static_assert(offsetof(C3dContextMirror, flags) == 32, "citro3d context: flags moved");
static_assert(offsetof(C3dContextMirror, tex) == 280, "citro3d context: tex[] moved");
void setTextureSlotNullImpl(unsigned unit) {
    if (unit > 2) return;
    auto* m = static_cast<C3dContextMirror*>(detail::c3dContextAddress());
    if (!m || !(m->flags & kFlagActive)) return;
    m->tex[unit] = nullptr;
    m->flags |= 1u << (23 + unit);
}

// The mirror's layout check (as setStateCheck's): the frame buffer where citro3d's accessor says,
// the context active, the GX queue of 32 entries. C3D_GetFrameBuf marks the frame buffer dirty.
bool mirrorLayoutOk() {
    static int ok = -1;
    if (ok < 0) {
        const C3dContextMirror* m = ctxMirror();
        C3D_FrameBuf* fb = C3D_GetFrameBuf();
        ok = fb && static_cast<const void*>(&m->fb) == static_cast<const void*>(fb) && (m->flags & kFlagActive) &&
                     m->gxQueue.maxEntries == 32
                 ? 1
                 : 0;
    }
    return ok == 1;
}
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
    int probeEvery = 0;          // probe_every=N: the terrain's GPU read-back every N frames
    bool probeNow = false;       // this frame is one of them
    bool probePrime = false;     // ... and its probe is the priming draw's (pass=terrain_prime)
    unsigned primeGpuMismatches = 0;
    unsigned washEvals = 0, washJudged = 0, washFrames = 0, washEpisodes = 0, washLongest = 0;
    unsigned washMaxSamples = 0, washFirstFrame = 0;
    u64 washTicks = 0, washMaxTicks = 0;
    unsigned terrainGpuMismatches = 0;
    int autoMarks = 0;           // automatic marks asked for (kMaxAutoMarks of each kind at most)
    unsigned washMarks = 0;      // of those, the wash detector's
    unsigned primeMarks = 0;     // ... the priming draw's mismatches (R3.3)
    unsigned probeMarks = 0;     // ... the other probes' mismatches
    unsigned probeInject = 0;    // probe_inject=N: the N-th probe reports a made-up mismatch
    bool autoMarkPending = false;
    u64 probeTicks = 0, probeMaxTicks = 0; // the probes' stalls
    unsigned queueMax = 0;       // most GX queue entries seen in a frame (citro3d's queue holds 32)
    float cmdMax = 0.0f;
    // The terrain's depth map of the current target (the water's must equal it).
    const void* terrainTarget = nullptr;
    float terrainScale = 0.0f, terrainOffset = 0.0f;
    bool terrainZ = false;
    float waterScale = 0.0f;
    // Counts by field for the periodic line.
    std::string fieldsSeen;
    // R3.3 evidence (state_check_pica.h "Evidence at a GPU mismatch").
    u32* passStartBuf = nullptr;  // the command buffer and word offset when the terrain pass started
    u32 passStartOffset = 0;
    uint32_t passStartSerial = 0;
    unsigned mismatchProbes = 0, rereadChanged = 0, canaryOk = 0, canaryStale = 0, details = 0;
    // Over the 60 frames of each AS3D_STATE line: at the prime probes, the GX queue entries queued
    // before the probe's list and the command words recorded in the frame before it (the context
    // of a burst of prime mismatches: what ran before the terrain in that frame).
    u32 primeQueueMin = ~0u, primeQueueMax = 0, primeWordsMin = ~0u, primeWordsMax = 0;
    uint32_t traceSerial[4] = {0, 0, 0, 0}; // frames whose wash judgement is logged in full (S and S+1)
    int traceAt = 0;
};

State& st() {
    static State s;
    return s;
}
unsigned g_probesTotal = 0; // every probe since the start (not reset by setStateCheck)
bool g_forcedThisPass = false; // detail::forceEffectState ran since the terrain pass started

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

// The canary (state_check_pica.h): GPUREG_LIGHT0_SPECULAR0 (0x140), three 8-bit fields at bits
// 0-7, 10-17 and 20-27; lighting is never enabled (no C3D_LightEnv), so the register has no effect.
constexpr u32 kCanaryReg = 0x140;
constexpr u32 kCanaryMask = 0x0FF3FCFFu;
u32 canaryValue(unsigned n) {
    return (n & 0xFFu) | (((n >> 8) & 0xFFu) << 10) | ((((n >> 16) & 0x7Fu) | 0x80u) << 20);
}

// The last writes to the registers of interest in the command words [from, to) of `buf` (the
// libctru layout: parameter 0, header (id 0-15, byte mask 16-19, extra parameters 20-27,
// consecutive 31), the extra parameters, a pad word when their count is odd).
struct ListScan {
    bool ok = false;
    u32 words = 0, commands = 0;
    int at107 = -1, at101 = -1, atCanary = -1, finalizes = 0;
    u32 v107 = 0, v101 = 0, m107 = 0, m101 = 0, vCanary = 0;
};
ListScan scanList(const u32* buf, u32 from, u32 to) {
    ListScan r;
    if (!buf || from > to) return r;
    r.ok = true;
    r.words = to - from;
    u32 i = from;
    while (i + 1 < to) {
        const u32 hdr = buf[i + 1];
        const u32 id = hdr & 0xFFFFu, mask = (hdr >> 16) & 0xFu, n = (hdr >> 20) & 0xFFu;
        const bool consec = (hdr >> 31) != 0;
        if (i + 2 + n > to) {
            r.ok = false; // a command cut by the range: the scan's start was not a command boundary
            break;
        }
        for (u32 k = 0; k <= n; ++k) {
            const u32 reg = consec ? id + k : id;
            const u32 v = k == 0 ? buf[i] : buf[i + 1 + k];
            const int at = static_cast<int>(i - from);
            if (reg == 0x107) {
                r.at107 = at;
                r.v107 = v;
                r.m107 = mask;
            } else if (reg == 0x101) {
                r.at101 = at;
                r.v101 = v;
                r.m101 = mask;
            } else if (reg == kCanaryReg) {
                r.atCanary = at;
                r.vCanary = v;
            } else if (reg == 0x010) {
                ++r.finalizes;
            }
        }
        ++r.commands;
        i += 2 + n + (n & 1u);
    }
    return r;
}

struct QueueSnap {
    u32 num = 0, cur = 0, last = 0;
};

} // namespace

void c3dSetTextureSlotNull(unsigned unit) { setTextureSlotNullImpl(unit); }

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
    const bool periodic = s.probeNow && std::strcmp(pass, s.probePrime ? "terrain_prime" : "terrain") == 0;
    if (!s.level || !s.ctxOk || !(s.level >= 2 || s.fullFrames > 0 || periodic)) return;
    ++g_probesTotal;
    ++s.probes;
    const C3dContextMirror* m = ctxMirror();
    const bool effectDirty = (m->flags & kFlagEffect) != 0;
    // The state the pass's draws used (nothing changed it after the last draw).
    const C3dEffectMirror ef = m->effect;
    const C3D_FrameBuf fb = m->fb;
    const C3D_Tex* tex0 = m->tex[0];
    // R3.3: the canary as the list's last word before the split; where the list stands; the words
    // the terrain pass recorded so far.
    const u32 canary = canaryValue(g_probesTotal);
    GPUCMD_AddWrite(kCanaryReg, canary);
    u32* cmd = nullptr;
    u32 cmdSize = 0, cmdOffset = 0;
    GPUCMD_GetBuffer(&cmd, &cmdSize, &cmdOffset);
    const bool samePass = cmd && s.passStartBuf == cmd && s.passStartSerial == frameSerial();
    const ListScan scan = scanList(cmd, samePass ? s.passStartOffset : cmdOffset, cmdOffset);
    const u32 cmdBase = cmd && m->cmdBuf ? static_cast<u32>(cmd - m->cmdBuf) : 0u;
    const QueueSnap q0{gxQueue()->numEntries, gxQueue()->curEntry, gxQueue()->lastEntry};
    if (std::strcmp(pass, "terrain_prime") == 0) {
        s.primeQueueMin = std::min(s.primeQueueMin, q0.num);
        s.primeQueueMax = std::max(s.primeQueueMax, q0.num);
        s.primeWordsMin = std::min(s.primeWordsMin, cmdBase + cmdOffset);
        s.primeWordsMax = std::max(s.primeWordsMax, cmdBase + cmdOffset);
    }
    // Everything recorded so far to the GPU, then wait for it.
    const u64 stall0 = svcGetSystemTick();
    splitCommandList();
    gxCmdQueueRun(gxQueue());
    gxCmdQueueWait(gxQueue(), -1);
    const QueueSnap q1{gxQueue()->numEntries, gxQueue()->curEntry, gxQueue()->lastEntry};
    u32 r40[16] = {}, r6d[1] = {}, r80[6] = {}, r100[30] = {}, rCanary[1] = {}, stat[1] = {};
    const bool ok = readRegs(0x40, r40, 16) && readRegs(0x6D, r6d, 1) && readRegs(0x80, r80, 6) && readRegs(0x100, r100, 30);
    const bool canaryRead = readRegs(kCanaryReg, rCanary, 1);
    GSPGPU_ReadHWRegs(0x400034u, stat, 4);
    const u64 stall = svcGetSystemTick() - stall0;
    s.probeTicks += stall;
    if (stall > s.probeMaxTicks) s.probeMaxTicks = stall;
    const bool canaryOk = canaryRead && (rCanary[0] & kCanaryMask) == (canary & kCanaryMask);
    if (canaryOk) ++s.canaryOk;
    const int slot = std::strncmp(pass, "water", 5) == 0 ? 1 : (std::strcmp(pass, "terrain_prime") == 0 ? 2 : 0);
    if (s.firstRegs[slot] || s.fullFrames > 0) {
        s.firstRegs[slot] = false;
        AS3D_INFO("AS3D_GPU_REGS pass=%s frame=%u read=%d dirty=%d cull=0x%lx map_scale=0x%lx map_offset=0x%lx map_enable=0x%lx "
                  "tex_config=0x%lx tex0=0x%lx color_op=0x%lx blend=0x%lx alpha=0x%lx stencil=0x%lx depth_mask=0x%lx "
                  "color_write=0x%lx depth_write=0x%lx depth_loc=0x%lx color_loc=0x%lx canary=0x%lx/0x%lx",
                  pass, static_cast<unsigned>(frameSerial()), ok ? 1 : 0, effectDirty ? 1 : 0,
                  static_cast<unsigned long>(r40[0]), static_cast<unsigned long>(r40[0xD]),
                  static_cast<unsigned long>(r40[0xE]), static_cast<unsigned long>(r6d[0]),
                  static_cast<unsigned long>(r80[0]), static_cast<unsigned long>(r80[5]),
                  static_cast<unsigned long>(r100[0]), static_cast<unsigned long>(r100[1]),
                  static_cast<unsigned long>(r100[4]), static_cast<unsigned long>(r100[5]),
                  static_cast<unsigned long>(r100[7]), static_cast<unsigned long>(r100[0x13]),
                  static_cast<unsigned long>(r100[0x15]), static_cast<unsigned long>(r100[0x1C]),
                  static_cast<unsigned long>(r100[0x1D]), static_cast<unsigned long>(canary),
                  static_cast<unsigned long>(rCanary[0]));
    }
    if (!ok) return;
    const bool prime = std::strcmp(pass, "terrain_prime") == 0;
    std::string fields;
    auto gpuMismatch = [&](const char* field, u32 expected, u32 actual) {
        ++s.gpuMismatches;
        if (std::strcmp(pass, "terrain") == 0) ++s.terrainGpuMismatches;
        if (prime) ++s.primeGpuMismatches;
        fields += (fields.empty() ? "" : ",") + std::string(field);
        // probe_every: the first mismatches of a level start dump the screens (the caller's mark),
        // kMaxAutoMarks for the priming draw's and as many for the other probes' (R3.3: the
        // priming draw's too, so the picture and the depth of that frame sit next to the registers).
        unsigned& budget = prime ? s.primeMarks : s.probeMarks;
        if (s.probeEvery > 0 && s.fullFrames <= 0 && budget < static_cast<unsigned>(kMaxAutoMarks) && !s.autoMarkPending) {
            s.autoMarkPending = true;
            ++budget;
            ++s.autoMarks;
        }
        noteField(s, pass, field);
        if (s.gpuLogged >= 64 && s.fullFrames <= 0 && s.frames - s.lastLogFrame < 60) return;
        ++s.gpuLogged;
        s.lastLogFrame = s.frames;
        AS3D_INFO("AS3D_GPU_MISMATCH pass=%s field=%s expected=0x%08lx actual=0x%08lx frame=%u target=%p dirty=%d", pass,
                  field, static_cast<unsigned long>(expected), static_cast<unsigned long>(actual),
                  static_cast<unsigned>(frameSerial()), static_cast<const void*>(currentTarget().target),
                  effectDirty ? 1 : 0);
    };
    if (s.probeInject && s.probes == s.probeInject && std::strcmp(pass, "terrain") == 0) gpuMismatch("inject", 0u, 1u); // probe_inject (self-test)
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
    if (fields.empty()) return; // (probe_inject's made-up mismatch goes on: the self-test of this evidence too)

    // R3.3: the evidence at a mismatch. The wash detector logs its judgement of this frame and the
    // next in full.
    ++s.mismatchProbes;
    if (!canaryOk) ++s.canaryStale;
    s.traceSerial[s.traceAt] = frameSerial();
    s.traceAt = (s.traceAt + 1) % 4;
    // A second read after another wait and 1 ms: a GPU still running when the first read was taken
    // has finished by then.
    gxCmdQueueWait(gxQueue(), -1);
    svcSleepThread(1000000LL);
    u32 b40[16] = {}, b6d[1] = {}, b100[30] = {}, bCanary[1] = {}, stat2[1] = {};
    const bool ok2 = readRegs(0x40, b40, 16) && readRegs(0x6D, b6d, 1) && readRegs(0x100, b100, 30) &&
                     readRegs(kCanaryReg, bCanary, 1);
    GSPGPU_ReadHWRegs(0x400034u, stat2, 4);
    const bool changed = ok2 && (b100[7] != r100[7] || b100[1] != r100[1] || b6d[0] != r6d[0] || b40[0xD] != r40[0xD] ||
                                 b40[0xE] != r40[0xE] || b100[0x15] != r100[0x15] || bCanary[0] != rCanary[0]);
    if (changed) ++s.rereadChanged;
    if (s.details >= static_cast<unsigned>(kDetailsPerLevel)) return;
    ++s.details;
    char listText[200];
    if (!samePass) std::snprintf(listText, sizeof listText, "none");
    else
        std::snprintf(listText, sizeof listText,
                      "%s/words=%lu/cmds=%lu/d107=0x%08lx,m%lx@%d/b101=0x%08lx,m%lx@%d/canary@%d/fin=%d",
                      scan.ok ? "ok" : "cut", static_cast<unsigned long>(scan.words), static_cast<unsigned long>(scan.commands),
                      static_cast<unsigned long>(scan.v107), static_cast<unsigned long>(scan.m107), scan.at107,
                      static_cast<unsigned long>(scan.v101), static_cast<unsigned long>(scan.m101), scan.at101,
                      scan.atCanary, scan.finalizes);
    AS3D_INFO("AS3D_GPU_MISMATCH_DETAIL n=%u pass=%s frame=%u fields=%s expected=depth:0x%04lx,blend:0x%08lx "
              "first=depth:0x%04lx,blend:0x%08lx,map:%lx/%06lx/%06lx,zwrite:%lx second=%s,depth:0x%04lx,blend:0x%08lx,"
              "map:%lx/%06lx/%06lx,zwrite:%lx canary=0x%08lx first=0x%08lx(%s) second=0x%08lx "
              "queue=%lu/%lu/%lu->%lu/%lu/%lu stat=0x%08lx/0x%08lx cmdbuf=%lu+%lu/%lu list=%s forced=%d trail=%s",
              s.details, pass, static_cast<unsigned>(frameSerial()), fields.c_str(),
              static_cast<unsigned long>(ef.depthTest & 0x1F71u), static_cast<unsigned long>(ef.alphaBlend),
              static_cast<unsigned long>(r100[7]), static_cast<unsigned long>(r100[1]), static_cast<unsigned long>(r6d[0]),
              static_cast<unsigned long>(r40[0xD] & 0xFFFFFFu), static_cast<unsigned long>(r40[0xE] & 0xFFFFFFu),
              static_cast<unsigned long>(r100[0x15]), !ok2 ? "unread" : (changed ? "changed" : "same"),
              static_cast<unsigned long>(b100[7]), static_cast<unsigned long>(b100[1]), static_cast<unsigned long>(b6d[0]),
              static_cast<unsigned long>(b40[0xD] & 0xFFFFFFu), static_cast<unsigned long>(b40[0xE] & 0xFFFFFFu),
              static_cast<unsigned long>(b100[0x15]), static_cast<unsigned long>(canary),
              static_cast<unsigned long>(rCanary[0]), canaryOk ? "ok" : "stale", static_cast<unsigned long>(bCanary[0]),
              static_cast<unsigned long>(q0.num), static_cast<unsigned long>(q0.cur), static_cast<unsigned long>(q0.last),
              static_cast<unsigned long>(q1.num), static_cast<unsigned long>(q1.cur), static_cast<unsigned long>(q1.last),
              static_cast<unsigned long>(stat[0]), static_cast<unsigned long>(stat2[0]), static_cast<unsigned long>(cmdBase),
              static_cast<unsigned long>(cmdOffset), static_cast<unsigned long>(m->cmdBufSize), listText,
              g_forcedThisPass ? 1 : 0, passProfile().trail().c_str());
}

void stateCheckNotePassStart() {
    State& s = st();
    g_forcedThisPass = false;
    if (!s.level) return;
    u32* buf = nullptr;
    u32 size = 0, offset = 0;
    GPUCMD_GetBuffer(&buf, &size, &offset);
    s.passStartBuf = buf;
    s.passStartOffset = offset;
    s.passStartSerial = frameSerial();
}

bool stateCheckWashTrace(uint32_t serial) {
    const State& s = st();
    if (!s.level || serial == 0) return false;
    for (uint32_t t : s.traceSerial)
        if (t != 0 && (serial == t || serial == t + 1u)) return true;
    return false;
}

namespace detail {
bool forceEffectState() {
    if (!mirrorLayoutOk()) return false;
    C3dContextMirror* m = const_cast<C3dContextMirror*>(ctxMirror());
    C3dEffectMirror& e = m->effect;
    // C3Di_EffectBind (citro3d 1.7.1 source/effect.c), word for word.
    GPUCMD_AddWrite(GPUREG_DEPTHMAP_ENABLE, e.zBuffer ? 1 : 0);
    GPUCMD_AddWrite(GPUREG_FACECULLING_CONFIG, static_cast<u32>(e.cullMode) & 0x3u);
    GPUCMD_AddIncrementalWrites(GPUREG_DEPTHMAP_SCALE, &e.zScale, 2);
    GPUCMD_AddIncrementalWrites(GPUREG_FRAGOP_ALPHA_TEST, &e.alphaTest, 4);
    GPUCMD_AddMaskedWrite(GPUREG_GAS_DELTAZ_DEPTH, 0x8, static_cast<u32>(GPU_MAKEGASDEPTHFUNC((e.depthTest >> 4) & 7)) << 24);
    GPUCMD_AddWrite(GPUREG_BLEND_COLOR, e.blendClr);
    GPUCMD_AddWrite(GPUREG_BLEND_FUNC, e.alphaBlend);
    GPUCMD_AddWrite(GPUREG_LOGIC_OP, static_cast<u32>(e.clrLogicOp));
    GPUCMD_AddMaskedWrite(GPUREG_COLOR_OPERATION, 7, e.fragOpMode);
    GPUCMD_AddWrite(GPUREG_FRAGOP_SHADOW, e.fragOpShadow);
    GPUCMD_AddMaskedWrite(GPUREG_EARLYDEPTH_TEST1, 1, e.earlyDepth ? 1 : 0);
    GPUCMD_AddWrite(GPUREG_EARLYDEPTH_TEST2, e.earlyDepth ? 1 : 0);
    GPUCMD_AddMaskedWrite(GPUREG_EARLYDEPTH_FUNC, 1, static_cast<u32>(e.earlyDepthFunc));
    GPUCMD_AddMaskedWrite(GPUREG_EARLYDEPTH_DATA, 0x7, e.earlyDepthRef);
    // citro3d writes the same block again with the next draw: its context and the GPU agree
    // either way, and no skipped update can leave the GPU behind it.
    m->flags |= kFlagEffect;
    g_forcedThisPass = true;
    return true;
}

uint32_t* c3dFlagsPointer() {
    if (!mirrorLayoutOk()) return nullptr;
    return &const_cast<C3dContextMirror*>(ctxMirror())->flags;
}
} // namespace detail

bool stateCheckSplitsThisFrame() {
    const State& s = st();
    return s.level && s.ctxOk && (s.level >= 2 || s.fullFrames > 0 || s.probeNow);
}

void stateCheckFrameBegin() {
    State& s = st();
    if (!s.level) return;
    s.probeNow = s.probeEvery > 0 && s.frames % static_cast<unsigned>(s.probeEvery) == 0;
    s.probePrime = s.probeNow && (s.frames / static_cast<unsigned>(s.probeEvery)) % 2 == 0;
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
              "mismatches=%u gpu_mismatches=%u gpu_busy=%u psc=%lx/%lx ppf=%lx canary=%u/%u reread_changed=%u "
              "prime_queue=%lu..%lu prime_words=%lu..%lu fields=%s",
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
              static_cast<unsigned long>(s.lastStat[1]), static_cast<unsigned long>(s.lastStat[2]), s.canaryOk, s.probes,
              s.rereadChanged, static_cast<unsigned long>(s.primeQueueMin == ~0u ? 0u : s.primeQueueMin),
              static_cast<unsigned long>(s.primeQueueMax), static_cast<unsigned long>(s.primeWordsMin == ~0u ? 0u : s.primeWordsMin),
              static_cast<unsigned long>(s.primeWordsMax), s.fieldsSeen.empty() ? "none" : s.fieldsSeen.c_str());
    s.cmdMax = 0.0f;
    s.primeQueueMin = s.primeWordsMin = ~0u;
    s.primeQueueMax = s.primeWordsMax = 0;
    s.queueMax = 0;
}

void setProbeEvery(int frames) {
    State& s = st();
    s.probeEvery = frames > 0 ? frames : 0;
    s.probeNow = false;
    if (s.level && s.probeEvery)
        AS3D_INFO("AS3D_STATE_CHECK probe_every=%d (the terrain's GPU read-back, one stall per eye; automatic marks: %d)",
                  s.probeEvery, kMaxAutoMarks);
}

void setProbeInject(int nthProbe) {
    State& s = st();
    s.probeInject = nthProbe > 0 ? static_cast<unsigned>(nthProbe) : 0u;
    if (s.level && s.probeInject)
        AS3D_INFO("AS3D_STATE_CHECK probe_inject=%u (probe %u reports a made-up terrain mismatch: a self-test)", s.probeInject,
                  s.probeInject);
}

bool stateCheckTakeAutoMark() {
    State& s = st();
    const bool due = s.autoMarkPending;
    s.autoMarkPending = false;
    return due;
}

unsigned stateCheckProbes() { return g_probesTotal; }

StateCheckCounts stateCheckCounts() {
    const State& s = st();
    StateCheckCounts c;
    c.checks = s.checks;
    c.probes = s.probes;
    c.mismatches = s.mismatches;
    c.gpuMismatches = s.gpuMismatches;
    c.terrainGpuMismatches = s.terrainGpuMismatches;
    c.primeGpuMismatches = s.primeGpuMismatches;
    c.washEvals = s.washEvals;
    c.washJudged = s.washJudged;
    c.washFrames = s.washFrames;
    c.washEpisodes = s.washEpisodes;
    c.washLongest = s.washLongest;
    c.washMaxSamples = s.washMaxSamples;
    c.washFirstFrame = s.washFirstFrame;
    c.washMs = static_cast<double>(s.washTicks) * 1000.0 / SYSCLOCK_ARM11;
    c.washMaxMs = static_cast<double>(s.washMaxTicks) * 1000.0 / SYSCLOCK_ARM11;
    c.busyFrames = s.busyFrames;
    c.marks = static_cast<unsigned>(s.marks);
    c.autoMarks = static_cast<unsigned>(s.autoMarks);
    c.washMarks = s.washMarks;
    c.contextRead = s.ctxOk;
    c.probeMs = static_cast<double>(s.probeTicks) * 1000.0 / SYSCLOCK_ARM11;
    c.probeMaxMs = static_cast<double>(s.probeMaxTicks) * 1000.0 / SYSCLOCK_ARM11;
    c.mismatchProbes = s.mismatchProbes;
    c.rereadChanged = s.rereadChanged;
    c.canaryOk = s.canaryOk;
    c.canaryStale = s.canaryStale;
    c.primeMarks = s.primeMarks;
    c.details = s.details;
    return c;
}

void stateCheckNoteWashEval(double ms, int judged) {
    State& s = st();
    ++s.washEvals;
    const u64 ticks = static_cast<u64>(ms * static_cast<double>(SYSCLOCK_ARM11) / 1000.0);
    s.washTicks += ticks;
    if (ticks > s.washMaxTicks) s.washMaxTicks = ticks;
    s.washJudged += static_cast<unsigned>(std::max(judged, 0));
}

void stateCheckNoteWashEpisode(unsigned startFrame, unsigned frames, int maxWashed) {
    State& s = st();
    if (s.washEpisodes == 0) s.washFirstFrame = startFrame;
    ++s.washEpisodes;
    s.washFrames += frames;
    s.washLongest = std::max(s.washLongest, frames);
    s.washMaxSamples = std::max(s.washMaxSamples, static_cast<unsigned>(std::max(maxWashed, 0)));
}

void stateCheckArmProbe(int frames) {
    State& s = st();
    if (s.level) s.fullFrames = std::max(s.fullFrames, frames);
}

bool stateCheckWashMarkDue() {
    State& s = st();
    if (s.washMarks >= static_cast<unsigned>(kMaxAutoMarks)) return false;
    ++s.autoMarks;
    ++s.washMarks;
    return true;
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
