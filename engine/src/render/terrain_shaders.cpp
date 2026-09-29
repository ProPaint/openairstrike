// GLSL ES 3.00 sources for the terrain, tile-overlay and water passes.
namespace as3d {

// Terrain: base * vertex colour, detail combined GL_ADD_SIGNED style (base + detail - 0.5).
extern const char* const kTerrainVertexSrc;
extern const char* const kTerrainFragmentSrc;
extern const char* const kWaterVertexSrc;
extern const char* const kWaterFragmentSrc;

const char* const kTerrainVertexSrc = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 2) in vec2 aUv0;
layout(location = 3) in vec2 aUv1;
uniform mat4 uView;
uniform mat4 uProj;
out vec3 vColor;
out vec2 vUv0;
out vec2 vUv1;
out float vDepth;
void main() {
    vec4 eye = uView * vec4(aPos, 1.0);
    vDepth = -eye.z;
    vColor = aColor;
    vUv0 = aUv0;
    vUv1 = aUv1;
    gl_Position = uProj * eye;
}
)";

// uMode 0: terrain (base [* detail]); uMode 1: tile overlay (texture rgba * colour).
const char* const kTerrainFragmentSrc = R"(#version 300 es
precision highp float;
in vec3 vColor;
in vec2 vUv0;
in vec2 vUv1;
in float vDepth;
uniform sampler2D uTex0;
uniform sampler2D uTex1;
uniform int uMode;
uniform int uDetail;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
out vec4 fragColor;
void main() {
    vec4 base = texture(uTex0, vUv0);
    vec3 rgb = base.rgb * vColor;
    float alpha = 1.0;
    if (uMode == 0) {
        if (uDetail != 0) rgb = rgb + texture(uTex1, vUv1).rgb - 0.5;
    } else {
        alpha = base.a;
    }
    rgb = clamp(rgb, 0.0, 1.0);
    float f = clamp((uFogEnd - vDepth) / max(uFogEnd - uFogStart, 1e-3), 0.0, 1.0);
    rgb = mix(uFogColor, rgb, f);
    fragColor = vec4(rgb, alpha);
}
)";

const char* const kWaterVertexSrc = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
uniform mat4 uView;
uniform mat4 uProj;
uniform float uPlaneZ;
uniform vec2 uTexOffset;
out vec2 vUv;
out float vDepth;
void main() {
    vec4 eye = uView * vec4(aPos.xy, uPlaneZ, 1.0);
    vDepth = -eye.z;
    vUv = aUv + uTexOffset;
    gl_Position = uProj * eye;
}
)";

const char* const kWaterFragmentSrc = R"(#version 300 es
precision highp float;
in vec2 vUv;
in float vDepth;
uniform sampler2D uTex0;
uniform vec3 uColor;
uniform float uAlpha;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
out vec4 fragColor;
void main() {
    vec3 rgb = clamp(texture(uTex0, vUv).rgb * uColor, 0.0, 1.0);
    float f = clamp((uFogEnd - vDepth) / max(uFogEnd - uFogStart, 1e-3), 0.0, 1.0);
    rgb = mix(uFogColor, rgb, f);
    fragColor = vec4(rgb, uAlpha);
}
)";

} // namespace as3d
