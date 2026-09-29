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

## Delta specs

| Spec | Status | Package | Notes |
|---|---|---|---|
