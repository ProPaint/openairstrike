# 091: front-end choices where the spec is silent or leaves a decision

Status: decided in WP-46 (implementation choices), for review.

1. **Wide screens** (`frontend.md` §8 question 3). The 800x600 field stays 4:3 and centred. The
   letterbox bars and the corner-texture rules extend to the framebuffer edges (the main menu's
   rotated corners stay at their 4:3 positions; the rules continue with the texture's edge
   column). Everything else, including full-screen fills (intro, game-over tint, loading), is
   drawn as in the original; full-screen fills cover the whole framebuffer.
2. **Brace width** (§8 question 4): `measureText` ignores `{` and `}` when markup is on, so
   highlighted lines are centred exactly (hint box width uses the same measure).
3. **Edit-field cursor** (§8 question 7): `_` drawn with the number routine.
4. **Helicopter grid and Tab**: §2.4 says the grid consumes Tab, §2.5 says "Tab moves on". We
   follow §2.5 (Tab leaves the grid).
5. **List clicks**: arrow zones are the 26 px at the top and bottom of the scroll column
   (x >= list x + w - 22); an arrow click steps the selection (like Up/Down); a track click
   sets the first visible row without moving the selection; event 1 is sent after any change.
6. **Spinner and list events**: event 1 is sent after every change; a list does not play
   `menu1.wav` on a row click (the widget consumes the click).
7. **Game Complete typing**: line i starts when the previous lines are fully typed; the first
   character of the whole text also plays `type.wav` (the level-name typewriter's "first
   character is silent" rule is not stated for this screen).
8. **Controls capture marker**: the blinking `=` is drawn at x = 403 on the capturing row, in
   place of its key names.
9. **Saving**: the profile is written after EndLevel (unlocks), after a high-score insert, when
   leaving Options or Configure controls, on Exit -> Yes and before Options -> Apply (the
   original writes game.bin only at exit and before a video restart).
10. **Right-click on the tutorial box**: closes and resumes (deviation table of the spec index).
11. **Weapon icons 10..19**: the original draws a quad with all-zero UVs; we draw nothing.
12. **Paletted pictures drawn with ALPHA** (`menu\gameover_0.tga`, `menu\loading.tga`, the
    headers' `_0` layers are drawn ADD): 8-bit colour-mapped TGAs carry no alpha, so
    `gameover_0.tga` shows its black rectangle, as the original would (render-pipeline.md
    9.1). Visible only over bright scenes.
