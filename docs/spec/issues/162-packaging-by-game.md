# 162: Packaging and tools by game

Status: done (package A6 of the multi-game preparation). Affects `tools/android_build.sh`,
`tools/android_smoke.sh`, `tools/web_build.sh`, `tools/web_known_files.py`,
`tools/extract_exe_texts.py`, `apps/game/android_main.cpp`, `apps/web/web_main.cpp`,
`apps/web/site/{app.js,files.js,index.html,known_files.json}`, `apps/web/test/`,
`apps/viewer/cmd_text.cpp`, `cmd_screen.cpp`, `docs/android.md`, `docs/web.md`.

The engine runs three games from one binary (`as3d`, `as2`, `gulf`, `tools/games.json`). This
package makes the packaging tools ready for more than one game. The owner-facing behaviour is
unchanged: the Android app and the web page run AirStrike 3D as before, the owner's save
survives the move to `<key>/profile.bin` (issue 160), and no sequel is offered (no selector
screen yet; the sequels are refused on Android and web unless `--allow-unfinished` /
`?unfinished=1`, issue 161).

## Choices

**Android asset layout: everything under `assets/<key>/`, the first game included.** One
layout, no special case: `assets/as3d/{pak0,pak1,pak2}.apk, Settings.xml, logo2s.tga,
texts_v170.txt`; `assets/as2/...`; `assets/gulf/{pak0,pak1,pak2,pak4}.apk, ...`. The native
side builds every path as `<profile key>/<name>` (`android_main.cpp`); paks are still opened by
`SDL_RWFromFile` with a relative path, which SDL resolves through the asset manager
(`engine/src/platform/rw_stream.cpp` needed no change). The flat layout of the previous build
is not kept: an APK never holds both, and `tools/android_build.sh` deletes anything else in the
assets directory (a stale flat file or an unselected game) before copying. Which games go in:
`AS3D_ANDROID_GAMES` (default `as3d`; it becomes `as3d,as2,gulf` when the sequels play). The
pak, Settings.xml, logo and texts names come from `tools/games.json` (no field added). The
refusal to build when a game file is tracked by git covers every game's files, and anything
tracked under `assets/` except its `.gitignore`.

Sizes (debug APK, arm64-v8a and x86_64): `as3d` 37 871 086 bytes; `as3d,as2,gulf`
129 150 857 bytes.

**Web: `data/<key>/` in the bundled build, `<key>/<file>` keys in browser storage.**
`AS3D_WEB_GAMES` (default `as3d`) selects the bundled games. The page loads the files of the
game named by `?game=` (default `as3d`) from `data/<key>/index.txt` and writes them to
`/data/<key>/` of the engine's file system, where `web_main.cpp` reads them. The byo page stores
each game's files in IndexedDB under `<key>/<name>`; files stored by the first version of the
page (plain names, all AirStrike 3D's) are renamed to `as3d/<name>` in one transaction when
found, so an existing byo visitor is not asked for the files again.

**`known_files.json` is generated**, by `tools/web_known_files.py`, from `tools/games.json`
(games, exe and texts names, paks, pak SHA-256) and the files on disk (sizes, and the hashes of
`Settings.xml`, the logo and the executable, which games.json does not hold). A value the disk
cannot give stays what the existing file says, so it can be regenerated on a machine with only
some games. `tools/web_build.sh` regenerates it into every site it assembles. The file is
grouped by game (`games.<key>.files.<name>`, with `title`, `version`, `playable`, `exe`, `texts`,
`paks`). `playable` is written by the script (`as3d` only; keep in step with
`as3d::gameIsPlayable`), not stored in games.json.

**A dropped file is assigned to a game by its SHA-256** (size first, to avoid hashing files no
game has in that size). A file that two games share (the same hash) is stored for both. The page
keeps a game's files once all its paks are there; it starts only `as3d` for now. Files of a
sequel are accepted and stored with the plain message "<title>: files recognised and stored;
the game is not playable yet", and `?game=as2` without `&unfinished=1` shows that the game is
not playable instead of starting (`state.notPlayable`).

**Byo build still contains no game data**, and the mechanical check now covers every game: no
file may be a pak-header carrier, none may hash like any known file of any game in
`tools/games.json` (the pak hashes are facts and need no file; other files are hashed when
present), no file may contain long lines of any game's extracted texts, and every file name
must be on the whitelist (so a `data/` directory is refused).

**Text tables by executable hash.** `tools/extract_exe_texts.py` and its port in `files.js`
choose the table of text addresses from the SHA-256 of the executable (`TABLES` /
`EXE_TABLES`). `as3d` (3b371bc2...) has the table that existed; `as2` (b24b62b2...) and `gulf`
(86195a96...) have an empty table and say "texts of <title> are not mapped yet" (Python: exit 1,
nothing written; the page: a note on the file, the executable is recognised but no texts are
kept). Another package fills the addresses. The output file is the game's `texts` field of
games.json, beside that game's extracted data (`assets_extracted/` for `as3d`,
`assets_extracted_games/<key>/` otherwise); `--game KEY` states which game is expected.

**Bounds from the game's rules.** `?level=` and `?difficulty=` in `app.js` are passed on as
numbers (bounded to 1..9999 / 0..9999 only for sanity); `android_main.cpp` and `web_main.cpp`
read `--level` and `--difficulty` first and check them against the chosen game's rules
(mission count, difficulty count) once `--game` is known, warning about a value outside. The
viewer's `text` and `screen` commands take the data directory, `Settings.xml` and the texts file
from `viewer::selectedGame()` (`locateGameData`) instead of the first game's fixed paths.

## Verification

* Android, emulator: `AS3D_SMOKE_MIGRATION=1 tools/android_smoke.sh`: the APK of commit
  `7753c66` (the last build with the flat layout and the version 1 save) is installed and run,
  Screen is set to 4:3 (written to `profile.bin`); the new APK is installed over it with
  `adb install -r`; `run-as org.as3dport.game ls -la files files/as3d` shows `profile.v1.bak`
  (same bytes as the old file, compared) and `as3d/profile.bin` (version 2, key `as3d`), no
  `profile.bin`, and the game comes up with Screen 4:3. The stage's outputs (`migration_before.txt`,
  `migration_after.txt`, the log) are under `$AS3D_SMOKE_OUT`.
* Web, headless Chromium: `apps/web/test/walk.py --only migration` (a version 1 file at
  `/persist/profile.bin`; after a reload and after a second one: settings in effect,
  `/persist/as3d/profile.bin` and `/persist/profile.v1.bak` there, `/persist/profile.bin` gone),
  and `--only byo` (a wrong file refused, the owner's files, old-style keys renamed, AirStrike 2
  files told apart and stored under `as2/`).
* `apps/web/test/files_check.js` (node): every game's sizes and hashes, dropped files assigned to
  their game, texts of `as3d` equal to the Python tool's output, the sequels' message.

## Not in this package

The game selector screen, offering a sequel to the owner, the text addresses of the sequels, the
sequel's own front-end files that the original does not ship (no `logo2s.tga` was found for
`as2` or `gulf`; the page and the Android build treat it as optional).
