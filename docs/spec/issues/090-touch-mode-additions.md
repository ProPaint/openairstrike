# 090: touch-mode additions to the front end

Status: implemented (WP-46), for review. `frontend.md` §7 lists what a touch-only device lacks;
this is what `MenuSystem::touchMode` / `Frontend::setTouchMode` add. Nothing changes when the
flag is off, except where marked "both modes".

## Input model

- A tap is a pointer move followed by a press and release of Mouse1 (`UiInput::tap`), so focus
  follows the tap and activation happens on the press, like the original.
- No hover: no tooltips, no hover re-test when a menu is pushed or popped, no pulsing arrow of
  the list scroll bar or grid cell under the pointer, no drawn cursor.

## Widgets

| Widget | Original | Touch mode |
|---|---|---|
| Spinner | a click anywhere while focused cycles forward; backwards only with Left / wheel | taps are hit-tested; a tap in the zone x-30..x+8 (around the "<" drawn left of the value) cycles backwards, elsewhere forwards; "<" and ">" are drawn around the value |
| Slider | a click anywhere while focused sets the value; drag only while the slider stays hovered | taps are hit-tested; a press anywhere on the row starts a drag that follows the finger until release |
| Edit field | a click sends event 1 (on the name entry: submits) | a tap only focuses the field |

## Screens

| Screen | Missing on touch | Added |
|---|---|---|
| Playing | Esc (in-game menu), P (pause) | a "MENU" text button at (360, 6, 80, 22), top centre between the health and score bars; it opens the in-game menu (Resume unpauses) |
| Configure controls | Esc to cancel a capture, Backspace/Delete to unbind | while a row waits for a key: "Clear" (250, 452, 130, 26) unbinds it, "Cancel" (420, 452, 130, 26) cancels. A tap elsewhere binds Mouse 1, as a click does in the original |
| Name entry | a keyboard | an on-screen keyboard of 4 x 10 keys (A-Z, 0-9, `.`, `-`, `_`, space) from (201, 390), keys 38 x 24, and a "Del" key right of the field (532, 282, 52, 22); `Frontend::wantsTextInput()` stays false so the host does not also open the system keyboard |
| Information | PgUp/PgDn, Left/Right | the page spinner's backwards zone; the "PgUp/PgDown" key hints are not drawn |
| Start Game, Options | reverse spinner cycling | the spinner zones above |

All added buttons are "text buttons" (`Menu::addTextButton`): the label on a 0x80000060 box,
rust, orange with a pulsing outline when focused, in the style of the focused spinner.

Esc / right click stand for "back" in the original; every screen that has a back action also has
a Back, No, Resume or OK button, so no general back button was added. The Android system Back
gesture can be mapped by the host to the Esc key code.

## Not in the menu system

- Video options (resolution, refresh rate, colour depth, fullscreen, 3D sound) are hidden when
  the host sets `FrontendContent::videoOptions = false` (Android); the viewer does so with
  `--touch`.
- In-game controls for the ten actions are the game loop's business (`frontend.md` §7 table).
