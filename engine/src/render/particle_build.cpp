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

namespace {

// The farthest a live particle of the description can be from its spawn base along axis k
// after `life` seconds: the offset range, the fastest velocity for the whole life, the
// acceleration's share. p = base + off + v t + a t^2 / 2 with |t| <= life.
float particleReach(const ParticleDesc& d, int k) {
    const float life = std::max(d.lifeTime, 0.0f);
    const float v = std::fabs(d.initVelocity[k]) + std::fabs(d.initVelocity[3 + k]);
    return std::fabs(d.initOffset[3 + k]) + v * life + 0.5f * std::fabs(d.accel[k]) * life * life;
}

// The largest half extent of a quad of the description over a particle's life.
float particleMaxSize(const ParticleDesc& d) {
    const float life = std::max(d.lifeTime, 0.0f);
    const float s0 = std::fabs(d.initSize[0]) + std::fabs(d.initSize[1]);
    return s0 + std::max(0.0f, d.sizeRate) * life;
}

} // namespace

bool particleEmitterBounds(const ParticleEmitter& emitter, Vec3& centre, float& radius) {
    const ParticleDesc& d = emitter.desc();
    const float kDiag = 1.4142136f;
    const int live = emitter.liveCount();
    if (live <= 0) return false;
    // R4.15: no walk where the description bounds the particles. Cilinder and Sphere particles
    // are relative to the origin whatever its motion; Decart ones were spawned at the origin
    // once it has stood still for a whole particle life (the sequels' fires: every one), so
    // the description's reach from the origin holds them all. A moving Decart emitter (a
    // trail) keeps the exact walk, its live particles are few.
    const float maxSize = particleMaxSize(d) * kDiag;
    if (d.coords != ParticleCoords::Decart) {
        const float r1 = std::fabs(d.initOffset[1]) + particleReach(d, 1);
        const float r2 = std::fabs(d.initOffset[2]) + particleReach(d, 2);
        centre = emitter.origin();
        radius = (d.coords == ParticleCoords::Cilinder ? r1 + r2 : r2) + maxSize;
        return true;
    }
    if (emitter.stillSeconds() >= d.lifeTime) {
        const Vec3& o = emitter.origin();
        centre = {o.x + d.initOffset[0], o.y + d.initOffset[1], o.z + d.initOffset[2]};
        const float ex = particleReach(d, 0), ey = particleReach(d, 1), ez = particleReach(d, 2);
        radius = std::sqrt(ex * ex + ey * ey + ez * ez) + maxSize;
        return true;
    }
    int seen = 0;
    {
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
