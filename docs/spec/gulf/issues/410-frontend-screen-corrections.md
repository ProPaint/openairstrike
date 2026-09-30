# 410: Gulf Thunder's front end: where the original's screens differ from frontend.delta.md

Status: decided (engine choices, each measured on the running original; corrections for the spec
owner). Raised by F4. Spec: [../frontend.delta.md](../frontend.delta.md); the text addresses of
[402](402-exe-texts-addresses.md) are fixed here too (item 9). Evidence: screenshots of the
original under Wine (`tools/run_original.sh gulf`, `out/reference/gulf/o_*.png` on the owner's
machine; never committed), compared with our render of the same screen by the viewer
(`as3d_viewer --game gulf screen <name> --state mt=1,...`).

Everything else in the delta matched the original screen: the title bar with its tint and scan
lines (row phase checked: the dark row is at y 100, then every 4), the panel, the text button
(37 high, no margin), the list, slider and spinner, the main and in-game menus' positions and
widths, the heli selection's arrows, the loading comic, the statistics in red, the credits'
layout, the three dialogues. The differences:

1. **Draw order of the title bar.** The delta says "Order: items, panel, title bar" (as in
   AirStrike 2). On the original's Start Game, Helicopter selection, Options, Controls, Mission
   Complete, Exit, Top Scores and Information screens the buttons of the bottom letterbox bar
   (Back at y 520, Next, Start) are drawn over the bar, and the panel's static fill darkens the
   emblem behind it. So the bar comes first (engine: `SequelScreens::build` wraps the screen's
   back drawer), then the panel and the items.
2. **Credits: 23 lines, not 21.** Lines 0 to 22 from y 120, 18 apart; line 21 "SOUND EFFECTS" and
   22 its name follow the music credit. `tools/exe_texts/gulf.json` has `credits.0` to
   `credits.22` (17 entries; blank lines have none).
3. **Information page 2 icons.** The "Machine Gun, Impulse Gun, Plasma Cannon, Quantum Gun" page
   shows the cells of HUD slots 0, 4, 1 and 2 of `weapons.tga` (texels (0, 0), (132, 0), (198, 35),
   (0, 35)), not slots 0 to 3; page 3 (Big Laser, Lightning Gun, Wave Gun) shows slots 5, 7, 8 at
   y 194, 295, 365. The missile and item pages are AirStrike 2's (texts keys 5 to 8). The icon
   of each page uses the HUD's UV table and blend as AirStrike 2's (`as2/frontend.md` 4.4).
4. **Configure Controls draws the outline** (200, 205, 400, 270) in the list's green (0x5000FF00,
   ALPHA); AirStrike 2's screen does not (issue as2/300), Gulf Thunder's does.
5. **Captions Gulf Thunder has twice.** The helicopter selection's Continue is " Continue "
   (0x48be28) and Game Complete's is "  Continue  " (0x48bc80); Mission Complete's Restart is
   "   Restart   " (0x48b8dc) and Game Over's "  Restart  " (0x48bd74; its frame measured 20
   pixels narrower: one space less a side, a space being 10 pixels wide). Keys
   `button.continue.heli` and `button.restart.gameover` in gulf.json; a front end without them
   falls back to `button.continue` and `button.restart`.
6. **The tutorial box's Ok.** No Gulf Thunder script reachable in play shows a hint (issue 400),
   so the box was not seen. The executable has "    Ok    " (0x48cd80) next to "Tutorial Tip"
   and "   Ok   " (0x48cc00) that the name entry uses (W = 84 measured, issue 400). The engine
   uses the first for the hint (key `button.hint_ok`, GUESS), so its Ok is 20 pixels wider than
   the name entry's. `hud.cpp`'s own hint drawer (used by nothing in this front end) keeps 84.
7. **Third helicopter.** "Steel Falcon" is what the original's helicopter selection shows for the
   third helicopter (it had been a GUESS in 402).
8. **Touch name entry.** The on-screen keyboard (ours) sits 10 pixels lower than AirStrike 2's
   (y 410), Gulf Thunder's panel bars being 7 pixels taller at the bottom.
9. **Text addresses (issue 402).** All 11 addresses that were not the first byte of their string
   and the six that named a neighbour are corrected in `tools/exe_texts/gulf.json` (addresses
   only): `title.enter_name` 0x48cbf0, `title.hint` 0x48cd70, `title.top_scores` 0x48cbd0,
   `difficulty.3` 0x48cd34, `difficulty.4` 0x48cd28, `mode.0` 0x48cd18, `mode.1` 0x48cd0c,
   `button.ok` 0x48cc00, `heli.2` 0x48bda4, `info.pages.1` to `7` from 0x48be80 down in steps of
   8, `ctl.row.0` to `9` (0x48b664, 0x48b654, 0x48b640, 0x48b630, 0x48b624, 0x48b618, 0x48b608,
   0x48b5f8, 0x48b5ec, 0x48b5e0: the row table has "-" separator records between its groups, the
   same finding as AirStrike 2's), and the credits (item 2). `heli.3` to `heli.5`,
   `info.pages.8` and the stray `info.4.0` are in `removed` (three helicopters, seven pages). The
   extraction tool and the web page's table read the file with no overrides any more; the
   generator `re/tools/gen_exe_texts_gulf.py` would put the old addresses back: the file is the
   authority now.

## Checked and unchanged

Colours (light grey, red, dark grey rows 0x404040), the list's scroll boxes (tint 0.75; the
hovered colour white stays a GUESS), the slider (bar colour white stays a GUESS), the name
entry without a title bar, the loading screen's name and bar, the game-over and game-complete
comics, the tint of the emblem per screen (white, 0xB0808080, 0x60808080), the dialogue panel.
