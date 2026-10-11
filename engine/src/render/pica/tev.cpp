// TEV presets (tev.h). The presets are neutral (the desktop tests evaluate them); applyTev is
// compiled for the 3DS only.
#include "tev.h"

#if defined(__3DS__)
#include <citro3d.h>
#endif

namespace as3d::pica {
namespace {

// One stage computing func(src0, src1, src2) with plain colour / alpha operands for both RGB
// and alpha.
TevStage stage(TevFunc func, TevSrc s0, TevSrc s1 = TevSrc::Primary, TevSrc s2 = TevSrc::Primary) {
    TevStage s;
    s.srcRgb[0] = s.srcA[0] = s0;
    s.srcRgb[1] = s.srcA[1] = s1;
    s.srcRgb[2] = s.srcA[2] = s2;
    s.funcRgb = s.funcA = func;
    return s;
}

void setAlpha(TevStage& s, TevFunc func, TevSrc s0, TevSrc s1 = TevSrc::Primary, TevSrc s2 = TevSrc::Primary) {
    s.funcA = func;
    s.srcA[0] = s0;
    s.srcA[1] = s1;
    s.srcA[2] = s2;
}

void setConstant(TevStage& s, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    s.constant[0] = r;
    s.constant[1] = g;
    s.constant[2] = b;
    s.constant[3] = a;
}

TevConfig one(const TevStage& s) {
    TevConfig c;
    c.count = 1;
    c.stages[0] = s;
    return c;
}

} // namespace

TevConfig tevReplaceTex() { return one(stage(TevFunc::Replace, TevSrc::Texture0)); }

TevConfig tevModulateTexColor() { return one(stage(TevFunc::Modulate, TevSrc::Texture0, TevSrc::Primary)); }

TevConfig tevClampAddTexColor() {
    TevStage s = stage(TevFunc::Add, TevSrc::Texture0, TevSrc::Primary);
    setAlpha(s, TevFunc::Modulate, TevSrc::Texture0, TevSrc::Primary);
    return one(s);
}

TevConfig tevConstantColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    TevStage s = stage(TevFunc::Replace, TevSrc::Constant);
    setConstant(s, r, g, b, a);
    return one(s);
}

TevConfig tevModulateTexConstant(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    TevStage s = stage(TevFunc::Modulate, TevSrc::Texture0, TevSrc::Constant);
    setConstant(s, r, g, b, a);
    return one(s);
}

TevConfig tevTerrain2Stage() {
    TevConfig c;
    c.count = 2;
    c.stages[0] = stage(TevFunc::Modulate, TevSrc::Texture0, TevSrc::Primary);
    setAlpha(c.stages[0], TevFunc::Replace, TevSrc::Primary);
    c.stages[1] = stage(TevFunc::AddSigned, TevSrc::Previous, TevSrc::Texture1);
    setAlpha(c.stages[1], TevFunc::Replace, TevSrc::Previous);
    return c;
}

TevConfig tevTerrainFar(uint8_t r, uint8_t g, uint8_t b) {
    TevConfig c = tevTerrain2Stage();
    c.stages[1] = stage(TevFunc::AddSigned, TevSrc::Previous, TevSrc::Constant);
    setAlpha(c.stages[1], TevFunc::Replace, TevSrc::Previous);
    setConstant(c.stages[1], r, g, b, 255);
    return c;
}

TevConfig tevCombine2(int combineMode) {
    TevConfig c = tevModulateTexColor();
    if (combineMode == 0) return c;
    TevStage s;
    switch (combineMode) {
    case 1: // rgb = mix(previous, tex2, tex2.a) = tex2 * tex2.a + previous * (1 - tex2.a)
        s = stage(TevFunc::Interpolate, TevSrc::Texture1, TevSrc::Previous, TevSrc::Texture1);
        s.opRgb[2] = TevOpRgb::SrcAlpha;
        setAlpha(s, TevFunc::Replace, TevSrc::Previous);
        break;
    case 2: // rgb = previous + tex2
        s = stage(TevFunc::Add, TevSrc::Previous, TevSrc::Texture1);
        setAlpha(s, TevFunc::Replace, TevSrc::Previous);
        break;
    case 3: // rgba = previous * tex2
        s = stage(TevFunc::Modulate, TevSrc::Previous, TevSrc::Texture1);
        break;
    default: // rgba = tex2
        s = stage(TevFunc::Replace, TevSrc::Texture1);
        break;
    }
    c.count = 2;
    c.stages[1] = s;
    return c;
}

TevConfig tevEnvGlitter(TevSrc colour, bool envAlpha, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    TevConfig c;
    c.count = 2;
    c.stages[0] = stage(TevFunc::Modulate, TevSrc::Texture0, colour);
    setConstant(c.stages[0], r, g, b, a);
    c.stages[1] = stage(TevFunc::Add, TevSrc::Previous, TevSrc::Texture1);
    if (envAlpha) setAlpha(c.stages[1], TevFunc::Modulate, TevSrc::Previous, TevSrc::Texture1);
    else setAlpha(c.stages[1], TevFunc::Replace, TevSrc::Previous);
    return c;
}

TevConfig tevEnvChrome(TevSrc colour, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    TevConfig c;
    c.count = 2;
    c.stages[0] = stage(TevFunc::Modulate, TevSrc::Texture1, colour);
    setConstant(c.stages[0], r, g, b, a);
    // skin * skin.a + previous * (1 - skin.a)
    c.stages[1] = stage(TevFunc::Interpolate, TevSrc::Texture0, TevSrc::Previous, TevSrc::Texture0);
    c.stages[1].opRgb[2] = TevOpRgb::SrcAlpha;
    setAlpha(c.stages[1], TevFunc::Replace, TevSrc::Previous);
    return c;
}

TevConfig tevEnvQuad(TevSrc colour, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    TevStage s = stage(TevFunc::Modulate, TevSrc::Texture1, colour);
    setConstant(s, r, g, b, a);
    return one(s);
}

TevConfig tevWaterGrid() {
    TevStage s = stage(TevFunc::Interpolate, TevSrc::Texture0, TevSrc::Texture1, TevSrc::Texture0);
    s.opRgb[2] = TevOpRgb::SrcAlpha;
    setAlpha(s, TevFunc::Replace, TevSrc::Primary);
    return one(s);
}

TevConfig tevShadow(uint8_t alpha) {
    TevStage s = stage(TevFunc::Replace, TevSrc::Constant);
    setAlpha(s, TevFunc::Modulate, TevSrc::Texture0, TevSrc::Constant);
    setConstant(s, 0, 0, 0, alpha);
    return one(s);
}

bool tevAppendFold(TevConfig& cfg, uint8_t s) {
    if (cfg.count >= kMaxTevStages) return false;
    TevStage st = stage(TevFunc::Modulate, TevSrc::Previous, TevSrc::Constant);
    setAlpha(st, TevFunc::Replace, TevSrc::Previous);
    setConstant(st, s, s, s, 255);
    st.scaleRgb = 2;
    cfg.stages[cfg.count++] = st;
    return true;
}

#if defined(__3DS__)

static_assert(static_cast<int>(TevSrc::Primary) == GPU_PRIMARY_COLOR, "TevSrc");
static_assert(static_cast<int>(TevSrc::Texture0) == GPU_TEXTURE0, "TevSrc");
static_assert(static_cast<int>(TevSrc::Texture1) == GPU_TEXTURE1, "TevSrc");
static_assert(static_cast<int>(TevSrc::Texture2) == GPU_TEXTURE2, "TevSrc");
static_assert(static_cast<int>(TevSrc::PreviousBuffer) == GPU_PREVIOUS_BUFFER, "TevSrc");
static_assert(static_cast<int>(TevSrc::Constant) == GPU_CONSTANT, "TevSrc");
static_assert(static_cast<int>(TevSrc::Previous) == GPU_PREVIOUS, "TevSrc");
static_assert(static_cast<int>(TevOpRgb::OneMinusSrcColor) == GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR, "TevOpRgb");
static_assert(static_cast<int>(TevOpRgb::SrcAlpha) == GPU_TEVOP_RGB_SRC_ALPHA, "TevOpRgb");
static_assert(static_cast<int>(TevOpRgb::OneMinusSrcAlpha) == GPU_TEVOP_RGB_ONE_MINUS_SRC_ALPHA, "TevOpRgb");
static_assert(static_cast<int>(TevOpA::OneMinusSrcAlpha) == GPU_TEVOP_A_ONE_MINUS_SRC_ALPHA, "TevOpA");
static_assert(static_cast<int>(TevFunc::Modulate) == GPU_MODULATE, "TevFunc");
static_assert(static_cast<int>(TevFunc::Add) == GPU_ADD, "TevFunc");
static_assert(static_cast<int>(TevFunc::AddSigned) == GPU_ADD_SIGNED, "TevFunc");
static_assert(static_cast<int>(TevFunc::Interpolate) == GPU_INTERPOLATE, "TevFunc");
static_assert(static_cast<int>(TevFunc::Subtract) == GPU_SUBTRACT, "TevFunc");
static_assert(static_cast<int>(TevFunc::MultiplyAdd) == GPU_MULTIPLY_ADD, "TevFunc");
static_assert(static_cast<int>(TevFunc::AddMultiply) == GPU_ADD_MULTIPLY, "TevFunc");

namespace {
bool sameStage(const TevStage& a, const TevStage& b) {
    for (int k = 0; k < 3; ++k)
        if (a.srcRgb[k] != b.srcRgb[k] || a.srcA[k] != b.srcA[k] || a.opRgb[k] != b.opRgb[k] || a.opA[k] != b.opA[k])
            return false;
    return a.funcRgb == b.funcRgb && a.funcA == b.funcA && a.scaleRgb == b.scaleRgb && a.constant[0] == b.constant[0] &&
           a.constant[1] == b.constant[1] && a.constant[2] == b.constant[2] && a.constant[3] == b.constant[3];
}
TevConfig g_lastTev;
bool g_lastTevValid = false;
// The brightness fold (tev.h): the configuration the renderer asked for, the fold's colour byte
// (0 off), whether the current blend takes it.
TevConfig g_reqTev;
bool g_reqTevValid = false;
uint8_t g_fold = 0;
bool g_foldable = true;
unsigned g_foldOverflows = 0;

void issueTev(const TevConfig& cfg);

void reissue() {
    if (g_reqTevValid) issueTev(g_reqTev);
}
} // namespace

void tevInvalidate() { g_lastTevValid = false; }

void setBrightnessFold(uint8_t s) {
    if (s == g_fold) return;
    g_fold = s;
    reissue();
    detail::c3dRefoldFog();
}

uint8_t brightnessFold() { return g_fold; }

unsigned brightnessFoldOverflows() { return g_foldOverflows; }

namespace detail {
void tevSetFoldable(bool foldable) {
    if (foldable == g_foldable) return;
    g_foldable = foldable;
    if (g_fold) {
        reissue();
        c3dRefoldFog();
    }
}
bool tevFoldable() { return g_foldable; }
} // namespace detail

void applyTev(const TevConfig& cfg) {
    g_reqTev = cfg;
    g_reqTevValid = true;
    issueTev(cfg);
}

namespace {
void issueTev(const TevConfig& requested) {
    static int lastCount = kMaxTevStages; // unknown at start: reset everything once
    TevConfig folded;
    const TevConfig* use = &requested;
    if (g_fold && g_foldable) {
        folded = requested;
        if (tevAppendFold(folded, g_fold)) use = &folded;
        else ++g_foldOverflows;
    }
    const TevConfig& cfg = *use;
    if (g_lastTevValid && cfg.count == g_lastTev.count) {
        bool same = true;
        for (int i = 0; i < cfg.count && i < kMaxTevStages && same; ++i) same = sameStage(cfg.stages[i], g_lastTev.stages[i]);
        if (same) return;
    }
    g_lastTev = cfg;
    g_lastTevValid = true;
    for (int i = 0; i < cfg.count && i < kMaxTevStages; ++i) {
        const TevStage& s = cfg.stages[i];
        C3D_TexEnv* env = C3D_GetTexEnv(i);
        env->srcRgb = static_cast<u16>(GPU_TEVSOURCES(static_cast<int>(s.srcRgb[0]), static_cast<int>(s.srcRgb[1]),
                                                      static_cast<int>(s.srcRgb[2])));
        env->srcAlpha = static_cast<u16>(GPU_TEVSOURCES(static_cast<int>(s.srcA[0]), static_cast<int>(s.srcA[1]),
                                                        static_cast<int>(s.srcA[2])));
        env->opRgb = GPU_TEVOPERANDS(static_cast<int>(s.opRgb[0]), static_cast<int>(s.opRgb[1]),
                                     static_cast<int>(s.opRgb[2]));
        env->opAlpha = GPU_TEVOPERANDS(static_cast<int>(s.opA[0]), static_cast<int>(s.opA[1]),
                                       static_cast<int>(s.opA[2]));
        env->funcRgb = static_cast<u16>(s.funcRgb);
        env->funcAlpha = static_cast<u16>(s.funcA);
        // GPUREG_TEXENVi_COLOR: R in bits 0-7, G 8-15, B 16-23, A 24-31.
        env->color = static_cast<u32>(s.constant[0]) | static_cast<u32>(s.constant[1]) << 8 |
                     static_cast<u32>(s.constant[2]) << 16 | static_cast<u32>(s.constant[3]) << 24;
        env->scaleRgb = s.scaleRgb == 4 ? GPU_TEVSCALE_4 : s.scaleRgb == 2 ? GPU_TEVSCALE_2 : GPU_TEVSCALE_1;
        env->scaleAlpha = GPU_TEVSCALE_1;
    }
    for (int i = cfg.count; i < lastCount; ++i) C3D_TexEnvInit(C3D_GetTexEnv(i));
    lastCount = cfg.count;
}
} // namespace

#endif

} // namespace as3d::pica
