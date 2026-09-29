#include "as3d/player_select.h"

#include <algorithm>

#include "world_internal.h"

namespace as3d {

int nextOwnedIndex(const int* counts, int n, int current, bool* found) {
    return nextOwnedIndexSkipping(counts, n, current, 0u, found);
}

int nextOwnedIndexSkipping(const int* counts, int n, int current, u32 skipMask, bool* found) {
    const int base = current < 0 ? -1 : current;
    for (int k = 1; k <= n; ++k) {
        const int value = base + k; // before the wrap-around
        if (value >= 0 && value < 32 && (skipMask & (1u << value))) continue;
        const int t = (value + n) % n;
        if (counts[t] > 0) {
            if (found) *found = true;
            return t;
        }
    }
    if (found) *found = false;
    return current;
}

int nextPowerupSlot(const int* powerups, int current) { return nextOwnedIndex(powerups, kPowerupKindsOwned, current); }
int nextMissileType(const int* missiles, int current) { return nextOwnedIndex(missiles, kMissileKindsOwned, current); }
int nextWeaponIndex(const int* upgrades, int currentWeapon) {
    return nextOwnedIndex(upgrades, kWeaponKindsOwned, currentWeapon);
}

int nextPowerupSlot(const PlayerRecord& p) { return nextPowerupSlot(p.powerups, p.currentPowerup); }
int nextMissileType(const PlayerRecord& p) { return nextMissileType(p.missiles, p.currentMissile); }
int nextWeaponIndex(const PlayerRecord& p) { return nextWeaponIndex(p.upgrades, ftol(p.weapon)); }

int nextPowerupSlot(const GameRules& rules, const int* powerups, int current, bool* found) {
    return nextOwnedIndexSkipping(powerups, kPowerupKindsOwned, current, rules.powerUpCycleSkip, found);
}

int nextWeaponIndex(const GameRules& rules, const int* upgrades, int currentWeapon, bool* found) {
    const int n = std::min(std::max(rules.weaponSlots, 1), kWeaponKindsOwned);
    return nextOwnedIndex(upgrades, n, currentWeapon, found);
}

int nextPowerupSlot(const GameRules& rules, const PlayerRecord& p) {
    return nextPowerupSlot(rules, p.powerups, p.currentPowerup);
}
int nextWeaponIndex(const GameRules& rules, const PlayerRecord& p) {
    return nextWeaponIndex(rules, p.upgrades, ftol(p.weapon));
}

} // namespace as3d
