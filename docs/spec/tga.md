# TGA image (`gfx\*.tga`, `menu\*.tga`, `models\**\*.tga`, `textures\**\*.tga`, `tiles\*.tga`)

Spec version 1.0. Reference implementation: `tools/ref/tga.py`.

All data claims are VERIFIED-DATA against all 464 shipped `.tga` files: the 463 files under
`assets_extracted/` (extracted from `pak0`/`pak1`/`pak2`) plus the one loose file
`third_party_local/original/data/gfx/logo2s.tga`, which is identical in format to the packed
files. This is the full population of TGA files shipped with v1.70, so "all" below means all of
them, not a sample.

This document only covers the subset of the Truevision TGA (TARGA) format that occurs in, or is
required to safely reject, AirStrike 3D's data. It is not a complete implementation guide for the
TGA format in general.

## Layout

```
[18-byte header] [image ID, idLength bytes] [colour map] [image/pixel data] [18-byte footer, optional]
```

All multi-byte integers are little-endian.

## Header (18 bytes)

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | u8 | idLength | length of the image ID field that follows the header |
| 1 | u8 | colourMapType | 0 = no colour map, 1 = colour map present |
| 2 | u8 | imageType | see below |
| 3 | u16 | colourMapFirstEntry | index of the first colour map entry |
| 5 | u16 | colourMapLength | number of colour map entries |
| 7 | u8 | colourMapDepth | bits per colour map entry: 15, 16, 24 or 32 |
| 8 | u16 | xOrigin | ignored by the decoder |
| 10 | u16 | yOrigin | ignored by the decoder |
| 12 | u16 | width | pixels |
| 14 | u16 | height | pixels |
| 16 | u8 | pixelDepth | bits per pixel: 8, 15, 16, 24 or 32 |
| 17 | u8 | imageDescriptor | see "Image descriptor" below |

VERIFIED-DATA: `idLength` is 0 in all 464 shipped files, so the image ID field is always empty
(the decoder still supports skipping a non-zero-length ID field, since nothing prevents new data
from setting one). `xOrigin`/`yOrigin` are not meaningful for a decoder producing a single
top-down image and are ignored.

### Image type

| Value | Meaning | Shipped? |
|---|---|---|
| 0 | no image data | no |
| 1 | uncompressed, colour-mapped | **yes**, 101 files |
| 2 | uncompressed, true colour | **yes**, 363 files |
| 3 | uncompressed, greyscale | no |
| 9 | RLE, colour-mapped | no |
| 10 | RLE, true colour | no |
| 11 | RLE, greyscale | no |
| other | anything else | no |

VERIFIED-DATA: only types 1 and 2 occur in the shipped data; no shipped file uses run-length
encoding. The decoder supports types 1, 2, 3, 9, 10 and 11 regardless, per the work package
requirement to be robust to data the game itself never ships (e.g. assets from other versions, or
future test fixtures). Types 0 and anything not in {1,2,3,9,10,11} are rejected.

### Pixel depth and colour map depth

| imageType | pixelDepth | Files |
|---|---|---|
| 1 (colour-mapped) | 8 | 101 |
| 2 (true colour) | 24 | 268 |
| 2 (true colour) | 32 | 95 |

VERIFIED-DATA: no shipped file uses 15- or 16-bit pixels. The decoder supports 8/15/16/24/32-bit
pixels for types 1/2/3/9/10/11 as specified, since malformed/synthetic inputs must not crash it
even though the real data never exercises those depths.

Every shipped colour-mapped file (all 101 of them) has `colourMapDepth = 24`,
`colourMapFirstEntry = 0`, `colourMapLength = 256` — a full 8-bit palette, BGR, no alpha. No
15/16/32-bit colour map occurs. The decoder supports colour maps of 15/16/24/32 bits per the
requirement.

Colour map entries are stored BGR (24-bit) or BGRA (32-bit), 2 bytes little-endian 1-5-5-5 (with
the top bit as attribute/alpha) for 15/16-bit, matching pixel data encoding (see below).

### Pixel encoding (uncompressed)

- 8-bit (type 1 only): one byte, a colour map index.
- 15-bit: `u16`, `0BBBBBGGGGGRRRRR` (top bit unused/attribute).
- 16-bit: `u16`, `ABBBBBGGGGGRRRRR` (top bit is a 1-bit alpha, per the TGA 2.0 spec; not present
  in shipped data).
- 24-bit: 3 bytes, `B G R`.
- 32-bit: 4 bytes, `B G R A`.

### RLE encoding (types 9-11)

GUESS (public Truevision TGA 2.0 spec; not exercised by any shipped file, so this section is not
VERIFIED-DATA): image data is a sequence of packets. Each packet starts with a 1-byte header:
bit 7 set = run-length packet, clear = raw packet; bits 0-6 = count-1 (1 to 128 pixels).

- Run-length packet: header byte, followed by **one** pixel (at the type's pixel depth), which is
  repeated `count` times.
- Raw packet: header byte, followed by `count` pixels verbatim.

Packets are concatenated until `width * height` pixels have been produced; a packet is never
allowed to overrun that total, and the decoder rejects files where it does (a malformed/hostile
RLE stream could otherwise write out of bounds or hang).

## Image descriptor byte (offset 17)

| Bits | Field | Meaning |
|---|---|---|
| 0-3 | attribute bits | number of attribute (typically alpha) bits per pixel, as declared by the writer |
| 4 | reserved / horizontal origin | 0 = left-to-right (only value seen); some non-conformant writers use 1 = right-to-left |
| 5 | vertical origin | 0 = bottom-to-top (first stored row is the bottom row), 1 = top-to-bottom |
| 6-7 | reserved | must be 0 |

VERIFIED-DATA: across all 464 files, the descriptor byte is only ever `0x00` (268 files, all
24-bit true colour) or `0x08` (196 files: 95 32-bit true colour + 101 8-bit colour-mapped). Bits
4, 6 and 7 are 0 everywhere. **Bit 5 (vertical origin) is 0 in every shipped file**: every file
stores its first scanline at the bottom, i.e. bottom-left origin, the historical TGA default.

### Orientation

Since every shipped file has origin bit 5 = 0 (bottom-left), the decoder must vertically flip
every one of them to produce `Image::rgba` with row 0 = top, as `as3d::Image` requires. The
decoder honours bit 5 generically (so a hypothetical top-left-origin file decodes correctly
too), and this is exercised by synthetic tests since no shipped file has bit 5 set.

### Alpha bits and `hasAlpha`

The attribute-bit nibble (bits 0-3) is **not** a reliable signal for whether a file actually
carries alpha: it is `8` on 196 files, but only 95 of those (the 32-bit true colour files) have
any alpha byte in their pixel data at all. The other 101 are 8-bit colour-mapped files whose
colour map is 24-bit (BGR, no alpha channel) — there is no alpha data anywhere in the file, yet
the writer still declared "8 attribute bits". GUESS: this looks like an artifact of whatever
batch TGA exporter produced these assets (e.g. a fixed "8-bit alpha channel" checkbox left on),
not a deliberate flag. Consequently `decodeTga` derives `hasAlpha` from the **actual** bit depth
that was decoded — pixel depth 32, or (for colour-mapped images) colour map depth 32 — never from
the descriptor's attribute-bit count. For every source without a real alpha channel, output alpha
is fixed at 255.

## TGA 2.0 footer

VERIFIED-DATA: all 464 shipped files end with the standard 26-byte TGA 2.0 footer:

| Offset (from end) | Size | Field | Shipped value |
|---|---|---|---|
| -26 | u32 | extension area offset | 0 (no extension area) |
| -22 | u32 | developer directory offset | 0 (no developer area) |
| -18 | 18 | signature | `TRUEVISION-XFILE.\0` |

The decoder does not need to read the footer (nothing in it affects pixel decoding for these
files), and does not require its presence: a TGA 1.0 file without a footer is equally valid and
must decode the same way. The footer, when present, is simply trailing data the decoder ignores.

## Inventory (all 464 files)

### By type and depth

| imageType | pixelDepth | colourMapDepth | Count |
|---|---|---|---|
| 1 (colour-mapped) | 8 | 24 | 101 |
| 2 (true colour) | 24 | — | 268 |
| 2 (true colour) | 32 | — | 95 |

### By size

All 464 files are square-power-of-two in *each* dimension independently (widths and heights are
each one of 4, 8, 16, 32, 64, 128, 256), but width and height are frequently different, so many
files are non-square (186 of 463 packed files, e.g. most UI/menu art and many model skins are
128x64, 256x128, 64x32, 256x32, 16x128...). The largest dimension seen is 256; the decoder's
8192 limit is a safety margin, not derived from the data. No file has a non-power-of-two size, a
zero size, or a truncated header or pixel payload — the shipped data is entirely well-formed.

Top sizes by frequency: 128x128 (124), 64x64 (74), 128x64 (49), 256x128 (46), 256x256 (39),
32x32 (31), 256x64 (23), 64x32 (17), 64x128 (15), 256x32 (9), 16x16 (8).

### Which directories carry an alpha channel

Only the 95 32-bit true-colour files carry real per-pixel alpha (see previous section — the
8-bit colour-mapped files never do, regardless of their descriptor byte). Usage is concentrated
in UI/HUD and level-tile art, and sparse elsewhere:

| Directory (top-level) | 32-bit / total |
|---|---|
| `tiles/` | 5/5 |
| `gfx/ui/` | 4/7 |
| `menu/` | 31/54 |
| `models/mapobjects/` | 29/81 |
| `models/helics/` | 7/34 |
| `gfx/weapons/` | 3/5 |
| `models/comanche/` | 3/9 |
| `gfx/` (top level) | 4/26 |
| `gfx/clouds/` | 1/1 |
| `models/bosses/`, `models/items/` | 2 each |
| `models/desert_builds/`, `models/ships/`, `models/weapons/` | 1 each |
| everything else (`models/apache`, `banner`, `btrs`, `cannons`, `futuristic`, `gibs`,
  `gruzovik`, `jeeps`, `misc`, `planes`, `rocketlauncher_mobil`, `tanks`, `trains`,
  `weapons_enemy`, `gfx/effects`, `gfx/water`, `textures/*`) | 0 |

The five `textures/{badlands,desert,grass,rock,snow}` terrain sets (5 files each, 25 total) are
all 8-bit colour-mapped and never carry alpha, consistent with them being tiled ground textures.

Of the 95 32-bit files, 90 have varying per-pixel alpha (real masks); 5 have a **constant** alpha
across the whole image, which is presumably either an authoring leftover or an intentional flat
tint/mask:

| File | Size | Constant alpha |
|---|---|---|
| `gfx/ui/weapons.tga` | 256x128 | 255 (fully opaque) |
| `tiles/tiles3.tga` | 128x64 | 255 (fully opaque) |
| `menu/yesno_2.tga` | 128x64 | 0 (fully transparent) |
| `models/items/ammo/laserbox.tga` | 64x64 | 0 (fully transparent) |
| `models/items/ammo/machinebox.tga` | 64x64 | 0 (fully transparent) |

### Separate alpha-mask companion files

`gfx/ui/` contains a `font.tga` / `font_alpha.tga` pair, matching what the executable's string
table references (`gfx\ui\font.tga`, `gfx\ui\font_alpha.tga`): `font.tga` is an 8-bit
colour-mapped 256x256 RGB glyph atlas (no alpha data at all — see above), and `font_alpha.tga`
is a *separate* 256x256 32-bit true-colour file that presumably supplies the per-glyph alpha
mask the renderer combines with `font.tga`'s colour. This is the only `*_alpha.tga` companion
file that ships (`find … -iname '*alpha*'` over all 463 packed files returns exactly this one
match); nothing else in the data uses this convention, so the renderer needs to special-case
`font.tga` rather than generically looking for a `<name>_alpha.tga`.

## Decoder behaviour (`as3d::decodeTga`)

- Always produces `as3d::Image` with row 0 at the top, RGBA8, regardless of source orientation
  (flips vertically when the source's origin bit is bottom-left, which is every shipped file).
- `hasAlpha` is true iff the *decoded* pixel data or colour map has a real alpha channel
  (pixel depth 32, or colour-map depth 32); never derived from the descriptor's attribute-bit
  count (see above). Output alpha is 255 everywhere else.
- Supports image types 1, 2, 3, 9, 10, 11; pixel/colour-map depths 8 (type 1/9 index only),
  15, 16, 24, 32.
- Rejects (returns `false`, leaves `out` empty, never reads out of bounds): type 0 or any type
  outside {1,2,3,9,10,11}; width or height of 0 or above 8192; a header, ID field, colour map,
  or pixel payload that doesn't fit in the supplied buffer; a colour-mapped image whose pixel
  index is outside `[colourMapFirstEntry, colourMapFirstEntry + colourMapLength)`; an RLE stream
  whose packets would produce more than `width * height` pixels.

## Changelog

- 1.0 (2026-09-28): initial version, covering all 464 shipped `.tga` files.
