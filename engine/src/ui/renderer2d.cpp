// Screen mapping and the 2D quad batcher. See as3d/ui.h.
#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>

#include "as3d/ui.h"

namespace as3d::ui {

Mapping computeMapping(int fbWidth, int fbHeight) {
    Mapping m;
    m.fbWidth = std::max(fbWidth, 1);
    m.fbHeight = std::max(fbHeight, 1);
    const float w = static_cast<float>(m.fbWidth), h = static_cast<float>(m.fbHeight);
    const float aspect = w / h;
    if (aspect >= 4.0f / 3.0f - 1e-4f) {
        m.scaleX = m.scaleY = h / kVirtualHeight;
        m.offsetX = (w - kVirtualWidth * m.scaleX) * 0.5f;
    } else if (aspect >= 1.25f) {
        m.scaleX = w / kVirtualWidth;
        m.scaleY = h / kVirtualHeight;
    } else {
        m.scaleX = m.scaleY = w / kVirtualWidth;
        m.offsetY = (h - kVirtualHeight * m.scaleY) * 0.5f;
    }
    return m;
}

namespace {

const char* kVertexSrc = R"(#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
uniform vec2 uInvHalfSize;
out vec2 vUv;
out vec4 vColor;
void main() {
    gl_Position = vec4(aPos.x * uInvHalfSize.x - 1.0, 1.0 - aPos.y * uInvHalfSize.y, 0.0, 1.0);
    vUv = aUv;
    vColor = aColor;
}
)";

const char* kFragmentSrc = R"(#version 300 es
precision mediump float;
uniform sampler2D uTex;
in vec2 vUv;
in vec4 vColor;
out vec4 oColor;
void main() {
    oColor = texture(uTex, vUv) * vColor;
}
)";

struct Vertex {
    float x, y, s, t, r, g, b, a;
};

// Colour actually sent to the GPU: additive quads use alpha as intensity, filter quads fade
// towards white (no change), so one shader with plain "texture * colour" serves all modes.
Color effectiveColor(const Quad& q) {
    Color c = q.color;
    if (q.blend == Blend::Add) {
        c.r *= c.a; c.g *= c.a; c.b *= c.a; c.a = 1.0f;
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
    layout.attribs = {{0, 2, 0, false}, {1, 2, 8, false}, {2, 4, 16, false}};
    im.vao.create(im.vbo, layout);
    return true;
}

void Renderer2D::begin(int fbWidth, int fbHeight) {
    mapping_ = computeMapping(fbWidth, fbHeight);
    quads_.clear();
    dropped_ = 0;
}

bool Renderer2D::add(const Quad& q) {
    if (static_cast<int>(quads_.size()) >= kMaxQuads) {
        dropped_++;
        return false;
    }
    quads_.push_back(q);
    return true;
}

bool Renderer2D::quad(float x, float y, float w, float h, float s0, float t0, float s1, float t1,
                      const Texture2D* tex, Color c, Blend blend) {
    Quad q;
    q.x = x; q.y = y; q.w = w; q.h = h;
    q.s0 = s0; q.t0 = t0; q.s1 = s1; q.t1 = t1;
    q.texture = tex;
    q.color = c;
    q.blend = blend;
    return add(q);
}

bool Renderer2D::pic(float x, float y, const Texture2D& tex, Color c, Blend blend) {
    return quad(x, y, static_cast<float>(tex.width()), static_cast<float>(tex.height()), 0, 0, 1, 1, &tex, c, blend);
}

bool Renderer2D::rect(float x, float y, float w, float h, Color c, Blend blend) {
    return quad(x, y, w, h, 0, 0, 1, 1, nullptr, c, blend);
}

bool Renderer2D::outline(float x, float y, float w, float h, Color c, Blend blend) {
    // One framebuffer pixel thick, in virtual units.
    const float tx = 1.0f / mapping_.scaleX, ty = 1.0f / mapping_.scaleY;
    bool ok = rect(x, y, w, ty, c, blend);
    ok &= rect(x, y + h - ty, w, ty, c, blend);
    ok &= rect(x, y + ty, tx, h - 2 * ty, c, blend);
    ok &= rect(x + w - tx, y + ty, tx, h - 2 * ty, c, blend);
    return ok;
}

bool Renderer2D::line(float x0, float y0, float x1, float y1, Color c, Blend blend) {
    Quad q;
    q.x = x0; q.y = y0; q.w = x1 - x0; q.h = y1 - y0;
    q.color = c;
    q.blend = blend;
    q.line = true;
    return add(q);
}

bool Renderer2D::fullscreen(Color c, Blend blend) {
    return rect(mapping_.left(), mapping_.top(), mapping_.right() - mapping_.left(),
                mapping_.bottom() - mapping_.top(), c, blend);
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
        if (q.line) {
            float x0 = m.toFbX(q.x), y0 = m.toFbY(q.y), x1 = m.toFbX(q.x + q.w), y1 = m.toFbY(q.y + q.h);
            float dx = x1 - x0, dy = y1 - y0;
            float len = std::sqrt(dx * dx + dy * dy);
            float nx = 0, ny = 0;
            if (len > 0) { nx = -dy / len * 0.5f; ny = dx / len * 0.5f; }
            px[0] = x0 + nx; py[0] = y0 + ny;
            px[1] = x0 - nx; py[1] = y0 - ny;
            px[2] = x1 - nx; py[2] = y1 - ny;
            px[3] = x1 + nx; py[3] = y1 + ny;
            for (int i = 0; i < 4; i++) ps[i] = pt[i] = 0.5f;
        } else {
            float x0 = m.toFbX(q.x), y0 = m.toFbY(q.y), x1 = m.toFbX(q.x + q.w), y1 = m.toFbY(q.y + q.h);
            px[0] = x0; py[0] = y0; ps[0] = q.s0; pt[0] = q.t0;
            px[1] = x0; py[1] = y1; ps[1] = q.s0; pt[1] = q.t1;
            px[2] = x1; py[2] = y1; ps[2] = q.s1; pt[2] = q.t1;
            px[3] = x1; py[3] = y0; ps[3] = q.s1; pt[3] = q.t0;
        }
        static const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int k : order) im.verts.push_back({px[k], py[k], ps[k], pt[k], c.r, c.g, c.b, c.a});
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
    im.vao.bind();

    int calls = 0;
    size_t i = 0;
    while (i < quads_.size()) {
        size_t j = i + 1;
        const Texture2D* tex = quads_[i].texture ? quads_[i].texture : &im.white;
        const Blend blend = quads_[i].blend;
        while (j < quads_.size() && quads_[j].blend == blend &&
               (quads_[j].texture ? quads_[j].texture : &im.white) == tex)
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
        }
        // Lines and untextured quads use the white texture; a texture that failed to load is
        // skipped by callers (they pass null only for untextured quads).
        tex->bind(0);
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(i * 6), static_cast<GLsizei>((j - i) * 6));
        calls++;
        i = j;
    }
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    return calls;
}

} // namespace as3d::ui
