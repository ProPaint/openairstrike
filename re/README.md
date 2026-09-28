# Reverse-engineering exports (AirStrike3D v1.70)

This directory holds the Ghidra headless pipeline that turns
`third_party_local/original/AirStrike3D.exe` (the original v1.70 game binary, not committed to
this repo) into plain text/JSON files that later reverse-engineering agents can grep, `jq`, and
read without ever running Ghidra themselves.

Per `docs/spec/README.md`: **implementation work reads specs and `tools/ref/` only, never
decompiler output.** The files here (`re/out/`) are reverse-engineering scratch material for
writing specs, not something application code or specs may quote verbatim.

## Install and run

Ghidra itself is installed machine-wide (not inside this repo, so other projects can reuse it):
`$HOME/tools/ghidra/current` (a symlink to the versioned install), resolved by
`re/run_ghidra.sh` in this order:

1. `$GHIDRA_HOME` env var, if set.
2. `$HOME/tools/ghidra/current`.
3. `analyzeHeadless` on `PATH`.

If none of those exist, run the installer once:

```bash
re/install_ghidra.sh
```

This downloads **Ghidra 11.0.3** (the newest release still built against JDK 17 -- 11.1+
requires JDK 21) from the official GitHub releases page, verifies it against the published
SHA-256, and unzips it to `$HOME/tools/ghidra/ghidra_11.0.3_PUBLIC/` with a `current` symlink.
It writes nothing into this repository. See `$HOME/tools/ghidra/README.md` for the
machine-level install notes (version, checksum, install date).

Java resolution follows the same pattern (`$JAVA_HOME`, else `$HOME/Android/jdk`, else `java`
on `PATH`); a JDK 17-20 is required (this machine uses Temurin 17.0.20 at
`$HOME/Android/jdk`). No system JDK or package was touched to get this working.

Once Ghidra is installed, run the pipeline from the repo root:

```bash
re/run_ghidra.sh                  # full import + auto-analysis + export (clean-state path)
re/run_ghidra.sh --export-only    # re-run naming pass + exports only, reusing the existing
                                   # analyzed project (fast: ~25-30s vs ~1-2 min for a full run)
```

Both are safe to re-run. The full run re-imports with `-overwrite`, so it can be run repeatedly
from a clean state (delete `third_party_local/ghidra_project/` and `re/out/` first if you want
a truly from-scratch run). `--export-only` fails with a clear message if no project exists yet.

Optional arguments let you point the same script at a different build later:

```bash
re/run_ghidra.sh [--export-only] [exe_path] [out_dir]
```

`exe_path` defaults to `third_party_local/original/AirStrike3D.exe`; a different exe gets its
own Ghidra project (named after the exe's basename) and, unless you pass `out_dir` explicitly,
its own default output directory `re/out/<exe_basename>` and its own symbols CSV
`re/symbols_<exe_basename>_auto.csv`, so it never collides with the v1.70 outputs.

The Ghidra **project** lives under `third_party_local/ghidra_project/` (gitignored, binary
game-derived data). The **exports** live under `re/out/` (gitignored, regenerate any time from
the exe). Only `re/run_ghidra.sh`, `re/install_ghidra.sh`, `re/ghidra_scripts/*`, `re/README.md`
and `re/symbols_v170_auto.csv` are meant to be committed.

### Timing

On this machine (8 cores, 15 GB RAM, `MAXMEM` capped at a few GB, `-max-cpu 4`): a full clean
import + auto-analysis + naming pass + export (996-1061 functions, ~1217 strings, full
decompile of every function) takes **about 1.5-2 minutes**. `--export-only` (naming pass +
export only, no re-analysis) takes **about 25-30 seconds**.

## Ghidra version used

- Ghidra 11.0.3 (PUBLIC), build 2024-Apr-10.
- Archive: `ghidra_11.0.3_PUBLIC_20240410.zip`.
- SHA-256: `2462a2d0ab11e30f9e907cd3b4aa6b48dd2642f325617e3d922c28e752be6761`
  (as published on <https://github.com/NationalSecurityAgency/ghidra/releases/tag/Ghidra_11.0.3_build>).
- Runs on JDK 17-20 (`application.java.min=17`, no max set in this release).

## What the pipeline does

Two Ghidra Java scripts run as headless post-scripts, in this order (see
`re/ghidra_scripts/`):

1. **`RenameByErrorStrings.java`** -- the naming pass. Scans every defined string for the
   Quake-style pattern `Identifier: message` or `Identifier (message` (e.g.
   `"R_LoadModel: Couldn't open model file '%s'."`). For each identifier, if **exactly one**
   function in the program references any string sharing that identifier, that function is
   renamed to the identifier. If multiple functions match, nothing is renamed and the
   candidates are recorded instead. Writes `re/symbols_v170_auto.csv`
   (`address,name,evidence_string,confidence`) -- this file **is** committed (it only contains
   addresses, names and short evidence strings, no decompiled code or asset content).
   Confidence is either `renamed` (applied) or `ambiguous(N candidates)` (not applied; one row
   per candidate function).

2. **`ExportAll.java`** -- everything else, written to `re/out/v170/` (or `re/out/<tag>/` for a
   non-default exe):

   - **`functions.json`** -- one entry per function: `entry`, `name`, `size` (bytes),
     `calling_convention`, `is_thunk`, `is_external`, `callers` (addresses),
     `callees` (`{address, name}`, includes imported API thunks like `glBegin`/`BASS_Init`),
     `strings` (`{address, text}` referenced by the function), `data_refs` (all referenced data
     addresses, string and non-string).
   - **`strings.json`** -- every defined string: `address`, `text`, `functions` (entry
     addresses of every function referencing it).
   - **`imports.json`** -- per DLL (OPENGL32.DLL, GLU32.DLL, BASS.DLL, WINMM.DLL, WININET.DLL,
     KERNEL32.DLL, USER32.DLL, GDI32.DLL, SHELL32.DLL): each imported function's `name`,
     `iat_slot_address` (the real `.rdata`/`.data` IAT pointer, when Ghidra created a Data item
     for it), `thunk_stub_addresses` (any real jump-stub function(s) in `.text` -- present for
     some DLLs like BASS, empty for DLLs called via a direct indirect `CALL DWORD PTR
     [iat_slot]` with no stub, like most of OPENGL32/USER32), and `callers` (addresses of every
     function that calls it, resolved past any thunk stub to the real call sites).
   - **`data_tables.json`** -- best-effort scan of `.data`/`.rdata` for repeated
     `{name pointer, field}` (8-byte stride) and `{name pointer, field, field}` (12-byte
     stride) record runs of length >= 3, plus a targeted lookup of specific known
     builtin-function-name and global-variable-name strings and their referencing
     code/records. Tables whose entries include >=3 of the known builtin names are tagged
     `"kind_guess": "builtin_function_table"`; >=3 known global names ->
     `"kind_guess": "global_variable_name_table"`. As a side effect, before the main export
     passes run, the script also does a preliminary scan to find the builtin-function table
     specifically and calls `createFunction()` on any of its function-pointer entries Ghidra's
     default analysis hadn't turned into a `Function` yet (code that's only reachable via that
     data table, never via a direct call) -- so `functions.json` / decompiled / disasm end up
     complete for every builtin implementation, not just the ones Ghidra found on its own.
   - **`decompiled/<address>_<name>.c`** -- one file per function (decompiler output, 60s
     timeout each), plus **`all.c`** (everything concatenated) and
     **`decompile_errors.txt`** (address, name, reason for any timeout/failure -- empty on the
     v1.70 binary, every function decompiled successfully).
   - **`disasm/<address>_<name>.asm`** -- per-function disassembly listing: address, raw
     instruction bytes (hex), and the instruction text (with resolved symbols/operands).
   - **`SUMMARY.md`** -- function/string counts, entry point and (heuristically identified)
     WinMain address, best-effort addresses for the pak/filesystem/object/model/level-list/
     heightmap/script/save-file loaders, the sound/OpenGL init functions, the script
     interpreter loop and the builtin lookup function, the four acceptance-check strings and
     their referencing functions, and the 30 largest functions by size with a few of their
     referenced strings each (a quick way to spot the main update/render functions). The
     heightmap and script loaders are identified via a raw `.text` byte scan for their
     `"HMAP"`/`"RCSL"` file-format magic tags (compared as 4-byte immediates in code, never
     printed anywhere, so a string search can't find them) rather than via `strings.json`.

All exported addresses are absolute virtual addresses (image base `0x400000`), formatted as
`0x00xxxxxx`, matching what Ghidra shows in its own UI, so you can cross-reference these files
with a live Ghidra session if you ever open one.

## Known limitations / heuristic caveats

- **WinMain** is identified heuristically (BFS from the PE entry point through direct callees,
  picking the function that calls the most distinct USER32 windowing APIs
  RegisterClass/CreateWindowEx/ShowWindow/GetMessage/... ). Verify by reading its decompile.
- The **naming pass** only fires on strings matching the `Identifier: ` / `Identifier (`
  pattern; plain log lines without that shape (e.g. `"Script stall detected."`) don't trigger a
  rename even when they uniquely identify a function -- that function keeps its default
  `FUN_xxxxxxxx` name, but is still fully present (and still findable via `strings.json`) in
  every other export.
- **`data_tables.json`**'s run-based table scan is a generic heuristic over the whole
  `.data`/`.rdata` sections; most detected runs are `"kind_guess": "unknown"` (arrays of other
  string+pointer/int structures the game uses, e.g. UI option lists) and are dumped as-is for a
  human/later agent to interpret. The builtin-function-table detection can pick up one spurious
  leading record at a table boundary (an unrelated adjacent string happening to precede the
  real table) -- check the first 1-2 entries of any detected table by eye.
- Functions newly created by the builtin-table discovery pre-pass go through Ghidra's
  decompiler and get a disassembly listing, but (in an `--export-only` run) are **not** run
  through the full auto-analysis pipeline (stack analysis, calling-convention detection, etc.)
  since `-noanalysis` skips that; a full (non `--export-only`) run does re-analyze them.

## Looking things up later

Everything is either JSON (`jq`) or plain text (`grep`/`sed`), all under `re/out/v170/`.

**Find the function that references string X** (e.g. an error message):

```bash
jq --arg t "Script stall detected." 'map(select(.text | contains($t)))' strings.json
```

**List callers of function Y** (by entry address, e.g. `0x0041c680`):

```bash
jq --arg a "0x0041c680" '.[] | select(.entry == $a) | .callers' functions.json
```

**List callees of function Y**, including imported API names:

```bash
jq --arg a "0x0041c680" '.[] | select(.entry == $a) | .callees' functions.json
```

**Show the decompilation of address Z**:

```bash
ls decompiled/ | grep -i ^0x0041c680_
cat decompiled/0x0041c680_SL_GetExternFunc.c
```

(or just `grep -A1 "0041c680" all.c` to find it inside the concatenated file.)

**Show the disassembly of address Z**: same idea, under `disasm/`.

**Find every caller of an imported API** (e.g. `glBegin`):

```bash
jq '.["OPENGL32.DLL"][] | select(.name == "glBegin")' imports.json
```

**List all functions in a DLL's import table**:

```bash
jq '.["BASS.DLL"] | map(.name)' imports.json
```

**Dump the builtin script function table** (name -> implementation address):

```bash
jq '.tables[] | select(.kind_guess == "builtin_function_table") |
    .entries[] | {name: .string_text, addr: .second_field_address}' data_tables.json
```

**Dump the global variable name table**:

```bash
jq '.tables[] | select(.kind_guess == "global_variable_name_table") |
    .entries[].string_text' data_tables.json
```

**Find the 30 largest functions** (candidates for the main update/render loop): see the
"30 largest functions" section of `SUMMARY.md`, or:

```bash
jq -r 'sort_by(-.size) | .[:30] | .[] | "\(.entry) \(.name) \(.size)"' functions.json
```

**Grep the naming-pass mapping** (what got auto-renamed and why):

```bash
column -s, -t re/symbols_v170_auto.csv | less
```
