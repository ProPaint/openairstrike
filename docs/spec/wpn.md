# Weapon definitions (`weapons\*.wpn`)

Spec version 1.0. Reference implementation: `tools/ref/defs.py`.
Engine implementation: `engine/src/game/defs.cpp`
(`engine/include/as3d/defs.h`, `WeaponDef`).

Builds on `docs/spec/text-blocks.md`. Each `.wpn` file is a sequence of
named blocks, one per weapon. VERIFIED-DATA against the 5 shipped
`weapons/*.wpn` files (63 blocks total: `bosses.wpn`, `e_missiles.wpn`,
`enemies.wpn`, `e_weapons.wpn`, `player.wpn`). Code-derived claims cite the
v1.70 executable, primarily the per-block parser at `0x0040c770`
(unnamed in the Ghidra export; referenced from the strings `flash`,
`missile`, `Too many weapons.`) and the object-name lookup `FUN_00409860`
at `0x00409860`.

## Statement reference

| Key | Count | Type | Default | Confidence |
|---|---|---|---|---|
| `missile` | 63 | string, resolved as an **object** name | empty / unresolved (0) | VERIFIED-CODE |
| `flash` | 48 | string, resolved as an **object** name | empty / unresolved (0) | VERIFIED-CODE |
| `speed` | 56 | float | 0.0 | VERIFIED-DATA |

All 3 distinct keys observed in the data map to typed `WeaponDef` fields;
nothing is left uninterpreted. `flash` is present on 48 of the 63 weapons —
the 15 without one (VERIFIED-DATA, e.g. `p_wpn_flamefrower` in
`weapons/player.wpn`) simply have `flashName` empty, meaning "no muzzle
flash object".

### `missile` / `flash`

Both are looked up **exclusively as object names** (VERIFIED-CODE: both
statements call the same object-table lookup, `FUN_00409860 @0x00409860`,
never the particle-system lookup used by `attach` in `docs/spec/obj.md`).
The name refers to a block in `objects/*.obj` — typically a small,
purpose-built object with its own `model`/`script` describing the
projectile's or muzzle flash's behavior (e.g. `p_wpn_machinegun`'s
`missile "mgun_proj"` resolves to the `mgun_proj` block, itself an ordinary
object with a `script` driving its flight). VERIFIED-CODE: the lookup is a
byte-exact, case-sensitive scan of the object table in load order, returning
the index of the first match (`0` / "not found" logs `ERROR: Unknown
object '%s'.` in the original and is treated as an unresolved reference by
this package's `validate()`).

### `speed`

A single float — VERIFIED-DATA shape only; used, per the manual
(`third_party_local/original/manual/weapons.html`, not part of this
package's authoritative sources but useful color) as each weapon's
projectile travel speed. Not present on every weapon of every kind (e.g.
`p_wpn_flamefrower` has no `speed` in the shipped data, defaulting to 0.0);
GUESS as to what 0.0 means for gameplay (instantaneous/hitscan, or simply
never read for that weapon's script-driven projectile) — out of scope to
verify without the weapon-firing code.

## Weapon identity (informational, not authoritative)

`weapons/*.wpn` block names follow a `p_wpn*`/`e_wpn*` naming convention
matching the corresponding `objects/weapons*.obj` blocks they reference via
`missile`/`flash`, and in turn correspond to the weapons described in
`third_party_local/original/manual/weapons.html` (e.g. `p_wpn_machinegun` =
the manual's "Machine Gun", `p_wpn_biglaser`/`_biglaser2`/`_biglaser3` = its
3 upgrade levels of "Laser"). This mapping is GUESS (by name and file
co-location only) and is not needed by, or asserted anywhere in, the typed
loader or its tests — it is included here purely as human-readable context
for a later gameplay/UI package.

## Record limit

VERIFIED-CODE: the weapon table holds at most **256** entries (`if (count ==
0x100) { ...; return; }`, logging `Too many weapons.` if exceeded). The
shipped data (63 weapons across 5 files) is far under this cap.

## Unresolved references

VERIFIED-DATA: running `validate()` against the shipped data finds **zero**
weapon `missile`/`flash` references that fail to resolve — every one of the
63 weapon blocks' non-empty `missile`/`flash` values names a real object.
`third_party_local/original/game.log` contains no `ERROR: Unknown weapon`
or `ERROR: Unknown object` lines either, consistent with this.

## Canonical serialization

`tools/ref/defs.py`'s `canonical_weapon()` and `engine/src/game/defs.cpp`'s
`canonicalWeapon()` produce, in order: `name`, `missile`, `flash` (all
verbatim strings), `speed` (a float, formatted as `0x` + 8 lowercase hex
digits of its IEEE-754 binary32 bit pattern — see `docs/spec/obj.md`
"Canonical serialization" for the exact rule), each as a `key=value` line
joined with `\n`.

## Changelog

- 1.0 (WP-1B): initial version.
