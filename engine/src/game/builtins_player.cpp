// Player record, level flow and camera builtins (docs/spec/rcsl-builtins-semantics.md
// family F, CameraQuake; engine-behaviour.md 7).
#include <algorithm>

#include "builtins_common.h"

namespace as3d {
namespace builtins {

namespace {

PlayerRecord& selfPlayer(World& w) { return w.player(w.currentPlayerIndex()); }

// Integer arguments of the G_ builtins: truncated, then range-tested unsigned.
u32 uintArg(const BuiltinArgs& a, int k) { return static_cast<u32>(ftol(a.f32(k))); }

// G_AddPowerUp / G_AddMissiles: n = count + c, capped at 99 as an unsigned number (a
// negative sum becomes 99); selects the type when none is selected.
void addCount(int* counts, u32 n, int& current, u32 type, i32 c) {
    if (type >= n) return;
    u32 sum = static_cast<u32>(counts[type]) + static_cast<u32>(c);
    if (sum > 98u) sum = 99u;
    counts[type] = static_cast<int>(sum);
    if (current < 0) current = static_cast<int>(type);
}

// G_UsePowerUp / G_UseMissile (0x40b150 / 0x40b220): nothing selected or the selected
// count < 1: return 0, no change. Otherwise decrement; when it drops below 1, set it to 0
// and select the next kind with a non-zero count (sel+1, sel+2, ... mod n; -1 if none).
// The sequels skip a candidate whose value before the wrap-around has its bit in
// `skipMask` (GameRules::powerUpCycleSkip; as2/rcsl-builtins-semantics.delta.md 80).
int useSelected(int* counts, int n, int& current, u32 skipMask = 0u) {
    if (current < 0 || current >= n) return 0;
    if (counts[current] < 1) return 0;
    --counts[current];
    if (counts[current] < 1) {
        counts[current] = 0;
        int next = -1;
        for (int k = 1; k <= n; ++k) {
            const int value = current + k;
            if (value < 32 && (skipMask & (1u << value))) continue;
            int t = value % n;
            if (counts[t] != 0) {
                next = t;
                break;
            }
        }
        current = next;
    }
    return 1;
}

void bAddPowerUp(BuiltinArgs& a, void*) {
    PlayerRecord& p = selfPlayer(worldOf(a));
    addCount(p.powerups, 16, p.currentPowerup, uintArg(a, 0), intArg(a, 1));
}
void bUsePowerUp(BuiltinArgs& a, void*) {
    PlayerRecord& p = selfPlayer(worldOf(a));
    a.setReturnFloat(static_cast<float>(useSelected(p.powerups, 16, p.currentPowerup, worldOf(a).rules().powerUpCycleSkip)));
}
void bGetPowerUp(BuiltinArgs& a, void*) {
    a.setReturnFloat(static_cast<float>(selfPlayer(worldOf(a)).currentPowerup));
}
void bAddMissiles(BuiltinArgs& a, void*) {
    PlayerRecord& p = selfPlayer(worldOf(a));
    addCount(p.missiles, 5, p.currentMissile, uintArg(a, 0), intArg(a, 1));
}
void bUseMissile(BuiltinArgs& a, void*) {
    PlayerRecord& p = selfPlayer(worldOf(a));
    a.setReturnFloat(static_cast<float>(useSelected(p.missiles, 5, p.currentMissile)));
}
void bGetMissiles(BuiltinArgs& a, void*) {
    a.setReturnFloat(static_cast<float>(selfPlayer(worldOf(a)).currentMissile));
}

// Upgrade slots: 20 in the first game, 9 in the sequels (GameRules::weaponSlots).
u32 upgradeSlots(World& w) {
    int n = w.rules().weaponSlots;
    return static_cast<u32>(n < 0 ? 0 : (n > kMaxWeaponSlots ? kMaxWeaponSlots : n));
}

void bGetUpgrade(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    PlayerRecord& p = selfPlayer(w);
    u32 i = uintArg(a, 0);
    a.setReturnFloat(i < upgradeSlots(w) ? static_cast<float>(p.upgrades[i]) : 0.0f);
}
void bSetUpgrade(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    PlayerRecord& p = selfPlayer(w);
    u32 i = uintArg(a, 0);
    if (i < upgradeSlots(w)) p.upgrades[i] = intArg(a, 1);
}

void bPlayerFreezeHealth(BuiltinArgs& a, void*) {
    PlayerRecord& p = selfPlayer(worldOf(a));
    if (a.f32(0) != 0.0f) ++p.freezeCount;
    else --p.freezeCount;
}

void bPlayerDisableAction(BuiltinArgs& a, void*) {
    PlayerRecord& p = selfPlayer(worldOf(a));
    p.actionsDisabled = a.f32(0) != 0.0f;
    if (p.actionsDisabled) p.action = 0.0f;
}

// RespawnPlayer(): G_SpawnPlayer(0) if self is player 1's entity, then G_SpawnPlayer(1)
// if self is player 2's.
void bRespawnPlayer(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int s = selfOf(w);
    if (s < 0) return;
    u32 ref = w.refOf(s);
    bool p1 = w.player(0).entityRef == ref;
    bool p2 = w.numPlayers() == 2 && w.player(1).entityRef == ref;
    if (p1) w.spawnPlayer(0);
    if (p2) w.spawnPlayer(1);
}

// EndLevel(): HUD hidden, paused, mission complete (the complete screen is UI). The sequels
// first store the checkpoint statistics (GameRules::campaignCheckpoint, World::endLevel);
// their end dialogue and level-end sequence belong to the front end.
void bEndLevel(BuiltinArgs& a, void*) { worldOf(a).endLevel(); }

void bCameraQuake(BuiltinArgs& a, void*) { worldOf(a).startQuake(a.f32(0)); }

// ShowTutorialHint(text): pauses the world until the hint's OK (PlayerInput::confirm).
void bShowTutorialHint(BuiltinArgs& a, void*) {
    const char* text = strArg(a, 0);
    worldOf(a).showHint(text ? text : "");
}

const BuiltinDesc kTable[] = {
    {"G_AddPowerUp", 2, bAddPowerUp, nullptr, BuiltinStatus::Implemented},
    {"G_UsePowerUp", 0, bUsePowerUp, nullptr, BuiltinStatus::Implemented},
    {"G_GetPowerUp", 0, bGetPowerUp, nullptr, BuiltinStatus::Implemented},
    {"G_AddMissiles", 2, bAddMissiles, nullptr, BuiltinStatus::Implemented},
    {"G_UseMissile", 0, bUseMissile, nullptr, BuiltinStatus::Implemented},
    {"G_GetMissiles", 0, bGetMissiles, nullptr, BuiltinStatus::Implemented},
    {"G_GetUpgrade", 1, bGetUpgrade, nullptr, BuiltinStatus::Implemented},
    {"G_SetUpgrade", 2, bSetUpgrade, nullptr, BuiltinStatus::Implemented},
    {"PlayerFreezeHealth", 1, bPlayerFreezeHealth, nullptr, BuiltinStatus::Implemented},
    {"PlayerDisableAction", 1, bPlayerDisableAction, nullptr, BuiltinStatus::Implemented},
    {"RespawnPlayer", 0, bRespawnPlayer, nullptr, BuiltinStatus::Implemented},
    {"EndLevel", 0, bEndLevel, nullptr, BuiltinStatus::Approximate},
    {"CameraQuake", 1, bCameraQuake, nullptr, BuiltinStatus::Implemented},
    {"ShowTutorialHint", 1, bShowTutorialHint, nullptr, BuiltinStatus::Approximate},
};

} // namespace

Family playerBuiltins() { return {kTable, sizeof kTable / sizeof kTable[0]}; }

} // namespace builtins
} // namespace as3d
