// Particle simulation. Behaviour: docs/spec/render-pipeline.md section 6.
#include "as3d/particles.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "as3d/defs.h"

namespace as3d {

ParticleDesc ParticleDesc::fromDef(const ParticleSystemDef& d) {
    ParticleDesc r;
    r.name = d.name;
    r.texture = d.texture;
    r.texCols = std::max(1, d.textureFrameW);
    r.texRows = std::max(1, d.textureFrameH);
    r.blend = static_cast<ParticleBlend>(static_cast<int>(d.blendMode));
    r.noDepthTest = (d.rflag & RF_NODEPTHTEST) != 0;
    r.coords = static_cast<ParticleCoords>(static_cast<int>(d.coords));
    r.drawMode = d.drawMode == DrawMode::Vert    ? ParticleDrawMode::Vert
                 : d.drawMode == DrawMode::Horiz ? ParticleDrawMode::Horiz
                                                 : ParticleDrawMode::Billboard;
    r.emitOnce = d.emitMode == EmitMode::Once;
    // emit_rate is parsed with atol in the original: truncate.
    r.emitRate = std::isfinite(d.emitRate) ? static_cast<int>(std::max(-1.0e6f, std::min(d.emitRate, 1.0e6f))) : 0;
    r.lifeTime = d.lifeTime;
    std::memcpy(r.initOffset, d.initOffset, sizeof r.initOffset);
    std::memcpy(r.initVelocity, d.initVelocity, sizeof r.initVelocity);
    std::memcpy(r.initSize, d.initSize, sizeof r.initSize);
    r.initFrame[0] = d.initFrame[0];
    r.initFrame[1] = d.initFrame[1];
    std::memcpy(r.initColor, d.initColor, sizeof r.initColor);
    std::memcpy(r.accel, d.accel, sizeof r.accel);
    r.fadeLinear = d.fadeMode == FadeMode::Linear;
    r.fadeFactor = d.fadeFactor;
    r.sizeRate = d.size;
    r.animLinear = d.animMode == AnimMode::Linear;
    return r;
}

int ParticleDesc::poolSize() const {
    if (emitRate <= 0) return 0;
    double n = emitOnce ? static_cast<double>(emitRate) : static_cast<double>(emitRate) * lifeTime + 4.0;
    if (!(n > 0.0)) return 0; // also catches NaN
    if (n > kMaxParticlesPerEmitter) return kMaxParticlesPerEmitter;
    return static_cast<int>(n);
}

namespace {

// Integer-degree sin table (the original looks angles up in a 360 entry table, cos is
// the same table shifted by 90).
struct SinTable {
    float v[360];
    SinTable() {
        for (int i = 0; i < 360; i++) v[i] = std::sin(degToRad(static_cast<float>(i)));
    }
};
const SinTable& sinTable() {
    static const SinTable t;
    return t;
}
int wrapDegrees(float a) {
    if (!std::isfinite(a)) return 0;
    a = std::max(-1.0e7f, std::min(a, 1.0e7f));
    int i = static_cast<int>(a) % 360;
    return i < 0 ? i + 360 : i;
}
float sinDeg(int a) { return sinTable().v[a]; }
float cosDeg(int a) { return sinTable().v[(a + 90) % 360]; }

u32 fnv(u32 h, const void* data, size_t n) {
    const u8* b = static_cast<const u8*>(data);
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}

} // namespace

ParticleEmitter::ParticleEmitter(const ParticleDesc& desc, u32 seed, const Vec3& origin, bool oriented)
    : desc_(&desc), rng_(seed), origin_(origin), prevOrigin_(origin), oriented_(oriented) {
    axis_[0] = {1, 0, 0};
    axis_[1] = {0, 1, 0};
    axis_[2] = {0, 0, 1};
    particles_.resize(static_cast<size_t>(desc.poolSize()));
    if (desc.emitOnce) {
        for (Particle& p : particles_) spawn(p);
    } else {
        for (Particle& p : particles_) p.age = desc.lifeTime + 1.0f; // dead
    }
}

void ParticleEmitter::setOrigin(const Vec3& origin) { origin_ = origin; }

void ParticleEmitter::setAxis(const Vec3& r0, const Vec3& r1, const Vec3& r2) {
    axis_[0] = r0;
    axis_[1] = r1;
    axis_[2] = r2;
}

void ParticleEmitter::activate() {
    stopped_ = false;
    clock_ = 0.0f;
    prevOrigin_ = origin_;
}

int ParticleEmitter::liveCount() const {
    int n = 0;
    for (const Particle& p : particles_) n += isLive(p) ? 1 : 0;
    return n;
}

bool ParticleEmitter::finished() const {
    if (!(stopped_ || desc_->emitOnce)) return false;
    return liveCount() == 0;
}

void ParticleEmitter::spawn(Particle& p) {
    const ParticleDesc& d = *desc_;
    p.age = 0.0f;
    float base[3] = {d.initOffset[0], d.initOffset[1], d.initOffset[2]};
    if (d.coords == ParticleCoords::Decart) {
        base[0] += prevOrigin_.x;
        base[1] += prevOrigin_.y;
        base[2] += prevOrigin_.z;
    }
    for (int i = 0; i < 3; i++) p.p[i] = base[i] + d.initOffset[3 + i] * rng_.signedUniform();
    float vel[3];
    for (int i = 0; i < 3; i++) vel[i] = d.initVelocity[i] + d.initVelocity[3 + i] * rng_.signedUniform();
    p.angle = 0.0f; // init_angle is absent from the data
    if (oriented_ && d.coords == ParticleCoords::Decart) {
        Vec3 w = axis_[0] * vel[0] + axis_[1] * vel[1] + axis_[2] * vel[2];
        vel[0] = w.x;
        vel[1] = w.y;
        vel[2] = w.z;
        // "+ yaw of the axis when oriented" (6.5). GUESS: atan2(row0.y, row0.x) in degrees.
        p.angle += radToDeg(std::atan2(axis_[0].y, axis_[0].x));
    }
    for (int i = 0; i < 3; i++) p.v[i] = vel[i];
    for (int i = 0; i < 4; i++) p.rgba[i] = d.initColor[i];
    p.size = std::trunc(d.initSize[0]) + d.initSize[1] * rng_.uniform();
    p.frame = d.initFrame[0] + static_cast<int>(rng_.uniform() * static_cast<float>(d.initFrame[1]));
}

void ParticleEmitter::update(float dt) {
    const ParticleDesc& d = *desc_;
    if (!(dt > 0.0f) || particles_.empty()) return;
    const int cap = capacity();

    clock_ += dt;
    if (!stopped_ && !d.emitOnce && d.emitRate > 0) {
        const float interval = 1.0f / static_cast<float>(d.emitRate);
        const float n = std::floor(clock_ / interval);
        const float step = n >= 1.0f ? 1.0f / n : 0.0f;
        const Vec3 start = prevOrigin_;
        int guard = 0;
        while (interval < clock_) {
            spawn(particles_[static_cast<size_t>(ring_)]);
            prevOrigin_ = prevOrigin_ + (origin_ - start) * step;
            clock_ -= interval;
            ring_ = (ring_ + 1) % cap;
            if (++guard > cap) { clock_ = 0.0f; break; } // huge dt: older spawns would be overwritten anyway
        }
        prevOrigin_ = origin_; // see docs/spec/issues/010-particle-prev-origin.md
    }

    const float life = d.lifeTime;
    const int cells = d.texCols * d.texRows;
    for (Particle& p : particles_) {
        if (!(p.age <= life)) continue;
        p.size += d.sizeRate * dt;
        for (int i = 0; i < 3; i++) {
            p.v[i] += d.accel[i] * dt;
            p.p[i] += p.v[i] * dt;
        }
        if (d.fadeLinear) {
            float ratio = life > 0.0f ? p.age / life : 1.0f;
            float k = (1.0f - ratio) * d.fadeFactor;
            switch (d.blend) {
            case ParticleBlend::Alpha: p.rgba[3] = d.initColor[3] * k; break;
            case ParticleBlend::Add:
                for (int i = 0; i < 3; i++) p.rgba[i] = d.initColor[i] * k;
                break;
            case ParticleBlend::Filter:
                for (int i = 0; i < 3; i++) p.rgba[i] = d.initColor[i] * (d.fadeFactor - k);
                break;
            case ParticleBlend::None: break;
            }
        }
        if (d.animLinear) {
            float ratio = life > 0.0f ? p.age / life : 1.0f;
            p.frame = static_cast<int>(ratio * static_cast<float>(cells));
        }
        p.age += dt;
    }
}

Vec3 ParticleEmitter::worldPosition(const Particle& p) const {
    switch (desc_->coords) {
    case ParticleCoords::Decart: return {p.p[0], p.p[1], p.p[2]};
    case ParticleCoords::Cilinder: {
        int a = wrapDegrees(p.p[0]);
        return {origin_.x + cosDeg(a) * p.p[1], origin_.y + sinDeg(a) * p.p[1], origin_.z + p.p[2]};
    }
    case ParticleCoords::Sphere: {
        int a = wrapDegrees(p.p[0]);
        int e = wrapDegrees(p.p[1]);
        float ce = cosDeg(e);
        return {origin_.x + cosDeg(a) * ce * p.p[2], origin_.y + sinDeg(a) * ce * p.p[2],
                origin_.z + sinDeg(e) * p.p[2]};
    }
    }
    return {};
}

u32 ParticleEmitter::stateHash() const {
    u32 h = 2166136261u;
    for (const Particle& p : particles_) h = fnv(h, &p, sizeof p);
    h = fnv(h, &clock_, sizeof clock_);
    h = fnv(h, &ring_, sizeof ring_);
    h = fnv(h, &prevOrigin_, sizeof prevOrigin_);
    return h;
}

} // namespace as3d
