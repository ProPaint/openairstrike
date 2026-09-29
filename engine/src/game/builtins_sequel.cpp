// AirStrike 2 and Gulf Thunder builtins (docs/spec/as2/rcsl-builtins-semantics.delta.md,
// signatures in rcsl-builtins-table.delta.md): the 16 new ones, and the sequel versions of
// the changed ones that no GameRules flag selects (`atan`, `create`, `Lightning`). The other
// changed builtins read their flag in the shared family code: `Shoot`
// (deadShootersBlocked), `RadialDamage` and `TraceLine` (civilians), `TraceLineDamage`
// (touchModeBits), `G_UsePowerUp` (powerUpCycleSkip), `G_GetUpgrade` / `G_SetUpgrade`
// (weaponSlots), `RespawnPlayer` (respawnChecksLives), `EndLevel` (campaignCheckpoint).
// `RotateTo` / `lRotateTo` and `AttachEntity` need nothing: their new null tests are what
// our engine already does for a null or stale reference (docs/spec/README.md).
#include <cmath>

#include "as3d/defs.h"
#include "builtins_common.h"

namespace as3d {
namespace builtins {

namespace {

constexpr double kRadToDegD = 180.0 / 3.14159265358979323846;

PlayerRecord& selfPlayer(World& w) { return w.player(w.currentPlayerIndex()); }

// ---------------------------------------------------------------------------------------
// A. Math (computed in double as the original's x87 code, stored as a float).
// ---------------------------------------------------------------------------------------

// atan(x): degrees of atan(x); the first game's quirk is gone (delta 9).
void bAtan(BuiltinArgs& a, void*) {
    a.setReturnFloat(static_cast<float>(std::atan(static_cast<double>(a.f32(0))) * kRadToDegD));
}
// atan2(y, x): degrees in (-180, 180] (delta 10).
void bAtan2(BuiltinArgs& a, void*) {
    a.setReturnFloat(static_cast<float>(std::atan2(static_cast<double>(a.f32(0)), static_cast<double>(a.f32(1))) * kRadToDegD));
}
// copysign(x, sign): |x| with the sign bit of `sign` (delta 15).
void bCopysign(BuiltinArgs& a, void*) {
    a.setReturnFloat(static_cast<float>(std::copysign(static_cast<double>(a.f32(0)), static_cast<double>(a.f32(1)))));
}
void bFloor(BuiltinArgs& a, void*) { a.setReturnFloat(static_cast<float>(std::floor(static_cast<double>(a.f32(0))))); }
// floor2(x, step): x - fmod(x, step), toward zero (delta 17).
void bFloor2(BuiltinArgs& a, void*) {
    const double x = a.f32(0), step = a.f32(1);
    a.setReturnFloat(static_cast<float>(x - std::fmod(x, step)));
}
void bFmod(BuiltinArgs& a, void*) {
    a.setReturnFloat(static_cast<float>(std::fmod(static_cast<double>(a.f32(0)), static_cast<double>(a.f32(1)))));
}

// ---------------------------------------------------------------------------------------
// B. Entities.
// ---------------------------------------------------------------------------------------

// create(name, pos) (delta 31): as in the first game, but the new reference is written to
// the return register after the new entity's init and first think (quirk 9 gone).
void bCreate(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    float pos[3];
    if (!vecArg(a, 1, pos)) return;
    const char* name = strArg(a, 0);
    const ObjectDef* def = name ? w.db().findObject(name) : nullptr;
    int idx = def ? w.spawnForCreate(def, Vec3{pos[0], pos[1], pos[2]}, selfOf(w)) : -1;
    if (idx < 0) {
        a.setReturnBits(0);
        return;
    }
    Entity& e = w.entity(idx);
    e.shadowKey = ftol(e.f(F_ANGLES + 2)); // render-pipeline.md 5.3, as in the first game
    e.hasShadowKey = true;
    const u32 ref = w.refOf(idx);
    w.finishCreate(idx, true);
    a.setReturnBits(ref);
}

// DetachEntity(ent) (delta 39).
void bDetachEntity(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    w.detachEntity(entityArg(w, a, 0));
}

// ---------------------------------------------------------------------------------------
// C. Terrain and water.
// ---------------------------------------------------------------------------------------

// TerraMorph(pos, name) (delta 95; World::terraMorph). Approximate: the simulation side is
// complete (heights, TerrainHeight, ground snapping), but no renderer consumes
// World::takeTerrainChanges yet, so the crater is not drawn.
void bTerraMorph(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    float pos[3];
    if (!vecArg(a, 0, pos)) return;
    w.terraMorph(pos[0], pos[1], strArg(a, 1));
}

// WaterHeight(x, y) (delta 67; World::waterHeight). Approximate: the original's water grid
// carries an animated wave term written by its renderer, which is not decoded (open
// question 3 of the delta) and not reproduced.
void bWaterHeight(BuiltinArgs& a, void*) { a.setReturnFloat(worldOf(a).waterHeight(a.f32(0), a.f32(1))); }

// ---------------------------------------------------------------------------------------
// D. Damage.
// ---------------------------------------------------------------------------------------

// RadialDamagePlayer(center, radius, rate) (delta 56): RadialDamage over the player records
// instead of the entity list, without a class test.
void bRadialDamagePlayer(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    float c[3];
    if (!vecArg(a, 0, c)) return;
    const float radius = a.f32(1), rate = a.f32(2);
    if (!(radius > 0.0f)) return; // guards the 0/0 of the original, as RadialDamage does
    const int attacker = w.attackerFor(selfOf(w));
    for (int p = 0; p < w.numPlayers(); ++p) {
        int pi = w.playerEntityIndex(p); // the original would crash without an entity
        if (pi < 0) continue;
        const Entity& e = w.entity(pi);
        if (e.rt & RT_REMOVED) continue;
        if (e.f(F_HEALTH) <= 0.0f) continue; // a NaN health passes
        float d = length(e.v3(F_ORIGIN) - Vec3{c[0], c[1], c[2]});
        if (d > radius) continue; // a NaN distance passes
        w.damageEntity(pi, w.frametimeGlobal * rate * (d / radius) * w.damageFactor(), attacker);
    }
}

// Lightning(radius) (delta 68): a bolt and damage to every on-screen, not dead enemy within
// the horizontal radius; then, when the target's timer reached the rules' interval, the hit
// effect object at its origin (spawned without init; its main runs from the next pass).
void bLightning(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int s = selfOf(w);
    if (s < 0) return;
    const float radius = a.f32(0);
    const GameRules& rl = w.rules();
    const ObjectDef* effect = rl.lightningEffectObject ? w.db().findObject(rl.lightningEffectObject) : nullptr;
    const Vec3 o = w.entity(s).v3(F_ORIGIN);
    for (int i : w.listEntities()) {
        if (!w.validIndex(i)) continue;
        const Entity& e = w.entity(i);
        if (e.f(F_DEAD) != 0.0f || !(e.rt & RT_COLLIDABLE) || e.f(F_CLASS) != kClassEnemy) continue;
        const float dx = e.f(F_ORIGIN) - o.x, dy = e.f(F_ORIGIN + 1) - o.y;
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d > radius) continue; // unordered passes
        const Vec3 target = e.v3(F_ORIGIN);
        w.queueLightning(o, target);
        w.damageEntity(i, w.entity(s).f(F_DAMAGE) * w.frametimeGlobal * w.damageFactor(), -1);
        Entity& t = w.entity(i);
        if (effect && t.lightningTimer >= rl.lightningEffectInterval) {
            t.lightningTimer = 0.0f;
            w.spawnRoot(effect, t.v3(F_ORIGIN), true);
        }
    }
}

// ---------------------------------------------------------------------------------------
// E. Camera.
// ---------------------------------------------------------------------------------------

// GetMapPosOfs() (delta 98): the y offset of the current camera mode's preset.
void bGetMapPosOfs(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int m = w.camera().mode;
    if (m < 0 || m >= kMaxCameraModes) m = 0;
    a.setReturnFloat(w.rules().cameraModes[m].yOffset);
}

// ---------------------------------------------------------------------------------------
// F. Level flow and players.
// ---------------------------------------------------------------------------------------

// GameOver() (delta 3). Approximate: the game-over menu and music belong to the front end.
void bGameOver(BuiltinArgs& a, void*) { worldOf(a).gameOverBuiltin(); }

// G_SetPowerUpCount(type, count) (delta 82): the count, 0 if negative; the selection is not
// touched. A type outside 0..15 is ignored (the original writes other record fields).
void bSetPowerUpCount(BuiltinArgs& a, void*) {
    PlayerRecord& p = selfPlayer(worldOf(a));
    i32 c = ftol(a.f32(1));
    if (c < 0) c = 0;
    const i32 t = ftol(a.f32(0));
    if (t < 0 || t >= 16) return;
    p.powerups[t] = c;
}

// GetPlayerAccel(out) (delta 99): the steering vector of self's player record when self is
// a player's entity (World::computeAccel); otherwise `out` is left alone.
void bGetPlayerAccel(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int s = selfOf(w);
    if (s < 0) return;
    const u32 ref = w.refOf(s);
    for (int p = 0; p < kMaxPlayers; ++p) {
        if (w.player(p).entityRef != ref) continue;
        if (!a.writeVec3(0, w.player(p).accel)) {
            a.fail("access to unmapped address");
            return;
        }
    }
}

// GetPlayersDistance() (delta 100): self's y minus the other player's, when two players with
// p_lives >= 0 are in the game and self is one of their entities; else -1.
void bGetPlayersDistance(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    float r = -1.0f;
    int s = selfOf(w);
    if (w.numPlayers() == 2 && s >= 0 && w.player(0).lives >= 0.0f && w.player(1).lives >= 0.0f) {
        const u32 ref = w.refOf(s);
        int me = w.player(0).entityRef == ref ? 0 : (w.player(1).entityRef == ref ? 1 : -1);
        u32 oy;
        if (me >= 0 && w.readRefField(w.player(1 - me).entityRef, F_ORIGIN + 1, oy)) {
            r = w.entity(s).f(F_ORIGIN + 1) - bitsf(oy);
        }
    }
    a.setReturnFloat(r);
}

void bIsMultiplayer(BuiltinArgs& a, void*) { a.setReturnFloat(worldOf(a).numPlayers() == 2 ? 1.0f : 0.0f); }

// IsPlayerInGame(i) (delta 97): 1 when player i was given an entity this level.
void bIsPlayerInGame(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    const i32 i = ftol(a.f32(0));
    a.setReturnFloat(i >= 0 && i < kMaxPlayers && w.player(i).entityRef != 0 ? 1.0f : 0.0f);
}

const BuiltinDesc kTable[] = {
    {"GameOver", 0, bGameOver, nullptr, BuiltinStatus::Approximate},
    {"atan", 1, bAtan, nullptr, BuiltinStatus::Implemented},
    {"atan2", 2, bAtan2, nullptr, BuiltinStatus::Implemented},
    {"copysign", 2, bCopysign, nullptr, BuiltinStatus::Implemented},
    {"floor", 1, bFloor, nullptr, BuiltinStatus::Implemented},
    {"floor2", 2, bFloor2, nullptr, BuiltinStatus::Implemented},
    {"fmod", 2, bFmod, nullptr, BuiltinStatus::Implemented},
    {"create", 2, bCreate, nullptr, BuiltinStatus::Implemented},
    {"DetachEntity", 1, bDetachEntity, nullptr, BuiltinStatus::Implemented},
    {"RadialDamagePlayer", 3, bRadialDamagePlayer, nullptr, BuiltinStatus::Implemented},
    {"WaterHeight", 2, bWaterHeight, nullptr, BuiltinStatus::Approximate},
    {"Lightning", 1, bLightning, nullptr, BuiltinStatus::Implemented},
    {"G_SetPowerUpCount", 2, bSetPowerUpCount, nullptr, BuiltinStatus::Implemented},
    {"TerraMorph", 2, bTerraMorph, nullptr, BuiltinStatus::Approximate},
    {"IsMultiplayer", 0, bIsMultiplayer, nullptr, BuiltinStatus::Implemented},
    {"IsPlayerInGame", 1, bIsPlayerInGame, nullptr, BuiltinStatus::Implemented},
    {"GetMapPosOfs", 0, bGetMapPosOfs, nullptr, BuiltinStatus::Implemented},
    {"GetPlayerAccel", 1, bGetPlayerAccel, nullptr, BuiltinStatus::Implemented},
    {"GetPlayersDistance", 0, bGetPlayersDistance, nullptr, BuiltinStatus::Implemented},
};

} // namespace

Family sequelBuiltins() { return {kTable, sizeof kTable / sizeof kTable[0]}; }

} // namespace builtins
} // namespace as3d
