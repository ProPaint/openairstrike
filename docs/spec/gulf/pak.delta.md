# PAK archive: Gulf Thunder delta (`gulf`)

Delta version 1.0. Amends [../as2/pak.delta.md](../as2/pak.delta.md) (which amends
[../pak.md](../pak.md)) for the game `gulf` (AirStrike II: Gulf Thunder v2.71). Tag:
`VERIFIED-DATA` over the four paks of `third_party_local/games/gulf/data/`
(`testdata/golden/gulf/pak_manifest.json`, 1920 entries, reproduced by `vfs_test.cpp` with
`AS3D_GAME=gulf`).

## Layout, file table, file bodies

Same as `as2` (magic, table at the end, key at 0x10, per-entry flag). The encrypted flag is
1 on every `.obj`, `.ps` and `.wpn` entry (29, 9 and 4) and on `maps\levels.txt`, and 0 on
everything else.

## Mounting rules

Four paks: `pak0.apk`, `pak1.apk`, `pak2.apk` and **`pak4.apk`**. There is no `pak3.apk`;
name order is the mount order, so `pak4.apk` is mounted last. `pak4.apk` has **one entry**,
`models\mapobjects\elektro\power_plant.tga` (66,348 bytes), which replaces the entry of the
same name in `pak0.apk`: the only override in the game's data (the first game and `as2` have
none). The `Vfs` rule "a later pak overrides an earlier one" is what makes the 256 x 256
texture win over `pak0.apk`'s (in `as2` the same file is 256 x 257, see tga.delta.md).
`data\` also holds 1143 loose files in `gfx`, `menu`, `models`, `morphmaps`, `textures` and
`tiles`, next to `Settings.xml`.

## Counts

| Pak | Size (bytes) | Entries | Encrypted |
|---|---|---|---|
| `pak0.apk` | 35,840,140 | 1142 | 0 |
| `pak1.apk` | 2,493,636 | 733 | 43 |
| `pak2.apk` | 5,051,540 | 45 | 0 |
| `pak4.apk` | 67,464 | 1 | 0 |

1921 entries and 1920 distinct names (`pak0.apk` keeps 1141 of its entries after the
override). `pak4.apk` is in `tools/games.json` and in the game profile's pak list.

## Checked sections

| Base section | Status |
|---|---|
| Layout | same |
| File table | same |
| File bodies | same (flag rule holds) |
| Mounting rules | changed (four paks, `pak4.apk` overrides one entry) |
| Counts (v1.70) | changed (table above) |

## Changelog

- 1.0 (B6): first version.
