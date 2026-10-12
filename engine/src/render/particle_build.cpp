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

bool particleEmitterBounds(const ParticleEmitter& emitter, Vec3& centre, float& radius) {
    const ParticleDesc& d = emitter.desc();
    const float kDiag = 1.4142136f;
    const int live = emitter.liveCount();
    if (live <= 0) return false;
    int seen = 0;
    if (d.coords == ParticleCoords::Decart) {
        bool any = false;
        float lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
        for (const Particle& p : emitter.particles()) {
            if (seen == live) break;
            if (!emitter.isLive(p)) continue;
            ++seen;
            const float r = std::fabs(p.size) * kDiag;
            for (int k = 0; k < 3; k++) {
                const float a = p.p[k] - r, b = p.p[k] + r;
                if (!any || a < lo[k]) lo[k] = a;
                if (!any || b > hi[k]) hi[k] = b;
            }
            any = true;
        }
        if (!any) return false;
        centre = {(lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f};
        const float ex = (hi[0] - lo[0]) * 0.5f, ey = (hi[1] - lo[1]) * 0.5f, ez = (hi[2] - lo[2]) * 0.5f;
        radius = std::sqrt(ex * ex + ey * ey + ez * ez);
        return true;
    }
    bool any = false;
    float far = 0.0f;
    for (const Particle& p : emitter.particles()) {
        if (seen == live) break;
        if (!emitter.isLive(p)) continue;
        ++seen;
        const float reach = d.coords == ParticleCoords::Cilinder ? std::fabs(p.p[1]) + std::fabs(p.p[2]) : std::fabs(p.p[2]);
        far = std::max(far, reach + std::fabs(p.size) * kDiag);
        any = true;
    }
    if (!any) return false;
    centre = emitter.origin();
    radius = far;
    return true;
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

int particleThinShares(const ParticleEmitter* const* emitters, size_t count, const ParticleFade& fade, int cap,
                       std::vector<int>& shares, int* faded) {
    shares.resize(count);
    const bool fading = fade.enabled && fade.stillEnd > fade.stillStart && fade.floor < 1.0f;
    const float floorW = std::max(0.0f, fade.floor);
    const float slope = fading ? (1.0f - floorW) / (fade.stillEnd - fade.stillStart) : 0.0f;
    // Pass 1: the fade's weight per emitter and the weighted total (one float per emitter).
    float weighted = 0.0f;
    int live = 0;
    float weightsStack[64];
    std::vector<float> weightsHeap;
    float* weights = weightsStack;
    if (count > 64) {
        weightsHeap.resize(count);
        weights = weightsHeap.data();
    }
    for (size_t e = 0; e < count; e++) {
        const ParticleEmitter& em = *emitters[e];
        const int n = std::max(em.liveCount(), 0);
        float we = 1.0f;
        if (fading && n > 0 && !em.desc().emitOnce && em.desc().emitRate > 0) {
            const float still = em.stillSeconds();
            if (still >= fade.stillEnd) we = floorW;
            else if (still > fade.stillStart) we = 1.0f - slope * (still - fade.stillStart);
        }
        weights[e] = we;
        weighted += we * static_cast<float>(n);
        live += n;
    }
    // Pass 2: the cap's scale (one division per frame, never per particle: the ARM11 has no divider).
    float scale = 1.0f;
    if (cap > 0 && weighted > static_cast<float>(cap)) scale = static_cast<float>(cap) / weighted;
    int built = 0;
    for (size_t e = 0; e < count; e++) {
        const int n = std::max(emitters[e]->liveCount(), 0);
        const float f = std::min(1.0f, weights[e] * scale);
        const int share = static_cast<int>(f * static_cast<float>(kParticleThinUnit) + 0.5f);
        shares[e] = share;
        built += static_cast<int>((static_cast<long long>(share) * n) / kParticleThinUnit);
    }
    if (faded) *faded = live - static_cast<int>(weighted + 0.5f);
    return built;
}

} // namespace as3d
