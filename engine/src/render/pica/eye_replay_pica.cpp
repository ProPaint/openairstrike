// The second eye from the first eye's command words (eye_replay_pica.h).
#include "eye_replay_pica.h"

#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "pica_frame.h"
#include "shader_set.h"
#include "tev.h"

// citro3d 1.7.1 (source/uniforms.c): marks every uniform ever set, the bool uniforms and the bound
// program's constants dirty, so the next draw uploads them all. External linkage, not in the
// public headers.
extern "C" void C3Di_DirtyUniforms(GPU_SHADER_TYPE type);

namespace as3d::pica {
namespace {

// citro3d 1.7.1 internal.h: the dirty bits re-emitted at a region's start and after a replay. Not
// the frame buffer (the region must not hold the left target's address), DrawUsed, Active, the
// procedural texture and gas bits (unused; their update dereferences tables we never set).
constexpr uint32_t kAttrInfo = 1u << 2, kBufInfo = 1u << 3, kEffect = 1u << 4, kViewport = 1u << 6, kScissor = 1u << 7,
                   kProgram = 1u << 8, kTexEnvBuf = 1u << 9, kLightEnv = 1u << 10, kVshCode = 1u << 11,
                   kTexStatus = 1u << 14, kFogLut = 1u << 17, kTexAll = 7u << 23, kTexEnvAll = 0x3Fu << 26;
constexpr uint32_t kDirtyAll = kAttrInfo | kBufInfo | kEffect | kViewport | kScissor | kProgram | kTexEnvBuf | kLightEnv |
                               kVshCode | kTexStatus | kFogLut | kTexAll | kTexEnvAll;

bool g_enabled = true;
EyeReplayStats g_stats;

struct Frame {
    bool requested = false;
    EyeView right;
    bool armed = false;
    float k1 = 0.0f, k2 = 0.0f;
    bool recording = false, recorded = false;
    u32* buffer = nullptr; // libctru's command buffer when the region started (a split moves it)
    u32 start = 0, end = 0; // word offsets of the region in it
} g_frame;

FlatVertex* g_prime = nullptr; // three equal vertices: a triangle of no area

void fail(const char* why) {
    ++g_stats.failed;
    g_stats.lastFailure = why;
}

// Everything citro3d tracks is emitted again with the next draw, uniforms included but `eye`
// (when `keepEye`, its value is the one the region must not contain).
bool markAllDirty(bool keepEye) {
    uint32_t* flags = detail::c3dFlagsPointer();
    if (!flags) return false;
    *flags |= kDirtyAll;
    C3Di_DirtyUniforms(GPU_VERTEX_SHADER);
    const int eye = shaders().eyeRegister();
    if (keepEye && eye >= 0) C3D_FVUnifDirty[GPU_VERTEX_SHADER][eye] = false;
    detail::c3dInvalidate();
    detail::forgetTextureBindings();
    return true;
}

// The priming draw: emits what is dirty (the target's frame buffer first) with a triangle of no
// area (three times the same vertex: no fragment).
bool primeDraw() {
    if (!g_prime) {
        g_prime = static_cast<FlatVertex*>(linearAlloc(3 * sizeof(FlatVertex)));
        if (!g_prime) return false;
        std::memset(g_prime, 0, 3 * sizeof(FlatVertex));
        GSPGPU_FlushDataCache(g_prime, 3 * sizeof(FlatVertex));
    }
    ShaderSet& sh = shaders();
    sh.bind(Shader::Flat);
    sh.setVertices(g_prime);
    C3D_DrawArrays(GPU_TRIANGLES, 0, 3);
    return true;
}

} // namespace

void setEyeReplayEnabled(bool on) { g_enabled = on; }
bool eyeReplayEnabled() { return g_enabled; }

bool eyeShiftOf(const Mat4& left, const Mat4& right, float* k1, float* k2) {
    // as3d::Mat4 is column-major: at(col, row).
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row) {
            if (row == 1 && (col == 2 || col == 3)) continue;
            const float a = left.at(col, row), b = right.at(col, row);
            if (std::fabs(a - b) > 1e-6f * std::max(1.0f, std::max(std::fabs(a), std::fabs(b)))) return false;
        }
    // Row 3 must be (0, 0, -1, 0): w = -z_view (the PICA projections of pica_space.h).
    if (left.at(0, 3) != 0.0f || left.at(1, 3) != 0.0f || left.at(2, 3) != -1.0f || left.at(3, 3) != 0.0f) return false;
    *k1 = -(right.at(2, 1) - left.at(2, 1));
    *k2 = right.at(3, 1) - left.at(3, 1);
    return std::isfinite(*k1) && std::isfinite(*k2);
}

void eyeReplayFrameBegin() { g_frame = Frame(); }

void eyeReplayRequest(const EyeView& right) {
    g_frame.requested = g_enabled;
    g_frame.right = right;
}

bool eyeReplayRequested(EyeView* right) {
    if (!g_frame.requested) return false;
    g_frame.requested = false; // the left eye takes it
    *right = g_frame.right;
    return true;
}

bool eyeReplayRecording() { return g_frame.recording; }
bool eyeReplayRecorded() { return g_frame.recorded; }

void eyeReplayArm(float k1, float k2) {
    if (!g_enabled || shaders().eyeRegister() < 0 || !detail::c3dFlagsPointer()) return;
    g_frame.armed = true;
    g_frame.k1 = k1;
    g_frame.k2 = k2;
    ++g_stats.armed;
}

bool eyeReplayArmed() { return g_frame.armed; }

bool eyeReplayShift(float* k1, float* k2) {
    if (!g_frame.armed) return false;
    *k1 = g_frame.k1;
    *k2 = g_frame.k2;
    return true;
}

bool eyeReplayRecordBegin() {
    if (!g_frame.armed) return false;
    shaders().setEye(0.0f, 0.0f);
    if (!primeDraw() || !markAllDirty(true)) {
        g_frame.armed = false;
        fail("prime");
        return false;
    }
    u32* buf = nullptr;
    u32 size = 0, offset = 0;
    GPUCMD_GetBuffer(&buf, &size, &offset);
    g_frame.buffer = buf;
    g_frame.start = offset;
    g_frame.recording = true;
    return true;
}

void eyeReplayRecordEnd() {
    if (!g_frame.recording) return;
    g_frame.recording = false;
    u32* buf = nullptr;
    u32 size = 0, offset = 0;
    GPUCMD_GetBuffer(&buf, &size, &offset);
    if (buf != g_frame.buffer || offset <= g_frame.start || ((offset - g_frame.start) & 1u)) {
        fail("split inside the region");
        return;
    }
    g_frame.end = offset;
    g_frame.recorded = true;
    ++g_stats.recorded;
}

bool eyeReplayRun() {
    if (!g_frame.recorded) return false;
    g_frame.recorded = false; // once
    const u64 t0 = svcGetSystemTick();
    const u32 words = g_frame.end - g_frame.start;
    shaders().setEye(g_frame.k1, g_frame.k2);
    if (!primeDraw()) {
        shaders().setEye(0.0f, 0.0f);
        fail("prime");
        return false;
    }
    u32* buf = nullptr;
    u32 size = 0, offset = 0;
    GPUCMD_GetBuffer(&buf, &size, &offset);
    // Room for the copy and what the frame still records after it (the 2D layers, the bottom
    // screen, the final split): a quarter of the buffer kept free.
    if (!buf || (offset & 1u) || offset + words + size / 4 > size) {
        shaders().setEye(0.0f, 0.0f);
        fail("command buffer");
        return false;
    }
    GPUCMD_AddRawCommands(g_frame.buffer + g_frame.start, words);
    // The GPU now holds the region's last state; citro3d and the shadow copies hold theirs: all of
    // it goes again with the next draw, and the eye shift back to 0.
    markAllDirty(false);
    shaders().setEye(0.0f, 0.0f);
    ++g_stats.replayed;
    g_stats.words += words;
    g_stats.lastWords = words;
    g_stats.replayTicks += svcGetSystemTick() - t0;
    return true;
}

const EyeReplayStats& eyeReplayStats() { return g_stats; }

} // namespace as3d::pica
