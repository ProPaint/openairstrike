// Skid-mark trails of the sequels' ground vehicles (as3d/world_skid.h):
// docs/spec/as2/engine-behaviour.delta.md 3.1.2 (VERIFIED-CODE as2@0x412200 allocation,
// as2@0x414850 update, as2@0x40b300 release, as2@0x40e474 pool rebuild), issue as2/210.
#include <algorithm>
#include <cmath>

#include "as3d/defs.h"
#include "as3d/world.h"
#include "world_internal.h"

namespace as3d {

namespace {

// Axis row `k` of an entity reduced to x, y and normalised in 2D; left as it is when of
// length 0 (as2@0x414850).
Vec2 flatAxis(const Entity& e, int k) {
    Vec2 v{e.f(F_AXIS + 3 * k), e.f(F_AXIS + 3 * k + 1)};
    const float len = std::sqrt(v.x * v.x + v.y * v.y);
    if (len > 0.0f) v = Vec2{v.x / len, v.y / len};
    return v;
}

} // namespace

void World::resetSkidTrails() {
    const int pool = rules_->skidMarks ? std::min(std::max(rules_->skidTrailPool, 0), kMaxSkidTrailPool) : 0;
    skidPool_.assign(static_cast<size_t>(pool), WorldSkidTrail());
    skidFree_.clear();
    skidLive_.clear();
    skidFree_.reserve(static_cast<size_t>(pool));
    skidLive_.reserve(static_cast<size_t>(pool));
    for (int i = pool - 1; i >= 0; --i) skidFree_.push_back(i);
}

void World::attachSkidTrails(int idx) {
    Entity& e = ents_[static_cast<size_t>(idx)];
    e.skidTrailCount = 0;
    if (!e.def || e.def->skidMarks.empty() || !rules_->skidMarks) return;
    const int n = std::min(static_cast<int>(e.def->skidMarks.size()), kMaxEntitySkidTrails);
    e.skidTrailCount = n;
    for (int k = 0; k < n; ++k) {
        e.skidTrails[k] = -1;
        if (skidFree_.empty()) continue; // the pool is empty: this mark is not laid
        const int t = skidFree_.back();
        skidFree_.pop_back();
        WorldSkidTrail& tr = skidPool_[static_cast<size_t>(t)];
        const SkidMarkDef& m = e.def->skidMarks[static_cast<size_t>(k)];
        tr = WorldSkidTrail();
        tr.inUse = true;
        tr.owner = idx;
        tr.ownerGeneration = e.generation;
        tr.x = m.x;
        tr.y = m.y;
        tr.width = m.width;
        tr.texture = &m.texture;
        skidLive_.insert(skidLive_.begin(), t); // linked at the head of the live list
        e.skidTrails[k] = t;
    }
}

void World::releaseSkidTrails(Entity& e) {
    for (int k = 0; k < e.skidTrailCount && k < kMaxEntitySkidTrails; ++k) {
        const int t = e.skidTrails[k];
        if (t < 0 || t >= static_cast<int>(skidPool_.size())) continue;
        WorldSkidTrail& tr = skidPool_[static_cast<size_t>(t)];
        if (tr.inUse && tr.owner >= 0 && &ents_[static_cast<size_t>(tr.owner)] == &e) tr.owner = -1;
        e.skidTrails[k] = -1;
    }
    e.skidTrailCount = 0;
}

void World::updateSkidTrails() {
    if (skidLive_.empty()) return;
    const float ft = frametime_;
    const float interval = rules_->skidNodeInterval;
    const float life = rules_->skidLife;
    const int maxNodes = std::min(std::max(rules_->skidMaxNodes, 1), kMaxSkidNodes);
    for (size_t li = 0; li < skidLive_.size();) {
        const int t = skidLive_[li];
        WorldSkidTrail& tr = skidPool_[static_cast<size_t>(t)];
        // A freed owner is noticed here too (its slot may have been reused since).
        if (tr.owner >= 0) {
            const Entity& o = ents_[static_cast<size_t>(tr.owner)];
            if (!o.inUse || o.generation != tr.ownerGeneration) tr.owner = -1;
        }
        // 1. No owner and no node left: back to the pool.
        if (tr.owner < 0 && tr.nodeCount == 0) {
            tr = WorldSkidTrail();
            skidFree_.push_back(t);
            skidLive_.erase(skidLive_.begin() + static_cast<std::ptrdiff_t>(li));
            continue;
        }
        // 2. Timer and ages; nodes at the end of their life leave from the front.
        tr.timer += ft;
        for (int k = 0; k < tr.nodeCount; ++k) tr.nodes[static_cast<size_t>(k)].age += ft;
        int expired = 0;
        while (expired < tr.nodeCount && tr.nodes[static_cast<size_t>(expired)].age >= life) ++expired;
        if (expired > 0) {
            std::copy(tr.nodes.begin() + expired, tr.nodes.begin() + tr.nodeCount, tr.nodes.begin());
            tr.nodeCount -= expired;
        }
        // 3. (The draw list is the live list, read by the renderer.)
        // 4. The head follows the owner.
        if (tr.owner >= 0) {
            const Entity& o = ents_[static_cast<size_t>(tr.owner)];
            const Vec2 fx = flatAxis(o, 0); // X̂: lateral (axis row 0)
            const Vec2 fy = flatAxis(o, 1); // Ŷ: forward (axis row 1)
            const Vec3 b = o.v3(F_BASE_ORIGIN);
            bool start = tr.nodeCount == 0;
            if (tr.timer >= interval) {
                tr.timer -= interval;
                start = true;
            }
            if (start) {
                if (tr.nodeCount >= maxNodes) {
                    std::copy(tr.nodes.begin() + 1, tr.nodes.begin() + tr.nodeCount, tr.nodes.begin());
                    --tr.nodeCount;
                }
                tr.nodes[static_cast<size_t>(tr.nodeCount)] = WorldSkidNode(); // age 0 (issue as2/210 §1)
                ++tr.nodeCount;
            }
            if (tr.hasPrevious) tr.length += length(b - tr.previous);
            tr.previous = b;
            tr.hasPrevious = true;
            WorldSkidNode& cur = tr.nodes[static_cast<size_t>(tr.nodeCount - 1)];
            cur.position = Vec2{b.x + tr.y * fy.x + tr.x * fx.x, b.y + tr.y * fy.y + tr.x * fx.y};
            cur.direction = Vec2{-fx.y, fx.x};
            cur.distance = tr.length;
        }
        ++li;
    }
}

} // namespace as3d
