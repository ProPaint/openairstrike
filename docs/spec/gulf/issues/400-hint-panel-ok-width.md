# 400: Gulf Thunder's hint panel: the Ok button's width

Status: decided (engine choice, measured on the original). Raised by F2. Spec:
[../frontend.delta.md](../frontend.delta.md) 2.2, 2.5, 3.15.

## What the spec says

The tutorial hint box is the Gulf Thunder panel (220, 220, 360, 160) titled, its text red with
markup, and an Ok text button at (400, 520). A Gulf Thunder text button is W wide and 37 high
with W = max(caption width, the item's minimum width at +0x5C); the hint's Ok button's minimum
width is not given.

## What the original shows

No Gulf Thunder script reachable in play shows a hint, so the hint box itself was not seen. The
name-entry panel after the last operation (`out/reference/gulf/panel_enter_name.png`, captured
under Wine) has an Ok button of the same kind centred on x 400 at y 520: its frame runs from
x 359 to the right piece's tab at x 440, rows 520 to 557, i.e. W = 84 (left piece at the frame's
left edge, right piece's tab 48 texels into its 51).

## Our engine

`drawHintOk` (engine/src/ui/hud.cpp) draws Gulf Thunder's Ok button 84 wide and 37 high,
centred on the layout's button centre (x 400) at y 520, caption "Ok" light grey (red when
focused). The hit rectangle stays `layoutHint`'s sequel rectangle (the AirStrike 2 caption width
+ 46, 30 high, centred on x 400 at y 520): `layoutHint` does not know the game, and the two
rectangles overlap over the caption.
