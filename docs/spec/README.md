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
| [mdl.md](mdl.md) | 1.0 | layout verified; tag direction and V orientation open |
| [rcsl-container.md](rcsl-container.md) | 1.0 | verified from code |
| [rcsl-opcodes-v0.md](rcsl-opcodes-v0.md) | 0 | opcodes verified from code; runtime spec pending |
