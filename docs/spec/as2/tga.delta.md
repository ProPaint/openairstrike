# TGA image: AirStrike 2 delta (`as2`)

Delta version 1.0. Amends [../tga.md](../tga.md) 1.0 for the game `as2` (AirStrike 2 v2.51).
Tag: `VERIFIED-DATA` over the 761 `.tga` files of `assets_extracted_games/as2` (the golden
set `testdata/golden/as2/tga_hashes.json`; `expected.json` has `textures.files` 761; the first
game's set has 463). Checked by `tools/ref/test_tga.py --game as2` (the reference decoder
agrees with PIL on every file, pixel for pixel) and the C++ `tga_test.cpp` with
`AS3D_GAME=as2` (`decodeTga` reproduces every golden hash). No decoder change was needed for
the game: all of it is inside what the base decoder already accepts.

## Header and image type

All 761 files are bottom-left origin (descriptor bits 4 and 5 are 0). Types and depths:

| Type | Pixel depth | Colour map | Files (first game) |
|---|---|---|---|
| 1 colour-mapped | 8 | 24-bit entries | 538 (101) |
| 2 true colour | 24 | none | 73 (268) |
| 2 true colour | 32 | none | 144 (94) |
| **3 greyscale** | 8 | none | **6** (0) |

No RLE (types 9 to 11) occurs. The six greyscale files are five morph stamps in
`morphmaps\` and `gfx\ui\snow.tga`; the decoder turns a grey level g into (g, g, g, 255) and
`hasAlpha` is false for them, as for every file that is not 32-bit or 32-bit-mapped (144
files have alpha; the descriptor's alpha-bit count is 8 in 674 files and is ignored, as in
the base).

## TGA 2.0 footer

The base says every shipped file ends with the TGA 2.0 footer. Here 719 files end with its
signature (`TRUEVISION-XFILE.` and a NUL) and **42 have none**: all 42 are under `gfx\ui\`. A file without a footer is a plain TGA 1.0 file:
the decoder never reads the footer (it locates the pixels from the header), so nothing
changes. 12 of the third game's files lack it (see the `gulf` delta).

## Sizes

Not every size is a power of two. Five files are not:

| File | Size | Note |
|---|---|---|
| `morphmaps\map1.tga`, `map2.tga` | 7 x 7 | greyscale morph stamps |
| `morphmaps\map_medium.tga` | 5 x 5 | greyscale morph stamp |
| `morphmaps\map1_big.tga` | 11 x 11 | greyscale morph stamp |
| `models\mapobjects\elektro\power_plant.tga` | 256 x 257 | colour-mapped model skin; the third game's `pak4.apk` replaces it with a 256 x 256 file |

`morphmaps\map_small.tga` is 4 x 4. The decoder returns each file at its own size (the base's limits: 1 to 8192 on each side);
a consumer that needs power-of-two textures must not assume the morph stamps are one. The tile atlases (`tiles\tiles1` to `tiles9`) are 512 x 64, 256 x 64,
256 x 64, 256 x 128, 512 x 64, 256 x 256, 256 x 64, 256 x 64 and 512 x 64 (see
hmap.delta.md).

## Inventory

The base sections "By size" and "Which directories carry an alpha channel" and "Separate
alpha-mask companion files" describe the first game's files. They are not repeated: the
golden file lists every decoded file with its width, height, alpha flag and pixel hash.

## Checked sections

| Base section | Status |
|---|---|
| Layout | same |
| Header (18 bytes) | same |
| Image type | changed (type 3 ships: 6 files) |
| Pixel depth and colour map depth | same (8 with 24-bit map; 24; 32; 8-bit grey) |
| Pixel encoding (uncompressed) | same |
| RLE encoding (types 9-11) | not checked (no file uses it) |
| Image descriptor byte | same (bottom-left origin everywhere) |
| Orientation | same |
| Alpha bits and `hasAlpha` | same rule (144 files with alpha) |
| TGA 2.0 footer | changed (42 files have none) |
| Inventory (all 464 files) | changed (761 files, table above) |
| By size | changed (five non-power-of-two sizes) |
| Which directories carry an alpha channel | not checked |
| Separate alpha-mask companion files | not checked |
| Decoder behaviour (`as3d::decodeTga`) | same (the decoder handles every file) |

## Changelog

- 1.0 (B6): first version.
