# 280: AirStrike 2 HUD: atlas checks, corrections and choices (package E2 + E5)

Status: implemented in `engine/src/ui/hud_layouts.cpp` (`hudLayout(GameId::AirStrike2)`),
drawn by `engine/src/ui/hud.cpp`, tested by `apps/tests/hud_layout_test.cpp`. Spec:
as2/frontend.md 4 and 3.15.

## Evidence used

- Every texel rectangle of as2/frontend.md 4.1, 4.4 and 3.1 (`mainbar2.tga`, `weapons.tga`,
  `missiles.tga`, `items.tga`, `interface.tga`) was cut out of the shipped atlases, enlarged and
  checked for content crossing its edges (lit texels just outside the rectangle, by alpha and by
  colour).
- Screenshots of the original game running under Wine (not committed; taken by another agent in
  the main checkout's `out/reference/as2/`): `m3_cheats.png`, `m3_hud_all_weapons_lake_paused.png`
  (mission 3 after the "glitteringprizes" cheat: five missile types and power-up slots 0 to 5 and
  8 at 99, lightning bomb selected), `m3_lake_t0..t3.png`, `m3_start_b.png`. Our
  `as3d_viewer --game as2 hud` render of the same state matches them element by element
  (positions to the pixel as far as can be read, colours, dimming, pips, counts, lives, level
  name and message lines).

## Correction

1. **One-player power-up column: no box frames and no selection line.** 4.2 says each power-up
   gets `box` mirrored covering (713, F, 87, 60) and, when selected, `sel_line` mirrored at
   (725, F + 5). The original's screenshots show only the icons and the counts in that column:
   no frame at all (the background shows through between the icons) and no green line over the
   selected lightning bomb, while the missile column on the left has its frames and its line.
   The icons (721, F + 17), the counts (right end 782, F + 13), their colours and the dimming
   are as 4.2 says. We follow the screenshots (`HudColumn::drawFrames`, `drawLine` false for
   this column only). Why the original draws nothing is not known: the score frame is also a
   mirrored quad (4.2) and does show, mirrored, in the same screenshots, so it is not simply
   "negative widths are culled". The two-player layout keeps the frames and lines of 4.3
   (no two-player screenshot yet; unverified).

## Rectangles checked and kept

- `mainbar2.tga`: `bar_fill`, `bar_frame`, `score_frame`, `box`, `sel_line` exact. The pips
  (`level_on` (16, 164, 7k, 6)) have a faint glow row at texel y 170 that the 6-row rectangle
  leaves out, and the atlas holds nine pips; invisible, kept.
- `weapons.tga`: all nine cells exact (the 58-wide cells of slots 2 and 7 are stretched to 66
  as the spec says). The two further cells of Gulf Thunder's `weapons.tga` (texel row 70, x 66
  and 132) are not in the AirStrike 2 table.
- `items.tga`: all ten cells exact; slot 2 (rocket strike) is the cross-hair at (66, 0), slot 3
  (cluster bomb) the canister at (132, 0), as the spec and the screenshots agree.
- `missiles.tga`: type 4 (M.A.D.) (0, 35, 66, 29) cuts the lower missile's nose (texel rows 64
  to 67 hold 4 rows of it). The file and the table are the first game's, which draws the same
  cut; kept as the original draws it.
- `interface.tga`: the panel, title tab, cable, button and rivet pieces of 3.1 and 2.5 are
  exact.

## Choices

- Unselected icons: ALPHA icons white with alpha 0x40/255, the ADD lightning-bomb icon grey
  0x40/255; timer slots 6, 7, 9 always opaque (4.4). Level pips: `upgrades[weapon]` clamped to
  the slot's maximum (a cheat could exceed it; the atlas has nine pips).
- The fill's segment count is clamped to 0..42 (4.2); with 0 the 17-pixel cap still shows.
- `life.tga` is drawn with the half-texel UV inset of `R_Add2DPic` (frontend.md conventions).
- `p_weapon` is rounded (4.2) for the sequels in `hudStateOf`; the first game keeps truncation.
- The 2D half-pixel shift (render-pipeline.delta.md 8.2) is not switched on by the HUD: it is a
  renderer-wide setting, invisible with the shipped data, and left to the front end's owner.
- Hint box (3.15): `UI_DrawPanel` titled "Tutorial Tip", opening over 0.125 s, lines once
  open, the "  Ok  " text button fully slid in at y 520 with the caption's own width as its
  width (the button's minimum width is not given for this box). The panel's noise offsets come
  from a generator of the HUD's own (issue 240 item 2). The plain front end (issue 260) keeps
  its own Ok button and its first-game box size; it gets the panel look through
  `drawHintPanel` once the assets are loaded for AirStrike 2.
- Gulf Thunder: AirStrike 2's layout (its `mainbar2.tga`, `life.tga`, `missiles.tga`,
  `items.tga` are the same files). Its panel atlas `interface_gulf.tga` is arranged differently
  (AirStrike 2's rectangles land on the wrong pieces), so its hint box stays the first game's
  plain box. Unverified beyond being readable.
