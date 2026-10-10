// Particle quads (backend neutral), docs/spec/render-pipeline.md 4.1 and 6.6.
#include <algorithm>
#include <cmath>

#include "as3d/particle_render.h"
#include "particle_quads.h"

namespace as3d {

void particleBillboardAxes(const Mat4& view, Vec3& right, Vec3& up) {
    right = {view.at(0, 0), view.at(1, 0), view.at(2, 0)};
    up = {view.at(0, 1), view.at(1, 1), view.at(2, 1)};
}

RenderBlend particleRenderBlend(ParticleBlend b) {
    switch (b) {
    case ParticleBlend::Alpha: return RenderBlend::Alpha;
    case ParticleBlend::Add: return RenderBlend::Add;
    case ParticleBlend::Filter: return RenderBlend::Filter;
    default: return RenderBlend::None;
    }
}

int buildParticleQuads(const ParticleEmitter* const* emitters, size_t count, const Mat4& view,
                       const ParticleTextureResolver& resolve, std::vector<ParticleVertex>& verts,
                       std::vector<DrawRun>& runs) {
    return visitParticleQuads(
        emitters, count, view, resolve,
        [&](const ParticleQuad& q) {
            // The engine's textures have v = 0 at the top; the original's have v = 0 at the
            // bottom, so every original t becomes 1 - t.
            auto V = [&](const Vec3& c, float u, float t) {
                return ParticleVertex{c.x, c.y, c.z, u, 1.0f - t, q.rgba[0], q.rgba[1], q.rgba[2], q.rgba[3]};
            };
            const ParticleVertex q0 = V(q.c0, q.u0, q.v0), q1 = V(q.c1, q.u0 + q.du, q.v0),
                                 q2 = V(q.c2, q.u0 + q.du, q.v0 + q.dv), q3 = V(q.c3, q.u0, q.v0 + q.dv);
            verts.insert(verts.end(), {q0, q1, q2, q0, q2, q3});
        },
        [&]() { return verts.size(); }, runs);
}

} // namespace as3d
