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
uniform vec3 uDynCube[6];  // dynamic lights folded into the ambient cube (CPU, per draw)
uniform int uEnvMode;      // 0 none, 1 glitter, 2 chrome, 3 quad (sphere-mapped env texture)
uniform mat3 uNormalMat;   // inverse transpose of the modelview rotation, not renormalised
uniform float uEnvAngle;   // ENV_QUAD texture rotation, radians
uniform bool uEnvView;     // the sequels' coordinates: 0.5 + 0.5 view-space |normal| (as2 delta 4.3)

out vec3 vLight;
out vec2 vUv;
out vec2 vEnvUv;
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
        cPos[i] = uAmbientColor + max(d, 0.0) * uSunColor + uDynCube[i];
        cNeg[i] = uAmbientColor + max(-d, 0.0) * uSunColor + uDynCube[i + 3];
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

    vEnvUv = vec2(0.0);
    if (uEnvMode != 0) {
        vec2 st;
        if (uEnvView) {
            // The folded normal through the world matrix (with its scale) and the view,
            // (0.5 + 0.5 n.x, 0.5 + 0.5 n.y) used as a v-down coordinate.
            vec3 nv = mat3(uView) * (mat3(uModel) * abs(aNormal));
            st = 0.5 + 0.5 * nv.xy;
        } else {
            // GL_SPHERE_MAP on the folded normal |n| (render-pipeline.md 4.3).
            vec3 u = normalize(eyePos.xyz);
            vec3 ne = uNormalMat * abs(aNormal);
            vec3 r = u - 2.0 * ne * dot(ne, u);
            float m = 2.0 * sqrt(r.x * r.x + r.y * r.y + (r.z + 1.0) * (r.z + 1.0));
            st = r.xy / max(m, 1e-6) + 0.5;
        }
        if (uEnvMode == 3) {
            float ca = cos(uEnvAngle), sa = sin(uEnvAngle);
            st = vec2(ca * st.x - sa * st.y, sa * st.x + ca * st.y);
        }
        vEnvUv = st;
    }
}
)";

const char* const kMeshFragmentSrc = R"(#version 300 es
precision highp float;
precision highp int;

in vec3 vLight;
in vec2 vUv;
in vec2 vEnvUv;
in float vDepth;

uniform sampler2D uTex;
uniform sampler2D uEnv;
uniform int uEnvMode;
uniform vec3 uFogColor;    // level colour; (0,0,0) for BLEND_ADD, (1,1,1) for BLEND_FILTER
uniform float uFogStart;
uniform float uFogEnd;
uniform vec4 uColor;       // entity colour, used only when uNoLighting
uniform bool uNoLighting;  // RF_NOLIGHTING
uniform bool uEnvView;     // the sequels: ENV_GLITTER alpha without the env alpha (as2 delta 4.3)

out vec4 fragColor;

void main() {
    vec4 skin = texture(uTex, vUv);
    // Lit models send vertex alpha 1 (transparency = texture alpha alone); unlit models
    // multiply the texture by the entity RGBA.
    vec4 c = uNoLighting ? uColor : vec4(vLight, 1.0);
    vec4 color;
    if (uEnvMode == 1) {          // ENV_GLITTER: skin * colour + env
        vec4 e = texture(uEnv, vEnvUv);
        color = vec4(skin.rgb * c.rgb + e.rgb, uEnvView ? skin.a * c.a : skin.a * c.a * e.a);
    } else if (uEnvMode == 2) {   // ENV_CHROME: skin over lit env by skin alpha
        vec4 e = texture(uEnv, vEnvUv);
        color = vec4(skin.rgb * skin.a + e.rgb * c.rgb * (1.0 - skin.a), e.a * c.a);
    } else if (uEnvMode == 3) {   // ENV_QUAD: env only
        vec4 e = texture(uEnv, vEnvUv);
        color = e * c;
    } else {
        color = skin * c;
    }

    float fogRange = max(uFogEnd - uFogStart, 0.0001);
    float fogFactor = clamp((uFogEnd - vDepth) / fogRange, 0.0, 1.0);
    fragColor = vec4(mix(uFogColor, color.rgb, fogFactor), color.a);
}
)";

} // namespace shaders
} // namespace as3d
