# 170: the player's game files from any copy, archive or installer

Status: open, design decided 2026-11-05. Raised by the r/airstrike3d thread: people download
the games from the links pinned there and from myabandonware, and the apps refuse what they get.
Affects `engine/src/game/game_data.cpp`, `game_import.cpp`, `exe_texts.cpp`, a new archive
module, `apps/web/site/files.js`, `apps/web/web_main.cpp`, `apps/game/android_import.cpp`,
`tools/games.json`, `tools/extract_exe_texts.py`, `tools/fetch_third_party.sh`, the docs.

Nothing here changes a game rule. The game data stays the player's own: no pak, no text of the
games and no executable is ever committed; the survey below records sizes and hashes only.

## 1. What people actually have (survey of 2026-11-05)

Every public copy of each game was downloaded and unpacked (the subreddit's pinned links, the
gametop installers, the five AirStrike 3D, four AirStrike 2 and six Gulf Thunder files of
myabandonware). Findings, by what the apps need:

**pak0.apk is the same in every copy of a game**, byte for byte in all but one case:

| Game | pak0.apk size | SHA-256 (first 12) | entries |
|---|---|---|---|
| as3d | 16717041 | `86745b285a6d` | 1201 |
| as2 | 39411244 | `9df601a5ebe1` | 1609 |
| as2, gametop "Air Assault 2" repack | 39411244 | `989b8324922a` (same 1609 files, same contents, packed differently; differs from byte 17) | 1609 |
| gulf | 35840140 | `0fde2db91134` | 1142 |

**The smaller paks vary by version.** pak1/pak2 of AirStrike 3D 1.70 (`548d8f1ad37c`,
`c992f0a28add`) are in every copy that has paks (1.55 and 1.70 "RIP" zips, the RAR). The
sequels' pak1/pak2 come in several builds with the same entry names and a few files of
different content (re-encoded sounds, 5 to 7 files out of 45 or 705). Gulf Thunder's pak4
(`836cf9c2d8e9`, one file) is in every Gulf copy and in gametop's. The "Windows 10 ... nVidia"
patch of AirStrike 3D adds `pak3.apk` (712541 bytes, 369 entries, all AirStrike 3D names:
an override pak the profile does not list today).

**Some copies have no pak0 at all**: gametop's "Desert Hawk" (a Gulf Thunder build) and
myabandonware's "AirStrike 2 Full RIP 2.50" keep pak0's content as loose files under `data/`
(gfx/, models/, textures/, tiles/, menu/, morphmaps/), next to pak1 and pak2. Both cover 100 %
of the game's pak0 entry names, so a pak0 can be built from them. The two "Latest Patch.exe"
CAB files of the nVidia packages hold the same loose files plus the executable.

**Containers seen**: zip (most), RAR 5 with a 64 MB dictionary (the pinned AirStrike 3D),
Inno Setup 5/6 installers (gametop, the "Setup" and "Version 2.71" and "Windows 10 ... nVidia"
installers of myabandonware), Microsoft CAB self-extractors with LZX (the "Windows 10 Setup",
"Version 1.48" and "Latest Patch" files), NSIS installers (two Gulf "Setup" files and the DRM-
wrapped AirStrike 2 "Setup"; 7-Zip extracts nothing from them), and archives nested in archives
(a zip holding a setup.exe, a RAR holding a game folder). A pak extracted from a split CAB can
come out truncated (its table offset points past the end of the file).

**Executables**: 12 distinct builds between 290 KB and 713 KB. The front-end texts are the
same strings in every build of a game, at different addresses: matched as whole NUL-terminated
strings, a build of AirStrike 3D holds 129 to 131 of the 131 reference texts, AirStrike 2 300
or 301 of 301, Gulf Thunder 225 to 230 of 230. A fixed-address table only fits the build it
was made from (two builds of Gulf Thunder share the 2.71 layout exactly). The gametop "Desert
Hawk" and "Air Assault 2" executables are such builds of Gulf Thunder and AirStrike 2.

## 2. Acceptance: by content, not by packing (package A)

1. **pak0 is identified by its entry names.** `identifyPak0File` reads the pak's file table
   (not the first 64 KB) and compares the set of entry names, lower-cased with `\`, with each
   game's reference set: a game whose set it equals, or overlaps by Jaccard >= 0.98, is the
   game. The reference sets are fingerprints kept in the engine as a hash per game (SHA-256
   of the sorted names joined by `\n`) plus the count, not the names; the exact-match path
   is that hash, the near-match path needs the names and is done by the tools and the
   importer with the lists in `tools/games.json` (a new `pak_entries` file per game under
   `tools/pak_entries/<key>.txt`, names only: a fact about the files, like the hashes). The
   old head signature stays as a fast path when it matches.
2. **The other paks are accepted by name beside an identified pak0** whatever their hash,
   when their entry names overlap the game's reference pak of that name by Jaccard >= 0.9.
   A pak whose names match no reference pak of the game is ignored with a note. An extra pak
   the profile does not list (`pak3.apk` of as3d) is kept and mounted after the listed ones
   when its names are all names of the game (an override pak); `GameProfile::paks` gains the
   optional `pak3.apk` for as3d.
3. **A truncated pak** (table offset or an entry body past the end of the file) is refused
   with a note that says so ("pak2.apk: truncated, 827833 bytes short: unpack it again").
4. **A game without pak0** is accepted when loose files under a `data` directory (or the
   directory itself) cover >= 99 % of the game's pak0 entry names: the importer writes a
   pak0.apk from them (stored bodies, zero XOR key, flag 0: docs/spec/pak.md) and the game is
   mounted as any other. The desktop tools do the same through `--paks DIR` when DIR has no
   pak0 (so a Desert Hawk install plays with `--game gulf --paks`).
5. **Known hashes** stay in `tools/games.json` for the fast path, extended with every
   variant of the survey (`pak_sha256` becomes a list per name).

## 3. Texts from any executable (package A)

`tools/extract_exe_texts.py`, `engine/src/game/exe_texts.cpp` and `files.js` match texts
by **hash dictionary**: for each game a generated file `tools/exe_texts/<key>.hashes.json`
maps every text key to the SHA-256 of the string's cp1251 bytes and its length (made once
from the owner's executable by the tool, `--write-hashes`; the strings themselves are not in
the repository). An unknown executable is scanned for NUL-terminated runs of printable cp1251
bytes (tab, LF, CR and 0x20..0xFF, 1 to 2000 bytes); each run's hash is looked up; the keys
found become the texts. A key whose string is not found gets no entry (the front end's
defaults or an empty line apply). The game of an executable is the one with the best ratio
of keys found when the address table does not know it, with a floor of 60 %; below it the
file is not a game executable. The fixed-address tables stay first for the known builds
(identical output). Duplicate values across keys are fine (the same string serves both).

## 4. Archives and installers (package B)

A new engine module `engine/src/formats/archive/` (desktop, Android, web; not the 3DS, which
never imports) gives the importer one way to open any container:

```
class ArchiveReader {  // as3d/archive.h
  static std::unique_ptr<ArchiveReader> open(IStream or path, std::string* error);
  const std::vector<ArchiveEntry>& entries();   // name ('/' separated), size, index
  bool extract(const ArchiveEntry&, sink, std::string* error);   // streamed
};
```

- **libarchive** (BSD) and **xz** (liblzma, public domain), fetched by
  `tools/fetch_third_party.sh archive` as pinned tarballs with SHA-256 and built from source
  by CMake like libopenmpt (`engine/src/formats/module.cmake`, option `AS3D_WITH_ARCHIVES`,
  ON except on the 3DS): formats zip, RAR 4 and 5, 7z, tar, CAB (LZX), ISO; zlib from the
  system (desktop), the NDK's libz (Android) and Emscripten's port (`-sUSE_ZLIB=1`); no
  OpenSSL, libxml2, bzip2, zstd or lz4.
- **Inno Setup** (own reader `inno_reader.cpp`): the loader's offset table at the end of the
  executable (`rDlPtS` signatures), the compressed setup header and file-location table of
  versions 5.x and 6.x (unicode and ANSI; the fields innoextract documents), stored, zlib,
  bzip2 (refused with a note) and LZMA1/LZMA2 chunks through liblzma's raw decoders, CRC-32
  per chunk. Only the entries of the `{app}` directory matter; the rest is listed and skipped.
- **CAB inside an executable**: the `MSCF` signature is searched in a PE file's overlay and
  libarchive opens the file from that offset.
- **Nested containers**: an entry that is itself an archive or an installer is opened in
  turn, two levels deep, in memory when below 64 MB and through a temporary file otherwise.
- **NSIS and DRM-wrapped installers** are recognised by their signatures and refused with a
  note that names them ("Setup.exe: an NSIS installer, which this app cannot open; install
  it on a PC and pick the game's data folder").
- The existing `ZipReader` stays for the 3DS build and as the fallback when archives are off.

The importer (`game_import.cpp`) walks archives through `ArchiveReader` instead of
`ZipReader`, with the same grouping by game directory. Nothing else changes in it.

## 5. The hosts (package B, then integration)

- **Android**: `ImportActivity` already copies the picked documents into `incoming/`; the
  importer now opens any of them. The picker's MIME filter accepts `*/*`.
- **Web**: `files.js` keeps hashing loose files in JavaScript; a dropped or picked file that
  is not a known loose file goes to the engine: `web_main.cpp` exports
  `as3d_unpack(ptr, len, nameptr) -> JSON` (Emscripten `EMSCRIPTEN_KEEPALIVE`, `ccall`),
  which runs the importer's classification on the archive in a MEMFS staging directory and
  returns the games' files it produced (paths in MEMFS); the page reads them back into Blobs
  and stores them as it does today. Memory: `-sALLOW_MEMORY_GROWTH` is already on; a 64 MB
  archive plus its output fits.
- **Desktop**: `as3d_game --import <file or directory>` runs the importer into the data root
  (`third_party_local/...` layout), so the desktop gets the same path and the tests cover it.

## 6. Tests

- Package A: unit tests on synthetic paks (names-based identification, near matches, extra
  pak, truncated pak, pak0 built from a folder, texts by hash on a synthetic "executable"
  made of NUL-terminated strings).
- Package B: fixture archives under `testdata/archives/` holding synthetic files (never game
  data): zip, 7z, RAR 5 (made once with `rar` from Debian non-free in Docker), CAB (`gcab`),
  an Inno Setup installer compiled once with ISCC under Wine from a two-file script, a zip
  nested in a zip, an exe with a CAB overlay. Each test opens, lists and extracts, and the
  importer test imports a synthetic game from each.
- The survey itself is not a test (no data in the repository); `docs/android.md` and
  `docs/web.md` get a "Where the files come from" section describing what works per source.

## 7. Out of scope

NSIS and DRM wrappers (refused with a message), the 3DS (its files are copied to the card),
texts of a build whose strings differ (a translation: the defaults apply).
