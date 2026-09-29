// The rule of the "next" actions (engine-behaviour.md 7.2): what pressing next weapon, next
// missile or next power-up selects. The simulation's player frame and the touch overlay's
// preview both call these, so they cannot disagree. All native: the player script does not
// take part in the cycling (it only reads p_weapon and G_UseMissile's selection afterwards).
//
// The sequels change two things (as2/engine-behaviour.delta.md 8.2, 8.3): weapons cycle over
// GameRules::weaponSlots, and "next power-up" skips the slots of GameRules::powerUpCycleSkip.
// The functions taking a GameRules follow them; the others are the first game's rules.
#pragma once

#include "as3d/game_profile.h"
#include "as3d/world.h"

namespace as3d {

// The index a "next" press selects from `current` (-1 = nothing selected yet) among `n`
// entries with `counts[i] > 0` owned: the first owned entry after `current`, wrapping round
// and ending on `current` itself. `current` is returned unchanged when nothing is owned
// (`found`, if given, tells whether something was).
int nextOwnedIndex(const int* counts, int n, int current, bool* found = nullptr);

// The same, skipping a candidate whose value *before* the wrap-around (current + k, with
// current = -1 when nothing is selected) is below 32 and has its bit set in `skipMask`
// (as2@0x413680: a value above 15 is not skipped even when it wraps onto a skipped slot).
int nextOwnedIndexSkipping(const int* counts, int n, int current, u32 skipMask, bool* found = nullptr);

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

// Per game.
int nextPowerupSlot(const GameRules& rules, const int* powerups, int current, bool* found = nullptr);
int nextWeaponIndex(const GameRules& rules, const int* upgrades, int currentWeapon, bool* found = nullptr);
int nextPowerupSlot(const GameRules& rules, const PlayerRecord& p);
int nextWeaponIndex(const GameRules& rules, const PlayerRecord& p);

} // namespace as3d
