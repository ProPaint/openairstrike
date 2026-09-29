# Specifications

These documents are the contract between reverse engineering and implementation.
Implementation work reads specs and `tools/ref/` only, never decompiler output.
If a spec is wrong or silent, add `docs/spec/issues/NNN-title.md` instead of guessing.

## Confidence tags

Every claim carries one of:

| Tag | Meaning |
|---|---|
| `VERIFIED-DATA` | proven by a script across all shipped files |
| `VERIFIED-CODE` | read from the v1.70 executable, address cited |
| `INFERRED-SEQUEL` | seen only in the sequel's decompilation |
| `GUESS` | plausible, unproven |

## Rules

- Specs describe formats and behaviour in our own words. No decompiled code, no asset content
  beyond short structural excerpts.
- Every binary format spec ships a Python reference parser in `tools/ref/` that accounts for
  every byte of every shipped file.
- Each spec has a version line and a changelog at the bottom.

## Index

| Spec | Version | Status |
|---|---|---|
| [pak.md](pak.md) | 1.0 | verified |
| [text-blocks.md](text-blocks.md) | 1.0 | verified |
| [tga.md](tga.md) | 1.0 | verified |
| [mdl.md](mdl.md) | 1.1 | verified, with corrections in render-pipeline.md section 11.3: front face is counter-clockwise, normals are renormalised |
| [hmap.md](hmap.md) | 1.0 | layout verified; engine behaviour from code |
| [obj.md](obj.md), [wpn.md](wpn.md), [ps.md](ps.md), [levels-txt.md](levels-txt.md) | 1.0 | verified from parsers; some sub-field names guessed |
| [engine-behaviour.md](engine-behaviour.md) | 1.0 | verified from code; collision projection is settled in render-pipeline.md; corrections to sections 4.3, 6.2 and 8.1 to 8.4 are listed in rcsl-builtins-semantics.md, which wins where they differ |
| [render-pipeline.md](render-pipeline.md) | 1.0 | verified from code; lists corrections to obj, ps, mdl and levels-txt specs in section 11.3 |
| [frontend.md](frontend.md) | 1.0 | menus, HUD, mission flow, save and settings, verified from code; wins over engine-behaviour.md section 11 and render-pipeline.md section 8.3 where they differ; answers issue 060 in its section 4.9 |
| [rcsl-vm.md](rcsl-vm.md) | 1.1 | verified from code; C++ VM (`engine/src/script`) reproduces the reference trace of all 339 scripts |
| [rcsl-builtins-table.md](rcsl-builtins-table.md) | 1.0 | signatures verified; behaviour is in rcsl-builtins-semantics.md, which wins where they differ |
| [rcsl-builtins-semantics.md](rcsl-builtins-semantics.md) | 1.0 | all 85 builtins, core behaviour verified from code; 62 are needed for mission 1; also lists corrections to engine-behaviour, rcsl-vm, hmap and wpn specs; checked by `tools/ref/check_builtin_semantics.py` |
| [rcsl-container.md](rcsl-container.md) | 1.0 | verified from code |
| [rcsl-opcodes-v0.md](rcsl-opcodes-v0.md) | 0 | opcodes verified from code |

## Engine decisions where we deliberately differ from the original

| Topic | Original | Our engine |
|---|---|---|
| Random numbers | MSVC `rand()` | xorshift32, seed 1 (`as3d::Rng`) |
| Terrain vertices facing away from the sun | black (ambient not scaled by 255) | lit with the ambient colour |
| `TerrainHeight` outside the map | reads one cell past the edge | clamps to the edge |
| Frame timing | variable step, capped at 100 ms | fixed step, for determinism |
| Collision rectangles | window pixels of the actual video mode, previous frame's matrices | same formulas on a fixed 800x600 viewport; the player's x clamp uses that same 4:3 view, whatever the window's aspect |
| Player 2 default keys | same keys as player 1 | W/A/S/D, F/G/H, E/Q/R on a fresh profile (issue 130) |
| Entity reference of 0 or to a freed entity | crashes, or acts on whatever now occupies the slot | references carry a generation count; a builtin given a null or stale reference does nothing and returns 0 |
| Uninitialised values (`MoveToNextWP` bank on straight stretches, `Shoot` muzzle offset without a model) | whatever is on the stack | interpolated table value; zero offset |
| Texts compiled into the executable (Information pages, congratulations, rank names, labels) | in the exe | a data setup tool reads them from the user's own exe into a gitignored text file that ships with the game data; short generic labels have built-in English defaults so the game runs without it (issue 080) |
| Right-click on the tutorial hint box | closes the box, game stays paused | closes the box and resumes |
| Wide screens | everything stretched | 4:3 play-field and 2D layer centred, bars at the sides |
| Online high scores, CD check | present | dropped |

## Open spec issues

| Issue | Topic |
|---|---|
| [001](issues/001-lcall-nan-timeout.md) | NaN timeout in LCALL; resolved in rcsl-vm.md 1.1 |
| [010](issues/010-particle-prev-origin.md) | particle emitter previous origin and yaw of oriented emitters; check once emitters move in game |
| [080](issues/080-frontend-texts-in-exe.md) | decided: import step from the user's exe, see deviations table |
| [030](issues/030-collision-segment-details.md) | collision segment end point; points behind the eye |
| [031](issues/031-entity-pass-details.md) | entity pass details: think bit, leaving test, dormant entities, two-player assignment |
| [032](issues/032-shared-return-register.md) | resolved: one return register shared by all script threads, as in the original |
| [033](issues/033-held-input-bits.md) | accepted: held input bits are re-applied every frame, for keys, mouse buttons and touch alike |
| [034](issues/034-entity-geometry-guesses.md) | bounding radius formula, ground-normal axis, missing tag on a definition child |
| [060](issues/060-hud-gaps.md) | HUD facts the specs do not give: weapon icon table, two-player layout, stars, upgrades, boss bar, blend modes |

## Implementation status

| Area | Where | State |
|---|---|---|
| Formats, VFS, definitions | `engine/src/{vfs,formats,game}` | all shipped files load |
| Script VM | `engine/src/script` | reproduces the reference trace of all 339 scripts |
| World, builtins, collision, level runtime | `engine/src/game`, `apps/sim_tool` | all 85 builtins bound; 3 approximate (`Lightning`, `EndLevel`, `ShowTutorialHint`) pending render and UI; headless bot finishes mission 1 |
| Rendering | `engine/src/render`, `apps/viewer` | meshes, terrain, water, particles, shadows, ground marks, sprites, dynamic lights, environment maps; choices in issue 070; not yet driven by the live world |
| 2D layer, font, HUD | `engine/src/ui` | follows frontend.md section 4 |
| Audio | `engine/src/audio` | mixer and music decoder; not yet driven by the world's sound queue |
| Game app | `apps/game`, `engine/src/input`, `engine/src/render/world_render*` | full flow on desktop: intro, menus, all 20 missions, mission complete, game over, high scores, options, two players, saved profile; all missions reach the end under the pilot in god mode (`docs/missions-status.md`); no cheat codes; video options and 3D sound hidden |
| Menus, progression, save | `engine/src/ui`, `engine/src/game/profile*`, `apps/game/game_flow.*`, `tools/extract_exe_texts.py` | wired into the game on both targets |
| Android gameplay | `android`, `apps/game`, `engine/src/input/touch_mapper.cpp`, `docs/android.md` | menus and play with touch controls pass the emulator smoke test; never run on a real phone; choices in issues 100 and 130 |
| [050](issues/050-game-integration-choices.md) | game integration choices: draw order, controls, audio |
| [070](issues/070-render-extras-choices.md) | shadows, lights and environment map choices |
| [090](issues/090-touch-mode-additions.md) | touch-mode additions to the menus |
| [091](issues/091-frontend-wide-screens-and-small-choices.md) | wide screens and small front end choices |
| [092](issues/092-information-page-layout.md) | Information page layout; paragraph icons are a guess |
| [120](issues/120-player-fire-at-screen-edge.md) | the on-screen bit is set when the rectangle overlaps the window, not when it is contained; corrects engine-behaviour 3.3, 5.1, 7.3, render-pipeline 9.3 and `Shoot` step 1; settles 034 (radius, pivot) |
| [100](issues/100-touch-controls.md) | touch control mapping and button placement |
| [110](issues/110-particle-damage.md) | particle damage: meaning of the three `damage` numbers is our reading; supersedes 050 sections 2 and 3 |
| [111](issues/111-health-bar-drawing.md) | how the two sprites split the health bar |
| [112](issues/112-shadow-key-and-music-jump.md) | shadow rotation key for spawned entities, game-over music jump |
| [113](issues/113-all-missions-findings.md) | findings of the all-missions run: missing files in the shipped data, boss part leaving in mission 20, pinned roots |
| [130](issues/130-frontend-game-integration.md) | front end integration choices |
| [140](issues/140-android-polish.md) | launcher icon, Wide / 4:3 setting, touch overlay, touch speed (drag gain only; the helicopter's own speed is untouched), FPS counter |
| [141](issues/141-next-item-preview.md) | next buttons preview the item they select, from the game's own cycling rule (`as3d/player_select.h`) |
