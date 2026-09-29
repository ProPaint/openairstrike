// Weapons, damage and targeting builtins (docs/spec/rcsl-builtins-semantics.md family D,
// PushPlayer; engine-behaviour.md 6).
#include <cmath>

#include "as3d/defs.h"
#include "builtins_common.h"

namespace as3d {
namespace builtins {

namespace {

float ft(World& w) { return w.frametimeGlobal; }

bool isPlayerRef(World& w, int p, int idx) {
    return p < w.numPlayers() && idx >= 0 && w.player(p).entityRef == w.refOf(idx);
}

// Shoot(weapon, point, dir) (rcsl-builtins-semantics.md 46, steps 1-12). Returns nothing.
void bShoot(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    float dir[3];
    if (!vecArg(a, 2, dir)) return;
    int s = selfOf(w);
    if (s < 0) return;
    // 1. Off-screen or leaving shooters cannot fire; in the sequels, dead ones neither (field
    //    4 not exactly 0, a NaN included; as2/rcsl-builtins-semantics.delta.md 46).
    if (!(w.entity(s).rt & RT_COLLIDABLE) || w.entity(s).state == ES_LEAVING) return;
    if (w.rules().deadShootersBlocked && w.entity(s).f(F_DEAD) != 0.0f) return;
    const char* pointc = strArg(a, 1);
    std::string point = pointc ? pointc : "origin";
    // 2. Muzzle from the shooter's last-think base origin and axis (a missing tag, or no
    //    model, gives a zero offset: engine decision for the original's stack garbage).
    Vec3 muzzle = w.tagWorldPosition(s, point);
    // 3. Weapon.
    const char* wname = strArg(a, 0);
    const WeaponDef* wd = wname ? w.db().findWeapon(wname) : nullptr;
    if (!wd) return; // "ERROR: Unknown weapon '%s'."
    // 4-9. The projectile.
    int p = -1;
    if (const ObjectDef* md = w.db().findObject(wd->missileName)) p = w.spawnRoot(md, Vec3{0, 0, 0}, false);
    if (p >= 0) {
        Entity& pe = w.entity(p);
        pe.setF(F_CLASS, kClassProjectile);
        pe.setV3(F_ORIGIN, muzzle);
        pe.setV3(F_PREV_ORIGIN, muzzle);
        Vec3 v{dir[0], dir[1], dir[2]};
        float len = length(v);
        if (len != 0.0f) v = v * (1.0f / len);
        float ang[3];
        vecToAngles(v.x, v.y, v.z, ang);
        for (int k = 0; k < 3; ++k) pe.setF(F_ANGLES + k, ang[k]);
        if (pe.fields[F_RENDER_TYPE] == 0) pe.setV3(F_ORIGIN, pe.v3(F_ORIGIN) + v * (-pe.boundsMin.y));
        pe.setV3(F_VELOCITY, v * wd->speed);
        // 9. init (not setting the time since damage), direct children's init (not
        //    recursive, no player index propagation), then one think.
        w.dispatch(p, script::EntryPoint::Init);
        std::vector<int> kids = pe.children;
        for (int c : kids) {
            if (w.validIndex(c) && w.entity(c).thread) w.dispatch(c, script::EntryPoint::Init);
        }
        w.think(p);
        // 10. Difficulty scaling of non-player projectiles, after init and first main.
        bool byPlayer = isPlayerRef(w, 0, s) || (w.numPlayers() == 2 && isPlayerRef(w, 1, s));
        if (!byPlayer) pe.setF(F_DAMAGE, pe.f(F_DAMAGE) * w.damageFactor());
        // 11. Two-player ownership.
        if (w.numPlayers() == 2) {
            if (isPlayerRef(w, 0, s)) {
                pe.playerIndex = 0;
                pe.setF(F_FLAGS, static_cast<float>(pe.flagBits() | 0x2000));
            } else if (isPlayerRef(w, 1, s)) {
                pe.playerIndex = 1;
                pe.setF(F_FLAGS, static_cast<float>(pe.flagBits() | 0x4000));
            } else {
                pe.playerIndex = w.entity(s).playerIndex;
            }
        }
    }
    // 12. Muzzle flash: a linked pool entity on the shooter's tag, abs, not thought now.
    if (!wd->flashName.empty()) {
        if (const ObjectDef* fd = w.db().findObject(wd->flashName)) {
            int f = w.spawnRoot(fd, Vec3{0, 0, 0}, false);
            if (f >= 0) {
                w.attachEntity(f, s, point, true);
                w.dispatch(f, script::EntryPoint::Init);
            }
        }
    }
}

void bDamage(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int t = entityArg(w, a, 0);
    if (t < 0) return;
    float amount = a.f32(1) * w.damageFactor();
    w.damageEntity(t, amount, w.attackerFor(selfOf(w)));
}

// RadialDamage(center, radius, rate): frametime * rate * (d / radius) * g_damage_factor to
// every live enemy with health > 0 within radius (grows toward the rim); civilians (class 5)
// too in the sequels (GameRules::civilians; as2/rcsl-builtins-semantics.delta.md 48).
void bRadialDamage(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    float c[3];
    if (!vecArg(a, 0, c)) return;
    float radius = a.f32(1), rate = a.f32(2);
    if (!(radius > 0.0f)) return; // guards the 0/0 of the original; negative: nothing in range
    int attacker = w.attackerFor(selfOf(w));
    for (int i : w.listEntities()) {
        const Entity& e = w.entity(i);
        const bool target = e.f(F_CLASS) == kClassEnemy || (w.rules().civilians && e.f(F_CLASS) == kClassCivilian);
        if (!e.inUse || (e.rt & RT_REMOVED) || !target || !(e.f(F_HEALTH) > 0.0f)) continue;
        float dist = length(e.v3(F_ORIGIN) - Vec3{c[0], c[1], c[2]});
        if (dist > radius) continue;
        w.damageEntity(i, ft(w) * rate * (dist / radius) * w.damageFactor(), attacker);
    }
}

bool projectSegment(World& w, BuiltinArgs& a, float s0[3], float s1[3]) {
    float p0[3], p1[3];
    if (!vecArg(a, 0, p0) || !vecArg(a, 1, p1)) return false;
    return w.projectPoint(Vec3{p0[0], p0[1], p0[2]}, s0) && w.projectPoint(Vec3{p1[0], p1[1], p1[2]}, s1);
}

// TraceLine(from, to, mask) (66): first player (mask 2) or enemy (mask 1) whose screen
// rectangle the projected segment crosses; in the sequels mask bit 4 also accepts civilians
// (GameRules::civilians; as2/rcsl-builtins-semantics.delta.md 66: masks 0 to 3 as before).
void bTraceLine(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    float s0[3], s1[3];
    if (!projectSegment(w, a, s0, s1)) {
        if (!a.failed()) a.setReturnBits(0);
        return;
    }
    int mask = intArg(a, 2);
    if (mask & 2) {
        for (int p = 0; p < w.numPlayers(); ++p) {
            int pi = w.liveIndexFromRef(w.player(p).entityRef);
            if (pi < 0 || w.entity(pi).f(F_DEAD) != 0.0f) continue;
            if (World::segmentHitsRect(s0, s1, w.entity(pi).rect)) {
                a.setReturnBits(w.refOf(pi));
                return;
            }
        }
    }
    const bool civilians = w.rules().civilians && (mask & TOUCH_BIT_CIVILIAN);
    if ((mask & 1) || civilians) {
        for (int i : w.listEntities()) {
            const Entity& e = w.entity(i);
            const float cls = e.f(F_CLASS);
            const bool accepted = ((mask & 1) && cls == kClassEnemy) || (civilians && cls == kClassCivilian);
            if ((e.rt & RT_REMOVED) || !(e.rt & RT_COLLIDABLE) || !accepted) continue;
            if (!(e.f(F_HEALTH) > 0.0f)) continue;
            if (World::segmentHitsRect(s0, s1, e.rect)) {
                a.setReturnBits(w.refOf(i));
                return;
            }
        }
    }
    a.setReturnBits(0);
}

// TraceLineDamage(from, to, dmg) (67): victims chosen by self's touch filter (exactly 2:
// players; exactly 1: on-screen model enemies); unscaled damage, no score.
void bTraceLineDamage(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    float s0[3], s1[3];
    if (!projectSegment(w, a, s0, s1)) return;
    int s = selfOf(w);
    if (s < 0) return;
    float dmg = a.f32(2);
    int tm = w.entity(s).touchMode;
    if (w.rules().touchModeBits) {
        // The sequels: the touch mode as a bit set (as2/rcsl-builtins-semantics.delta.md 67).
        if (tm == 0) return;
        if (tm & TOUCH_BIT_PLAYER) {
            for (int p = 0; p < w.numPlayers(); ++p) {
                int pi = w.liveIndexFromRef(w.player(p).entityRef);
                if (pi < 0 || w.entity(pi).f(F_DEAD) != 0.0f) continue;
                if (World::segmentHitsRect(s0, s1, w.entity(pi).rect)) w.damageEntity(pi, dmg, -1);
            }
        }
        if (tm == TOUCH_BIT_PLAYER) return;
        for (int i : w.listEntities()) {
            const Entity& e = w.entity(i);
            const float cls = e.f(F_CLASS);
            const bool accepted = ((tm & TOUCH_BIT_ENEMIES) && cls == kClassEnemy) ||
                                  ((tm & TOUCH_BIT_CIVILIAN) && cls == kClassCivilian);
            if ((e.rt & RT_REMOVED) || !(e.rt & RT_COLLIDABLE) || !accepted) continue;
            if (e.fields[F_RENDER_TYPE] != 0 || (e.flagBits() & FL_POINT_COLLISION) || e.f(F_DEAD) != 0.0f) continue;
            if (World::segmentHitsRect(s0, s1, e.rect)) w.damageEntity(i, dmg, -1);
        }
        return;
    }
    if (tm == 2) {
        for (int p = 0; p < w.numPlayers(); ++p) {
            int pi = w.liveIndexFromRef(w.player(p).entityRef);
            if (pi < 0 || w.entity(pi).f(F_DEAD) != 0.0f) continue;
            if (World::segmentHitsRect(s0, s1, w.entity(pi).rect)) w.damageEntity(pi, dmg, -1);
        }
    } else if (tm == 1) {
        for (int i : w.listEntities()) {
            const Entity& e = w.entity(i);
            if ((e.rt & RT_REMOVED) || !(e.rt & RT_COLLIDABLE) || e.f(F_CLASS) != kClassEnemy) continue;
            if (e.fields[F_RENDER_TYPE] != 0 || (e.flagBits() & FL_POINT_COLLISION) || e.f(F_DEAD) != 0.0f) continue;
            if (World::segmentHitsRect(s0, s1, e.rect)) w.damageEntity(i, dmg, -1);
        }
    }
}

// Lightning() (68): queues a bolt record from self to every live on-screen enemy within 500
// units and damages it (the World keeps the bolts for the renderer, render-pipeline.md 7.1).
// Reads no arguments. The removed bit is not tested.
void bLightning(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int s = selfOf(w);
    if (s < 0) return;
    Vec3 o = w.entity(s).v3(F_ORIGIN);
    for (int i : w.listEntities()) {
        const Entity& e = w.entity(i);
        if (e.f(F_DEAD) != 0.0f || !(e.rt & RT_COLLIDABLE) || e.f(F_CLASS) != kClassEnemy) continue;
        if (!(length(e.v3(F_ORIGIN) - o) <= 500.0f)) continue;
        w.queueLightning(o, e.v3(F_ORIGIN));
        w.damageEntity(i, w.entity(s).f(F_DAMAGE) * ft(w) * w.damageFactor(), -1);
    }
}

// LockTarget() (61): nearest (search radius 9999) targetable on-screen living enemy that
// lies ahead (normalised y >= 0.3) of the player of the *candidate's* player index.
void bLockTarget(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    float best = 9999.0f;
    int found = -1;
    for (int i : w.listEntities()) {
        const Entity& e = w.entity(i);
        if ((e.rt & RT_REMOVED) || e.f(F_CLASS) != kClassEnemy || (e.flagBits() & FL_NONTARGET)) continue;
        if (!(e.rt & RT_COLLIDABLE) || e.f(F_DEAD) != 0.0f || !(e.f(F_HEALTH) > 0.0f)) continue;
        u32 pref = w.player(e.playerIndex == 1 ? 1 : 0).entityRef;
        u32 px, py, pz;
        if (!w.readRefField(pref, F_ORIGIN, px) || !w.readRefField(pref, F_ORIGIN + 1, py) ||
            !w.readRefField(pref, F_ORIGIN + 2, pz)) {
            continue;
        }
        Vec3 v = e.v3(F_ORIGIN) - Vec3{bitsf(px), bitsf(py), bitsf(pz)};
        float len = length(v);
        if (len != 0.0f) v = v * (1.0f / len);
        if (v.y >= 0.3f && len < best) {
            best = len;
            found = i;
        }
    }
    if (found < 0) {
        a.setReturnBits(0);
        return;
    }
    w.entity(found).rt |= RT_LOCKED; // written, never read (VERIFIED-CODE)
    a.setReturnBits(w.refOf(found));
}

// PushPlayer(): player velocity += normalize(player - self in the ground plane) * 4000 *
// frametime, for self's player.
void bPushPlayer(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int s = selfOf(w);
    if (s < 0) return;
    int pe = w.liveIndexFromRef(w.player(w.currentPlayerIndex()).entityRef);
    if (pe < 0) return;
    Entity& p = w.entity(pe);
    float dx = p.f(F_ORIGIN) - w.entity(s).f(F_ORIGIN);
    float dy = p.f(F_ORIGIN + 1) - w.entity(s).f(F_ORIGIN + 1);
    float len = std::sqrt(dx * dx + dy * dy);
    if (len != 0.0f) {
        dx /= len;
        dy /= len;
    }
    float k = 4000.0f * ft(w);
    p.setF(F_VELOCITY, p.f(F_VELOCITY) + dx * k);
    p.setF(F_VELOCITY + 1, p.f(F_VELOCITY + 1) + dy * k);
}

const BuiltinDesc kTable[] = {
    {"Shoot", 3, bShoot, nullptr, BuiltinStatus::Implemented},
    {"Damage", 2, bDamage, nullptr, BuiltinStatus::Implemented},
    {"RadialDamage", 3, bRadialDamage, nullptr, BuiltinStatus::Implemented},
    {"TraceLine", 3, bTraceLine, nullptr, BuiltinStatus::Implemented},
    {"TraceLineDamage", 3, bTraceLineDamage, nullptr, BuiltinStatus::Implemented},
    {"Lightning", 0, bLightning, nullptr, BuiltinStatus::Implemented},
    {"LockTarget", 0, bLockTarget, nullptr, BuiltinStatus::Implemented},
    {"PushPlayer", 0, bPushPlayer, nullptr, BuiltinStatus::Implemented},
};

} // namespace

Family combatBuiltins() { return {kTable, sizeof kTable / sizeof kTable[0]}; }

} // namespace builtins
} // namespace as3d
