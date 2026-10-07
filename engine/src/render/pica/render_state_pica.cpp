// PICA implementation of as3d/render_state.h and applyMaterialState (as3d/material_key.h).
//
// Mapping from the GL backend (render_state_gl.cpp, material_state.cpp):
//  * Blend: C3D_AlphaBlend(ADD, ADD, src, dst, src, dst) with GL's factors, the same factor
//    for colour and alpha as glBlendFunc. "Off" is ONE, ZERO.
//  * Depth: the PICA depth runs the other way (1 at the near plane, pica_frame.h), so GL's
//    LEQUAL is GEQUAL. GL writes no depth while the test is off; the PICA would (Citra and the
//    hardware write depth whenever the mask has it), so a disabled test also clears the
//    depth write bit.
//  * Cull: GL culls with front faces counter-clockwise. The tilt (pica_space.h) is a rotation,
//    which keeps the winding, so Back is GPU_CULL_BACK_CCW and Front GPU_CULL_FRONT_CCW
//    (checked with a CCW and a CW quad in apps/3ds/pica_test.cpp).
//  * Viewport and scissor take GL rectangles (origin bottom-left of the upright screen) and
//    convert them to the rotated framebuffer with picaFramebufferRect (pica_space.h).
//    C3D_SetViewport disables the scissor, so setViewport re-applies it.
//  * clear / clearColor / clearDepth: without a scissor, a GX memory fill of the whole target
//    (C3D_FrameSplit first, so the fill runs after the draws already recorded); with a scissor,
//    a quad through the flat shader. The quad path rebinds the flat shader and the TEV: the
//    caller binds its own again (every PICA renderer binds per draw).
#include <citro3d.h>

#include <algorithm>

#include "as3d/material_key.h"
#include "as3d/render_state.h"
#include "pica_frame.h"
#include "pica_space.h"
#include "tev.h"

namespace as3d {
namespace pica {
namespace {

struct BlendFactors {
    GPU_BLENDFACTOR src = GPU_ONE;
    GPU_BLENDFACTOR dst = GPU_ZERO;
};

// One row per RenderBlend value, in declaration order (the GL table of material_state.cpp).
constexpr BlendFactors kRenderBlend[] = {
    /* None       */ {GPU_ONE, GPU_ZERO},
    /* Alpha      */ {GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA},
    /* Add        */ {GPU_ONE, GPU_ONE},
    /* Filter     */ {GPU_DST_COLOR, GPU_ZERO},
    /* Brightness */ {GPU_DST_COLOR, GPU_SRC_COLOR},
};

struct RenderState {
    BlendFactors blend;
    bool depthTest = true;
    bool depthWrite = true;
    CullMode cull = CullMode::Back;
    float depthOffset = 0.0f;
    bool scissor = false;
    FbRect scissorRect;
    bool viewportFull = true;
    FbRect viewport;
};

RenderState& state() {
    static RenderState s;
    return s;
}

void issueBlend() {
    const BlendFactors& b = state().blend;
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, b.src, b.dst, b.src, b.dst);
}

void issueDepth() {
    const RenderState& s = state();
    if (s.depthTest) C3D_DepthTest(true, GPU_GEQUAL, s.depthWrite ? GPU_WRITE_ALL : GPU_WRITE_COLOR);
    else C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
}

void issueCull() {
    switch (state().cull) {
    case CullMode::Off: C3D_CullFace(GPU_CULL_NONE); break;
    case CullMode::Back: C3D_CullFace(GPU_CULL_BACK_CCW); break;
    case CullMode::Front: C3D_CullFace(GPU_CULL_FRONT_CCW); break;
    }
}

void issueScissor() {
    const RenderState& s = state();
    if (!s.scissor) {
        C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
        return;
    }
    const FbRect& r = s.scissorRect;
    C3D_SetScissor(GPU_SCISSOR_NORMAL, static_cast<u32>(r.x), static_cast<u32>(r.y), static_cast<u32>(r.x + r.w),
                   static_cast<u32>(r.y + r.h));
}

void issueViewport() {
    const RenderState& s = state();
    const FrameTarget& t = currentTarget();
    if (s.viewportFull) {
        C3D_SetViewport(0, 0, static_cast<u32>(t.height), static_cast<u32>(t.width));
    } else {
        C3D_SetViewport(static_cast<u32>(s.viewport.x), static_cast<u32>(s.viewport.y), static_cast<u32>(s.viewport.w),
                        static_cast<u32>(s.viewport.h));
    }
    issueScissor(); // C3D_SetViewport turned it off
}

FbRect clampRect(FbRect r, int fbWidth, int fbHeight) {
    int x0 = std::clamp(r.x, 0, fbWidth), y0 = std::clamp(r.y, 0, fbHeight);
    int x1 = std::clamp(r.x + r.w, 0, fbWidth), y1 = std::clamp(r.y + r.h, 0, fbHeight);
    return FbRect{x0, y0, std::max(x1 - x0, 0), std::max(y1 - y0, 0)};
}

u8 toByte(float v) { return static_cast<u8>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); }

u32 packRgba(const Vec4& c) {
    return static_cast<u32>(toByte(c.x)) << 24 | static_cast<u32>(toByte(c.y)) << 16 |
           static_cast<u32>(toByte(c.z)) << 8 | toByte(c.w);
}

// GL depth value (0 near, 1 far) as the D24S8 fill word (stencil 0): the PICA stores 1 - d.
u32 packDepth(float glDepth) {
    return static_cast<u32>((1.0f - std::clamp(glDepth, 0.0f, 1.0f)) * 16777215.0f + 0.5f) & 0xFFFFFFu;
}

// A memory fill of the whole current target, after the draws recorded so far.
void fillTarget(bool color, const Vec4& c, bool depth, float glDepth) {
    const FrameTarget& t = currentTarget();
    if (!t.target || (!color && !depth)) return;
    C3D_ClearBits bits = static_cast<C3D_ClearBits>((color ? C3D_CLEAR_COLOR : 0) | (depth ? C3D_CLEAR_DEPTH : 0));
    C3D_FrameSplit(0);
    C3D_FrameBufClear(&t.target->frameBuf, bits, packRgba(c), packDepth(glDepth));
}

// A quad over the scissor rectangle writing colour and/or depth.
void fillQuad(bool color, const Vec4& c, bool depth, float glDepth) {
    if (!backendReady()) return;
    auto* v = frameArena().allocArray<FlatVertex>(4);
    if (!v) return;
    // Clip-space corners (identity mvp); z gives the depth: Z-buffer depth = -z, W-buffer
    // depth = -z / near with w = 1 (pica_frame.h), and the PICA depth is 1 - the GL depth.
    float d = 1.0f - std::clamp(glDepth, 0.0f, 1.0f);
    float z = -d;
    if (depthMapping() == FogDepthMap::WBuffer) z = -d * depthMappingNear();
    const float xs[4] = {-1, 1, -1, 1}, ys[4] = {-1, -1, 1, 1};
    for (int i = 0; i < 4; ++i) {
        v[i].pos[0] = xs[i];
        v[i].pos[1] = ys[i];
        v[i].pos[2] = z;
        v[i].uv[0] = v[i].uv[1] = 0.0f;
        v[i].rgba[0] = toByte(c.x);
        v[i].rgba[1] = toByte(c.y);
        v[i].rgba[2] = toByte(c.z);
        v[i].rgba[3] = toByte(c.w);
    }
    ShaderSet& sh = shaders();
    sh.bind(Shader::Flat);
    sh.setMvp(Mat4::identity());
    sh.setVertices(v);
    applyTev(tevConstantColor(toByte(c.x), toByte(c.y), toByte(c.z), toByte(c.w)));
    const bool fog = fogEnabled();
    if (fog) C3D_FogGasMode(GPU_NO_FOG, GPU_PLAIN_DENSITY, false);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
    C3D_DepthTest(true, GPU_ALWAYS,
                  static_cast<GPU_WRITEMASK>((color ? GPU_WRITE_COLOR : 0) | (depth ? GPU_WRITE_DEPTH : 0)));
    C3D_CullFace(GPU_CULL_NONE);
    if (state().depthOffset != 0.0f) applyDepthMap(0.0f);
    const bool full = state().viewportFull;
    state().viewportFull = true;
    issueViewport();
    C3D_DrawArrays(GPU_TRIANGLE_STRIP, 0, 4);
    // Back to the tracked state.
    state().viewportFull = full;
    issueViewport();
    if (state().depthOffset != 0.0f) applyDepthMap(state().depthOffset);
    issueBlend();
    issueDepth();
    issueCull();
    if (fog) C3D_FogGasMode(GPU_FOG, GPU_PLAIN_DENSITY, false);
}

void clearImpl(bool color, const Vec4& c, bool depth, float glDepth) {
    depth = depth && currentTarget().hasDepth;
    if (state().scissor) fillQuad(color, c, depth, glDepth);
    else fillTarget(color, c, depth, glDepth);
}

} // namespace

namespace detail {

void resetViewState() {
    RenderState& s = state();
    s.scissor = false;
    s.viewportFull = true;
    issueViewport();
}

void applyDefaultState() {
    issueBlend();
    issueDepth();
    issueCull();
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
}

} // namespace detail
} // namespace pica

using pica::state;

void setBlend(GlBlend mode) {
    pica::BlendFactors& b = state().blend;
    switch (mode) {
    case GlBlend::Off: b = {GPU_ONE, GPU_ZERO}; break;
    case GlBlend::AlphaBlend: b = {GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA}; break;
    case GlBlend::Additive: b = {GPU_SRC_ALPHA, GPU_ONE}; break;
    }
    pica::issueBlend();
}

void setDepth(bool test, bool write) {
    state().depthTest = test;
    state().depthWrite = write;
    pica::issueDepth();
}

void setCull(CullMode mode) {
    state().cull = mode;
    pica::issueCull();
}

void setViewport(int x, int y, int w, int h) {
    const pica::FrameTarget& t = pica::currentTarget();
    pica::RenderState& s = state();
    s.viewportFull = x == 0 && y == 0 && w == t.width && h == t.height;
    s.viewport = pica::picaFramebufferRect(t.width, x, y, w, h);
    pica::issueViewport();
}

void setScissor(bool enabled, int x, int y, int w, int h) {
    const pica::FrameTarget& t = pica::currentTarget();
    pica::RenderState& s = state();
    s.scissor = enabled;
    if (enabled) s.scissorRect = pica::clampRect(pica::picaFramebufferRect(t.width, x, y, w, h), t.height, t.width);
    pica::issueScissor();
}

void clear(const Vec4& color, bool depth, float depthValue) { pica::clearImpl(true, color, depth, depthValue); }

void clearColor(const Vec4& color) { pica::clearImpl(true, color, false, 1.0f); }

void clearDepth() {
    state().depthWrite = true;
    pica::issueDepth();
    pica::clearImpl(false, Vec4{0, 0, 0, 0}, true, 1.0f);
}

void applyMaterialState(const MaterialKey& key) {
    pica::RenderState& s = state();
    s.blend = pica::kRenderBlend[static_cast<int>(key.blend)];
    pica::issueBlend();
    s.depthTest = key.depthTest;
    s.depthWrite = key.depthWrite;
    pica::issueDepth();
    s.cull = key.cull;
    pica::issueCull();
    const float offset = key.polygonOffset ? pica::kPolygonOffsetDepth : 0.0f;
    if (offset != s.depthOffset) {
        s.depthOffset = offset;
        pica::applyDepthMap(offset);
    }
    pica::applyFogColor(fogColorFor(key.fog, pica::levelFogColor()));
    C3D_AlphaTest(key.alphaTest, GPU_GREATER, 0x7F);
}

} // namespace as3d
