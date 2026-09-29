// The rule of the "next" actions (engine-behaviour.md 7.2): what pressing next weapon, next
// missile or next power-up selects. The simulation's player frame and the touch overlay's
// preview both call these, so they cannot disagree. All native: the player script does not
// take part in the cycling (it only reads p_weapon and G_UseMissile's selection afterwards).
#pragma once

#include "as3d/world.h"

namespace as3d {

// The index a "next" press selects from `current` (-1 = nothing selected yet) among `n`
// entries with `counts[i] > 0` owned: the first owned entry after `current`, wrapping round
// and ending on `current` itself. `current` is returned unchanged when nothing is owned
// (`found`, if given, tells whether something was).
int nextOwnedIndex(const int* counts, int n, int current, bool* found = nullptr);

constexpr int kPowerupKindsOwned = 16;
constexpr int kMissileKindsOwned = 5;
constexpr int kWeaponKindsOwned = 20;

int nextPowerupSlot(const PlayerRecord& p);
int nextMissileType(const PlayerRecord& p);
int nextWeaponIndex(const PlayerRecord& p);

// The same from the numbers a HUD keeps (weapon = p_weapon truncated, `upgrades` = the
// weapon table with 0 = not owned).
int nextPowerupSlot(const int* powerups, int current);
int nextMissileType(const int* missiles, int current);
int nextWeaponIndex(const int* upgrades, int currentWeapon);

} // namespace as3d
