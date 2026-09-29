// Screen mapping and the 2D quad batcher. See as3d/ui.h.
#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>

#include "as3d/ui.h"

namespace as3d::ui {

Color pulse(float f, float phi, float mt) {
    const float v = 0.5f + 0.5f * std::sin(f * 3.14159265f * mt - phi);
    return {v, v, v, 1.0f};
}

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

void Mapping::fieldPixels(int& x, int& y, int& w, int& h) const {
    const int x0 = std::max(0, static_cast<int>(std::lround(toFbX(0))));
    const int y0 = std::max(0, static_cast<int>(std::lround(toFbY(0))));
    const int x1 = std::min(fbWidth, static_cast<int>(std::lround(toFbX(kVirtualWidth))));
    const int y1 = std::min(fbHeight, static_cast<int>(std::lround(toFbY(kVirtualHeight))));
    x = x0;
    y = y0;
    w = std::max(x1 - x0, 0);
    h = std::max(y1 - y0, 0);
}

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

bool Renderer2D::quadSpec(float x, float y, float w, float h, float s0, float t0, float s1, float t1,
                          const Texture2D* tex, Color c, Blend blend) {
    // Spec convention: top-left samples (s0, t1) with t = 1 at the top of the image; ours has
    // v = 0 at the top, so v = 1 - t.
    return quad(x, y, w, h, s0, 1.0f - t1, s1, 1.0f - t0, tex, c, blend);
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

bool Renderer2D::circle(float cx, float cy, float radius, Color c, Blend blend, float feather) {
    return ring(cx, cy, radius, radius, c, blend, feather);
}

bool Renderer2D::ring(float cx, float cy, float radius, float thickness, Color c, Blend blend, float feather) {
    if (!(radius > 0)) return false;
    // Round on screen: the horizontal radius in virtual pixels follows the mapping's aspect.
    const float rx = radius * mapping_.scaleY / mapping_.scaleX;
    Quad q;
    q.x = cx - rx;
    q.y = cy - radius;
    q.w = 2 * rx;
    q.h = 2 * radius;
    q.color = c;
    q.blend = blend;
    q.shape = QuadShape::Ellipse;
    q.inner = thickness >= radius ? 0.0f : std::max(0.0f, (radius - thickness) / radius);
    q.feather = std::max(feather, 0.0f) / radius;
    return add(q);
}

bool Renderer2D::fullscreen(Color c, Blend blend) {
    return rect(mapping_.left(), mapping_.top(), mapping_.right() - mapping_.left(),
                mapping_.bottom() - mapping_.top(), c, blend);
}

void Renderer2D::quadCorners(const Quad& q, float px[4], float py[4]) const {
    const Mapping& m = mapping_;
    // Virtual corners: top-left, bottom-left, bottom-right, top-right (a line: its two ends
    // in slots 0 and 2).
    float vx[4] = {q.x, q.x, q.x + q.w, q.x + q.w};
    float vy[4] = {q.y, q.y + q.h, q.y + q.h, q.y};
    if (!q.line && q.rotation != 0.0f) {
        const float a = q.rotation * 3.14159265f / 180.0f;
        const float ca = std::cos(a), sa = std::sin(a);
        const float cx = q.x + q.w * 0.5f, cy = q.y + q.h * 0.5f;
        for (int i = 0; i < 4; i++) {
            const float dx = vx[i] - cx, dy = vy[i] - cy;
            vx[i] = cx + dx * ca - dy * sa;
            vy[i] = cy + dx * sa + dy * ca;
        }
    }
    if (halfPixelShift_) {
        for (int i = 0; i < 4; i++) {
            vx[i] -= 0.5f;
            vy[i] -= 0.5f;
        }
    }
    if (q.line) {
        float x0 = m.toFbX(vx[0]), y0 = m.toFbY(vy[0]), x1 = m.toFbX(vx[2]), y1 = m.toFbY(vy[2]);
        float dx = x1 - x0, dy = y1 - y0;
        float len = std::sqrt(dx * dx + dy * dy);
        float nx = 0, ny = 0;
        if (len > 0) { nx = -dy / len * 0.5f; ny = dx / len * 0.5f; }
        px[0] = x0 + nx; py[0] = y0 + ny;
        px[1] = x0 - nx; py[1] = y0 - ny;
        px[2] = x1 - nx; py[2] = y1 - ny;
        px[3] = x1 + nx; py[3] = y1 + ny;
        return;
    }
    for (int i = 0; i < 4; i++) {
        px[i] = m.toFbX(vx[i]);
        py[i] = m.toFbY(vy[i]);
    }
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
