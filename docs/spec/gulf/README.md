# AirStrike II: Gulf Thunder v2.71 (`gulf`): executable and export facts

Facts about the executable and the reverse-engineering export only. No decompiled code.
Spec deltas against v1.70 are added to the table at the bottom by later packages.

## Executable

| Field | Value |
|---|---|
| File | `AirStrike3D II - Gulf.exe` (`third_party_local/games/gulf/`, from `AirStrike/AirStrike II Gulf Thunder` in `binaries/AirStrike.zip`) |
| Size | 655,360 bytes |
| SHA-256 | `86195a9653489064844c172ce43307c703a50e53be7e00d45fe346c45d5ae077` |
| PE timestamp | 1179236968 (0x4649BA68), 2007-05-15 13:49:28 UTC |
| Linker | 8.0 (MSVC 2005) |
| Image base | 0x00400000 |
| Entry point | 0x0043a076 |
| Protection | none (Ghidra analyses it directly) |

Sections (virtual address, virtual size, raw offset, raw size): `.text` 0x401000, 0x7c7c5,
0x1000, 0x7d000; `.rdata` 0x47e000, 0x17060, 0x7e000, 0x18000; `.data` 0x496000, 0x1d825a4
(mostly uninitialised), 0x96000, 0x7000; `.data1` 0x2219000, 0x900; `.rsrc` 0x221a000, 0x1308.

## Imports (178 functions)

`ADVAPI32.DLL` 5, `BASS.DLL` 28, `D3D8.DLL` 1 (`Direct3DCreate8` only: the renderer is
Direct3D 8, there is no `OPENGL32.DLL` / `GLU32.DLL` as in v1.70), `GDI32.DLL` 1,
`KERNEL32.DLL` 93, `SHELL32.DLL` 1, `USER32.DLL` 37, `WININET.DLL` 6, `WINMM.DLL` 6. Same set
as the AirStrike 2 executable.

## Game data

`tools/setup_data.sh gulf`: game directory in `third_party_local/games/gulf/`, paks extracted
to `assets_extracted_games/gulf/` (not inside `assets_extracted/`, which belongs to v1.70 and is
walked recursively by some tests). Paks in mount order: `pak0.apk` 1142 entries, `pak1.apk`
733, `pak2.apk` 45, `pak4.apk` 1 (`models\mapobjects\elektro\power_plant.tga`, which replaces
the texture of the same name from an earlier pak); 1921 entries, 1920 files after the override.
`data/` also holds 1143 loose files (`gfx`, `menu`, `models`, `morphmaps`, `textures`,
`tiles`, `Settings.xml`).

## Ghidra export

- Command: `AS3D_DATA_ROOT=<main checkout> re/run_ghidra.sh --game gulf` (see `re/README.md`).
- Output: `re/out/gulf/` (gitignored), project `third_party_local/ghidra_project/gulf/`,
  generated names in `re/symbols_gulf_auto.csv`.
- Ghidra 11.0.3. 1910 functions (all decompiled, 0 failures), 1370 defined strings.
- Entry point 0x0043a076; WinMain (heuristic, BFS from entry) 0x00414650, size 113.

## Probe results (`re/probes/gulf.json`)

- Builtin table: 101 of 101 probe names found (the 85 of v1.70 plus 16). Table starts at
  0x0049b448, 101 entries, stride 8: CONFIRMED (the scan's run starts two records earlier, at
  0x0049b438, on two unrelated "bad allocation" records).
- Script-global table: 28 of 28 probe names found (24 of v1.70 plus `player1`, `player2`,
  `p_maxHealth`, `cameramode`). Table at 0x0049b780, 28 entries, stride 12: CONFIRMED.
- The four acceptance strings are all present, each referenced by one function:
  `R_LoadModel` string 0x0048a498 (function 0x00416ca0), `Script stall detected.` 0x0048a990
  (0x0041d540), `SL_GetExternFunc` 0x0048afb0 (0x00420670), `G_LoadBin` 0x00488f9c (0x00406940).

## Delta specs

| Spec | Status | Package | Notes |
|---|---|---|---|
