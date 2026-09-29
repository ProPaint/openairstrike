// Player record, level flow and camera builtins (docs/spec/engine-behaviour.md 6.6,
// 7, 8.2, 8.3, 9.4, 10.3; rcsl-builtins-table.md).
#include <algorithm>

#include "builtins_common.h"

namespace as3d {
namespace builtins {

namespace {

PlayerRecord& selfPlayer(World& w) { return w.player(w.currentPlayerIndex()); }

// "Use" rule shared by missiles and power-ups (8.3): if the selected count is > 0,
// decrement it and return 1; otherwise select the next type that has some and return 0
// (the selection is kept when none has any: GUESS, issue 034).
int useSelected(int* counts, int n, int& current) {
    if (current >= 0 && current < n && counts[current] > 0) {
        --counts[current];
        return 1;
    }
    int start = (current >= 0 && current < n) ? current : -1;
    for (int k = 1; k <= n; ++k) {
        int t = (start + k + n) % n;
        if (counts[t] > 0) {
            current = t;
            break;
        }
    }
    return 0;
}

void addCount(int* counts, int n, int& current, int type, int count) {
    if (type < 0 || type >= n) return;
    counts[type] = std::min(counts[type] + count, 99);
    if (current < 0) current = type;
}

void bAddPowerUp(BuiltinArgs& a, void*) {
    PlayerRecord& p = selfPlayer(worldOf(a));
    addCount(p.powerups, 16, p.currentPowerup, intArg(a, 0), intArg(a, 1));
}
void bUsePowerUp(BuiltinArgs& a, void*) {
    PlayerRecord& p = selfPlayer(worldOf(a));
    a.setReturnFloat(static_cast<float>(useSelected(p.powerups, 16, p.currentPowerup)));
}
void bGetPowerUp(BuiltinArgs& a, void*) {
    a.setReturnFloat(static_cast<float>(selfPlayer(worldOf(a)).currentPowerup));
}
void bAddMissiles(BuiltinArgs& a, void*) {
    PlayerRecord& p = selfPlayer(worldOf(a));
    addCount(p.missiles, 5, p.currentMissile, intArg(a, 0), intArg(a, 1));
}
void bUseMissile(BuiltinArgs& a, void*) {
    PlayerRecord& p = selfPlayer(worldOf(a));
    a.setReturnFloat(static_cast<float>(useSelected(p.missiles, 5, p.currentMissile)));
}
void bGetMissiles(BuiltinArgs& a, void*) {
    a.setReturnFloat(static_cast<float>(selfPlayer(worldOf(a)).currentMissile));
}

// Both upgrade builtins round their arguments to nearest (8.2).
void bGetUpgrade(BuiltinArgs& a, void*) {
    PlayerRecord& p = selfPlayer(worldOf(a));
    int i = roundToInt(a.f32(0));
    a.setReturnFloat((i >= 0 && i < 20) ? static_cast<float>(p.upgrades[i]) : 0.0f);
}
void bSetUpgrade(BuiltinArgs& a, void*) {
    PlayerRecord& p = selfPlayer(worldOf(a));
    int i = roundToInt(a.f32(0));
    if (i >= 0 && i < 20) p.upgrades[i] = roundToInt(a.f32(1));
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

void bRespawnPlayer(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int s = selfOf(w);
    int p = -1;
    if (s >= 0 && w.isPlayerEntity(s, &p)) w.spawnPlayer(p);
}

void bEndLevel(BuiltinArgs& a, void*) { worldOf(a).endLevel(); }

void bCameraQuake(BuiltinArgs& a, void*) { worldOf(a).startQuake(a.f32(0)); }

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
};

} // namespace

Family playerBuiltins() { return {kTable, sizeof kTable / sizeof kTable[0]}; }

} // namespace builtins
} // namespace as3d
