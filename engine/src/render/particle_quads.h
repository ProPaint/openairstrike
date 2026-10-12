// The particle quads of buildParticleQuads (as3d/particle_render.h) for any vertex format:
// visitParticleQuads walks the live particles and calls the sink once per quad with its four
// corners, its frame cell and its colour; buildParticleQuads (particle_build.cpp) writes them as
// ParticleVertex, the PICA (particle_renderer_pica.cpp) straight into the frame arena as
// FlatVertex. Private to the render module.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "as3d/particle_render.h"

namespace as3d {

// One quad: corners c0..c3 (counter-clockwise: -x-y, +x-y, +x+y, -x+y), the frame cell from
// (u0, v0) of size (du, dv) in the original's convention (t up; the engine's v = 1 - t), the
// particle's colour.
struct ParticleQuad {
    Vec3 c0, c1, c2, c3;
    float u0, v0, du, dv;
    const float* rgba;
};

RenderBlend particleRenderBlend(ParticleBlend b);

// A thinning of the visit: of every `of` live particles (counted across all emitters, in visit
// order) only `keep` are built, spread evenly (Bresenham over the running index), so every
// emitter keeps about the same fraction of its particles instead of the last emitters losing
// all of theirs to a cap. `keep` 0 or >= `of`: every particle.
struct ParticleThin {
    int keep = 0;
    int of = 0;
};

// `sink(const ParticleQuad&)` per particle; `vertices()` (six per quad, counted by the caller's
// sink) gives the run boundaries. Runs as buildParticleQuads. Returns the particles built.
template <class Sink, class Count>
int visitParticleQuads(const ParticleEmitter* const* emitters, size_t count, const Mat4& view,
                       const ParticleTextureResolver& resolve, Sink&& sink, Count&& vertices, std::vector<DrawRun>& runs,
                       ParticleThin thin = {}) {
    Vec3 R, U;
    particleBillboardAxes(view, R, U);
    int built = 0;
    const bool thinning = thin.keep > 0 && thin.of > thin.keep;
    long long index = 0; // running index of the live particles visited
    for (size_t e = 0; e < count; e++) {
        const ParticleEmitter& em = *emitters[e];
        const ParticleDesc& d = em.desc();
        const size_t first = vertices();
        const float du = 1.0f / static_cast<float>(d.texCols);
        const float dv = 1.0f / static_cast<float>(d.texRows);
        for (const Particle& p : em.particles()) {
            if (!em.isLive(p)) continue;
            if (thinning) {
                const long long i = index++;
                if (((i + 1) * thin.keep) / thin.of == (i * thin.keep) / thin.of) continue;
            }
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
            ParticleQuad q;
            q.u0 = static_cast<float>(f % d.texCols) * du;
            q.v0 = 1.0f - dv - static_cast<float>(f / d.texRows) * dv; // GL convention (v up)
            q.du = du;
            q.dv = dv;
            const float s = p.size;
            const Vec3 sx = X * s, sy = Y * s;
            q.c0 = P - sx - sy;
            q.c1 = P + sx - sy;
            q.c2 = P + sx + sy;
            q.c3 = P - sx + sy;
            q.rgba = p.rgba;
            sink(q);
            built++;
        }
        if (vertices() > first) {
            DrawRun r;
            r.key = blendKey(particleRenderBlend(d.blend), !d.noDepthTest, false, CullMode::Back);
            r.texture = resolve ? resolve(d.texture) : nullptr;
            r.first = static_cast<int>(first);
            r.count = static_cast<int>(vertices() - first);
            r.texMode = d.blend == ParticleBlend::Filter ? kTexClampAdd : kTexModulate;
            appendDrawRun(runs, r);
        }
    }
    return built;
}

// The live particles of the emitters (an upper bound of the quads before visiting them).
inline size_t liveParticleCount(const ParticleEmitter* const* emitters, size_t count) {
    size_t n = 0;
    for (size_t e = 0; e < count; e++)
        for (const Particle& p : emitters[e]->particles())
            if (emitters[e]->isLive(p)) ++n;
    return n;
}

} // namespace as3d
