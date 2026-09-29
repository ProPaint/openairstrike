// Particle simulation (docs/spec/render-pipeline.md section 6, corrections in 11.3).
// Platform independent: no GL, no defs.h dependency (ParticleSystemDef is only
// forward-declared, so this header can be included next to as3d/gfx.h).
//
// One ParticleEmitter is one particle-system instance of the original (0x60 byte record
// plus its particle array). It owns a fixed pool sized like the original
// (emit_rate for EMIT_ONCE, else int(emit_rate * life_time + 4)) and never allocates
// after construction. Randomness comes from as3d::Rng so runs are reproducible.
//
// Not implemented, because the original never implements them either: FADE_EXP,
// ANIM_NORMAL, ANIM_LOOP, emit_time, anim_speed, axis, EMIT_DURATION (acts like the
// continuous default). RF_NOLIGHTING / RF_NOCULLING / RF_NODEPTHWRITE are parsed and
// unused. texture_set, spin, color and init_angle are absent from the shipped data and
// are not part of ParticleSystemDef, so they are not simulated (angle stays at 0 unless
// the emitter is oriented).
#pragma once

#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/math.h"

namespace as3d {

struct ParticleSystemDef;

enum class ParticleBlend { None = 0, Alpha = 1, Add = 2, Filter = 3 };
enum class ParticleCoords { Decart = 0, Cilinder = 1, Sphere = 2 };
enum class ParticleDrawMode { Billboard = 0, Vert = 1, Horiz = 2 };

// Hard bound on the pool of one emitter (the largest shipped pool is 404).
constexpr int kMaxParticlesPerEmitter = 4096;

// Plain-data copy of the fields of a ParticleSystemDef that the simulation and the
// renderer use. Can be built by hand for tests.
struct ParticleDesc {
    std::string name;
    std::string texture;
    int texCols = 1; // ps.md calls these textureFrameW/H; they are grid columns and rows
    int texRows = 1;
    ParticleBlend blend = ParticleBlend::None;
    bool noDepthTest = false;       // RF_NODEPTHTEST, the only flag the original reads
    ParticleCoords coords = ParticleCoords::Decart;
    ParticleDrawMode drawMode = ParticleDrawMode::Billboard;
    bool emitOnce = false;          // EMIT_ONCE
    int emitRate = 0;               // integer in the original (atol)
    float lifeTime = 0.0f;
    float initOffset[6] = {0, 0, 0, 0, 0, 0};
    float initVelocity[6] = {0, 0, 0, 0, 0, 0};
    float initSize[2] = {0, 0};
    int initFrame[2] = {0, 0};      // base, range
    float initColor[4] = {0, 0, 0, 1};
    float accel[3] = {0, 0, 0};
    bool fadeLinear = false;        // FADE_LINEAR; FADE_EXP does nothing
    float fadeFactor = 1.0f;
    float sizeRate = 0.0f;          // "size": growth per second
    bool animLinear = false;        // ANIM_LINEAR; the other modes do nothing

    static ParticleDesc fromDef(const ParticleSystemDef& def);
    // Pool size of an emitter made from this description, clamped to [0, kMax...].
    int poolSize() const;
};

struct Particle {
    float age = 0.0f;
    int frame = 0;
    float p[3] = {0, 0, 0}; // position in the space of the coords mode
    float v[3] = {0, 0, 0};
    float size = 0.0f;      // half extent
    float angle = 0.0f;     // degrees
    float rgba[4] = {0, 0, 0, 0};
};

class ParticleEmitter {
public:
    // `desc` must outlive the emitter.
    ParticleEmitter(const ParticleDesc& desc, u32 seed, const Vec3& origin = {0, 0, 0}, bool oriented = false);

    // Moves the emitter. The axis rows (right, forward, up of the attachment) rotate
    // spawn velocities when the emitter is oriented; identity by default.
    void setOrigin(const Vec3& origin);
    void setAxis(const Vec3& row0, const Vec3& row1, const Vec3& row2);
    const Vec3& origin() const { return origin_; }

    // AttachDeactivate / AttachActivate. EMIT_ONCE emitters never emit again.
    void stop() { stopped_ = true; }
    void activate();
    bool stopped() const { return stopped_; }

    // Advances by dt seconds: emission, then the update of every live particle.
    void update(float dt);

    const ParticleDesc& desc() const { return *desc_; }
    const std::vector<Particle>& particles() const { return particles_; }
    int capacity() const { return static_cast<int>(particles_.size()); }
    bool isLive(const Particle& p) const { return p.age <= desc_->lifeTime; }
    int liveCount() const;
    // Stopped (or EMIT_ONCE) and every particle dead: the instance can be freed.
    bool finished() const;

    // World position of a particle for its coords mode (section 6.5 table).
    Vec3 worldPosition(const Particle& p) const;

    // Hash of the complete state, for determinism tests.
    u32 stateHash() const;

private:
    const ParticleDesc* desc_;
    Rng rng_;
    std::vector<Particle> particles_;
    Vec3 origin_, prevOrigin_;
    Vec3 axis_[3];
    bool oriented_;
    bool stopped_ = false;
    float clock_ = 0.0f;
    int ring_ = 0;

    void spawn(Particle& p);
};

} // namespace as3d
