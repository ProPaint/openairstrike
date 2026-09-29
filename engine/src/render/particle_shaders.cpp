// GLSL ES 3.00 for particles: the "unlit" program of render-pipeline.md section 10.
namespace as3d {

extern const char* const kParticleVertexSrc;
extern const char* const kParticleFragmentSrc;

const char* const kParticleVertexSrc = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
uniform mat4 uView;
uniform mat4 uProj;
out vec2 vUv;
out vec4 vColor;
out float vDepth;
void main() {
    vec4 eye = uView * vec4(aPos, 1.0);
    vDepth = -eye.z;
    vUv = aUv;
    vColor = aColor;
    gl_Position = uProj * eye;
}
)";

// uTexMode 1: modulate (tex * col); 2: add, used by BLEND_FILTER (texture env COMBINE/ADD).
const char* const kParticleFragmentSrc = R"(#version 300 es
precision highp float;
in vec2 vUv;
in vec4 vColor;
in float vDepth;
uniform sampler2D uTex;
uniform int uTexMode;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
out vec4 fragColor;
void main() {
    vec4 t = texture(uTex, vUv);
    vec4 c = clamp(vColor, 0.0, 1.0);
    vec4 o;
    if (uTexMode == 2) o = vec4(clamp(t.rgb + c.rgb, 0.0, 1.0), t.a * c.a);
    else o = t * c;
    float f = clamp((uFogEnd - vDepth) / max(uFogEnd - uFogStart, 1e-3), 0.0, 1.0);
    o.rgb = mix(uFogColor, o.rgb, f);
    fragColor = o;
}
)";

} // namespace as3d
