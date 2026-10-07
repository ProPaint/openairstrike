// Lightning bolts, GL part (geometry: engine/src/render/lightning_build.cpp),
// docs/spec/render-pipeline.md 7.1.
#include "as3d/lightning_render.h"

#include <GLES3/gl3.h>

#include <cmath>
#include <cstddef>
#include <vector>

#include "as3d/gfx.h"

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

} // namespace


struct LightningRenderer::Impl {
    ShaderProgram program;
    VertexBuffer vbo;
    VertexArray vao;
    std::vector<LightningVertex> verts;
    std::vector<DrawRun> runs;
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
    im.runs.clear();
    lastBolts_ = buildLightningBolts(starts, ends, count, params.time, texture, im.verts, im.runs);
    if (im.runs.empty()) return;
    im.vbo.upload(im.verts.data(), im.verts.size() * sizeof(LightningVertex), true);
    im.program.use();
    im.program.setMat4("uView", params.view);
    im.program.setMat4("uProj", params.projection);
    im.program.setFloat("uFogStart", params.fogStart);
    im.program.setFloat("uFogEnd", params.fogEnd);
    im.program.setInt("uTex", 0);
    im.vao.bind();
    for (const DrawRun& r : im.runs) {
        r.texture->bind(0);
        applyMaterialState(r.key);
        glDrawArrays(GL_TRIANGLES, r.first, r.count);
    }
    glDisable(GL_BLEND);
    setDepth(true, true);
    setCull(CullMode::Back);
}

} // namespace as3d
