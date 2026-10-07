// Particle quads (backend neutral), docs/spec/render-pipeline.md 4.1 and 6.6.
#include <algorithm>
#include <cmath>

#include "as3d/particle_render.h"

namespace as3d {

void particleBillboardAxes(const Mat4& view, Vec3& right, Vec3& up) {
    right = {view.at(0, 0), view.at(1, 0), view.at(2, 0)};
    up = {view.at(0, 1), view.at(1, 1), view.at(2, 1)};
}

namespace {

RenderBlend renderBlendOf(ParticleBlend b) {
    switch (b) {
    case ParticleBlend::Alpha: return RenderBlend::Alpha;
    case ParticleBlend::Add: return RenderBlend::Add;
    case ParticleBlend::Filter: return RenderBlend::Filter;
    default: return RenderBlend::None;
    }
}

} // namespace

int buildParticleQuads(const ParticleEmitter* const* emitters, size_t count, const Mat4& view,
                       const ParticleTextureResolver& resolve, std::vector<ParticleVertex>& verts,
                       std::vector<DrawRun>& runs) {
    Vec3 R, U;
    particleBillboardAxes(view, R, U);
    int built = 0;
    for (size_t e = 0; e < count; e++) {
        const ParticleEmitter& em = *emitters[e];
        const ParticleDesc& d = em.desc();
        const size_t first = verts.size();
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
                return ParticleVertex{q.x, q.y, q.z, u, 1.0f - t, p.rgba[0], p.rgba[1], p.rgba[2], p.rgba[3]};
            };
            const ParticleVertex q0 = V(c0, u0, v0), q1 = V(c1, u0 + du, v0), q2 = V(c2, u0 + du, v0 + dv),
                                 q3 = V(c3, u0, v0 + dv);
            verts.insert(verts.end(), {q0, q1, q2, q0, q2, q3});
            built++;
        }
        if (verts.size() > first) {
            DrawRun r;
            r.key = blendKey(renderBlendOf(d.blend), !d.noDepthTest, false, CullMode::Back);
            r.texture = resolve ? resolve(d.texture) : nullptr;
            r.first = static_cast<int>(first);
            r.count = static_cast<int>(verts.size() - first);
            r.texMode = d.blend == ParticleBlend::Filter ? kTexClampAdd : kTexModulate;
            appendDrawRun(runs, r);
        }
    }
    return built;
}

} // namespace as3d
