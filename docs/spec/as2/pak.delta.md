# PAK archive: AirStrike 2 delta (`as2`)

Delta version 1.0. Amends [../pak.md](../pak.md) 1.0 for the game `as2` (AirStrike 2 v2.51).
Tag: `VERIFIED-DATA` over the three paks of `third_party_local/games/as2/data/`
(`tools/paktool.py` reads all of them; `tools/paktool.py manifest` gives the 2359 entries of
`testdata/golden/as2/pak_manifest.json`, which the C++ `vfs_test.cpp` with `AS3D_GAME=as2`
reproduces byte for byte through `Vfs`).

## Layout, file table, file bodies

Same format. All three files start with the magic `00 00 80 3F 99 99 00 00`, the file table
ends exactly at the end of the file (`table offset + 76 x count` = file size), every body lies
inside the file, and the table and the encrypted bodies use the key at 0x10 as before.
The encrypted flag is 1 on every `.obj`, `.ps` and `.wpn` entry (39, 9 and 4) and on
`maps\levels.txt`, and 0 on everything else, as in the first game.

## Mounting rules

Same. The game's paks are `pak0.apk`, `pak1.apk`, `pak2.apk` (mount order = name order), and
no name occurs in two of them, so no override happens. `data\` also holds 1610 loose files in
`gfx`, `menu`, `models`, `morphmaps`, `textures` and `tiles` next to `Settings.xml` (the first
game's `data\` had `Settings.xml` and `gfx` only, 2 loose files). Whether the executable
prefers a loose file to a pak entry is not checked here; `assets_extracted_games/as2/` is the
merged content of the three paks (`tools/paktool.py extract`), not the loose files.

## Counts

| Pak | Size (bytes) | Entries | Encrypted |
|---|---|---|---|
| `pak0.apk` | 39,411,244 | 1609 | 0 |
| `pak1.apk` | 2,459,895 | 705 | 53 |
| `pak2.apk` | 5,953,690 | 45 | 0 |

2359 distinct names in total (the first game: 1351, from 1201, 113 and 37 entries). The pak
files are identified by their content, not by name (`as3d/game_data.h`, `identifyPaks`);
`tools/games.json` has their SHA-256.

## Checked sections

| Base section | Status |
|---|---|
| Layout | same |
| File table | same |
| File bodies | same (the flag rule holds: 39 `.obj`, 9 `.ps`, 4 `.wpn`, `levels.txt`) |
| Mounting rules | same (three paks, no override) |
| Counts (v1.70) | changed (table above) |

## Changelog

- 1.0 (B6): first version.
