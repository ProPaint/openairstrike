// Sprite quads, GL part (geometry and runs: engine/src/render/sprite_build.cpp),
// docs/spec/render-pipeline.md 3.3.
#include "as3d/gfx.h"
#include "as3d/sprite_render.h"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>

namespace as3d {

namespace {

const char* const kSpriteVertexSrc = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColour;
uniform mat4 uView;
uniform mat4 uProj;
out vec2 vUv;
out vec4 vColour;
out float vDepth;
void main() {
    vec4 eye = uView * vec4(aPos, 1.0);
    vDepth = -eye.z;
    vUv = aUv;
    vColour = aColour;
    gl_Position = uProj * eye;
}
)";

const char* const kSpriteFragmentSrc = R"(#version 300 es
precision highp float;
in vec2 vUv;
in vec4 vColour;
in float vDepth;
uniform sampler2D uTex;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
out vec4 fragColor;
void main() {
    vec4 c = texture(uTex, vUv) * vColour;
    float f = clamp((uFogEnd - vDepth) / max(uFogEnd - uFogStart, 1e-3), 0.0, 1.0);
    fragColor = vec4(mix(uFogColor, c.rgb, f), c.a);
}
)";

} // namespace

struct SpriteRenderer::Impl {
    ShaderProgram program;
    VertexBuffer vbo;
    VertexArray vao;
    std::vector<SpriteVertex> verts;
    std::vector<DrawRun> runs;
};

SpriteRenderer::SpriteRenderer() : impl_(new Impl) {}
SpriteRenderer::~SpriteRenderer() = default;

bool SpriteRenderer::init(std::string* error) {
    Impl& im = *impl_;
    if (im.program.valid()) return true;
    if (!im.program.compile(kSpriteVertexSrc, kSpriteFragmentSrc, error)) return false;
    SpriteVertex dummy[6] = {};
    im.vbo.upload(dummy, sizeof(dummy), true);
    VertexLayout l;
    l.strideBytes = sizeof(SpriteVertex);
    l.attribs = {{0, 3, offsetof(SpriteVertex, x), false},
                 {1, 2, offsetof(SpriteVertex, u), false},
                 {2, 4, offsetof(SpriteVertex, r), false}};
    im.vao.create(im.vbo, l);
    return true;
}

void SpriteRenderer::draw(const SpriteInstance* sprites, size_t count, const SpriteViewParams& params) {
    Impl& im = *impl_;
    lastSprites_ = 0;
    if (!im.program.valid() || count == 0) return;

    im.verts.clear();
    im.runs.clear();
    lastSprites_ = buildSpriteBatches(sprites, count, params.view, params.cullBackFaces, im.verts, im.runs);
    if (im.runs.empty()) {
        setCull(params.cullBackFaces ? CullMode::Back : CullMode::Off); // as before: the pass always set the cull mode
        return;
    }
    im.vbo.upload(im.verts.data(), im.verts.size() * sizeof(SpriteVertex), true);

    im.program.use();
    im.program.setMat4("uView", params.view);
    im.program.setMat4("uProj", params.projection);
    im.program.setFloat("uFogStart", params.fogStart);
    im.program.setFloat("uFogEnd", params.fogEnd);
    im.program.setInt("uTex", 0);
    im.vao.bind();
    for (const DrawRun& r : im.runs) {
        applyMaterialState(r.key);
        im.program.setVec3("uFogColor", fogColorFor(r.key.fog, params.fogColour));
        r.texture->bind(0);
        glDrawArrays(GL_TRIANGLES, r.first, r.count);
    }
    glDisable(GL_BLEND);
    setDepth(true, true);
}

} // namespace as3d
