// Entity builtins: creation, removal, activation, attachment, callbacks
// (docs/spec/rcsl-builtins-table.md "Entities", engine-behaviour.md 3).
#include <algorithm>
#include <cmath>
#include <cstring>

#include "as3d/defs.h"
#include "as3d/model.h"
#include "builtins_common.h"

namespace as3d {
namespace builtins {

namespace {

// create(name, pos): spawns the definition at pos with self's angles and player index,
// runs its init and one think, returns the reference (0.0 on failure).
// Not reproduced: the original writes the reference into the (engine-global) return
// register *before* the new entity's init runs, so a RET in that init changes what the
// caller receives (rcsl-vm.md quirk 9). Our VM keeps one return register per thread.
void bCreate(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    float pos[3];
    if (!vecArg(a, 1, pos)) return;
    const char* name = strArg(a, 0);
    const ObjectDef* def = name ? w.db().findObject(name) : nullptr;
    if (!def) {
        a.setReturnBits(0);
        return;
    }
    int idx = w.createEntity(def, Vec3{pos[0], pos[1], pos[2]}, selfOf(w), true);
    a.setReturnBits(idx >= 0 ? w.refOf(idx) : 0u);
}

void bRemove(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    w.removeEntity(entityArg(w, a, 0));
}

void bActivate(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    w.setActive(entityArg(w, a, 0), true);
}

void bDeactivate(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    w.setActive(entityArg(w, a, 0), false);
}

void bGetentity(BuiltinArgs& a, void*) { a.setReturnBits(a.bits(0) + kEntityRefOffset); }

void bCallback(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int e = entityArg(w, a, 0);
    if (e < 0) return;
    w.runCallback(e, a.f32(1), a.f32(2), a.f32(3));
}

void bAttachEntity(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int child = entityArg(w, a, 0);
    if (child < 0) return;
    int parent = entityArg(w, a, 1);
    const char* tag = strArg(a, 2);
    w.attachEntity(child, parent, tag ? tag : "", intArg(a, 3) != 0);
}

// First child of self with this attach id; `skipRemoved` as the builtin requires.
int findChild(World& w, int self, const char* name, bool skipRemoved) {
    if (self < 0 || !name) return -1;
    const Entity& e = w.entity(self);
    for (int c : e.children) {
        if (!w.validIndex(c)) continue;
        const Entity& ce = w.entity(c);
        if (ce.parent != self || ce.name != name) continue;
        if (skipRemoved && (ce.rt & RT_REMOVED)) continue;
        return c;
    }
    return -1;
}

void bAttachActivate(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int c = findChild(w, selfOf(w), strArg(a, 0), true);
    if (c >= 0 && !(w.entity(c).rt & RT_ACTIVE)) w.setActive(c, true);
}

void bAttachDeactivate(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int c = findChild(w, selfOf(w), strArg(a, 0), true);
    if (c >= 0 && (w.entity(c).rt & RT_ACTIVE)) w.setActive(c, false);
}

void bAttachCallback(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int c = findChild(w, selfOf(w), strArg(a, 0), false);
    if (c < 0) return;
    const Entity& ce = w.entity(c);
    if ((ce.rt & RT_REMOVED) || !(ce.rt & RT_ACTIVE)) return;
    w.runCallback(c, a.f32(1), a.f32(2), a.f32(3));
}

void bParentCallback(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int s = selfOf(w);
    if (s < 0) return;
    int p = w.entity(s).parent;
    if (!w.validIndex(p)) return;
    w.runCallback(p, a.f32(0), a.f32(1), a.f32(2));
}

// IsValidTarget reads the fields through the reference itself (any slot, like the
// original reading possibly freed memory).
void bIsValidTarget(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    u32 ref = a.bits(0);
    int i = w.indexFromRef(ref);
    bool ok = ref != 0 && i >= 0 && w.entity(i).f(F_DEAD) == 0.0f && w.entity(i).f(F_HEALTH) > 0.0f;
    a.setReturnFloat(ok ? 1.0f : 0.0f);
}

void bFreezeHealth(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int e = entityArg(w, a, 0);
    if (e < 0) return;
    if (a.f32(1) != 0.0f) w.entity(e).rt |= RT_HEALTH_FROZEN;
    else w.entity(e).rt &= ~RT_HEALTH_FROZEN;
}

// SetModel(name): self's model = model lookup of name. Only the collision-relevant part
// (box, radius, tags) exists in this headless world.
void bSetModel(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int s = selfOf(w);
    const char* name = strArg(a, 0);
    if (s < 0 || !name) return;
    const ModelData* m = w.loadModel(name);
    if (!m) return;
    Entity& e = w.entity(s);
    e.model = m;
    e.modelPath = normalizePath(name);
    e.boundsMin = m->boundsMin;
    e.boundsMax = m->boundsMax;
    float x = std::max(std::fabs(m->boundsMin.x), std::fabs(m->boundsMax.x));
    float y = std::max(std::fabs(m->boundsMin.y), std::fabs(m->boundsMax.y));
    float z = std::max(std::fabs(m->boundsMin.z), std::fabs(m->boundsMax.z));
    e.radius = std::sqrt(x * x + y * y + z * z);
}

const BuiltinDesc kTable[] = {
    {"create", 2, bCreate, nullptr, BuiltinStatus::Implemented},
    {"remove", 1, bRemove, nullptr, BuiltinStatus::Implemented},
    {"activate", 1, bActivate, nullptr, BuiltinStatus::Implemented},
    {"deactivate", 1, bDeactivate, nullptr, BuiltinStatus::Implemented},
    {"getentity", 1, bGetentity, nullptr, BuiltinStatus::Implemented},
    {"callback", 4, bCallback, nullptr, BuiltinStatus::Implemented},
    {"AttachEntity", 4, bAttachEntity, nullptr, BuiltinStatus::Implemented},
    {"AttachActivate", 1, bAttachActivate, nullptr, BuiltinStatus::Implemented},
    {"AttachDeactivate", 1, bAttachDeactivate, nullptr, BuiltinStatus::Implemented},
    {"AttachCallback", 4, bAttachCallback, nullptr, BuiltinStatus::Implemented},
    {"ParentCallback", 3, bParentCallback, nullptr, BuiltinStatus::Implemented},
    {"IsValidTarget", 1, bIsValidTarget, nullptr, BuiltinStatus::Implemented},
    {"FreezeHealth", 2, bFreezeHealth, nullptr, BuiltinStatus::Implemented},
    {"SetModel", 1, bSetModel, nullptr, BuiltinStatus::Approximate},
};

} // namespace

Family entityBuiltins() { return {kTable, sizeof kTable / sizeof kTable[0]}; }

} // namespace builtins
} // namespace as3d
