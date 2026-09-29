// GLSL ES 3.00 shader source for as3d::MeshRenderer (as3d/scene.h). Per-vertex Lambert
// lighting from the sun plus a flat ambient term, texture modulation, vertex colour
// (Material::tint * the draw's own colour, combined by MeshRenderer::submit into one
// "uColor" uniform), linear fog, and an optional alpha test for cutout materials
// (Material::alphaTest, see material.cpp). See docs/graphics.md, "Material state
// table", for how Material's flags map to the uniforms set here.
#include "shaders.h"

namespace as3d {
namespace shaders {

const char* const kMeshVertexSrc = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

uniform mat4 uModel;
uniform mat4 uViewProj;
uniform mat3 uNormalMat;

out vec3 vNormal;
out vec2 vUv;
out vec3 vWorldPos;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = uNormalMat * aNormal;
    vUv = aUv;
    gl_Position = uViewProj * worldPos;
}
)";

const char* const kMeshFragmentSrc = R"(#version 300 es
precision mediump float;

in vec3 vNormal;
in vec2 vUv;
in vec3 vWorldPos;

uniform sampler2D uTex;
uniform vec3 uSunDir;      // points TOWARD the sun; used directly as the Lambert light vector
uniform vec3 uSunColor;
uniform vec3 uAmbientColor;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform vec3 uCameraPos;
uniform vec4 uColor;       // Material::tint * the draw's own colour
uniform bool uNoLighting;  // RF_NOLIGHTING
uniform bool uAlphaTest;   // Material::alphaTest

out vec4 fragColor;

void main() {
    vec4 texColor = texture(uTex, vUv);
    if (uAlphaTest && texColor.a < 0.5) discard;

    vec3 lit;
    if (uNoLighting) {
        lit = vec3(1.0);
    } else {
        vec3 n = normalize(vNormal);
        float diff = max(dot(n, normalize(uSunDir)), 0.0);
        lit = uAmbientColor + uSunColor * diff;
    }

    vec4 color = vec4(texColor.rgb * lit, texColor.a) * uColor;

    float dist = length(uCameraPos - vWorldPos);
    float fogRange = max(uFogEnd - uFogStart, 0.0001);
    float fogFactor = clamp((uFogEnd - dist) / fogRange, 0.0, 1.0);
    vec3 finalRgb = mix(uFogColor, color.rgb, fogFactor);

    fragColor = vec4(finalRgb, color.a);
}
)";

} // namespace shaders
} // namespace as3d
