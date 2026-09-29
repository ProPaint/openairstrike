# 160: The save file knows which game it belongs to

Status: done (package A3 of the multi-game preparation). Affects `as3d/profile.h`,
`engine/src/game/profile_io.cpp`, `engine/src/platform/user_data.cpp`, `as3d/platform.h`,
`apps/game/game_flow.*`, tests `profile_test.cpp`, `profile_migration_test.cpp`.

The engine runs three games from one binary (`as3d`, `as2`, `gulf`). Each keeps its own save.

## File format, version 2

Little-endian. Version 1 had the same layout without the key.

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | magic `AS3DPROF` |
| 8 | 4 | version (2) |
| 12 | 4 | payload size |
| 16 | 4 | CRC-32 of the key bytes followed by the payload |
| 20 | 1 | key length, 1..15 |
| 21 | key length | game key, `a-z 0-9 _` (`as3d`, `as2`, `gulf`) |
| 21 + key length | payload size | chunks `tag[4] | u32 size | data`, unknown tags skipped |

Chunk `PROG`: u8 count (<= 15) of high scores `{u8 name length <= 31, name, i64 score, u8 rank}`;
u8 count (<= `kMaxHelicopters`) of helicopter flags; u8 count (<= `kMaxMissions`) of mission
flags. Chunk `SETT`: unchanged (named integers).

Reading: accepted when the key equals the expected game's and the counts are within the
maxima. Entries beyond the game's current counts are ignored, entries the file lacks take the
game's defaults (a save from before a game gained missions keeps its progress). Everything
else as before: the size field must match the file, the CRC is checked, every length and count
is validated against what remains before anything is allocated or indexed; a bad file or chunk
falls back to the defaults of that chunk. Writing goes through `<path>.tmp` and a rename.
A version 1 file is accepted as the first game's only, with the strict count check of before
(20 missions, 10 helicopters, 15 scores).

## Location and migration

`<user data dir>/<key>/profile.bin` on every platform (Linux `$XDG_DATA_HOME` or
`~/.local/share/airstrike3d`, overridable with `AS3D_USER_DATA_DIR`; Android internal storage;
web `/persist`). `--profile PATH` and other explicit paths are used as given, without migration.

When the first game's file `<key>/profile.bin` does not exist and `<user data dir>/profile.bin`
does, it is loaded as `as3d`, written as version 2 to the new place, and the old file is renamed
`profile.v1.bak` beside it (`.bak.1` ... if a backup exists already; never deleted, never
overwritten). If the directory cannot be created or the new file not written, the old file is
left untouched and used for the session (and migrated at the next start that can write). If
the old file is unreadable it is left alone and the defaults are used. A sequel never looks at
the old location. Once the new file exists the old one is ignored.

## Web

Directories under `/persist` (IDBFS) are created with `mkdir` and the rename is an ordinary
MEMFS rename; `FS.syncfs(false)` writes the whole tree, so the page's existing sync after a
save covers new directories and the removed file. The game calls `profileSaved` right after a
migration so the first sync happens before the next save. To re-verify in a browser: a
`/persist/profile.bin` from before, start, reload the page: settings and unlocks are still
there, `/persist/as3d/profile.bin` and `/persist/profile.v1.bak` exist, `/persist/profile.bin`
does not.
