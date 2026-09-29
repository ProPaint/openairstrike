# TGA image: Gulf Thunder delta (`gulf`)

Delta version 1.0. Amends [../as2/tga.delta.md](../as2/tga.delta.md) (which amends
[../tga.md](../tga.md)) for the game `gulf` (AirStrike II: Gulf Thunder v2.71). Tag:
`VERIFIED-DATA` over the 541 `.tga` files of `assets_extracted_games/gulf`
(`testdata/golden/gulf/tga_hashes.json`; `expected.json` has `textures.files` 541), checked
by `tools/ref/test_tga.py --game gulf` (agreement with PIL on every file) and the C++
`tga_test.cpp` with `AS3D_GAME=gulf`. No decoder change was needed.

## Header and image type

Bottom-left origin everywhere. Same set of types as `as2`:

| Type | Pixel depth | Colour map | Files (`as2`) |
|---|---|---|---|
| 1 colour-mapped | 8 | 24-bit entries | 315 (538) |
| 2 true colour | 24 | none | 121 (73) |
| 2 true colour | 32 | none | 99 (144) |
| 3 greyscale | 8 | none | 6 (6): the same five morph stamps and `gfx\ui\snow.tga` |

99 files have alpha (`hasAlpha`).

## TGA 2.0 footer

529 files end with the footer signature and **12 do not**, all under `gfx\ui\` (`as2`: 42).

## Sizes

Four files are not a power of two, the four morph stamps (`morphmaps\map1.tga` and `map2.tga`
7 x 7, `map_medium.tga` 5 x 5, `map1_big.tga` 11 x 11). `models\mapobjects\elektro\power_plant.tga`
is 256 x 256 here: `pak4.apk` replaces the 256 x 257 file of `pak0.apk` (pak.delta.md).
`morphmaps\map_small.tga` is 4 x 4.

## Checked sections

| Base section | Status |
|---|---|
| Layout | same |
| Header (18 bytes) | same |
| Image type | same as `as2` (type 3: 6 files) |
| Pixel depth and colour map depth | same |
| Pixel encoding (uncompressed) | same |
| RLE encoding (types 9-11) | not checked (no file uses it) |
| Image descriptor byte | same |
| Orientation | same |
| Alpha bits and `hasAlpha` | same rule (99 files with alpha) |
| TGA 2.0 footer | changed (12 files have none) |
| Inventory (all 464 files) | changed (541 files) |
| By size | changed (four non-power-of-two sizes) |
| Which directories carry an alpha channel | not checked |
| Separate alpha-mask companion files | not checked |
| Decoder behaviour (`as3d::decodeTga`) | same |

## Changelog

- 1.0 (B6): first version.
