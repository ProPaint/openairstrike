// Fog lookup table for the PICA200 fog unit. Platform neutral (engine/src/render/fog_lut.cpp);
// the 3DS backend binds it in engine/src/render/pica/fog_pica.cpp.
//
// Depth convention (checked against citro3d 1.7 source, M1.3): the projection is citro3d's
// Mtx_PerspTilt / Mtx_PerspStereoTilt, whose clip z runs from -w at the near plane to 0 at the
// far plane (z_ndc = -1 at near, 0 at far). The fog unit indexes its table by the fragment's
// depth value, the same value the depth buffer stores after C3D_DepthMap(zBuffer, scale,
// offset); with citro3d's default C3D_DepthMap(true, -1.0, 0.0) (ZBuffer below) that is
//     d = -z_ndc = n (f - z) / (z (f - n))         d(n) = 1, d(f) = 0
//     z(d) = n f / (d (f - n) + n)                 = citro3d's FogLut_CalcZ(d, n, f)
// for eye distance z (> 0 in front of the camera), near n, far f. Depth is 1 at the NEAR
// plane: the depth test is GPU_GREATER and the depth buffer clears to 0.
// With a W-buffer, C3D_DepthMap(false, -1/n, 0) (WBuffer below), the depth is linear in the
// eye distance (as Citra/Azahar emulate it: depth = z_clip * scale + offset * w):
//     d = (f - z) / (f - n)                        z(d) = f - d (f - n)
// The Z-buffer squeezes the whole fog range of a level into the first few table entries
// (n = 4, f = 2000: z >= 100 is d < 0.038, i.e. entries 0..4), so the linear interpolation
// between entries is off by up to ~0.2 there; the W-buffer spreads it over the table.
//
// Table (citro3d FogLut_FromArray input, 256 floats; with C3D_FogGasMode(GPU_FOG, ...,
// zFlip = false)): the hardware takes index = d * 128, i = min(floor(index), 127),
// frac = index - i and computes clamp(out[i] + out[128 + i] * frac, 0, 1). So out[i] (i =
// 0..127) is the fog factor at depth i/128 and out[128 + i] = factor((i+1)/128) - factor(i/128),
// as citro3d's FogLut_Exp builds them. Entry 0 is the FAR plane.
// Fog factor: 1 = no fog, 0 = fully fogged; clamp((fogEnd - z)/(fogEnd - fogStart), 0, 1),
// the same as the GLES shaders (uFogStart/uFogEnd). The unit computes
// mix(fogColour, colour, factor) on the TEV output.
#pragma once

namespace as3d {

enum class FogDepthMap { ZBuffer, WBuffer };

float fogDepthFromEye(float nearPlane, float farPlane, float eyeZ, FogDepthMap map = FogDepthMap::ZBuffer);
float fogEyeFromDepth(float nearPlane, float farPlane, float depth, FogDepthMap map = FogDepthMap::ZBuffer);
float fogFactorAtEyeDepth(float fogStart, float fogEnd, float eyeZ);
// out[0..127] factors at depth i/128, out[128..255] the differences to the next entry.
void buildLinearFogLut(float nearPlane, float farPlane, float fogStart, float fogEnd, float out[256],
                       FogDepthMap map = FogDepthMap::ZBuffer);
// What the fog unit computes from `lut` at `depth` (see above), for tests and comparisons.
float fogLutLookup(const float lut[256], float depth);

} // namespace as3d
