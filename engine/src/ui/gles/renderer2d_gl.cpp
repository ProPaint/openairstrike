// GLES backend of the 2D quad batcher: shader, vertex buffer and flush(). See as3d/ui.h.
#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>

#include "as3d/ui.h"

namespace as3d::ui {

namespace {

const char* kVertexSrc = R"(#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec2 aLocal;
layout(location = 4) in vec4 aShape;
layout(location = 5) in vec3 aUv2; // second texture coordinates, combine mode (0 = none)
uniform vec2 uInvHalfSize;
out vec2 vUv;
out vec4 vColor;
out vec2 vLocal;
out vec4 vShape;
out vec3 vUv2;
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
in vec4 vShape; // ellipse flag, hole radius, feather (fractions of the radius), scale rgb too
in vec3 vUv2;
out vec4 oColor;
void main() {
    vec4 c = texture(uTex, vUv) * vColor;
    if (vUv2.z > 0.5) {
        // Second stage of the sequels' 2D list (as2 render-pipeline.delta.md 8.2).
        vec4 t2 = texture(uTex2, vUv2.xy);
        if (vUv2.z < 1.5) c.rgb = mix(c.rgb, t2.rgb, t2.a);
        else if (vUv2.z < 2.5) c.rgb = c.rgb + t2.rgb;
        else if (vUv2.z < 3.5) c = c * t2;
        else c = t2;
    }
    if (vShape.x > 0.5) {
        // Distance from the centre in radii; one pixel of anti-aliasing plus the feather.
        highp float d = length(vLocal);
        float aa = max(fwidth(d), 1e-4) + vShape.z;
        float cov = 1.0 - smoothstep(1.0 - aa, 1.0, d);
        if (vShape.y > 0.0) cov *= smoothstep(vShape.y - aa, vShape.y, d);
        c.a *= cov;
        if (vShape.w > 0.5) c.rgb *= cov;
    }
    oColor = c;
}
)";

struct Vertex {
    float x, y, s, t, r, g, b, a;
    float lx, ly;                            // position in the inscribed ellipse, in radii
    float kind, inner, feather, scaleRgb;    // aShape
    float s2, t2, mode2;                     // aUv2
};

// Colour actually sent to the GPU: additive quads use alpha as intensity, filter quads fade
// towards white (no change), so one shader with plain "texture * colour" serves all modes.
Color effectiveColor(const Quad& q) {
    Color c = q.color;
    if (q.blend == Blend::Add) {
        c.r *= c.a; c.g *= c.a; c.b *= c.a; c.a = 1.0f;
    } else if (q.blend == Blend::Opaque) {
        c.a = 1.0f;
    } else if (q.blend == Blend::Filter) {
        c.r = 1.0f + (c.r - 1.0f) * c.a;
        c.g = 1.0f + (c.g - 1.0f) * c.a;
        c.b = 1.0f + (c.b - 1.0f) * c.a;
        c.a = 1.0f;
    }
    return c;
}

} // namespace

struct Renderer2D::Impl {
    ShaderProgram program;
    VertexBuffer vbo;
    VertexArray vao;
    Texture2D white;
    std::vector<Vertex> verts;
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
    layout.strideBytes = sizeof(Vertex);
    layout.attribs = {{0, 2, 0, false}, {1, 2, 8, false}, {2, 4, 16, false}, {3, 2, 32, false}, {4, 4, 40, false},
                     {5, 3, 56, false}};
    im.vao.create(im.vbo, layout);
    return true;
}

int Renderer2D::flush() {
    Impl& im = *impl_;
    if (quads_.empty() || !im.program.valid()) return 0;

    im.verts.clear();
    im.verts.reserve(quads_.size() * 6);
    const Mapping& m = mapping_;
    for (const Quad& q : quads_) {
        const Color c = effectiveColor(q);
        float px[4], py[4], ps[4], pt[4];
        static const float kLx[4] = {-1, -1, 1, 1}, kLy[4] = {-1, 1, 1, -1};
        const float kind = q.shape == QuadShape::Ellipse ? 1.0f : 0.0f;
        const float scaleRgb = q.blend == Blend::Add ? 1.0f : 0.0f;
        quadCorners(q, px, py);
        if (q.line) {
            for (int i = 0; i < 4; i++) ps[i] = pt[i] = 0.5f;
        } else {
            ps[0] = q.s0; pt[0] = q.t0;
            ps[1] = q.s0; pt[1] = q.t1;
            ps[2] = q.s1; pt[2] = q.t1;
            ps[3] = q.s1; pt[3] = q.t0;
        }
        const float ps2[4] = {q.s0b, q.s0b, q.s1b, q.s1b}, pt2[4] = {q.t0b, q.t1b, q.t1b, q.t0b};
        float mode2 = 0.0f;
        if (q.texture2 && q.combine2 != 0) mode2 = (q.combine2 >= 1 && q.combine2 <= 3) ? static_cast<float>(q.combine2) : 4.0f;
        static const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int k : order)
            im.verts.push_back({px[k], py[k], ps[k], pt[k], c.r, c.g, c.b, c.a, kLx[k], kLy[k], kind, q.inner,
                                q.feather, scaleRgb, ps2[k], pt2[k], mode2});
    }
    im.vbo.upload(im.verts.data(), im.verts.size() * sizeof(Vertex), true);

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
    size_t i = 0;
    while (i < quads_.size()) {
        size_t j = i + 1;
        const Texture2D* tex = quads_[i].texture ? quads_[i].texture : &im.white;
        const Texture2D* tex2 = quads_[i].texture2 ? quads_[i].texture2 : &im.white;
        const Blend blend = quads_[i].blend;
        while (j < quads_.size() && quads_[j].blend == blend &&
               (quads_[j].texture ? quads_[j].texture : &im.white) == tex &&
               (quads_[j].texture2 ? quads_[j].texture2 : &im.white) == tex2)
            j++;
        switch (blend) {
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
        // Lines and untextured quads use the white texture; a texture that failed to load is
        // skipped by callers (they pass null only for untextured quads).
        tex2->bind(1);
        tex->bind(0);
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(i * 6), static_cast<GLsizei>((j - i) * 6));
        calls++;
        i = j;
    }
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    return calls;
}

} // namespace as3d::ui
