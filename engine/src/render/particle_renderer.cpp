// Particle billboards. See docs/spec/render-pipeline.md sections 4.1, 6.6 and 10.
#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <vector>

#include "as3d/image.h"
#include "as3d/particle_render.h"

namespace as3d {

extern const char* const kParticleVertexSrc;
extern const char* const kParticleFragmentSrc;

namespace {

struct Vertex {
    float x, y, z;
    float u, v;
    float r, g, b, a;
};

struct Batch {
    const ParticleDesc* desc;
    size_t first; // vertex index
    size_t count;
};

} // namespace

struct ParticleRenderer::Impl {
    Vfs* vfs = nullptr;
    ShaderProgram program;
    VertexBuffer vbo;
    VertexArray vao;
    std::vector<Vertex> verts;
    std::vector<Batch> batches;
    std::unordered_map<std::string, std::unique_ptr<Texture2D>> textures;
    std::unordered_map<std::string, bool> warned;
    Texture2D fallback;

    const Texture2D* texture(const std::string& path, int& missing) {
        std::string key = normalizePath(path);
        auto it = textures.find(key);
        if (it != textures.end()) return it->second.get();
        Blob blob;
        Image image;
        bool ok = vfs && vfs->read(path, blob) && decodeTga(blob.data(), blob.size(), image);
        auto tex = std::make_unique<Texture2D>();
        if (ok) {
            TextureOptions opts;
            opts.mipmaps = true; // particle textures are mipmapped (4.4), repeat wrap
            tex->create(image, opts);
        } else {
            if (!warned[key]) {
                warned[key] = true;
                AS3D_WARN("ParticleRenderer: texture '%s' could not be loaded, using white", path.c_str());
            }
            missing++;
            Image white;
            white.width = white.height = 1;
            white.rgba = {255, 255, 255, 255};
            tex->create(white);
        }
        const Texture2D* raw = tex.get();
        textures[key] = std::move(tex);
        return raw;
    }
};

ParticleRenderer::ParticleRenderer() : impl_(new Impl) {}
ParticleRenderer::~ParticleRenderer() = default;

bool ParticleRenderer::init(Vfs& vfs, std::string* error) {
    Impl& im = *impl_;
    im.vfs = &vfs;
    if (!im.program.compile(kParticleVertexSrc, kParticleFragmentSrc, error)) return false;
    // Placeholder storage so the VAO can be created before the first upload.
    Vertex dummy{};
    im.vbo.upload(&dummy, sizeof dummy, true);
    VertexLayout layout;
    layout.strideBytes = static_cast<int>(sizeof(Vertex));
    layout.attribs = {{0, 3, 0, false}, {1, 2, 3 * sizeof(float), false}, {2, 4, 5 * sizeof(float), false}};
    im.vao.create(im.vbo, layout);
    return true;
}

void ParticleRenderer::billboardAxes(const Mat4& view, Vec3& right, Vec3& up) {
    right = {view.at(0, 0), view.at(1, 0), view.at(2, 0)};
    up = {view.at(0, 1), view.at(1, 1), view.at(2, 1)};
}

void ParticleRenderer::draw(const ParticleEmitter* const* emitters, size_t count, const ParticleViewParams& params) {
    Impl& im = *impl_;
    lastParticles_ = 0;
    if (!im.program.valid()) return;
    Vec3 R, U;
    billboardAxes(params.view, R, U);

    im.verts.clear();
    im.batches.clear();
    for (size_t e = 0; e < count; e++) {
        const ParticleEmitter& em = *emitters[e];
        const ParticleDesc& d = em.desc();
        const size_t first = im.verts.size();
        const float du = 1.0f / static_cast<float>(d.texCols);
        const float dv = 1.0f / static_cast<float>(d.texRows);
        for (const Particle& p : em.particles()) {
            if (!em.isLive(p)) continue;
            const Vec3 P = em.worldPosition(p);
            Vec3 X = R, Y = U;
            if (d.drawMode != ParticleDrawMode::Billboard) {
                int ai = static_cast<int>(std::max(-1.0e7f, std::min(p.angle, 1.0e7f)));
                float a = degToRad(static_cast<float>(ai));
                float c = std::cos(a), s = std::sin(a);
                if (d.drawMode == ParticleDrawMode::Vert) {
                    X = {c, 0, s};
                    Y = {-s, 0, c};
                } else {
                    X = {c, s, 0};
                    Y = {-s, c, 0};
                }
            }
            // Frame cell, formula of the original (6.6): the row divides by rows, not cols.
            const int f = p.frame < 0 ? 0 : p.frame;
            const float u0 = static_cast<float>(f % d.texCols) * du;
            const float v0 = 1.0f - dv - static_cast<float>(f / d.texRows) * dv; // GL convention (v up)
            const float s = p.size;
            const Vec3 sx = X * s, sy = Y * s;
            const Vec3 c0 = P - sx - sy, c1 = P + sx - sy, c2 = P + sx + sy, c3 = P - sx + sy;
            // The engine's textures have v = 0 at the top; the original's have v = 0 at the
            // bottom, so every original t becomes 1 - t.
            auto V = [&](const Vec3& q, float u, float t) {
                return Vertex{q.x, q.y, q.z, u, 1.0f - t, p.rgba[0], p.rgba[1], p.rgba[2], p.rgba[3]};
            };
            const Vertex q0 = V(c0, u0, v0), q1 = V(c1, u0 + du, v0), q2 = V(c2, u0 + du, v0 + dv),
                         q3 = V(c3, u0, v0 + dv);
            im.verts.insert(im.verts.end(), {q0, q1, q2, q0, q2, q3});
            lastParticles_++;
        }
        if (im.verts.size() > first) im.batches.push_back({&d, first, im.verts.size() - first});
    }
    if (im.batches.empty()) return;

    im.vbo.upload(im.verts.data(), im.verts.size() * sizeof(Vertex), true);
    im.program.use();
    im.program.setMat4("uView", params.view);
    im.program.setMat4("uProj", params.projection);
    const bool fogOn = params.fogEnd > params.fogStart;
    im.program.setFloat("uFogStart", fogOn ? params.fogStart : 0.0f);
    im.program.setFloat("uFogEnd", fogOn ? params.fogEnd : 1.0e30f);
    im.program.setInt("uTex", 0);
    im.vao.bind();
    setCull(CullMode::Back);

    for (const Batch& b : im.batches) {
        const ParticleDesc& d = *b.desc;
        Vec3 fog = params.fogColour;
        switch (d.blend) {
        case ParticleBlend::None: glDisable(GL_BLEND); break;
        case ParticleBlend::Alpha:
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case ParticleBlend::Add:
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            fog = {0, 0, 0};
            break;
        case ParticleBlend::Filter:
            glEnable(GL_BLEND);
            glBlendFunc(GL_DST_COLOR, GL_ZERO);
            fog = {1, 1, 1};
            break;
        }
        setDepth(!d.noDepthTest, false); // particles never write depth
        im.program.setVec3("uFogColor", fog);
        im.program.setInt("uTexMode", d.blend == ParticleBlend::Filter ? 2 : 1);
        im.texture(d.texture, missing_)->bind(0);
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(b.first), static_cast<GLsizei>(b.count));
    }
    glDisable(GL_BLEND);
    setDepth(true, true);
}

} // namespace as3d
