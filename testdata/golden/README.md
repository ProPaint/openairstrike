# Golden files

One directory per game: `testdata/golden/<key>/` with `<key>` one of `as3d`, `as2`, `gulf`
(`tools/games.json`). The files hold metadata computed from the game's own data by the Python
reference parsers in `tools/ref/`: names, counts, sizes, hashes and structural numbers. No game
content is stored. The C++ tests (`apps/tests/`) and the Python tests (`tools/ref/test_*.py`)
compare the engine's and the parsers' results with them, for the game selected by
`$AS3D_GAME` / `--game` (default `as3d`). `tools/ci.sh` runs everything once per game present
on the machine.

| File | Made by | Read by |
|---|---|---|
| `expected.json` | by hand, see below | all tests |
| `pak_manifest.json` | `tools/paktool.py manifest` over the game's paks in mount order | `vfs_test.cpp` |
| `tga_hashes.json` | `tools/ref/test_tga.py` | `tga_test.cpp` |
| `mdl_summary.json` | `tools/ref/test_mdl.py` | `mdl_test.cpp` |
| `hmap_summary.json` | `tools/ref/test_hmap.py` | `hmap_test.cpp` |
| `terrain_heights.json` | `tools/ref/gen_terrain_golden.py` | `engine/include/as3d/terrain.h` cites it |
| `textblock_counts.json` | `tools/ref/test_textblock.py` | `textblock_test.cpp` |
| `defs_summary.json` | `tools/ref/test_defs.py` | `defs_test.cpp` |
| `rcsl_summary.json` | `tools/ref/test_rcsl.py` | Python only |
| `rcsl_trace_hashes.json` | `tools/ref/test_rcsl_vm.py` | `script_test.cpp` |
| `rcsl_builtins.json` | builtin package (B2) | `tools/rcsl_disasm.py`, `tools/ref/rcsl_vm.py`, `check_builtin_calls.py` (`gulf` reads the `as2` table) |

A golden file is written on the first run of its test when it does not exist, and compared on
every later run. To regenerate one on purpose delete it (or set the `AS3D_*_REGENERATE_GOLDEN`
variable its test names), check the diff and commit.

## expected.json

Flat, one object per test area. The comment key `_comment` is ignored. Every number is what the
data must give; a change is a finding, not something to paper over.

| Key | Meaning |
|---|---|
| `game` | the key |
| `playable` | `false` for a game whose play tests (bot regression, all missions, game flow, integration, polish, web key bindings, rendering of levels) are skipped, loudly |
| `paks.files` | files in the merged paks (after overrides) = entries of `pak_manifest.json` |
| `textures.files` | `.tga` files under the extracted directory = entries of `tga_hashes.json` |
| `models.files`, `models.ok` | `.mdl` files, and how many parse |
| `models.empty` | zero-byte `.mdl` files (paths relative to the extracted directory) |
| `models.broken` | non-empty `.mdl` files whose header counts do not fit the file, with the byte difference |
| `maps.files`, `maps.names` | `.hsc` files under `maps/` |
| `maps.tile_sets` | highest tile set index in use (sets are `tiles/tiles<n>.tga`) |
| `maps.pad_tile_set`, `maps.pad_tile_index` | tile set and index of the centre of the 3x3 helipad |
| `maps.markers_on_pads` | `end_of_the_level` placements on a helipad centre |
| `maps.unresolved_names` | `[map, name]` pairs of type or item names with no object definition |
| `maps.odd_tiles` | cells whose tile index is outside the atlas or whose rotation is not 0, 3, 6, 9 |
| `maps.odd_waypoints` | optional: `[map, placement, waypoint]` that break the range rules of `test_hmap.py` (a cell outside the map, a delay above 1e6) |
| `maps.reference_exceptions` | optional: `{map: n}` for maps that are not named by exactly one `maps/levels.txt` block |
| `text_blocks.files` | brace-block files: `maps/levels.txt` and every `.obj`, `.wpn`, `.ps` |
| `text_blocks.object_blocks`, `.distinct_object_names` | named blocks in `objects/*.obj`, and distinct names |
| `definitions.*` | objects, distinct object names, weapons, particle systems, levels after loading |
| `definitions.unresolved_references` | the warnings of the reference validation (a file or definition a definition names that does not exist) |
| `scripts.files` | compiled scripts (`.scr`, and the one leftover `.sc` of the sequels) |
| `scripts.no_code` | scripts without a CODE section (an empty program) |
| `scripts.mode_pairs` | distinct (opcode, mode) pairs in the corpus (sequels; the first game checks the full table of the spec) |
| `scripts.extra_mode_pairs` | pairs no delta spec lists yet |
| `scripts.builtins` | builtins of the game's table |
| `sounds.files` | `sounds/*.wav` |
| `missions.first`, `.last` | missions the all-missions test runs |
| `bot_regression` | mission, seed, difficulty and the result of the scripted bot (end frame, score, lives) |
