# 100: touch controls and the Android app: choices where the specs are silent

Status: open, choices made. Raised by WP-48 (the game on Android). Affects
`engine/src/input/touch_mapper.cpp`, `apps/game/game_loop.cpp`, `apps/game/touch_overlay.cpp`,
`apps/game/android_main.cpp`, `android/`.

The original has no touch input. `frontend.md` 7 lists what a touch-only landscape device lacks;
`engine-behaviour.md` 7.2 and 7.3 give the input bits and the movement model. The player script
owns speed and acceleration: whatever the controls do, they can only hold the four direction
bits, as the keyboard does.

## 1. Relative drag steering (a target point, steered with the direction bits)

* A finger that goes down in the play area (not on a button) starts a drag. The mapper sets a
  **target point** at the helicopter's current screen position: the helicopter never jumps to
  the finger.
* Moving the finger moves the target by the finger's displacement times a gain of 1.5, in the
  virtual 800x600 screen (the 4:3 play-field).
* Every simulation frame the mapper holds the direction bits that steer the helicopter toward
  the target, like the original's mouse control (`engine-behaviour.md` 7.3: bits forced by the
  sign of pointer minus helicopter screen centre, dead zone 20 px). Two differences:
  * the dead zone is 8 px, so the helicopter ends close to where the finger says;
  * the error is taken against the helicopter's position 10 frames ahead at its current screen
    velocity (a proportional-derivative rule with a bang-bang output), so it brakes before the
    target instead of overshooting and oscillating. With the real mission 1 helicopter a
    90-pixel drag ends within 7 px of the target with no reversal (`touch_test.cpp`).
* The helicopter's screen centre is the centre of its collision rectangle of the last step
  (window coordinates of the fixed 800x600 viewport, y flipped), the same point the original's
  mouse control uses.
* The target never gets more than 160 px ahead of the helicopter (screen edges, the script's y
  limits, the fly-in), so reversing the finger reverses the helicopter at once.
* A finger lifted releases all direction bits (the velocity then decays as with released keys).
  A new finger starts a new drag from the helicopter's position. While there is no visible
  player helicopter (respawn) nothing steers, and the drag restarts from where it appears.

Why not the original mouse control on the finger position: the finger would hide the
helicopter, and "fly toward the finger" makes the helicopter jump to wherever the screen is
touched. Why not a virtual joystick: the helicopter's speed is fixed by the script, so a stick
only gives direction, and precise positioning (dodging bullets) is easier by dragging.

## 2. Fire, buttons, pause

* **Auto-fire**: the primary weapon fires while any finger is down, except a finger on the
  pause button. A tap shorter than one frame still fires once (latched, as the key mapper).
* Buttons, drawn with the 2D layer: **missile** and **power-up** (held bits 0x002 and 0x004),
  **next missile**, **next weapon**, **next power-up** (the one-shot bits 0x100, 0x400, 0x200,
  held while the finger is down; the world acts on the press only, issue 033), **pause**.
  A finger that goes down on a button keeps it until lifted, even if it slides off, and never
  steers. Multi-touch: up to 10 fingers; the first finger down in the play area steers, the
  others only fire.
* Any new finger also sends the tutorial hint's OK (`PlayerInput::confirm`) for one frame.
* Pause: the pause button toggles the pause; while the game is paused by the player, the app or
  the back key, a tap anywhere continues (`GameSession` then clears `p_action`, 1.3 state 6) and
  that finger does nothing else until lifted.

## 3. Layout

* Wider than 4:3 with room (side bar at least 0.10 x screen height plus a margin, which holds
  from about 16:10 up): the buttons go **outside** the 4:3 play-field. The action column is in
  the right bar (bottom up: missile, power-up, next missile, next weapon, next power-up; the two
  big buttons 0.15 x screen height), pause at the top of the left bar. Opacity 0.85.
* Otherwise (4:3, 5:4, narrower) the buttons are **inside** the field at opacity 0.4: the
  action column at the bottom right, below the power-up icons of the HUD (virtual y > 200), and
  pause at the top centre between the health and score bars.
* The world renderer draws the 3D view across the whole screen (only the HUD and the collision
  space are 4:3), so on wide screens the "outside" buttons lie over the extra world view, not
  over black bars. The helicopter cannot fly there (its x is clamped to the 4:3 frustum).
* Display cutouts: the activity lays out under short-edge cutouts; the Java side reports the
  cutout's safe insets and the layout keeps every button out of them (the play-field itself is
  not moved).
* Touch areas are the drawn buttons plus 0.4 x the gap between buttons on each side.

## 4. App lifecycle

* Going to the background pauses the world (if nothing else holds the pause), releases every
  finger and key, and pauses the audio (SDL also pauses the device). Coming back, the game stays
  paused under the pause overlay until a tap; the time spent away is not caught up.
* Back pauses the game (SDL's back button is trapped); it never quits in the middle of a
  mission. Home or the task switcher leave the app.
* If SDL has to create a new EGL context (`SDL_RENDER_DEVICE_RESET`), the renderer, HUD and
  overlay are destroyed and rebuilt from the VFS (a level's particles restart). The smoke test
  forces the same rebuild on every resume (`rebuild_on_resume` extra) to exercise that path.
* Landscape only (`sensorLandscape`, both ways round), immersive full screen, screen kept on.

## 5. Timing

A fixed 60 Hz step as on desktop; each displayed frame runs at most 5 steps and drops the rest
of a longer stall (counted in `AS3D_PERF ... dropped_steps`), so a slow frame never snowballs.
The display runs at the device's vsync; a frame is drawn only after a step (no interpolation).

## Open

* The player's flamethrower and other particle damage still do nothing (issue 050, section 2).
* Model textures load on first draw (only terrain, water and the map objects' shadow silhouettes
  are built at level load); on a slow device a new enemy type can cause a short hitch. A
  `WorldRenderer` pre-cache of every definition the level can spawn would fix it
  (`engine/src/render`, not changed here).
* Two players on one touch screen are not supported.
