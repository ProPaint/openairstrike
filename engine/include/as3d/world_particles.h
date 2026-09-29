// The world's particle-system instances (docs/spec/render-pipeline.md 6.1 to 6.4,
// engine-behaviour.md 2 step 7). Part of the simulation: the World owns one WorldParticles,
// advances it once per unpaused fixed step after the entity pass, and particles of systems
// with a `damage` statement hurt players or enemies (engine-behaviour.md 6.2). The renderer
// only reads it.
//
// Each emitter holder (an entity whose `emitter` is set) gets one ParticleEmitter the first
// frame it has thought, moved to the holder's attachment every frame. Randomness comes from
// the instance pool's own as3d::Rng (one draw per new instance), never from the world's
// script random stream, so particle spawning does not shift what scripts see.
//
// Platform independent (no GL).
#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "as3d/core.h"
#include "as3d/particles.h"

namespace as3d {

class World;
struct ParticleSystemDef;

constexpr int kMaxWorldEmitters = 256; // particle-system instance pool (render-pipeline.md 6.1)

// Particle damage (engine-behaviour.md 6.2; the parameter roles are docs/spec/issues/110):
// `damage TOUCH_x n a b` hurts with lerp(a, b, age / life_time) every particle whose index
// in the pool is a multiple of int(n), once per frame, no difficulty factor, attacker -1.
struct ParticleDamage {
    int touch = 0;      // 0 none, 1 TOUCH_ENEMIES, 2 TOUCH_PLAYER
    int every = 1;      // int(n), at least 1
    float start = 0.0f; // a: damage of a new particle
    float end = 0.0f;   // b: damage at the end of its life
    static ParticleDamage fromDef(const ParticleSystemDef& def);
    // Amount for a particle of `age` in a system of `lifeTime`.
    float amount(float age, float lifeTime) const;
};

class WorldParticles {
public:
    WorldParticles();
    ~WorldParticles();
    WorldParticles(const WorldParticles&) = delete;
    WorldParticles& operator=(const WorldParticles&) = delete;

    // Drops every emitter and reseeds (level start).
    void reset(u32 seed = 1);
    // One fixed step: picks up new holders, moves every emitter to its holder, stops emitters
    // whose holder was deactivated or removed (they finish their live particles), advances
    // all of them by `dt`, then applies particle damage through world.damageEntity.
    void update(World& world, float dt);

    // Live instances, most recently created first (the original's draw order).
    void collect(std::vector<const ParticleEmitter*>& out) const;
    int emitterCount() const;
    int liveParticles() const;
    // Instances that could not be created because the pool was full.
    std::uint64_t refused() const { return refused_; }
    // Total damage dealt by particles since the last reset, per target class.
    float damageToPlayers() const { return damageToPlayers_; }
    float damageToEnemies() const { return damageToEnemies_; }
    // Hash of the state of every emitter, for determinism tests and state dumps.
    u32 stateHash() const;

private:
    struct Instance;
    const ParticleDesc* descOf(const ParticleSystemDef* def);
    void applyDamage(World& world, const Instance& inst);

    std::vector<std::unique_ptr<Instance>> instances_; // creation order
    std::vector<std::pair<const ParticleSystemDef*, std::unique_ptr<ParticleDesc>>> descs_;
    // Per entity slot: the instance mirroring its holder (checked against the generation).
    std::vector<Instance*> bySlot_;
    std::vector<u32> refusedGen_; // per slot: holder generation + 1 whose creation was refused
    Rng rng_{1};
    std::uint64_t refused_ = 0;
    float damageToPlayers_ = 0.0f;
    float damageToEnemies_ = 0.0f;
};

} // namespace as3d
