// Damage, death, drops and score (docs/spec/engine-behaviour.md 6).
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "as3d/defs.h"
#include "as3d/world.h"
#include "world_internal.h"

namespace as3d {

using script::EntryPoint;

int World::attackerFor(int callerIdx) const {
    if (config_.players < 2) return 0;
    if (!validIndex(callerIdx)) return 0;
    return (ents_[static_cast<size_t>(callerIdx)].flagBits() & 0x2000) ? 0 : 1;
}

void World::damageEntity(int idx, float amount, int attacker) {
    if (!validIndex(idx)) return;
    Entity& e = ents_[static_cast<size_t>(idx)];
    if (e.f(F_DEAD) != 0.0f) return;
    if (e.rt & RT_HEALTH_FROZEN) return;
    int p = -1;
    if (isPlayerEntity(idx, &p)) {
        if (players_[p].freezeCount != 0 || config_.godMode) return;
    }
    e.sinceDamage = 0.0f;
    e.setF(F_HEALTH, e.f(F_HEALTH) - amount);
    dispatch(idx, EntryPoint::Damage);
    if (!(e.f(F_HEALTH) <= 0.0f)) return;
    if (e.f(F_CLASS) == kClassEnemy && !(e.flagBits() & FL_NONTARGET) && attacker >= 0 &&
        attacker < config_.players) {
        ++players_[attacker].kills;
    }
    e.setF(F_DEAD, 1.0f);
    if (e.drop) {
        const ObjectDef* drop = e.drop;
        e.drop = nullptr;
        // Spawned like `create` (init and one think); GUESS on the think, spec 6.1.
        createEntity(drop, e.v3(F_ORIGIN), -1, true);
    }
    if (attacker >= 0 && attacker < config_.players) awardScore(attacker, idx);
}

void World::awardScore(int p, int idx) {
    if (p < 0 || p >= config_.players || !validIndex(idx)) return;
    PlayerRecord& pr = players_[p];
    Entity& e = ents_[static_cast<size_t>(idx)];
    i32 amount = ftol(e.f(F_SCORE));
    pr.scores = std::min(pr.scores + static_cast<float>(amount), 1.0e9f);
    // Floating digits (6.4). Nothing is shown for a zero or negative award (GUESS).
    if (amount <= 0 || !db_) return;
    if (!rules_->scoreDigitObject) return;
    const ObjectDef* digitDef = db_->findObject(rules_->scoreDigitObject);
    if (!digitDef) return;
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d", amount);
    int n = static_cast<int>(std::strlen(buf));
    Vec3 o = e.v3(F_ORIGIN);
    for (int k = 0; k < n; ++k) {
        Vec3 pos{o.x - (n * 12.0f / 2.0f - 6.0f) + 12.0f * k, o.y, o.z + 8.0f};
        int d = createEntity(digitDef, pos, -1, false);
        if (d >= 0) ents_[static_cast<size_t>(d)].setF(F_FRAME, static_cast<float>(buf[k] - '0'));
    }
}

} // namespace as3d
