# 060: HUD facts the specs do not give

Status: open. The HUD in `engine/src/ui/hud.cpp` follows `engine-behaviour.md` 11.2 exactly where
that section speaks, and makes the following choices where it is silent. A reverse-engineering
pass should establish the items in the last list.

## Choices made (all GUESS unless stated)

1. **Blend modes of HUD pictures.** Not stated. `mainbar`, `life`, `font` are paletted (no alpha)
   and `weapons.tga` is 32-bit with alpha 255 everywhere, so all four are drawn BLEND_ADD (black
   is transparent). `missiles.tga` and `items.tga` have real alpha and use BLEND_ALPHA.
2. **Cell sizes in `weapons.tga`, `missiles.tga`, `items.tga`.** 64x34 texel cells, 4 columns
   (weapon w and missile type t at column `n mod 4`, row `n div 4`); measured from the pictures
   (3 rows of weapons, 5 missile icons, 4 item icons). The spec says "UVs from a table indexed by
   p_weapon"; the table is not given, so index = cell number.
3. **Weapon box frame** is `mainbar.tga` texels (0,42)-(70,81) (the only 70x39 frame in the file).
4. **Slots.** Missile frames and power-up frames are packed (k counts owned entries) rather than
   placed by type index. Power-up frames are drawn like missile frames (grey 0.5), count in the
   same orange as the selected missile.
5. **Missile count position** inside its frame: right-aligned at frame.x + 64, y = frame.y + 25.
6. **Level-name typewriter** colour white, y = 500; message line centred at y = 110, colour
   (1, 0.85, 0.3).
7. **Stars, weapon upgrade level, boss bar.** Nothing in the spec places them. Stars: text
   "STARS n" at the bottom right (one player) or under the player's column (two players).
   Upgrade level: small yellow pips along the top edge inside the weapon box. Boss bar: the
   health frame stretched to 300 px at (250, 10). The original may not have any of these on
   the HUD (entities draw their own health bars in the entity pass).
8. **Two-player HUD (0x402a90, not decoded).** Player 1 gets a left column, player 2 its mirror:
   health bar (10, 10), score number at scale 0.75 below it, weapon box (10, 48), missile frames
   from y = 89, power-ups in a second column at x = 86, lives icons at the bottom, all mirrored
   about x = 400 for player 2 (frames and bars flip their texture, icons and text do not).
9. **Screen mapping.** The 2D layer keeps the 4:3 field centred on wide screens (uniform scale by
   height); aspects between 5:4 and 4:3 stretch like the original; narrower ones letterbox
   vertically. The original stretched everything, which is unusable on 16:9 and wider.
10. **Bytes 0x80 to 0xFF** advance the pen but draw nothing (the original would draw solid
    additive rectangles because `font_rus.tga` is missing).
11. **Tutorial hint** text is centred per line, wrapped by width (max 680 px) instead of being
    cut at 64 characters; at most 16 lines. Panel colours and the OK button look are ours.

## To establish from the executable

- Weapon icon UV table (index p_weapon to cell) and whether it also picks a frame colour.
- Blend modes and colours of every HUD draw call, and the item icon cell size.
- Two-player HUD layout (0x402a90): positions, whether it shows both scores, lives, stars.
- Whether and where stars, weapon upgrades and boss health are shown outside the world.
- Typewriter and message colours and exact y positions.
- Whether missile and power-up frames are packed or fixed per type.
