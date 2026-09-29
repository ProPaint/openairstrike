// Lightning bolts, docs/spec/render-pipeline.md 7.1.
#include "as3d/lightning_render.h"

#include <GLES3/gl3.h>

#include <cmath>
#include <cstddef>
#include <vector>

namespace as3d {

namespace {

const char* const kLightningVertexSrc = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
uniform mat4 uView;
uniform mat4 uProj;
out vec2 vUv;
out float vDepth;
void main() {
    vec4 eye = uView * vec4(aPos, 1.0);
    vDepth = -eye.z;
    vUv = aUv;
    gl_Position = uProj * eye;
}
)";

// Colour white, additive: the fog colour is black, so fog only fades the bolt.
const char* const kLightningFragmentSrc = R"(#version 300 es
precision highp float;
in vec2 vUv;
in float vDepth;
uniform sampler2D uTex;
uniform float uFogStart;
uniform float uFogEnd;
out vec4 fragColor;
void main() {
    vec4 c = texture(uTex, vUv);
    float f = clamp((uFogEnd - vDepth) / max(uFogEnd - uFogStart, 1e-3), 0.0, 1.0);
    fragColor = vec4(c.rgb * f, c.a);
}
)";

constexpr float kHalfWidth = 8.0f;
constexpr float kUnitsPerRepeat = 96.0f;
constexpr float kScrollPerSecond = 3.0f;

} // namespace

Vec3 perpendicularVector(const Vec3& d) {
    const float a[3] = {std::fabs(d.x), std::fabs(d.y), std::fabs(d.z)};
    int k = 0;
    if (a[1] < a[k]) k = 1;
    if (a[2] < a[k]) k = 2;
    Vec3 axis{k == 0 ? 1.0f : 0.0f, k == 1 ? 1.0f : 0.0f, k == 2 ? 1.0f : 0.0f};
    Vec3 p = axis - d * dot(axis, d);
    float len = length(p);
    return len > 1e-12f ? p * (1.0f / len) : axis;
}

bool buildLightningQuads(const Vec3& start, const Vec3& end, float time, LightningVertex out[8]) {
    Vec3 d = end - start;
    float len = length(d);
    if (!(len > 1e-6f) || !std::isfinite(len)) return false;
    Vec3 dn = d * (1.0f / len);
    Vec3 p1 = perpendicularVector(dn);
    Vec3 p2 = cross(dn, p1);
    float s0 = std::fmod(time * kScrollPerSecond, 1.0f); // repeat wrap: only the fraction matters
    float s1 = s0 + len / kUnitsPerRepeat;
    const Vec3 ps[2] = {p1, p2};
    for (int q = 0; q < 2; ++q) {
        Vec3 o = ps[q] * kHalfWidth;
        LightningVertex* v = out + 4 * q;
        v[0] = {start - o, {s0, 1.0f}};
        v[1] = {end - o, {s1, 1.0f}};
        v[2] = {end + o, {s1, 0.0f}};
        v[3] = {start + o, {s0, 0.0f}};
    }
    return true;
}

struct LightningRenderer::Impl {
    ShaderProgram program;
    VertexBuffer vbo;
    VertexArray vao;
    std::vector<LightningVertex> verts;
};

LightningRenderer::LightningRenderer() : impl_(new Impl) {}
LightningRenderer::~LightningRenderer() = default;

bool LightningRenderer::init(std::string* error) {
    Impl& im = *impl_;
    if (im.program.valid()) return true;
    if (!im.program.compile(kLightningVertexSrc, kLightningFragmentSrc, error)) return false;
    LightningVertex dummy[6] = {};
    im.vbo.upload(dummy, sizeof(dummy), true);
    VertexLayout l;
    l.strideBytes = sizeof(LightningVertex);
    l.attribs = {{0, 3, offsetof(LightningVertex, pos), false}, {1, 2, offsetof(LightningVertex, uv), false}};
    im.vao.create(im.vbo, l);
    return true;
}

void LightningRenderer::draw(const Vec3* starts, const Vec3* ends, size_t count, const Texture2D* texture,
                             const LightningViewParams& params) {
    Impl& im = *impl_;
    lastBolts_ = 0;
    if (!im.program.valid() || count == 0 || !texture) return;
    im.verts.clear();
    for (size_t i = 0; i < count; ++i) {
        LightningVertex q[8];
        if (!buildLightningQuads(starts[i], ends[i], params.time, q)) continue;
        const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int k = 0; k < 2; ++k)
            for (int j : order) im.verts.push_back(q[4 * k + j]);
        ++lastBolts_;
    }
    if (im.verts.empty()) return;
    im.vbo.upload(im.verts.data(), im.verts.size() * sizeof(LightningVertex), true);
    im.program.use();
    im.program.setMat4("uView", params.view);
    im.program.setMat4("uProj", params.projection);
    im.program.setFloat("uFogStart", params.fogStart);
    im.program.setFloat("uFogEnd", params.fogEnd);
    im.program.setInt("uTex", 0);
    im.vao.bind();
    texture->bind(0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    setDepth(false, false);
    setCull(CullMode::Off);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(im.verts.size()));
    glDisable(GL_BLEND);
    setDepth(true, true);
    setCull(CullMode::Back);
}

} // namespace as3d
