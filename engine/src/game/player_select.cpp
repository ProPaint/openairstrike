#include "as3d/player_select.h"

#include "world_internal.h"

namespace as3d {

int nextOwnedIndex(const int* counts, int n, int current, bool* found) {
    const int base = current < 0 ? -1 : current;
    for (int k = 1; k <= n; ++k) {
        const int t = (base + k + n) % n;
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

} // namespace as3d
