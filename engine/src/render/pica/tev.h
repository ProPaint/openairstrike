// Texture combiner (TEV) presets of the PICA backend, as data.
//
// The PICA200 has no fragment shader: each fragment runs through up to six combiner stages,
// each computing func(arg0, arg1, arg2) for RGB and alpha separately, where each argument is
// a source (vertex colour, a texture unit, the previous stage, a constant) with an operand
// (its colour, 1 - colour, its alpha, ...). Results are clamped to [0, 1] after each stage.
// The functions (3dbrew "GPU/Internal Registers", citro3d texenv.h):
//   Replace a | Modulate a*b | Add a+b | AddSigned a+b-0.5 | Interpolate a*c + b*(1-c)
//   Subtract a-b | MultiplyAdd a*b+c | AddMultiply min(a+b,1)*c
//
// A TevConfig is plain data so the desktop tests can evaluate the presets on the CPU
// (apps/tests/pica_core_test.cpp); applyTev() (3DS only, tev.cpp) writes it to citro3d's
// C3D_TexEnv stages. The enum values equal libctru's GPU_TEVSRC / GPU_TEVOP_* /
// GPU_COMBINEFUNC (static_asserts in tev.cpp).
//
// Texture units: TEXTURE0 samples texcoord0, TEXTURE1 texcoord1 (the shaders' outputs).
#pragma once

#include <cstdint>

namespace as3d::pica {

enum class TevSrc : uint8_t {
    Primary = 0x00,  // the vertex colour (shader output `color`)
    Texture0 = 0x03,
    Texture1 = 0x04,
    Texture2 = 0x05,
    PreviousBuffer = 0x0D,
    Constant = 0x0E, // the stage's constant colour
    Previous = 0x0F, // the previous stage's result
};
enum class TevOpRgb : uint8_t { SrcColor = 0, OneMinusSrcColor = 1, SrcAlpha = 2, OneMinusSrcAlpha = 3 };
enum class TevOpA : uint8_t { SrcAlpha = 0, OneMinusSrcAlpha = 1 };
enum class TevFunc : uint8_t {
    Replace = 0,
    Modulate = 1,
    Add = 2,
    AddSigned = 3,
    Interpolate = 4,
    Subtract = 5,
    MultiplyAdd = 8,
    AddMultiply = 9,
};

struct TevStage {
    TevSrc srcRgb[3] = {TevSrc::Previous, TevSrc::Primary, TevSrc::Primary};
    TevSrc srcA[3] = {TevSrc::Previous, TevSrc::Primary, TevSrc::Primary};
    TevOpRgb opRgb[3] = {TevOpRgb::SrcColor, TevOpRgb::SrcColor, TevOpRgb::SrcColor};
    TevOpA opA[3] = {TevOpA::SrcAlpha, TevOpA::SrcAlpha, TevOpA::SrcAlpha};
    TevFunc funcRgb = TevFunc::Replace;
    TevFunc funcA = TevFunc::Replace;
    uint8_t constant[4] = {255, 255, 255, 255}; // r, g, b, a of TevSrc::Constant
};

constexpr int kMaxTevStages = 6;

struct TevConfig {
    int count = 0; // stages in use; the rest pass the previous result through
    TevStage stages[kMaxTevStages];
};

// The texture's own colour: REPLACE(TEX0) for RGB and alpha.
TevConfig tevReplaceTex();
// Texture times vertex colour, RGB and alpha: MODULATE(TEX0, PRIMARY). The usual draw; also
// the particles' kTexModulate.
TevConfig tevModulateTexColor();
// clamp(texture + vertex colour) for RGB, texture alpha * vertex alpha: the particles'
// kTexClampAdd (DrawRun::texMode 2, the Filter blend's look).
TevConfig tevClampAddTexColor();
// One constant colour, RGB and alpha (untextured fills).
TevConfig tevConstantColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
// Texture times a constant colour (RF_NOLIGHTING models: skin * entity colour).
TevConfig tevModulateTexConstant(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
// The terrain: stage 0 MODULATE(TEX0, PRIMARY) = base * vertex colour (alpha 1 from the
// vertex), stage 1 ADD_SIGNED(PREVIOUS, TEX1) = base * vColor + detail - 0.5 (alpha passed on).
// Without a detail texture use tevModulateTexColor.
TevConfig tevTerrain2Stage();
// The 2D layer's second texture (as3d/batch2d.h CombineMode, values as there): stage 0 is
// texture * vertex colour, stage 1 combines TEXTURE1 with it: 1 Blend (by the second
// texture's alpha, RGB only), 2 Add (RGB only), 3 Modulate (RGBA), 4 Replace (RGBA by the
// second texture); 0 None gives stage 0 alone.
TevConfig tevCombine2(int combineMode);
// Environment-mapped models (as3d/envmap.h combineEnvMap, the GL mesh fragment shader's three
// modes in one draw): TEXTURE0 is the skin, TEXTURE1 the environment map (texcoord1 from
// env.v.pica), `colour` the light: Primary (lit: the vertex colour, alpha 1) or Constant (RF_NOLIGHTING:
// the entity colour, `r g b a`). The GL computes these without intermediate clamps; every
// intermediate here stays within [0, 1] (products of values <= 1, or a mix), so only the final
// sum of the glitter is clamped, as the framebuffer does.
//   Glitter: stage 0 rgb = skin * colour, a = skin.a * colour.a; stage 1 rgb = previous + env,
//            a = previous * env.a (envAlpha) or previous (the sequels, as2 delta 4.3)
//   Chrome:  stage 0 rgba = env * colour; stage 1 rgb = skin * skin.a + previous * (1 - skin.a)
//            (INTERPOLATE by the skin alpha), a = previous = env.a * colour.a
//   Quad:    stage 0 rgba = env * colour (the skin is not drawn)
TevConfig tevEnvGlitter(TevSrc colour, bool envAlpha, uint8_t r = 255, uint8_t g = 255, uint8_t b = 255,
                        uint8_t a = 255);
TevConfig tevEnvChrome(TevSrc colour, uint8_t r = 255, uint8_t g = 255, uint8_t b = 255, uint8_t a = 255);
TevConfig tevEnvQuad(TevSrc colour, uint8_t r = 255, uint8_t g = 255, uint8_t b = 255, uint8_t a = 255);
// The sequels' wave-grid water (the GL kWaterGridFragmentSrc before the fog): TEXTURE0 the base
// layer, TEXTURE1 the shine layer, rgb = INTERPOLATE(base, shine, base.a) = mix(shine, base,
// base.a), alpha = the vertex alpha (weight * opacity).
TevConfig tevWaterGrid();
// Model shadows: constant black, alpha = texture alpha * alpha/255 (default 0.4).
TevConfig tevShadow(uint8_t alpha = 102);

#if defined(__3DS__)
// Writes `cfg` to the citro3d texture environment: stages 0..count-1 from the config, the
// stages the previous call used beyond count reset to pass-through.
void applyTev(const TevConfig& cfg);
#endif

} // namespace as3d::pica
