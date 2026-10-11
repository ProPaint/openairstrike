// Entity pass: state machine, think, attachment and transforms
// (docs/spec/engine-behaviour.md 2 step 6, 3.5, 4).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "as3d/defs.h"
#include "as3d/model.h"
#include "as3d/world.h"
#include "world_internal.h"

namespace as3d {

using script::EntryPoint;

namespace {
bool iequals(const std::string& a, const char* b) {
    size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}
} // namespace

bool World::tagLocal(int idx, const std::string& tag, Vec3& out) const {
    out = {0.0f, 0.0f, 0.0f};
    if (iequals(tag, "origin")) return true;
    if (!validIndex(idx)) return false;
    const ModelData* m = ents_[static_cast<size_t>(idx)].model;
    if (!m) return false;
    const ModelTag* t = m->findTag(tag.c_str());
    if (!t) return false;
    out = t->position;
    return true;
}

Vec3 World::tagWorldPosition(int idx, const std::string& tag) const {
    const Entity& e = ents_[static_cast<size_t>(idx)];
    Vec3 local;
    tagLocal(idx, tag, local);
    // G_GetTagWorldPos: base origin + axis * (tag * scale). A scale at or below 0.01 is
    // "unset" and treated as 1 (the same rule as the attachment formula, mdl.md).
    float s = e.f(F_SCALE);
    float k = s > 0.01f ? s : 1.0f;
    local = local * k;
    Vec3 base = e.v3(F_BASE_ORIGIN);
    Vec3 fw = e.v3(F_AXIS), lf = e.v3(F_AXIS + 3), up = e.v3(F_AXIS + 6);
    return base + fw * local.x + lf * local.y + up * local.z;
}

void World::attachToTag(int idx) {
    Entity& e = ents_[static_cast<size_t>(idx)];
    if (!validIndex(e.parent)) return;
    const Entity& p = ents_[static_cast<size_t>(e.parent)];
    Vec3 tag;
    bool found;
    if (e.tagCacheValid && e.tagCacheModel == p.model) {
        found = e.tagCacheFound; // tagLocal depends only on the tag name and the model
        tag = e.tagCachePos;
    } else {
        found = tagLocal(e.parent, e.tagName, tag);
        e.tagCacheValid = true;
        e.tagCacheFound = found;
        e.tagCacheModel = p.model;
        e.tagCachePos = tag;
    }
    if (!found) {
        // "Tag '%s' not found in model '%s'": the original detaches the child. A pool
        // entity is detached; a definition child keeps using the parent origin (it is
        // not in the pool list, so detaching it would orphan it).
        if (e.inList) {
            AS3D_WARN("Tag '%s' not found in model '%s'", e.tagName.c_str(), p.modelPath.c_str());
            // The detached entity no longer pins its old root (docs/spec/issues/113): without
            // this a removed root (e.g. a rocket launcher whose muzzle flash asked for a
            // missing tag) would never be freed.
            if (e.countedInRoot) {
                int r = rootOf(e.parent);
                if (r >= 0) --ents_[static_cast<size_t>(r)].attachRefCount;
                e.countedInRoot = false;
            }
            e.parent = -1;
            return;
        }
        tag = {0.0f, 0.0f, 0.0f};
    }
    float s = p.f(F_SCALE);
    float k = s > 0.01f ? s : 1.0f;
    Vec3 off = e.v3(F_ATTACH_OFFSET);
    Vec3 local = e.absAttach ? (tag + off) * k : tag * k;
    Vec3 base = p.v3(F_BASE_ORIGIN);
    Vec3 fw = p.v3(F_AXIS), lf = p.v3(F_AXIS + 3), up = p.v3(F_AXIS + 6);
    Vec3 pos = base + fw * local.x + lf * local.y + up * local.z;
    if (!e.absAttach) pos = pos + off;
    e.setV3(F_ORIGIN, pos);
}

void World::setupTransform(int idx) {
    Entity& e = ents_[static_cast<size_t>(idx)];
    bool root = e.parent < 0;
    if (root) snapToGround(e);
    float axis[9];
    const int fl = root ? e.flagBits() : 0; // (read by the root branches only)
    if (root && (fl & 3) == 3 && terrainValid_) {
        // FL_ONGROUND_NORMAL: plane through three terrain samples (hmap.md "Spawning"),
        // combined with the yaw.
        float x = e.f(F_ORIGIN), y = e.f(F_ORIGIN + 1);
        Vec3 a{x - 10.0f, y + 15.0f, terrainHeight(x - 10.0f, y + 15.0f)};
        Vec3 b{x + 10.0f, y + 15.0f, terrainHeight(x + 10.0f, y + 15.0f)};
        Vec3 c{x, y - 25.0f, terrainHeight(x, y - 25.0f)};
        Vec3 n = cross(b - a, c - a);
        if (n.z < 0.0f) n = -n;
        n = normalize(n);
        float yaw = e.f(F_ANGLES + 2) * kDegToRad;
        Vec3 f0{std::cos(yaw), std::sin(yaw), 0.0f};
        Vec3 left = normalize(cross(n, f0));
        Vec3 fwd = cross(left, n);
        float m[9] = {fwd.x, fwd.y, fwd.z, left.x, left.y, left.z, n.x, n.y, n.z};
        std::copy(m, m + 9, axis);
    } else if (root && rules_->waterFlags && (fl & kFlOnWaterTiltBit) && terrainValid_) {
        // FL_ONWATER_NORMAL (as2 G_AlignToWater, engine-behaviour.delta.md 4.2): the same
        // construction on the plane through the water surface at the same three points.
        const Vec3 n = waterSample(e.f(F_ORIGIN), e.f(F_ORIGIN + 1)).normal;
        float yaw = e.f(F_ANGLES + 2) * kDegToRad;
        Vec3 f0{std::cos(yaw), std::sin(yaw), 0.0f};
        Vec3 left = normalize(cross(n, f0));
        Vec3 fwd = cross(left, n);
        float m[9] = {fwd.x, fwd.y, fwd.z, left.x, left.y, left.z, n.x, n.y, n.z};
        std::copy(m, m + 9, axis);
    } else {
        // anglesToAxisRows is a pure function of the three angle values: its last result
        // is reused while their bits are unchanged (most entities do not turn every frame;
        // six sin/cos per call).
        const u32* key = &e.fields[F_ANGLES];
        if (!(e.axisCacheValid && e.axisCacheKey[0] == key[0] && e.axisCacheKey[1] == key[1] &&
              e.axisCacheKey[2] == key[2])) {
            anglesToAxisRows(e.f(F_ANGLES), e.f(F_ANGLES + 1), e.f(F_ANGLES + 2), e.axisCache);
            for (int k = 0; k < 3; ++k) e.axisCacheKey[k] = key[k];
            e.axisCacheValid = true;
        }
        std::copy(e.axisCache, e.axisCache + 9, axis);
    }
    if (!root && e.absAttach && validIndex(e.parent)) {
        // Child axis = own axis x tag axis (identity in v1.70, mdl.md) x parent axis.
        const Entity& p = ents_[static_cast<size_t>(e.parent)];
        float pa[9];
        for (int k = 0; k < 9; ++k) pa[k] = p.f(F_AXIS + k);
        float r[9];
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                r[i * 3 + j] = axis[i * 3 + 0] * pa[0 * 3 + j] + axis[i * 3 + 1] * pa[1 * 3 + j] +
                               axis[i * 3 + 2] * pa[2 * 3 + j];
            }
        }
        std::copy(r, r + 9, axis);
    }
    for (int k = 0; k < 9; ++k) e.setF(F_AXIS + k, axis[k]);
    for (int k = 0; k < 3; ++k) {
        e.fields[F_BASE_ORIGIN + k] = e.fields[F_ORIGIN + k];
        e.fields[F_PREV_ORIGIN + k] = e.fields[F_ORIGIN + k];
    }
}

void World::think(int idx) {
    if (!validIndex(idx)) return;
    Entity& e = ents_[static_cast<size_t>(idx)];
    if (e.rt & (RT_REMOVED | RT_THOUGHT)) return;
    e.rt |= RT_THOUGHT;
    thoughtSlots_.push_back(idx);
    // For an attached pool entity the parent is thought first.
    if (e.inList && validIndex(e.parent)) {
        int r = rootOf(e.parent);
        // (think(r) returns at once for a root that has thought: not called then)
        if (validIndex(r) && ents_[static_cast<size_t>(r)].inList && !(ents_[static_cast<size_t>(r)].rt & (RT_REMOVED | RT_THOUGHT)))
            think(r);
    }
    if (!paused_) {
        e.setF(F_AGE, e.f(F_AGE) + frametime_);
        e.sinceDamage += frametime_;
        // The sequels' Lightning timer (as2/rcsl-vm.delta.md, entity update).
        if (e.lightningTimer < rules_->lightningTimerCap) e.lightningTimer += frametime_;
        // (dispatch does nothing for an entity without a thread: not called for those)
        if (e.thread && (e.rt & RT_ACTIVE) && e.state != ES_DORMANT) {
            const bool parentAllows = !validIndex(e.parent) || ents_[static_cast<size_t>(e.parent)].state != ES_DORMANT;
            if (parentAllows) dispatch(idx, EntryPoint::Main);
        }
    }
    if (validIndex(e.parent)) {
        if (ents_[static_cast<size_t>(e.parent)].rt & RT_COLLIDABLE) e.rt |= RT_COLLIDABLE;
        else e.rt &= ~RT_COLLIDABLE;
        attachToTag(idx);
    }
    setupTransform(idx);
    if (e.emitter) return; // (computeScreenBounds does nothing for an emitter holder)
    if (!validIndex(e.parent) || (e.rt & RT_ATTACHED_ENTITY)) computeScreenBounds(idx);
    for (size_t i = 0; i < e.children.size(); ++i) {
        int c = e.children[i];
        if (validIndex(c) && ents_[static_cast<size_t>(c)].parent == idx) think(c);
    }
}

bool World::inActivationArea(const Entity& e) const {
    float x = e.f(F_ORIGIN), y = e.f(F_ORIGIN + 1), z = e.f(F_ORIGIN + 2), r = e.radius;
    return y - r >= mapPos_ + 16.0f && y - r <= mapPos_ + 800.0f && x + r >= 0.0f && x - r <= 1280.0f &&
           z + r >= hmin_;
}

void World::freeRemoved() {
    std::vector<int>& doomed = doomedScratch_;
    doomed.clear();
    for (auto it = listOldest_.rbegin(); it != listOldest_.rend(); ++it) { // newest first
        const Entity& e = ents_[static_cast<size_t>(*it)];
        if ((e.rt & RT_REMOVED) && e.attachRefCount < 1) doomed.push_back(*it);
    }
    if (doomed.empty()) return;
    std::vector<int>& freed = freedScratch_;
    freed.clear();
    for (int i : doomed) {
        unlink(i);
        freeTree(i, freed);
        ++stats_.entitiesFreed; // pool entities (children are freed with them)
    }
    // The list array without the unlinked entities (nothing was linked meanwhile).
    listOldest_.erase(std::remove_if(listOldest_.begin(), listOldest_.end(),
                                     [this](int i) { return !ents_[static_cast<size_t>(i)].inList; }),
                      listOldest_.end());
    // Pool entities attached (AttachEntity) to a freed slot lose their parent. Done once for
    // all the slots freed above: the same links end up cleared as with a walk per freed
    // slot, since nothing between the frees reads the parent of a list entity (an entity
    // freed later in the loop is freed whatever its parent; see freeTree).
    for (int i : freed) freedMark_[static_cast<size_t>(i)] = 1;
    for (int i : listOldest_) {
        Entity& o = ents_[static_cast<size_t>(i)];
        if (o.parent >= 0 && freedMark_[static_cast<size_t>(o.parent)]) o.parent = -1;
    }
    for (int i : freed) freedMark_[static_cast<size_t>(i)] = 0;
}

namespace {
// The lines of an entity the passes read (the hot members and the script fields 0..52,
// see Entity): its first 128 bytes and the fields.
inline void prefetchEntityHot(const Entity& e) {
#if defined(__GNUC__) || defined(__clang__)
    const char* p = reinterpret_cast<const char*>(&e);
    __builtin_prefetch(p);
    __builtin_prefetch(p + 64);
    const char* f = reinterpret_cast<const char*>(e.fields);
    __builtin_prefetch(f);
    __builtin_prefetch(f + 64);
    __builtin_prefetch(f + 128);
    __builtin_prefetch(f + 192);
#else
    (void)e;
#endif
}
} // namespace

// The entity pass walks the list newest first and thinks each entity, then its definition
// children (entity -> child array -> child: dependent loads). Late in a mission the slots in
// use do not fit the caches, so the walk asks for them ahead: the hot lines of the entity 4
// places ahead, the child array of the one 2 ahead (whose first line asked for itself two
// places earlier). No value changes.
void World::prefetchAhead(size_t k) const {
#if defined(__GNUC__) || defined(__clang__)
    if (k >= 4) prefetchEntityHot(ents_[static_cast<size_t>(listOldest_[k - 4])]);
    if (k >= 2) {
        const Entity& b = ents_[static_cast<size_t>(listOldest_[k - 2])];
        if (!b.children.empty()) __builtin_prefetch(b.children.data());
    }
#else
    (void)k;
#endif
}

void World::runEntities() {
    freeRemoved();
    // A new pass: every entity may think once (runtime bit 0x02). Entities created before
    // this point in the frame (map spawns, their inits' creates) think again here; ones
    // created during the pass are newer than the walk and keep the bit from their
    // immediate think (GUESS for where the original clears the bit: issue 031).
    // Only slots in thoughtSlots_ can carry the bit, so this equals clearing it on every
    // slot in use.
    for (int i : thoughtSlots_) {
        Entity& e = ents_[static_cast<size_t>(i)];
        if (e.inUse) e.rt &= ~RT_THOUGHT;
    }
    thoughtSlots_.clear();
    // Newest first over the list as it is now: entities linked during the pass are appended
    // past `k` and not visited, and nothing is unlinked before the next freeRemoved.
    for (size_t k = listOldest_.size(); k-- > 0;) {
        const int i = listOldest_[k];
        prefetchAhead(k);
        Entity& e = ents_[static_cast<size_t>(i)];
        if (!(e.rt & RT_REMOVED)) {
            bool skip = false;
            if (!paused_) {
                bool in = inActivationArea(e);
                if (e.state == ES_DORMANT) {
                    if (in) setStateRecursive(i, ES_ACTIVE);
                } else if (e.state == ES_ACTIVE) {
                    if (!in && !(e.flagBits() & FL_TEMPORARY)) setStateRecursive(i, ES_LEAVING);
                } else if (e.state == ES_LEAVING) {
                    // Removed once out of the view frustum. The second test of the
                    // original (0x405140, visible terrain box) is not specified.
                    if (!sphereInFrustum(e.v3(F_ORIGIN), e.radius)) {
                        removeEntity(i);
                        skip = true;
                    }
                }
            }
            if (!skip) think(i);
        }
    }
    if (!phaseTiming_) {
        runCollisions();
        return;
    }
    const auto t0 = std::chrono::steady_clock::now(); // as3d_sim --time (measurement only)
    runCollisions();
    double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    phaseTimes_.collisions += dt;
    phaseTimes_.entities -= dt; // step() laps the whole entity pass
}

} // namespace as3d
