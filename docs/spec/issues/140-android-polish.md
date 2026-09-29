# 140: Android polish: launcher icon, 4:3 screen mode, round touch buttons

Status: open, choices made. Raised by WP-51 (the owner's requests after playing the APK on a
phone: "add an icon, enable a 4:3 toggle, better looking controller overlay"). Affects
`android/app/src/main/res`, `tools/android_build.sh`, `engine/src/input/touch_mapper.cpp`,
`engine/src/ui/renderer2d.cpp`, `engine/src/ui/screens_options.cpp`,
`engine/src/game/profile*.cpp`, `engine/src/render/world_render.cpp`, `apps/game/*`.

Nothing here is in the original; every addition is ours and changes no game rule.

## 1. Launcher icon

* **Our own icon, committed**: an adaptive icon (`mipmap-anydpi-v26/ic_launcher.xml`, minSdk 26
  so no legacy PNG mipmaps) with vector layers: a dark background with a faint warm radial
  glow; the foreground, a helicopter seen from above (red teardrop fuselage, dark canopy,
  skids, tail boom, stabiliser, orange tail rotor) under a faint orange rotor disc with three
  orange blades; and a monochrome layer (the same shapes in one colour, canopy cut out) for
  themed icons on Android 13 and later. Everything lies inside the 66 dp safe circle. Three
  blades rather than two: two crossed blades read as an "X" (a "no" sign) at launcher size.
* **Icon from the owner's data, optional**: `tools/android_build.sh` renders the default player
  helicopter (`p_comanche`, helicopter 1 of the Start Game grid) with
  `as3d_viewer object p_comanche --pitch 89.9` from above, four times with the camera turned by
  0, 90, 180 and 270 degrees. The viewer's backdrop and square ground grid look the same in
  the four views and the helicopter does not, so a pixel equal to the same pixel of another view
  is background. The translucent rotor disc (grey-blue over the backdrop) is dropped, the
  largest connected piece kept (stray rotor-tip arcs go), the result turned nose up and scaled
  to 60 % of a 432 x 432 transparent PNG. It goes into `android/app/src/icon_from_data/res/`
  (gitignored) with an `ic_launcher.xml` that uses it as the foreground over our background and
  monochrome layers; `build.gradle` adds that directory to the debug and release resources
  when it exists, and build-type resources override `main`'s. `AS3D_ICON_FROM_DATA=0`, a
  missing viewer binary, missing data or PIL, or any failure gives our own icon silently. The
  script refuses to build if the generated directory is tracked by git.
* The app label is "AirStrike 3D" (`strings.xml`, unchanged).

## 2. Screen: Wide / 4:3

* `Settings::screenMode` (`kScreenWide` = 0, default; `kScreen4x3` = 1), stored in the profile's
  `SETT` chunk under the key `screenMode`. The chunk is a list of named integers and unknown or
  missing names are skipped or defaulted, so this needs no version bump: files written before
  load with Wide, and older builds reading a newer file ignore the key (`polish_test.cpp`
  builds a pre-WP-51 file byte by byte and loads it).
* **Wide**: as before; the 3D world fills the screen, the HUD and the game's rules use the
  centred 4:3 area (issue 100 section 3).
* **4:3**: `GameView` draws the world into the centred 4:3 rectangle only
  (`WorldRenderer::render` with a viewport rectangle; the clear is scissored to it, and the
  projection uses its 4:3 aspect, which is the aspect the game rules already assume), then the
  2D layer and brightness as usual, and finally clears everything outside the rectangle to
  black, so menus' full-screen dimming and letterbox bars end black too. The touch buttons are
  drawn after that, in the bars.
* Applied live (the Options spinner calls the host's `settingsChanged`; the view reads the mode
  every frame). The simulation never sees it: `polish_test.cpp` runs mission 1 with the bot
  drawn in both modes at 20:9 and compares the state dumps.
* **Options**: a "Screen:" spinner (Wide, 4:3) in the rows the original's video options use
  (160 and 180; this port never offers the video options). Android: always. Desktop: when the
  window is wider than 4:3 at the time Options opens (the host reports the framebuffer size).
  `as3d_game --screen wide|4x3` overrides the setting for the session until the player changes
  the option.
* Screens narrower than 4:3 (the stretched 5:4 case and letterboxed portrait shapes): 4:3
  mode draws the world in the virtual 800 x 600 rectangle of the 2D mapping, whatever it is.

## 3. Round touch buttons

Semantics of issue 100 are unchanged (relative drag, auto-fire, the same six buttons and the
same bits); the layout and the drawing are new.

* **Sizes in millimetres**: main buttons (missile, power-up) 12.5 mm, satellites (next
  missile, next weapon, next power-up) 9 mm, pause 8 mm drawn, 1.2 mm between buttons, 2 mm
  from the screen edges and cutout insets. Every touch area is at least 9 mm across (the drawn
  circle plus half the gap, or 9 mm). The density comes from `SDL_GetDisplayDPI` on Android
  (the diagonal value, Android's bucketed `densityDpi`; logged as `AS3D_DPI`); outside 80 to
  1200 dpi, or on the desktop (`--dpi N` sets one), the screen is taken for a landscape phone
  68 mm tall. Sizes are also capped by fractions of the screen height (main 0.20, satellite
  0.15, pause 0.12) for small or low-density screens.
* **Cluster** under the right thumb: missile in the bottom corner, power-up straight above it,
  next missile and next power-up as satellites on the upper inner side of their main button,
  next weapon above power-up. The satellites' angle goes from 145 degrees (towards the screen
  centre) down to 90 (straight up, a single column) until the cluster fits the bar beside
  the 4:3 field; if even the column does not fit, the main buttons narrow to the bar (the
  satellites stay 9 mm, down to 80 % of that when the height is short). 20:9 phones get the
  arc; 16:9 the column. Narrower screens (16:10 and 4:3 tablets) put the cluster inside the
  field at the bottom, below the HUD's power-up column (right) or above the lives and below
  the missile column (left), as before.
* **Pause**: a small round button in the top corner of the other side, inside the insets; top
  centre of the field when the buttons are inside.
* **Left-handed**: `Settings::leftHanded` ("Controls: Right / Left" in Options, touch mode only,
  stored like `screenMode` under `leftHanded`) mirrors the layout (cluster left, pause right);
  inside the field the left cluster is placed to clear the HUD's left column and lives, so it
  is not an exact mirror there.
* **Hit test**: circles; where two touch areas overlap the button whose centre is nearest
  relative to its radius wins. A finger that goes down in a bar outside the buttons drags as
  anywhere else.
* **Drawing** (`apps/game/touch_overlay.cpp`): a soft shadow, a dark translucent body, a rim
  (orange for the main buttons, light grey for the satellites) and a faint inner ring. Pressed:
  a warm orange body, a bright rim and an additive orange glow; pressed buttons are always at
  full opacity. Icons come from the HUD atlases through `ui::missileIconUv`,
  `ui::powerupIconUv` and `ui::weaponIconUv`: missile shows the selected missile type and its
  count, power-up the selected power-up and its count (the HUD shows counts above 1 only; the
  button always shows it), next weapon the current weapon with a small orange "next" chevron;
  next missile and next power-up a white double chevron. Counts use the game's number font on a
  dark backing; a zero count greys the icon and the digits. Nothing is removed from the HUD.
  Without the HUD textures the old drawn symbols are used.
* **Fade** (`as3d::TouchFade`): full opacity while a button is held and for 2 s after the last
  button use, then a 0.5 s ease to the resting opacity: 0.5 over the world (Wide), 0.7 over the
  black bars (4:3), 0.35 inside the field. Play starts at full opacity. No opacity setting.
* **Circle primitive**: `Renderer2D::circle` and `ring` add a quad flagged as an inscribed
  ellipse; the fragment shader computes the distance from the centre and anti-aliases the
  edges over one pixel (`fwidth`) plus an optional feather. The horizontal radius follows the
  mapping, so circles stay round in the stretched 5:4 to 4:3 case. Existing quads are
  untouched (the shape flag is 0).
* `AS3D_LAYOUT` now logs each button as `name=x,y,r` (framebuffer pixels) plus the screen mode,
  hand and pixels per millimetre; `tools/android_smoke.sh` taps those centres.
* Headless: `as3d_game --headless --touch --ui-script` accepts `finger down|move|up <id>
  <x> <y>|<button name>` lines to press the buttons for screenshots.

## 4. Touch speed

The owner found the helicopter "could be a tad faster" under the finger. Measured on mission 1
with the real player script (`polish_test.cpp`, "a fast swipe on the real helicopter"): the
finger moves 200 virtual px in 0.1 s.

| Step | gain | lead | helicopter moved | 150 px after | 90 % after | top speed |
|---|---|---|---|---|---|---|
| 0 (WP-48) | 1.5 | 160 | 165 px of 300 asked | 31 frames | 30 frames | 7.6 px/frame |
| 1 (default) | 1.875 | 200 | 204 px of 375 | 26 | 35 | 8.2 |
| 2 | 2.25 | 240 | 244 px of 450 | 26 | 37 | 8.2 |
| 3 | 2.625 | 280 | 284 px of 525 | 26 | 41 | 8.2 |
| 4 | 3.0 | 320 | 331 px of 600 | 26 | 47 | 8.2 |

Two limits: (a) the target's lead over the helicopter (160 px) cut a fast swipe short, so the
helicopter covered only about half of what the finger asked, and braked early (the 10-frame
look-ahead) without ever running at full speed for long; (b) the helicopter's own top speed,
150 units/s (about 8.2 virtual px a frame, 490 px/s), set by the player script. From step 1 on
the arrival time over 150 px is the same 26 frames: (b) is then the limit, and the higher steps
only let one swipe carry the helicopter further.

* **Setting**: "Touch speed:" in Options (touch mode: Android, desktop `--touch`), a 5-step
  spinner (Original, x1.25, x1.5, x1.75, x2), stored as `touchSpeed` like the other new keys,
  default step 1 ("a tad faster"), applied live. `applyTouchSpeed` scales the drag gain and
  the lead by the same factor (1 + 0.25 step); the dead zone and look-ahead stay.
* **The helicopter's speed is not changed.** The engine-behaviour.md 7.3 hook would be
  `p_speedfactor`, but it is owned by the scripts: `player.scr` sets it to 1.0 at spawn and the
  speed-up and speed-down items (`p_speedup.scr`, `p_speeddown.scr`) ramp it between 1.0 and
  1.4 and back. Scaling it from the engine would fight those scripts (or need a hidden
  multiplier on a script variable), so there is no clean hook and the game balance is left
  exactly as the original's: only the touch mapping changes, which is input, not simulation.
  Keyboard, bot and input-script play never see the setting (`game_flow_test.cpp` plays the
  bot and the keyboard under opposite Touch speed, Screen and Controls settings and compares
  the state dumps).

## Open

* The data-derived icon is dark (the Comanche's dark green-grey body) on a dark background; it is
  recognisable but less punchy than our own drawing.
* The icons on the buttons are the HUD's 66 x 35 pictures; the missile pictures are small at
  9 to 12 mm.
