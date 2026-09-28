# Specifications

These documents are the contract between reverse engineering and implementation.
Implementation work reads specs and `tools/ref/` only, never decompiler output.
If a spec is wrong or silent, add `docs/spec/issues/NNN-title.md` instead of guessing.

## Confidence tags

Every claim carries one of:

| Tag | Meaning |
|---|---|
| `VERIFIED-DATA` | proven by a script across all shipped files |
| `VERIFIED-CODE` | read from the v1.70 executable, address cited |
| `INFERRED-SEQUEL` | seen only in the sequel's decompilation |
| `GUESS` | plausible, unproven |

## Rules

- Specs describe formats and behaviour in our own words. No decompiled code, no asset content
  beyond short structural excerpts.
- Every binary format spec ships a Python reference parser in `tools/ref/` that accounts for
  every byte of every shipped file.
- Each spec has a version line and a changelog at the bottom.

## Index

| Spec | Version | Status |
|---|---|---|
| [pak.md](pak.md) | 1.0 | verified |
| [text-blocks.md](text-blocks.md) | 1.0 | verified |
| [tga.md](tga.md) | 1.0 | verified |
| [mdl.md](mdl.md) | 1.1 | verified; winding reconciliation open |
| [hmap.md](hmap.md) | 1.0 | layout verified; engine behaviour from code |
| [obj.md](obj.md), [wpn.md](wpn.md), [ps.md](ps.md), [levels-txt.md](levels-txt.md) | 1.0 | verified from parsers; some sub-field names guessed |
| [rcsl-vm.md](rcsl-vm.md) | 1.0 | verified from code |
| [rcsl-builtins-table.md](rcsl-builtins-table.md) | 1.0 | signatures verified; gameplay builtin internals pending |
| [rcsl-container.md](rcsl-container.md) | 1.0 | verified from code |
| [rcsl-opcodes-v0.md](rcsl-opcodes-v0.md) | 0 | opcodes verified from code |

## Engine decisions where we deliberately differ from the original

| Topic | Original | Our engine |
|---|---|---|
| Random numbers | MSVC `rand()` | xorshift32, seed 1 (`as3d::Rng`) |
| Smooth model normals | unnormalised sums | normalised |
| Terrain vertices facing away from the sun | black (ambient not scaled by 255) | lit with the ambient colour |
| `TerrainHeight` outside the map | reads one cell past the edge | clamps to the edge |
| Online high scores, CD check | present | dropped |
