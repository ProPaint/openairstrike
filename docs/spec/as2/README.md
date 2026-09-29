# AirStrike 2 v2.51 (`as2`): executable and export facts

Facts about the executable and the reverse-engineering export only. No decompiled code.
Spec deltas against v1.70 are added to the table at the bottom by later packages.

## Executable

| Field | Value |
|---|---|
| File | `AirStrike3D II.exe` (`third_party_local/games/as2/`, from `AirStrike/AirStrike 2` in `binaries/AirStrike.zip`) |
| Size | 663,552 bytes |
| SHA-256 | `b24b62b2c5b61cfa1cf0aad781788aa777a2e4f4a385c73ba53014b039e46f5b` |
| PE timestamp | 1179237256 (0x4649BB88), 2007-05-15 13:54:16 UTC |
| Linker | 8.0 (MSVC 2005) |
| Image base | 0x00400000 |
| Entry point | 0x0043b756 |
| Protection | none (Ghidra analyses it directly) |

Sections (virtual address, virtual size, raw offset, raw size): `.text` 0x401000, 0x7dec5,
0x1000, 0x7e000; `.rdata` 0x47f000, 0x18340, 0x7f000, 0x19000; `.data` 0x498000, 0x1d82724
(mostly uninitialised), 0x98000, 0x7000; `.data1` 0x221b000, 0x900; `.rsrc` 0x221c000, 0x1308.

## Imports (178 functions)

`ADVAPI32.DLL` 5, `BASS.DLL` 28, `D3D8.DLL` 1 (`Direct3DCreate8` only: the renderer is
Direct3D 8, there is no `OPENGL32.DLL` / `GLU32.DLL` as in v1.70), `GDI32.DLL` 1,
`KERNEL32.DLL` 93, `SHELL32.DLL` 1, `USER32.DLL` 37, `WININET.DLL` 6, `WINMM.DLL` 6. Same set
as the Gulf Thunder executable.

## Game data

`tools/setup_data.sh as2`: game directory in `third_party_local/games/as2/`, paks extracted
to `assets_extracted_games/as2/` (not inside `assets_extracted/`, which belongs to v1.70 and is
walked recursively by some tests). Paks in mount order: `pak0.apk` 1609 entries, `pak1.apk`
705, `pak2.apk` 45; 2359 files in total, no pak overrides another. `data/` also holds 1610
loose files (`gfx`, `menu`, `models`, `morphmaps`, `textures`, `tiles`, `Settings.xml`).

## Ghidra export

- Command: `AS3D_DATA_ROOT=<main checkout> re/run_ghidra.sh --game as2` (see `re/README.md`).
- Output: `re/out/as2/` (gitignored), project `third_party_local/ghidra_project/as2/`,
  generated names in `re/symbols_as2_auto.csv`.
- Ghidra 11.0.3. 1919 functions (all decompiled, 0 failures), 1465 defined strings.
- Entry point 0x0043b756; WinMain (heuristic, BFS from entry) 0x00415c20, size 113.

## Probe results (`re/probes/as2.json`)

- Builtin table: 101 of 101 probe names found (the 85 of v1.70 plus 16). Table starts at
  0x0049d5d0, 101 entries, stride 8: CONFIRMED (the scan's run starts two records earlier, at
  0x0049d5c0, on two unrelated "bad allocation" records).
- Script-global table: 28 of 28 probe names found (24 of v1.70 plus `player1`, `player2`,
  `p_maxHealth`, `cameramode`). Table at 0x0049d908, 28 entries, stride 12: CONFIRMED.
- The four acceptance strings are all present, each referenced by one function:
  `R_LoadModel` string 0x0048bba4 (function 0x00418270), `Script stall detected.` 0x0048c09c
  (0x0041eb10), `SL_GetExternFunc` 0x0048c6bc (0x00421c30), `G_LoadBin` 0x00489fbc (0x004069e0).

## Implementation notes

Issues 230 to 234 (`issues/`) record choices made while implementing the script host, builtins and player rules.

## Delta specs

| Spec | Status | Package | Notes |
|---|---|---|---|
| [frontend.md](frontend.md) | verified from code and by running the game's own drawing code in an emulator; two-player screens never played | B7 | a full document mirroring the first game's frontend.md: intro comic, new menus in a framed panel, helicopter selection, portrait dialogues, HUD, save; text addresses in `tools/exe_texts/as2.json` (addresses only); quirks and keep-or-fix choices in [issue 240](issues/240-frontend-quirks-and-choices.md); wins over engine-behaviour.delta.md on menu flow |
| [pak](pak.delta.md), [tga](tga.delta.md), [mdl](mdl.delta.md), [hmap](hmap.delta.md), [obj](obj.delta.md), [levels-txt](levels-txt.delta.md) deltas | verified on all shipped files | B6 | syntax and counts only; the meaning of the new object keywords is in engine-behaviour.delta.md; goldens and expected counts in `testdata/golden/as2/` |
| [render-pipeline.delta.md](render-pipeline.delta.md) | verified from code; the water look is unconfirmed on a running original | B5 | player-visible differences only: new water (grid, shore fade, waves, two layers), skid marks, boats on waves, shadows multiplied and not clipped at the ground, environment map coordinates, sprite facing; also amends hmap.md's water syntax; issues [220](issues/220-water-surface.md), [221](issues/221-skid-marks.md); corrects `re/symbols_as2.csv` in `re/symbols_as2_render.csv` |
| [engine-behaviour.delta.md](engine-behaviour.delta.md) | verified from code; co-op read from code only; sections 12 (sound) and 13 (rendering) not checked | B4 | player movement through an acceleration vector, 9 weapon slots with a loadout per mission, campaign checkpoint, civilians, touch mode bits, water, skid trails; its values are in `engine/src/game/game_profiles.cpp`; corrects `re/symbols_as2.csv` in `re/symbols_as2_game.csv`; issues [210](issues/210-skid-trail-details.md), [211](issues/211-load-time-spawn-and-statistics.md) |
| [rcsl-builtins-semantics.delta.md](rcsl-builtins-semantics.delta.md) | verified from code | B3 | 70 same, 15 changed, 16 new; 66 needed for mission 1; wins over rcsl-builtins-table.delta.md for `atan`, `RespawnPlayer`, `G_UsePowerUp` and wherever the two differ; `TerraMorph` needs mutable terrain ([issue 200](issues/200-terramorph-dynamic-terrain.md)) |
| [symbol-map.md](symbol-map.md) | 522 of 529 named v1.70 functions mapped or marked removed | B1 | overview of what changed in the engine; `re/symbols_as2.csv`, `re/symbols_as2_data.csv`; matching tools in `re/tools/` |
| [rcsl-container.delta.md](rcsl-container.delta.md) | verified from code | B2 | effectively the same; a script without a CODE section is valid |
| [rcsl-opcodes.delta.md](rcsl-opcodes.delta.md) | verified from code | B2 | all handlers the same; 7 opcode/mode pairs new to the corpus need no new code |
| [rcsl-vm.delta.md](rcsl-vm.delta.md) | verified from code | B2 | script-visible entity fields 0 to 87 unchanged; 28 globals; `create` returns the new entity; touch modes are a bit set |
| [rcsl-builtins-table.delta.md](rcsl-builtins-table.delta.md) | signatures verified | B2 | 101 builtins; `Lightning` gained an argument; machine-readable in `testdata/golden/as2/rcsl_builtins.json`; checked by `tools/ref/check_builtin_calls.py --game as2` |
