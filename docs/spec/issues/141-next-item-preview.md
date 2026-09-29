# 141: the "next" touch buttons show what comes next (WP-53)

Owner request: the double-arrow buttons of the touch overlay display the next weapon, missile
or power-up.

## Rule (all native, none in the player script)

The player frame (`World::playerFrame`) handles the three action edges. For each list the
press selects the first owned entry (count > 0) after the current one, wrapping, ending on the
current one; if nothing is owned the selection is left alone. Lists: power-ups 16 slots
(`currentPowerup`), missiles 5 types (`currentMissile`), weapons 20 entries (`upgrades[]`,
current = truncated `p_weapon`); current -1 counts as "before entry 0".
`engine/include/as3d/player_select.h` holds it (`nextOwnedIndex`, `nextPowerupSlot`,
`nextMissileType`, `nextWeaponIndex`); the player frame and the overlay both call it.
`ui::HudPlayer` carries `upgrades[]` so the overlay can evaluate the weapon cycle.

## Overlay

Each next button shows the next item's atlas icon (drawn symbol when the atlas has no picture,
e.g. weapons 10..19 and power-up kinds 4..15), the count of the next item (missile, power-up)
and a small orange double arrow. When the press would change nothing the button shows only a
dimmed arrow. Icons on all item buttons are larger and at full brightness.

## Tests

`apps/tests/next_item_test.cpp`: the rule over all missile combinations, wrap/single/empty,
the simulation against the prediction, headless pixels (set `AS3D_SHOT_DIR` for screenshots).
