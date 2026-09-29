// Entity pass: state machine, think, attachment and transforms
// (docs/spec/engine-behaviour.md 2 step 6, 3.5, 4).
#include <algorithm>
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
    if (!tagLocal(e.parent, e.tagName, tag)) {
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
    int fl = e.flagBits();
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
        anglesToAxisRows(e.f(F_ANGLES), e.f(F_ANGLES + 1), e.f(F_ANGLES + 2), axis);
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
    // For an attached pool entity the parent is thought first.
    if (e.inList && validIndex(e.parent)) {
        int r = rootOf(e.parent);
        if (validIndex(r) && ents_[static_cast<size_t>(r)].inList) think(r);
    }
    if (!paused_) {
        e.setF(F_AGE, e.f(F_AGE) + frametime_);
        e.sinceDamage += frametime_;
        // The sequels' Lightning timer (as2/rcsl-vm.delta.md, entity update).
        if (e.lightningTimer < rules_->lightningTimerCap) e.lightningTimer += frametime_;
        bool parentAllows = !validIndex(e.parent) || ents_[static_cast<size_t>(e.parent)].state != ES_DORMANT;
        if ((e.rt & RT_ACTIVE) && e.state != ES_DORMANT && parentAllows) dispatch(idx, EntryPoint::Main);
    }
    if (validIndex(e.parent)) {
        if (ents_[static_cast<size_t>(e.parent)].rt & RT_COLLIDABLE) e.rt |= RT_COLLIDABLE;
        else e.rt &= ~RT_COLLIDABLE;
        attachToTag(idx);
    }
    setupTransform(idx);
    if (!validIndex(e.parent) || (e.rt & RT_ATTACHED_ENTITY)) computeScreenBounds(idx);
    if (e.emitter) return;
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
    std::vector<int> doomed;
    for (int i = newest_; i != -1; i = ents_[static_cast<size_t>(i)].older) {
        const Entity& e = ents_[static_cast<size_t>(i)];
        if ((e.rt & RT_REMOVED) && e.attachRefCount < 1) doomed.push_back(i);
    }
    for (int i : doomed) {
        unlink(i);
        freeTree(i);
        ++stats_.entitiesFreed; // pool entities (children are freed with them)
    }
}

void World::runEntities() {
    freeRemoved();
    // A new pass: every entity may think once (runtime bit 0x02). Entities created before
    // this point in the frame (map spawns, their inits' creates) think again here; ones
    // created during the pass are newer than the walk and keep the bit from their
    // immediate think (GUESS for where the original clears the bit: issue 031).
    for (Entity& e : ents_) {
        if (e.inUse) e.rt &= ~RT_THOUGHT;
    }
    for (int i = newest_; i != -1;) {
        Entity& e = ents_[static_cast<size_t>(i)];
        int older = e.older;
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
        i = older;
    }
    runCollisions();
}

} // namespace as3d
