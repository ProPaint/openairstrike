// GLSL ES 3.00 shader source for as3d::MeshRenderer (as3d/scene.h). Follows
// docs/spec/render-pipeline.md: lighting is the original's CPU "ambient cube" evaluated per
// vertex (section 2.3: six colours, one per signed local axis, blended by acos weights of
// the model-space normal), the entity colour only applies to RF_NOLIGHTING models, there is
// no alpha test (section 4.1) and linear fog uses the eye-space depth with a per-blend-mode
// fog colour (section 2.2, 10).
#include "shaders.h"

namespace as3d {
namespace shaders {

const char* const kMeshVertexSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uViewProj;
uniform vec3 uSunDir;      // points TOWARD the sun (normalised here)
uniform vec3 uSunColor;
uniform vec3 uAmbientColor;

out vec3 vLight;
out vec2 vUv;
out float vDepth;

const float kHalfPi = 1.57079632679;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vec4 eyePos = uView * worldPos;
    vDepth = -eyePos.z;
    vUv = aUv;
    gl_Position = uViewProj * worldPos;

    // Ambient cube: colour for +axis i / -axis i, from the world direction of local axis i.
    vec3 L = normalize(uSunDir);
    vec3 cPos[3];
    vec3 cNeg[3];
    for (int i = 0; i < 3; i++) {
        vec3 a = normalize(uModel[i].xyz);
        float d = dot(a, L);
        cPos[i] = uAmbientColor + max(d, 0.0) * uSunColor;
        cNeg[i] = uAmbientColor + max(-d, 0.0) * uSunColor;
    }
    vec3 n = normalize(aNormal + vec3(0.0, 0.0, 1e-9));
    float a = acos(min(abs(n.x), 1.0)) / kHalfPi;
    float b = acos(min(abs(n.z), 1.0)) / kHalfPi;
    float wx = b - a * b;
    float wy = a * b;
    float wz = 1.0 - b;
    vec3 cx = n.x >= 0.0 ? cPos[0] : cNeg[0];
    vec3 cy = n.y >= 0.0 ? cPos[1] : cNeg[1];
    vec3 cz = n.z >= 0.0 ? cPos[2] : cNeg[2];
    vLight = clamp(wx * cx + wy * cy + wz * cz, 0.0, 1.0);
}
)";

const char* const kMeshFragmentSrc = R"(#version 300 es
precision highp float;

in vec3 vLight;
in vec2 vUv;
in float vDepth;

uniform sampler2D uTex;
uniform vec3 uFogColor;    // level colour; (0,0,0) for BLEND_ADD, (1,1,1) for BLEND_FILTER
uniform float uFogStart;
uniform float uFogEnd;
uniform vec4 uColor;       // entity colour, used only when uNoLighting
uniform bool uNoLighting;  // RF_NOLIGHTING

out vec4 fragColor;

void main() {
    vec4 texColor = texture(uTex, vUv);
    // Lit models send vertex alpha 1 (transparency = texture alpha alone); unlit models
    // multiply the texture by the entity RGBA.
    vec4 color = uNoLighting ? texColor * uColor : vec4(texColor.rgb * vLight, texColor.a);

    float fogRange = max(uFogEnd - uFogStart, 0.0001);
    float fogFactor = clamp((uFogEnd - vDepth) / fogRange, 0.0, 1.0);
    fragColor = vec4(mix(uFogColor, color.rgb, fogFactor), color.a);
}
)";

} // namespace shaders
} // namespace as3d
