// GLSL ES 3.00 sources for the terrain, tile-overlay and water passes.
namespace as3d {

// Terrain: base * vertex colour, detail combined GL_ADD_SIGNED style (base + detail - 0.5).
extern const char* const kTerrainVertexSrc;
extern const char* const kTerrainFragmentSrc;
extern const char* const kWaterVertexSrc;
extern const char* const kWaterFragmentSrc;
extern const char* const kWaterGridVertexSrc;
extern const char* const kWaterGridFragmentSrc;

const char* const kTerrainVertexSrc = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 2) in vec2 aUv0;
layout(location = 3) in vec2 aUv1;
layout(location = 4) in vec3 aNormal;
uniform mat4 uView;
uniform mat4 uProj;
uniform int uLightCount;                 // dynamic lights, render-pipeline.md 2.4
uniform vec4 uLightPosRadius[32];
uniform vec3 uLightColour[32];
uniform vec4 uLightDirSpot[32];          // xyz spot direction, w = 1 for spot lights
uniform vec2 uLightCos[32];              // outer, inner cosine
out vec3 vColor;
out vec2 vUv0;
out vec2 vUv1;
out float vDepth;
void main() {
    vec4 eye = uView * vec4(aPos, 1.0);
    vDepth = -eye.z;
    vec3 col = aColor;
    for (int i = 0; i < uLightCount; i++) {
        vec3 v = uLightPosRadius[i].xyz - aPos;
        float radius = uLightPosRadius[i].w;
        float dist = length(v);
        if (radius <= 0.0 || dist > radius) continue;
        if (dist > 0.0) v /= dist;
        float spot = 1.0;
        if (uLightDirSpot[i].w > 0.5) {
            float c = dot(-v, uLightDirSpot[i].xyz);
            if (c < uLightCos[i].x) continue;
            if (c < uLightCos[i].y) spot = (c - uLightCos[i].x) / (uLightCos[i].y - uLightCos[i].x);
        }
        float nd = dot(aNormal, v);
        if (nd <= 0.0) continue;
        col += uLightColour[i] * ((0.7 * nd + 0.3) * (radius - dist) / radius * spot);
    }
    vColor = min(col, vec3(1.0));
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

// The sequels' water grid (as2/render-pipeline.delta.md 12.5, as3d/water.h): the wave of the
// deep vertices, alpha = weight × opacity, two scrolled layers blended by the shine alpha,
// unlit, fogged.
const char* const kWaterGridVertexSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec4 aGrid; // column, row, terrain height, depth weight
uniform mat4 uView;
uniform mat4 uProj;
uniform float uLevel;
uniform float uOpacity;
uniform float uTime;      // game time T
uniform float uLastRow;   // H: the first and last map rows are not animated
uniform vec2 uBaseOffset;
uniform vec2 uShineOffset;
out vec2 vUvBase;
out vec2 vUvShine;
out float vAlpha;
out float vDepth;
void main() {
    float c = aGrid.x, r = aGrid.y, w = aGrid.w;
    float z = aGrid.z;
    if (w >= 0.0001 && r > 0.5 && r < uLastRow - 0.5)
        z = uLevel + 16.0 * w * 0.5 * (sin(0.75 * r + uTime) + sin(c + uTime));
    vec4 eye = uView * vec4(c * 40.0, r * 40.0, z, 1.0);
    vDepth = -eye.z;
    vec2 cr = vec2(c, r) * 0.25;
    vUvBase = 2.0 * cr + uBaseOffset;
    vUvShine = 1.5 * cr + uShineOffset;
    vAlpha = w * uOpacity;
    gl_Position = uProj * eye;
}
)";

const char* const kWaterGridFragmentSrc = R"(#version 300 es
precision highp float;
in vec2 vUvBase;
in vec2 vUvShine;
in float vAlpha;
in float vDepth;
uniform sampler2D uBase;
uniform sampler2D uShine;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
out vec4 fragColor;
void main() {
    vec4 b = texture(uBase, vUvBase);
    vec4 s = texture(uShine, vUvShine);
    vec3 rgb = mix(b.rgb, s.rgb, s.a);
    float f = clamp((uFogEnd - vDepth) / max(uFogEnd - uFogStart, 1e-3), 0.0, 1.0);
    fragColor = vec4(mix(uFogColor, rgb, f), vAlpha);
}
)";

} // namespace as3d
