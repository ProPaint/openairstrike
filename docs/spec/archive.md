# Archives and installers

Version 1.0. How the engine opens the containers the games are downloaded in, so that the
importer (`as3d/game_import.h`) takes the player's download as it is
([issues/170](issues/170-game-files-from-anywhere.md), sections 1 and 4). Code:
`engine/include/as3d/archive.h`, `engine/src/formats/archive/`, built by
`engine/src/formats/archive.cmake` on the desktop, Android and the web (option
`AS3D_WITH_ARCHIVES`, forced off on the 3DS, which never imports; `as3d/zip_reader.h` stays for
it). Formats of other people's software are described here only as far as this reader needs.

## 1. What is read

| Container | Reader | Notes |
|---|---|---|
| zip (stored, deflate, LZMA, xz, zip64) | libarchive | |
| RAR 4 and RAR 5 (any dictionary size, solid) | libarchive | a RAR 5 file must end with its end-of-archive block, else it is refused as truncated (section 5) |
| 7z (LZMA, LZMA2, BCJ, PPMd, deflate) | libarchive | |
| Microsoft CAB (stored, MSZIP, LZX) | libarchive | also inside an executable, section 2 |
| tar, tar.gz, tar.xz | libarchive | |
| ISO 9660 | libarchive | |
| Inno Setup installers, setup data 5.1.0 to 6.4.x | `inno_reader.cpp` | section 3 |
| NSIS installers | refused | "an NSIS installer, which this app cannot open; install it on a PC and pick the game's data folder" |
| Gentee CreateInstall installers without a CAB | refused | the same note naming Gentee |
| other Windows programs | refused | "a Windows program, not an installer or archive this app can open"; the importer keeps a small one as a candidate game executable |

Third-party code, fetched by `tools/fetch_third_party.sh archive` into
`${AS3D_DATA_ROOT}/third_party_local/` with pinned SHA-256 and built from source:
libarchive 3.8.11 (BSD 2-clause; its read side only, with a hand-written configuration header,
`archive/libarchive_config.h`, and no OpenSSL, libxml2, expat, bzip2, zstd, lz4, lzo, iconv, ACL
or xattr support) and xz 5.8.4 (liblzma, 0BSD; decoders only, no threads). zlib comes from the
system on the desktop, the NDK on Android and Emscripten's port on the web.

`ArchiveReader::open` decides the kind (section 4) and returns a reader whose `entries()` have
'/' separated names with no leading '/', the uncompressed size and an index. `extract` streams
an entry to a sink and checks the container's own checksum; `extractToMemory` (with a size cap)
and `extractToFile` are built on it. libarchive readers keep one open handle between calls, so
extracting entries in their order decompresses a solid archive once; an earlier entry reopens
it. `openEntry` opens an entry that is itself a container: in memory up to 64 MB, else through a
temporary file that its reader removes. The importer opens nested containers two levels deep (a
setup.exe in a zip, a zip in a RAR), see `collectArchive` in `engine/src/game/game_import.cpp`.

## 2. Containers inside an executable

A self-extractor is a Windows program with the archive in it. The detector looks, in order, for:

1. a 7z, RAR or CAB signature at the start of the overlay, the bytes after the end of the last
   section (the largest `PointerToRawData + SizeOfRawData` of the section table);
2. a CAB header anywhere in the file whose fields are those of a cabinet: `MSCF`, reserved
   fields 0, version 1.3, at least one folder, a first file entry inside both the cabinet and
   the bytes that follow. The cabinet may claim more bytes than follow: the survey's
   "Windows 10 Setup" of AirStrike 3D keeps a cabinet inside the data of a Gentee CreateInstall
   stub, 1 MB into the file, and the cabinet is cut short (its last files read as truncated);
3. a zip end-of-central-directory record in the last 64 KB + 22 bytes.

The reader then opens the file from that offset as a window of the stream.

## 3. Inno Setup

An Inno Setup installer is a small loader program followed by the installer's data. Everything
below is little-endian; "string" is a 32-bit byte length followed by the bytes (UTF-16LE in the
unicode builds, the ANSI code page otherwise).

### 3.1 The loader's offset table

Somewhere in the loader (a resource) is a 44-byte table:

| Offset | Size | Field |
|---|---|---|
| 0 | 12 | `rDlPtS` followed by `CD E6 D7 7B 0B 2A` |
| 12 | 4 | table version (1) |
| 16 | 4 | minimum size of the whole file |
| 20 | 4 | offset of the compressed setup program |
| 24 | 4 | its uncompressed size |
| 28 | 4 | its CRC-32 |
| 32 | 4 | `Offset0`: where the setup data starts |
| 36 | 4 | `Offset1`: where the file data starts (0: in separate `setup-N.bin` files) |
| 40 | 4 | CRC-32 of bytes 0..39 |

Loaders before 5.1.5 have no version field (a 40-byte table, CRC of bytes 0..35); the reader
accepts both shapes by their CRC. The table is searched in the first 32 MB of the file.
Installers with `Offset1` 0 (disk spanning) are refused with a note.

### 3.2 The setup data

At `Offset0`: a 64-byte identifier, `Inno Setup Setup Data (a.b.c)`, followed by ` (u)` in the
unicode builds of 5.x (all 6.x builds are unicode), padded with zeros. Then two blocks, each:

| Size | Field |
|---|---|
| 4 | CRC-32 of the next 5 bytes |
| 4 | stored size of the chunks that follow |
| 1 | 1 when the content is compressed |
| ... | chunks: a CRC-32, then up to 4096 bytes, until the stored size is used up |

The concatenated chunk bytes are an LZMA1 stream (5 property bytes, then the raw LZMA data),
or the content itself when not compressed.

The first block holds the setup header, then the entry tables in this order: languages, custom
messages, permissions, types, components, tasks, directories, files, icons, INI entries,
registry entries, install-delete, uninstall-delete, run, uninstall-run, then the wizard images.
The reader reads up to the file entries and stops. The second block holds the file location
entries, one per stored file.

Every record is written as its strings (each a length-prefixed string, in declaration order;
the 8-bit "AnsiString" fields come after the others) followed by its fixed-size part. The
header's fixed part starts with the 16 entry counts (32-bit) in the order of the tables above; in
the ANSI builds a 32-byte lead-byte set comes first. The reader needs, per version:

| Setup data version | Header strings | Header fixed part (ANSI / unicode) | CompressMethod at (ANSI / unicode) | Language entry fixed part | File entry strings | Location entry size | Location flags at | Checksum |
|---|---|---|---|---|---|---|---|---|
| 5.1.0 | 27 | 178 / - | 169 / - | 24 | 9 | 69 | 68 | MD5 |
| 5.1.13 | 28 | 178 / - | 169 / - | 24 | 9 | 70 | 68 | MD5 |
| 5.2.1 | 29 | 186 / - | 169 / - | 24 | 9 | 70 | 68 | MD5 |
| 5.2.3 | 29 | 186 / - | 169 / - | 25 | 9 | 70 | 68 | MD5 |
| 5.3.0 | 29 | 186 / 154 | 169 / 137 | 25 / 21 | 10 | 70 | 68 | MD5 |
| 5.3.3 | 29 | 188 / 156 | 169 / 137 | 25 / 21 | 10 | 70 | 68 | MD5 |
| 5.3.6 | 29 | 192 / 160 | 169 / 137 | 25 / 21 | 10 | 70 | 68 | MD5 |
| 5.3.8 | 30 | 192 / 160 | 169 / 137 | 25 / 21 | 10 | 70 | 68 | MD5 |
| 5.3.9 | 30 | 196 / 164 | 173 / 141 | 25 / 21 | 10 | 74 | 72 | SHA-1 |
| 5.3.10 | 30 | 188 / 156 | 173 / 141 | 25 / 21 | 10 | 74 | 72 | SHA-1 |
| 5.5.0 | 31 | 192 / 160 | 173 / 141 | 25 / 21 | 10 | 74 | 72 | SHA-1 |
| 5.5.6 | 32 | 192 / 160 | 173 / 141 | 25 / 21 | 10 | 74 | 72 | SHA-1 |
| 5.5.7 | 32 | 189 / 157 | 170 / 138 | 25 / 21 | 10 | 74 | 72 | SHA-1 |
| 6.0.0 | 34 | 200 / 167 | 180 / 148 | 25 / 21 | 10 | 74 | 72 | SHA-1 |
| 6.3.0 | 36 | - / 166 | - / 148 | 21 | 10 | 75 | 72 | SHA-1 |
| 6.4.0 | 36 | - / 177 | - / 160 | 21 | 10 | 87 | 84 | SHA-256 |
| 6.4.2 | 37 | - / 177 | - / 160 | 21 | 10 | 87 | 84 | SHA-256 |
| 6.4.3 | 37 | - / 177 | - / 160 | 21 | 10 | 85 | 84 | SHA-256 |

A row applies from its version up to the next row. In every version here a language entry has
10 strings, a custom message 2 strings and 4 bytes, a permission entry 1 string, a type entry
4 strings and 30 bytes, a component 5 strings and 42 bytes, a task 6 strings and 26 bytes, a
directory 7 strings and 27 bytes, and a file entry's fixed part is 43 bytes with the index of
its location entry at offset 20. The file entry's second string is its destination name, such
as `{app}\data\pak0.apk`; the reader names the entry after it with '/' separators. The
version numbers in the identifier change only when the layout does (6.1.0 is written by
6.1 and 6.2, 5.5.7 by 5.5.7 to 5.6.1). 6.5 and later (new file entry fields, 64-bit offsets)
and 5.0.x are refused with a note that names the version. The 5.1.x to 6.4.x layouts were taken
from Inno Setup's published `Struct.pas` of each release and checked against the survey's
5.1.10 (ANSI) and 6.0.0 (unicode) installers.

A file location entry: first and last disk slice (32-bit each), the chunk's start offset
(32-bit, from `Offset1`), the file's offset inside the decompressed chunk (64-bit), its size
(64-bit), the chunk's compressed size (64-bit), the checksum of the file (16, 20 or 32 bytes),
a time stamp and version numbers, then the flags. The flag bits the reader uses: before 6.4.3,
bit 4 call instructions transformed, bit 6 encrypted, bit 7 compressed; from 6.4.3, bit 2, bit 3
and bit 4.

### 3.3 The file data

A chunk at `Offset1 + start offset` begins with `zlb` 0x1A, then the compressed stream of every
file in the chunk, one after the other (a solid installer has one chunk). The method is the
header's `CompressMethod`: 0 stored, 1 zlib, 2 bzip2 (refused), 3 LZMA1 (5 property bytes, then
raw LZMA data), 4 LZMA2 (one dictionary-size property byte, then raw LZMA2 data); a chunk whose
compressed flag is clear is stored. The reader decodes with liblzma's raw decoders and zlib,
skips to the file's offset in the decompressed stream and reads its size. Encrypted chunks and
files spread over several slices are refused.

### 3.4 Call instruction transform

Files of x86 code (the compiler applies it to executables and libraries) had the 32-bit
operands of their `E8` (CALL) and `E9` (JMP) instructions made absolute before compression, so
the reader undoes it after decompression. Three variants:

- before 5.2.0: over the whole file, a running position starting at 5; after an `E8` or `E9`
  byte the next four bytes, read as a little-endian number added to by carries, have the
  position subtracted;
- 5.2.0 to 5.3.8: per 64 KB block, at each `E8`/`E9` whose fourth operand byte is 0x00 or
  0xFF, the low three operand bytes have (block offset + position after the operand)
  subtracted;
- 5.3.9 and later: as the previous, the 24-bit operand having that address subtracted
  modulo 2^24, and the fourth byte inverted when bit 23 of the result is set.

Then the checksum of the location entry is compared with the file's (MD5, SHA-1 or SHA-256).

## 4. The detector

`archiveKind(path | stream | bytes)` returns one of zip, rar, 7z, cab, tar, iso, inno, nsis,
gentee, pe, unknown, from the head bytes first: `PK` 03 04 / 05 06 / 07 08 (zip), `Rar!` 1A 07
(rar), `7z` BC AF 27 1C (7z), `MSCF` (cab), gzip or xz magic or `ustar` at 257 (tar), `CD001` at
0x8001 (iso). A file starting with `MZ` is a Windows program: the Inno Setup offset table
(section 3.1) makes it inno; `NullsoftInst` in its first 32 MB makes it nsis; then section 2's
self-extractor checks; then `Gentee.Installer` in its first 1 MB makes it gentee; otherwise pe.
From a head only (bytes not the whole file) the checks that need the end of the file are
skipped and a program is pe.

## 5. Limits

- Truncated files: libarchive refuses most; a cut RAR 5 file is refused when its last 8 bytes
  are not an end-of-archive block (CRC-32 of the next 4 bytes, size 3, type 5); an Inno Setup
  installer with its file data cut reads its header and fails on the files ("truncated: the
  installer's data ends early"); a CAB cut short gives its complete files and fails on the
  rest.
- Passwords: refused (libarchive's encryption support is not built; encrypted Inno Setup
  chunks are refused).
- Multi-volume RAR, split CAB sets and Inno Setup disk spanning: not supported.
- Memory: the setup header block is capped at 64 MB; nested containers above 64 MB go through
  a temporary file.

## Changelog

- 1.0 (2026-11-05): first version (docs/spec/issues/170, package B).
