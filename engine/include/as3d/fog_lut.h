// Fog lookup table for the PICA200 fog unit. Platform neutral.
//
// Depth mapping assumed (to be verified by M1.3 against C3D_DepthMap(true, -1.0, 0.0)):
// with eye distance z > 0 (in front of the camera), near n, far f, the depth value the fog
// unit indexes by is
//     d(z) = f/(f-n) * (1 - n/z)        d(n) = 0, d(f) = 1
// i.e. the standard perspective z/w mapped to [0,1], 0 at the near plane. The inverse is
//     z(d) = n*f / (f - d*(f-n))
// Entry i (0..127) holds the fog factor at depth d = i/128 (the hardware interpolates between
// entries). If the hardware depth turns out inverted (1 at near), reverse the table.
// Fog factor: 1 = no fog, 0 = fully fogged; clamp((fogEnd - z)/(fogEnd - fogStart), 0, 1),
// the same as the GLES shaders (uFogStart/uFogEnd).
#pragma once

namespace as3d {

float fogDepthFromEye(float nearPlane, float farPlane, float eyeZ);
float fogEyeFromDepth(float nearPlane, float farPlane, float depth);
float fogFactorAtEyeDepth(float fogStart, float fogEnd, float eyeZ);
void buildLinearFogLut(float nearPlane, float farPlane, float fogStart, float fogEnd, float out[128]);

} // namespace as3d
