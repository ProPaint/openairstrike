// GLES backend of the 2D quad batcher: shader, vertex buffer and flush(). See as3d/ui.h.
#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "as3d/gfx.h"
#include "as3d/batch2d.h"
#include "as3d/ui.h"

namespace as3d::ui {

namespace {

const char* kVertexSrc = R"(#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec2 aLocal;
layout(location = 4) in vec2 aShape; // hole radius, feather (fractions of the radius)
layout(location = 5) in vec2 aUv2;   // second texture coordinates
uniform vec2 uInvHalfSize;
out vec2 vUv;
out vec4 vColor;
out vec2 vLocal;
out vec2 vShape;
out vec2 vUv2;
void main() {
    gl_Position = vec4(aPos.x * uInvHalfSize.x - 1.0, 1.0 - aPos.y * uInvHalfSize.y, 0.0, 1.0);
    vUv = aUv;
    vColor = aColor;
    vLocal = aLocal;
    vShape = aShape;
    vUv2 = aUv2;
}
)";

const char* kFragmentSrc = R"(#version 300 es
precision mediump float;
uniform sampler2D uTex;
uniform sampler2D uTex2;
in vec2 vUv;
in vec4 vColor;
in highp vec2 vLocal;
in vec2 vShape; // hole radius, feather
in vec2 vUv2;
uniform vec3 uFlags; // ellipse flag, scale rgb too (Add), combine mode of texture2 (0 = none)
out vec4 oColor;
void main() {
    vec4 c = texture(uTex, vUv) * vColor;
    if (uFlags.z > 0.5) {
        // Second stage of the sequels' 2D list (as2 render-pipeline.delta.md 8.2).
        vec4 t2 = texture(uTex2, vUv2);
        if (uFlags.z < 1.5) c.rgb = mix(c.rgb, t2.rgb, t2.a);
        else if (uFlags.z < 2.5) c.rgb = c.rgb + t2.rgb;
        else if (uFlags.z < 3.5) c = c * t2;
        else c = t2;
    }
    if (uFlags.x > 0.5) {
        // Distance from the centre in radii; one pixel of anti-aliasing plus the feather.
        highp float d = length(vLocal);
        float aa = max(fwidth(d), 1e-4) + vShape.y;
        float cov = 1.0 - smoothstep(1.0 - aa, 1.0, d);
        if (vShape.x > 0.0) cov *= smoothstep(vShape.x - aa, vShape.x, d);
        c.a *= cov;
        if (uFlags.y > 0.5) c.rgb *= cov;
    }
    oColor = c;
}
)";

} // namespace

struct Renderer2D::Impl {
    ShaderProgram program;
    VertexBuffer vbo;
    VertexArray vao;
    Texture2D white;
    std::vector<Vertex2D> verts;
    std::vector<Run2D> runs;
};

Renderer2D::Renderer2D() : impl_(new Impl) { mapping_ = computeMapping(800, 600); }
Renderer2D::~Renderer2D() = default;

bool Renderer2D::init(std::string* error) {
    Impl& im = *impl_;
    if (!im.program.compile(kVertexSrc, kFragmentSrc, error)) return false;
    Image px;
    px.width = px.height = 1;
    px.hasAlpha = true;
    px.rgba = {255, 255, 255, 255};
    TextureOptions to;
    to.minFilter = to.magFilter = Filter::Nearest;
    to.wrapS = to.wrapT = Wrap::ClampToEdge;
    im.white.create(px, to);
    float dummy[8] = {};
    im.vbo.upload(dummy, sizeof dummy, true);
    VertexLayout layout;
    layout.strideBytes = sizeof(Vertex2D);
    layout.attribs = {{0, 2, offsetof(Vertex2D, x), false},     {1, 2, offsetof(Vertex2D, u), false},
                     {2, 4, offsetof(Vertex2D, color), false}, {3, 2, offsetof(Vertex2D, lx), false},
                     {4, 2, offsetof(Vertex2D, inner), false}, {5, 2, offsetof(Vertex2D, u2), false}};
    im.vao.create(im.vbo, layout);
    return true;
}

int Renderer2D::flush() {
    Impl& im = *impl_;
    if (quads_.empty() || !im.program.valid()) return 0;

    const Mapping& m = mapping_;
    Batch2DOptions opts;
    opts.halfPixelShift = halfPixelShift_;
    opts.tessellateShapes = tessellate_;
    opts.circleSegments = segments_;
    buildBatch2D(quads_, m, im.verts, im.runs, opts);
    im.vbo.upload(im.verts.data(), im.verts.size() * sizeof(Vertex2D), true);

    glViewport(0, 0, m.fbWidth, m.fbHeight);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    im.program.use();
    im.program.setVec2("uInvHalfSize", {2.0f / static_cast<float>(m.fbWidth), 2.0f / static_cast<float>(m.fbHeight)});
    im.program.setInt("uTex", 0);
    im.program.setInt("uTex2", 1);
    im.vao.bind();

    int calls = 0;
    for (const Run2D& run : im.runs) {
        const Texture2D* tex = run.tex ? run.tex : &im.white;
        const Texture2D* tex2 = run.tex2 ? run.tex2 : &im.white;
        switch (run.blend) {
            case Blend::Alpha:
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
                break;
            case Blend::Add:
                glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
                break;
            case Blend::Filter:
                glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE);
                break;
            case Blend::Opaque:
                glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ONE);
                break;
        }
        im.program.setVec3("uFlags", {run.shape ? 1.0f : 0.0f, run.blend == Blend::Add ? 1.0f : 0.0f,
                                      static_cast<float>(run.combine2)});
        // Lines and untextured quads use the white texture; a texture that failed to load is
        // skipped by callers (they pass null only for untextured quads).
        tex2->bind(1);
        tex->bind(0);
        glDrawArrays(GL_TRIANGLES, run.first, run.count);
        calls++;
    }
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    return calls;
}

} // namespace as3d::ui
