# PAK archive (`data\pak*.apk`)

Spec version 1.0. Reference implementation: `tools/paktool.py`.
All claims VERIFIED-DATA against the three v1.70 paks (1351 files after overrides).

All integers are little-endian.

## Layout

| Offset | Size | Field |
|---|---|---|
| 0x00 | 8 | magic `00 00 80 3F 99 99 00 00` |
| 0x08 | u32 | file table offset |
| 0x0C | u32 | entry count |
| 0x10 | 1024 | XOR key |
| 0x410 | ... | file bodies, uncompressed |
| table offset | count × 76 | file table, XOR-encrypted |

## File table

The whole table is XORed with the key: table byte `i` (counted from the start of the
table) is XORed with `key[i % 1024]`.

Each decrypted entry is 76 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 64 | name, NUL-terminated, cp1251, backslash separators |
| 64 | u32 | body offset from start of archive |
| 68 | u32 | body size |
| 72 | u32 | encrypted flag (0 or 1) |

## File bodies

When the encrypted flag is 1, body byte `j` is XORed with `key[j % 1024]` (the key index
restarts at 0 for each file). In v1.70 the flag is set on every `.obj`, `.ps`, `.wpn` and
on `maps\levels.txt`, and clear on everything else.

## Mounting rules

- Paks are mounted in name order: `pak0`, `pak1`, `pak2`. A file in a later pak overrides
  the same name in an earlier pak.
- Lookups are case-insensitive, and `/` and `\` are equivalent. Object definitions use
  forward slashes (`models/tanks/x.mdl`) while scripts and the exe use backslashes.
- The original also scans `data\*.apk` and reports "Too many paks" past a limit (GUESS:
  not relevant to us).

## Counts (v1.70)

| Pak | Entries |
|---|---|
| pak0.apk | 1201 |
| pak1.apk | 113 |
| pak2.apk | 37 |

1351 distinct names in total, so no overrides actually occur in the shipped data.
