// WorldParticles (as3d/world_render.h): one ParticleEmitter per emitter holder of the
// world, following docs/spec/render-pipeline.md 6.3 (lifecycle) and 6.4 (update order: the
// entity pass moves the emitters, then every instance is updated once per unpaused frame).
#include <algorithm>

#include "as3d/world_render.h"

namespace as3d {

struct WorldParticles::Instance {
    std::unique_ptr<ParticleEmitter> emitter;
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

void WorldParticles::update(const World& world, float dt) {
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
    //    it only matters when the pool is full).
    std::vector<int> roots = world.listEntities();
    std::vector<int> stack;
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
            if (!e.emitter || (e.rt & RT_REMOVED) || !(e.rt & RT_THOUGHT)) continue;
            Instance* cur = bySlot_[static_cast<size_t>(i)];
            if (cur && cur->generation == e.generation) continue;
            if (refusedGen_[static_cast<size_t>(i)] == e.generation + 1u) continue;
            if (static_cast<int>(instances_.size()) >= kMaxWorldEmitters) {
                // An instance allocated from an empty pool is not created (6.1); the holder
                // stays without one (it is not retried every frame).
                ++refused_;
                refusedGen_[static_cast<size_t>(i)] = e.generation + 1u;
                continue;
            }
            std::unique_ptr<Instance> inst(new Instance());
            inst->slot = i;
            inst->generation = e.generation;
            inst->emitter.reset(new ParticleEmitter(*descOf(e.emitter), rng_.next(), e.v3(F_BASE_ORIGIN), e.absAttach));
            bySlot_[static_cast<size_t>(i)] = inst.get();
            instances_.push_back(std::move(inst));
        }
    }

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

    // 4. Advance, and free orphans that have finished.
    for (auto& inst : instances_) inst->emitter->update(dt);
    instances_.erase(std::remove_if(instances_.begin(), instances_.end(),
                                    [](const std::unique_ptr<Instance>& in) {
                                        return in->slot < 0 && in->emitter->finished();
                                    }),
                     instances_.end());
}

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
