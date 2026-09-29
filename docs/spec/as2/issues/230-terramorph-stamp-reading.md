# 230: How our engine reads and applies a `TerraMorph` stamp

Status: decided (engine choices). Raised by C3. Behaviour of the original:
rcsl-builtins-semantics.delta.md, entry `TerraMorph`; issue [200](200-terramorph-dynamic-terrain.md).
Implementation: `World::terraMorph` in `engine/src/game/world_terrain.cpp`.

## Where the spec leaves room

1. **The ID field.** The original's raw TGA reader does not skip the image ID field, so a stamp
   with an ID would be read shifted. All five shipped stamps have no ID (VERIFIED-DATA, header
   byte 0 = 0). We read the pixels with our TGA decoder, which skips it; the result is the same
   for every shipped stamp.
2. **Types 1 and 2.** The original accepts uncompressed types 1, 2 and 3 at 8 bits per pixel and
   uses the raw bytes. We check the same header fields, then take the red channel of our
   decoder's output: for type 3 (every shipped stamp) that is the grey byte; for an 8-bit
   colour-mapped image it would be the palette colour, not the index. No shipped stamp is
   affected.
3. **Row order.** The original uses the file order (no flip). Our decoder turns every image top
   row first, so we put the rows back in file order using the descriptor's top-down bit.
4. **Stored name.** The name kept for the lookup is the argument with its extension replaced by
   `.tga`; the lookup compares the argument with the stored names. A name with another
   extension therefore never matches and loads again at every call, using up the 64 slots, as
   in the original. The shipped names all end in `.tga`.
5. **Precision.** The original adds (p − 128) × (hmax − hmin) / 255 on the x87 stack and stores
   a float. We compute in double and store a float; the results differ from the original's
   only by rounding in the last bit, if at all.
6. **Bounds.** A stamp side above 1024 pixels is refused (an allocation bound; the shipped
   stamps are 4 to 11 pixels). A larger stamp than the terrain is applied where it falls on the
   grid, never outside it.
7. **What the renderer sees.** Every call that wrote at least one vertex adds the rectangle of
   written vertices to `World::terrainChanges()`; the renderer takes them with
   `World::takeTerrainChanges()` (at most 64 per frame, further ones merge into the last). The
   original redraws the whole grid every frame; nothing else is recomputed in either.
