// Weapons, damage and targeting builtins (docs/spec/engine-behaviour.md 6, 8;
// rcsl-builtins-table.md "Complex gameplay builtins").
#include <cmath>

#include "as3d/defs.h"
#include "builtins_common.h"

namespace as3d {
namespace builtins {

namespace {

float ft(World& w) { return w.frametimeGlobal; }

// Shoot(weapon, tag, dir) (engine-behaviour.md 8.1). Returns nothing to the script (the
// builtin table's VERIFIED-CODE return kind is "none").
void bShoot(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    float dir[3];
    if (!vecArg(a, 2, dir)) return;
    int s = selfOf(w);
    if (s < 0) return;
    // 1. Only on-screen shooters that are not leaving can fire.
    {
        const Entity& se = w.entity(s);
        if (!(se.rt & RT_COLLIDABLE) || se.state == ES_LEAVING) return;
    }
    const char* wname = strArg(a, 0);
    const char* tagc = strArg(a, 1);
    std::string tag = tagc ? tagc : "origin";
    const WeaponDef* wd = wname ? w.db().findWeapon(wname) : nullptr;
    if (!wd) return; // "ERROR: Unknown weapon '%s'."
    const ObjectDef* md = w.db().findObject(wd->missileName);
    if (!md) return;
    // 2. Muzzle.
    Vec3 muzzle = w.tagWorldPosition(s, tag);
    // 3. The projectile.
    int m = w.spawnRoot(md, muzzle);
    if (m < 0) return;
    Entity& me = w.entity(m);
    const Entity& se = w.entity(s);
    Vec3 d{dir[0], dir[1], dir[2]};
    float len = length(d);
    if (len > 0.0f) d = d * (1.0f / len);
    me.setF(F_CLASS, kClassProjectile);
    float ang[3];
    vecToAngles(d.x, d.y, d.z, ang);
    for (int k = 0; k < 3; ++k) me.setF(F_ANGLES + k, ang[k]);
    Vec3 o = me.v3(F_ORIGIN) + d * (-me.boundsMin.y);
    me.setV3(F_ORIGIN, o);
    me.setV3(F_PREV_ORIGIN, o);
    me.setV3(F_VELOCITY, d * wd->speed);
    w.entity(m).playerIndex = se.playerIndex;
    for (int c : me.children) {
        if (w.validIndex(c)) w.entity(c).playerIndex = se.playerIndex;
    }
    // 4. init, then one think.
    w.runInit(m);
    w.think(m);
    // 5. Enemy projectiles are scaled here and again by Damage (6.2).
    if (!w.isPlayerEntity(s)) me.setF(F_DAMAGE, me.f(F_DAMAGE) * w.damageFactor());
    // 6. Two-player ownership bits.
    if (w.numPlayers() == 2) {
        int fl = me.flagBits() | (se.playerIndex == 0 ? 0x2000 : 0x4000);
        me.setF(F_FLAGS, static_cast<float>(fl));
    }
    // 7. Muzzle flash attached to the shooter at the tag (as AttachEntity, non-abs).
    if (!wd->flashName.empty()) {
        if (const ObjectDef* fd = w.db().findObject(wd->flashName)) {
            int f = w.spawnRoot(fd, muzzle);
            if (f >= 0) {
                w.attachEntity(f, s, tag, false);
                w.entity(f).playerIndex = w.entity(s).playerIndex;
                w.runInit(f);
            }
        }
    }
}

void bDamage(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int t = entityArg(w, a, 0);
    if (t < 0) return;
    w.damageEntity(t, a.f32(1) * w.damageFactor(), w.attackerFor(selfOf(w)));
}

// RadialDamage(center, radius, rate): frametime * rate * (d / radius) * g_damage_factor
// to every live enemy with health > 0 within radius (grows toward the edge, 6.2).
void bRadialDamage(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    float c[3];
    if (!vecArg(a, 0, c)) return;
    float radius = a.f32(1), rate = a.f32(2);
    if (!(radius > 0.0f)) return; // the original divides by zero
    int attacker = w.attackerFor(selfOf(w));
    for (int i : w.listEntities()) {
        const Entity& e = w.entity(i);
        if (!e.inUse || (e.rt & RT_REMOVED) || e.f(F_CLASS) != kClassEnemy || !(e.f(F_HEALTH) > 0.0f)) continue;
        Vec3 d = e.v3(F_ORIGIN) - Vec3{c[0], c[1], c[2]};
        float dist = length(d);
        if (dist > radius) continue;
        w.damageEntity(i, ft(w) * rate * (dist / radius) * w.damageFactor(), attacker);
    }
}

// TraceLineDamage(from, to, dmg): screen-space segment against the rectangles of the
// players (caller TOUCH_PLAYER) or of on-screen model enemies (caller TOUCH_ENEMIES).
void bTraceLineDamage(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    float p0[3], p1[3];
    if (!vecArg(a, 0, p0) || !vecArg(a, 1, p1)) return;
    int s = selfOf(w);
    if (s < 0) return;
    float s0[3], s1[3];
    if (!w.projectPoint(Vec3{p0[0], p0[1], p0[2]}, s0) || !w.projectPoint(Vec3{p1[0], p1[1], p1[2]}, s1)) return;
    float dmg = a.f32(2);
    int tm = w.entity(s).touchMode;
    if (tm == 2) {
        for (int p = 0; p < w.numPlayers(); ++p) {
            int pi = w.playerEntityIndex(p);
            if (pi < 0) continue;
            if (World::segmentHitsRect(s0, s1, w.entity(pi).rect)) w.damageEntity(pi, dmg, -1);
        }
    } else if (tm == 1) {
        for (int i : w.listEntities()) {
            const Entity& e = w.entity(i);
            if (!e.inUse || (e.rt & RT_REMOVED) || e.f(F_CLASS) != kClassEnemy || !(e.rt & RT_COLLIDABLE)) continue;
            if (w.isPointCollider(e) || e.f(F_DEAD) != 0.0f) continue;
            if (World::segmentHitsRect(s0, s1, e.rect)) w.damageEntity(i, dmg, -1);
        }
    }
}

// Lightning(): damage part only (the bolt is a render effect). Reads no arguments.
void bLightning(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int s = selfOf(w);
    if (s < 0) return;
    Vec3 o = w.entity(s).v3(F_ORIGIN);
    float amount = w.entity(s).f(F_DAMAGE) * ft(w) * w.damageFactor();
    for (int i : w.listEntities()) {
        const Entity& e = w.entity(i);
        if (!e.inUse || (e.rt & RT_REMOVED) || e.f(F_CLASS) != kClassEnemy || !(e.rt & RT_COLLIDABLE)) continue;
        if (e.f(F_DEAD) != 0.0f) continue;
        if (length(e.v3(F_ORIGIN) - o) > 500.0f) continue;
        w.damageEntity(i, amount, -1);
    }
}

// LockTarget(): nearest (3D, from the caller's player, search radius 9999) on-screen,
// targetable, living, unlocked enemy ahead of the player (normalised d.y >= 0.3).
void bLockTarget(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int s = selfOf(w);
    int pe = s >= 0 ? w.playerEntityIndex(w.entity(s).playerIndex) : -1;
    if (pe < 0) {
        a.setReturnBits(0);
        return;
    }
    Vec3 po = w.entity(pe).v3(F_ORIGIN);
    float best = 9999.0f;
    int found = -1;
    for (int i : w.listEntities()) {
        const Entity& e = w.entity(i);
        if ((e.rt & (RT_REMOVED | RT_LOCKED)) || !(e.rt & RT_COLLIDABLE)) continue;
        if (e.f(F_CLASS) != kClassEnemy || (e.flagBits() & FL_NONTARGET)) continue;
        if (e.f(F_DEAD) != 0.0f || !(e.f(F_HEALTH) > 0.0f)) continue;
        Vec3 d = e.v3(F_ORIGIN) - po;
        float len = length(d);
        if (!(len > 0.0f) || d.y / len < 0.3f) continue;
        if (len < best) {
            best = len;
            found = i;
        }
    }
    if (found < 0) {
        a.setReturnBits(0);
        return;
    }
    w.entity(found).rt |= RT_LOCKED;
    a.setReturnBits(w.refOf(found));
}

// PushPlayer(): player velocity.xy += normalize(player - self).xy * 4000 * frametime.
void bPushPlayer(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int s = selfOf(w);
    if (s < 0) return;
    int pe = w.playerEntityIndex(w.entity(s).playerIndex);
    if (pe < 0) return;
    Entity& p = w.entity(pe);
    float dx = p.f(F_ORIGIN) - w.entity(s).f(F_ORIGIN);
    float dy = p.f(F_ORIGIN + 1) - w.entity(s).f(F_ORIGIN + 1);
    float len = std::sqrt(dx * dx + dy * dy);
    if (len > 0.0f) {
        dx /= len;
        dy /= len;
    }
    float k = 4000.0f * ft(w);
    p.setF(F_VELOCITY, p.f(F_VELOCITY) + dx * k);
    p.setF(F_VELOCITY + 1, p.f(F_VELOCITY + 1) + dy * k);
}

const BuiltinDesc kTable[] = {
    {"Shoot", 3, bShoot, nullptr, BuiltinStatus::Approximate},
    {"Damage", 2, bDamage, nullptr, BuiltinStatus::Implemented},
    {"RadialDamage", 3, bRadialDamage, nullptr, BuiltinStatus::Implemented},
    {"TraceLineDamage", 3, bTraceLineDamage, nullptr, BuiltinStatus::Implemented},
    {"Lightning", 0, bLightning, nullptr, BuiltinStatus::Approximate},
    {"LockTarget", 0, bLockTarget, nullptr, BuiltinStatus::Implemented},
    {"PushPlayer", 0, bPushPlayer, nullptr, BuiltinStatus::Implemented},
};

} // namespace

Family combatBuiltins() { return {kTable, sizeof kTable / sizeof kTable[0]}; }

} // namespace builtins
} // namespace as3d
