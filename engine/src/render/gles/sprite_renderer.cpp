// Sprite quads, docs/spec/render-pipeline.md 3.3.
#include "as3d/sprite_render.h"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>

namespace as3d {

namespace {

struct SpriteVertex {
    float x, y, z;
    float u, v;
    float r, g, b, a;
};

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

void spriteBillboardAxes(const Mat4& view, Vec3& right, Vec3& up) {
    right = {view.at(0, 0), view.at(1, 0), view.at(2, 0)};
    up = {view.at(0, 1), view.at(1, 1), view.at(2, 1)};
}

void spriteUvs(const SpriteInstance& sp, Vec2 uv[4]) {
    float s0 = sp.minS, s1 = sp.maxS, t0 = sp.minT, t1 = sp.maxT;
    if (sp.frameCols > 0) {
        int cols = sp.frameCols, rows = std::max(sp.frameRows, 1);
        float du = 1.0f / static_cast<float>(cols), dv = 1.0f / static_cast<float>(rows);
        int f = std::max(sp.frame, 0);
        s0 = static_cast<float>(f % cols) * du;
        s1 = s0 + du;
        t0 = 1.0f - dv - static_cast<float>(f / cols) * dv;
        t1 = t0 + dv;
    }
    // Original t counts from the bottom of the picture; the engine's v from the top.
    uv[0] = {s0, 1.0f - t0};
    uv[1] = {s1, 1.0f - t0};
    uv[2] = {s1, 1.0f - t1};
    uv[3] = {s0, 1.0f - t1};
}

SpriteQuad buildSpriteQuad(const SpriteInstance& sp, const Vec3& right, const Vec3& up) {
    SpriteQuad q;
    spriteUvs(sp, q.uv);
    const float lx[4] = {sp.minX, sp.maxX, sp.maxX, sp.minX};
    const float ly[4] = {sp.minY, sp.minY, sp.maxY, sp.maxY};
    float a = degToRad(sp.yawDegrees);
    float c = std::cos(a), s = std::sin(a);
    float k = sp.scale > 0.001f ? sp.scale : 1.0f;
    for (int i = 0; i < 4; i++) {
        switch (sp.kind) {
            case SpriteKind::Billboard: {
                float x = (lx[i] * c - ly[i] * s) * k;
                float y = (lx[i] * s + ly[i] * c) * k;
                q.pos[i] = sp.origin + right * x + up * y;
                break;
            }
            case SpriteKind::Horizontal: // quad in the XY plane, turned about Z
                q.pos[i] = sp.origin + Vec3{lx[i] * c - ly[i] * s, lx[i] * s + ly[i] * c, 0.0f};
                break;
            case SpriteKind::Vertical: // vertex (x, 0, y) turned about Z: local y is height
                q.pos[i] = sp.origin + Vec3{lx[i] * c, lx[i] * s, ly[i]};
                break;
        }
    }
    return q;
}

struct SpriteRenderer::Impl {
    ShaderProgram program;
    VertexBuffer vbo;
    VertexArray vao;
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
    count = std::min(count, kMaxSprites);
    Vec3 right, up;
    spriteBillboardAxes(params.view, right, up);

    std::vector<SpriteVertex> verts;
    verts.reserve(count * 6);
    for (size_t i = 0; i < count; i++) {
        const SpriteInstance& sp = sprites[i];
        SpriteQuad q = buildSpriteQuad(sp, right, up);
        const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int k : order)
            verts.push_back({q.pos[k].x, q.pos[k].y, q.pos[k].z, q.uv[k].x, q.uv[k].y, sp.colour.x, sp.colour.y,
                             sp.colour.z, sp.colour.w});
    }
    im.vbo.upload(verts.data(), verts.size() * sizeof(SpriteVertex), true);

    im.program.use();
    im.program.setMat4("uView", params.view);
    im.program.setMat4("uProj", params.projection);
    im.program.setFloat("uFogStart", params.fogStart);
    im.program.setFloat("uFogEnd", params.fogEnd);
    im.program.setInt("uTex", 0);
    im.vao.bind();
    setCull(params.cullBackFaces ? CullMode::Back : CullMode::Off); // as2 delta 3.3: the sequels draw sprites unculled
    for (size_t i = 0; i < count; i++) {
        const SpriteInstance& sp = sprites[i];
        if (!sp.texture) continue;
        Vec3 fog = params.fogColour;
        switch (sp.blend) {
            case DecalBlend::None: glDisable(GL_BLEND); break;
            case DecalBlend::Alpha: glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
            case DecalBlend::Add: glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE); fog = {0, 0, 0}; break;
            case DecalBlend::Filter: glEnable(GL_BLEND); glBlendFunc(GL_DST_COLOR, GL_ZERO); fog = {1, 1, 1}; break;
        }
        setDepth(!sp.noDepthTest, !sp.noDepthWrite);
        im.program.setVec3("uFogColor", fog);
        sp.texture->bind(0);
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(i * 6), 6);
        lastSprites_++;
    }
    glDisable(GL_BLEND);
    setDepth(true, true);
}

} // namespace as3d
