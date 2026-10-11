// WorldParticles (as3d/world_particles.h): one ParticleEmitter per emitter holder of the
// world, following docs/spec/render-pipeline.md 6.3 (lifecycle) and 6.4 (update order: the
// entity pass moves the emitters, then every instance is updated once per unpaused frame and
// applies its particle damage, engine-behaviour.md 2 step 7 and 6.2).
#include "as3d/world_particles.h"

#include <algorithm>
#include <cmath>

#include "as3d/defs.h"
#include "as3d/world.h"

namespace as3d {

ParticleDamage ParticleDamage::fromDef(const ParticleSystemDef& def) {
    ParticleDamage d;
    if (!def.hasDamage) return d;
    // ps.md: only TOUCH_ENEMIES and TOUCH_PLAYER are recognised by the parser.
    if (def.damageTouch == TouchMode::Enemies) d.touch = 1;
    else if (def.damageTouch == TouchMode::Player) d.touch = 2;
    float n = def.damageAmount;
    d.every = (std::isfinite(n) && n >= 1.0f) ? static_cast<int>(std::min(n, 1.0e6f)) : 1;
    d.start = std::isfinite(def.damageParam2) ? def.damageParam2 : 0.0f;
    d.end = std::isfinite(def.damageParam3) ? def.damageParam3 : 0.0f;
    return d;
}

float ParticleDamage::amount(float age, float lifeTime) const {
    float t = lifeTime > 0.0f ? age / lifeTime : 1.0f;
    t = std::min(std::max(t, 0.0f), 1.0f);
    return start + (end - start) * t;
}

struct WorldParticles::Instance {
    std::unique_ptr<ParticleEmitter> emitter;
    ParticleDamage damage;
    int touchBits = 0;       // the sequels: the system's touch mode as a bit set
    int slot = -1;           // holder slot while attached, -1 once orphaned
    u32 generation = 0;      // holder generation
    bool wasActive = true;
};

WorldParticles::WorldParticles()
    : bySlot_(static_cast<size_t>(kMaxEntitySlots), nullptr), refusedGen_(static_cast<size_t>(kMaxEntitySlots), 0u) {}
WorldParticles::~WorldParticles() = default;

void WorldParticles::reset(u32 seed) {
    instances_.clear();
    std::fill(bySlot_.begin(), bySlot_.end(), nullptr);
    std::fill(refusedGen_.begin(), refusedGen_.end(), 0u);
    rng_ = Rng(seed);
    refused_ = 0;
    pending_.clear();
    rescan_ = true;
    damageToPlayers_ = damageToEnemies_ = 0.0f;
}

const ParticleDesc* WorldParticles::descOf(const ParticleSystemDef* def) {
    for (auto& d : descs_) {
        if (d.first == def) return d.second.get();
    }
    std::unique_ptr<ParticleDesc> desc(new ParticleDesc(ParticleDesc::fromDef(*def)));
    const ParticleDesc* p = desc.get();
    descs_.emplace_back(def, std::move(desc));
    return p;
}

void WorldParticles::update(World& world, float dt) {
    // 1. Orphan instances whose holder is gone (freed, or the slot reused).
    for (auto& inst : instances_) {
        if (inst->slot < 0) continue;
        int s = inst->slot;
        const bool alive = world.validIndex(s) && world.entity(s).generation == inst->generation &&
                           world.entity(s).emitter != nullptr;
        if (!alive) {
            inst->emitter->stop();
            if (bySlot_[static_cast<size_t>(s)] == inst.get()) bySlot_[static_cast<size_t>(s)] = nullptr;
            inst->slot = -1;
        }
    }

    // 2. New holders, oldest root first, parents before children (a deterministic order;
    //    it only matters when the pool is full): the order of a walk of the whole entity
    //    tree (scanAll), applied to the pending holders only.
    newHolders_.clear();
    world.takeNewHolders(newHolders_);
    const bool rescan = rescan_;
    rescan_ = false;
    if (rescan) scanAll(world); // refills pending_ with the holders it saw still waiting
    for (const auto& h : newHolders_) {
        Pending p;
        p.slot = h.first;
        p.generation = h.second;
        pending_.push_back(std::move(p));
    }
    if (!rescan) {
        candidates_.clear();
        for (Pending& p : pending_) {
            if (!world.validIndex(p.slot) || world.entity(p.slot).generation != p.generation) continue;
            const Entity& e = world.entity(p.slot);
            if (!e.emitter || (e.rt & RT_REMOVED) || !(e.rt & RT_THOUGHT)) continue;
            // Where the walk meets it: up the parent links to a list entity (a walk root),
            // each level through the first place in the parent's child array (the walk
            // visits the children in attach order, a child whose parent link is elsewhere
            // never). Not reached: the walk would not see it.
            p.path.clear();
            int n = p.slot;
            bool reached = true;
            for (int guard = 0; !world.entity(n).inList; ++guard) {
                const int par = world.entity(n).parent;
                if (guard > kMaxEntitySlots || !world.validIndex(par)) {
                    reached = false;
                    break;
                }
                const std::vector<int>& kids = world.entity(par).children;
                const auto at = std::find(kids.begin(), kids.end(), n);
                if (at == kids.end()) {
                    reached = false;
                    break;
                }
                p.path.push_back(static_cast<int>(at - kids.begin()));
                n = par;
            }
            if (!reached) continue;
            std::reverse(p.path.begin(), p.path.end());
            p.rootOrder = world.listOrder(n);
            candidates_.push_back(&p);
        }
        // Walk order: oldest root first, then preorder (a parent's path is a prefix of its
        // descendants' and comes first).
        std::sort(candidates_.begin(), candidates_.end(), [](const Pending* a, const Pending* b) {
            if (a->rootOrder != b->rootOrder) return a->rootOrder < b->rootOrder;
            return a->path < b->path;
        });
        for (Pending* p : candidates_) pickUp(world, p->slot);
    }
    // Drop the holders that are gone or settled (a holder settles once per generation).
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                  [&](const Pending& p) {
                                      if (!world.validIndex(p.slot) || world.entity(p.slot).generation != p.generation) return true;
                                      const Instance* cur = bySlot_[static_cast<size_t>(p.slot)];
                                      if (cur && cur->generation == p.generation) return true;
                                      return refusedGen_[static_cast<size_t>(p.slot)] == p.generation + 1u;
                                  }),
                   pending_.end());

    // 3. Follow the holders; activation state (AttachActivate / AttachDeactivate, remove).
    for (auto& inst : instances_) {
        if (inst->slot < 0) continue;
        const Entity& e = world.entity(inst->slot);
        ParticleEmitter& em = *inst->emitter;
        em.setOrigin(e.v3(F_BASE_ORIGIN));
        em.setAxis(e.v3(F_AXIS), e.v3(F_AXIS + 3), e.v3(F_AXIS + 6));
        if (e.rt & RT_REMOVED) {
            em.stop();
            continue;
        }
        bool active = (e.rt & RT_ACTIVE) != 0;
        if (!active && inst->wasActive) em.stop();
        else if (active && !inst->wasActive) em.activate();
        inst->wasActive = active;
    }

    // 4. Advance and hurt, newest instance first (the original walks the list from its tail),
    //    then free orphans that have finished.
    for (auto it = instances_.rbegin(); it != instances_.rend(); ++it) {
        (*it)->emitter->update(dt);
        if (world.rules().touchModeBits ? (*it)->touchBits != 0 : (*it)->damage.touch != 0) applyDamage(world, **it);
    }
    instances_.erase(std::remove_if(instances_.begin(), instances_.end(),
                                    [](const std::unique_ptr<Instance>& in) {
                                        return in->slot < 0 && in->emitter->finished();
                                    }),
                     instances_.end());
}

void WorldParticles::pickUp(World& world, int i) {
    const Entity& e = world.entity(i);
    Instance* cur = bySlot_[static_cast<size_t>(i)];
    if (cur && cur->generation == e.generation) return;
    if (refusedGen_[static_cast<size_t>(i)] == e.generation + 1u) return;
    if (static_cast<int>(instances_.size()) >= kMaxWorldEmitters) {
        // An instance allocated from an empty pool is not created (6.1); the holder stays
        // without one (it is not retried every frame).
        ++refused_;
        refusedGen_[static_cast<size_t>(i)] = e.generation + 1u;
        return;
    }
    std::unique_ptr<Instance> inst(new Instance());
    inst->slot = i;
    inst->generation = e.generation;
    inst->damage = ParticleDamage::fromDef(*e.emitter);
    inst->touchBits = e.emitter->hasDamage ? (static_cast<int>(e.emitter->damageTouch) & 0xF) : 0;
    inst->emitter.reset(new ParticleEmitter(*descOf(e.emitter), rng_.next(), e.v3(F_BASE_ORIGIN), e.absAttach));
    bySlot_[static_cast<size_t>(i)] = inst.get();
    instances_.push_back(std::move(inst));
}

void WorldParticles::scanAll(World& world) {
    pending_.clear();
    std::vector<int>& roots = roots_;
    world.listEntitiesInto(roots);
    std::vector<int>& stack = dfs_;
    for (auto it = roots.rbegin(); it != roots.rend(); ++it) {
        stack.clear();
        stack.push_back(*it);
        int guard = 0;
        while (!stack.empty() && guard++ < kMaxEntitySlots) {
            int i = stack.back();
            stack.pop_back();
            if (!world.validIndex(i)) continue;
            const Entity& e = world.entity(i);
            for (auto c = e.children.rbegin(); c != e.children.rend(); ++c) {
                if (world.validIndex(*c) && world.entity(*c).parent == i) stack.push_back(*c);
            }
            if (!e.emitter) continue;
            if (!(e.rt & RT_REMOVED) && (e.rt & RT_THOUGHT)) pickUp(world, i);
            Pending p;
            p.slot = i;
            p.generation = e.generation;
            pending_.push_back(std::move(p)); // the settled ones are dropped by update()
        }
    }
}

namespace {
void applyDamageBits(World& world, int bits, const float win[3], float amount, std::vector<int>& list,
                     float& damageToPlayers_, float& damageToEnemies_);
} // namespace

void WorldParticles::applyDamage(World& world, const Instance& inst) {
    const ParticleEmitter& em = *inst.emitter;
    const ParticleDamage& d = inst.damage;
    const std::vector<Particle>& ps = em.particles();
    const float life = em.desc().lifeTime;
    std::vector<int>& list = damageList_; // taken at the first particle that needs it
    list.clear();
    for (size_t k = 0; k < ps.size(); k += static_cast<size_t>(d.every)) {
        const Particle& p = ps[k];
        if (!em.isLive(p)) continue;
        float win[3];
        if (!world.projectPoint(em.worldPosition(p), win)) continue;
        const float amount = d.amount(p.age, life);
        if (!(amount > 0.0f)) continue;
        if (world.rules().touchModeBits) {
            applyDamageBits(world, inst.touchBits, win, amount, list, damageToPlayers_, damageToEnemies_);
            continue;
        }
        if (d.touch == 2) {
            // TOUCH_PLAYER: every player whose rectangle contains the particle.
            for (int pl = 0; pl < world.numPlayers(); ++pl) {
                int pi = world.playerEntityIndex(pl);
                if (pi < 0) continue;
                const Entity& pe = world.entity(pi);
                if (pe.f(F_DEAD) != 0.0f || !(pe.rt & RT_COLLIDABLE)) continue;
                if (!World::pointInRect(win, pe.rect)) continue;
                world.damageEntity(pi, amount, -1);
                damageToPlayers_ += amount;
            }
        } else {
            // TOUCH_ENEMIES: the first on-screen enemy (newest first) that contains it.
            if (list.empty()) world.listEntitiesInto(list);
            for (int i : list) {
                if (!world.validIndex(i)) continue;
                const Entity& e = world.entity(i);
                if ((e.rt & RT_REMOVED) || !(e.rt & RT_COLLIDABLE) || e.f(F_CLASS) != kClassEnemy) continue;
                if (e.f(F_DEAD) != 0.0f || (e.rt & RT_HEALTH_FROZEN)) continue;
                if (!World::pointInRect(win, e.rect)) continue;
                world.damageEntity(i, amount, -1);
                damageToEnemies_ += amount;
                break;
            }
        }
    }
}

// The sequels' G_ParticleDamage (as2/engine-behaviour.delta.md 6.2): the touch mode as a bit
// set. Bit 0x2: every player whose rectangle contains the particle; otherwise the first list
// entity that is alive (health > 0), on screen and (bit 0x1 and class 2) or (bit 0x4 and
// class 5), containing it. Attacker -1.
namespace {
void applyDamageBits(World& world, int bits, const float win[3], float amount, std::vector<int>& list,
                     float& damageToPlayers_, float& damageToEnemies_) {
    if (bits & TOUCH_BIT_PLAYER) {
        for (int pl = 0; pl < world.numPlayers(); ++pl) {
            int pi = world.playerEntityIndex(pl);
            if (pi < 0) continue;
            const Entity& pe = world.entity(pi);
            if (pe.f(F_DEAD) != 0.0f || !(pe.rt & RT_COLLIDABLE)) continue;
            if (!World::pointInRect(win, pe.rect)) continue;
            world.damageEntity(pi, amount, -1);
            damageToPlayers_ += amount;
        }
        return;
    }
    if (list.empty()) world.listEntitiesInto(list);
    for (int i : list) {
        if (!world.validIndex(i)) continue;
        const Entity& e = world.entity(i);
        if ((e.rt & RT_REMOVED) || !(e.rt & RT_COLLIDABLE) || !(e.f(F_HEALTH) > 0.0f)) continue;
        const float cls = e.f(F_CLASS);
        if (!(((bits & TOUCH_BIT_ENEMIES) && cls == kClassEnemy) || ((bits & TOUCH_BIT_CIVILIAN) && cls == kClassCivilian))) continue;
        if (!World::pointInRect(win, e.rect)) continue;
        world.damageEntity(i, amount, -1);
        damageToEnemies_ += amount;
        break;
    }
}
} // namespace

void WorldParticles::collect(std::vector<const ParticleEmitter*>& out) const {
    out.clear();
    for (auto it = instances_.rbegin(); it != instances_.rend(); ++it) out.push_back((*it)->emitter.get());
}

int WorldParticles::emitterCount() const { return static_cast<int>(instances_.size()); }

int WorldParticles::liveParticles() const {
    int n = 0;
    for (const auto& inst : instances_) n += inst->emitter->liveCount();
    return n;
}

u32 WorldParticles::stateHash() const {
    u32 h = 2166136261u;
    for (const auto& inst : instances_) {
        h ^= inst->emitter->stateHash();
        h *= 16777619u;
    }
    return h;
}

} // namespace as3d
