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
| [text-blocks.md](text-blocks.md) | 1.1 | verified |
| [tga.md](tga.md) | 1.0 | verified |
| [mdl.md](mdl.md) | 1.1 | verified, with corrections in render-pipeline.md section 11.3: front face is counter-clockwise, normals are renormalised |
| [hmap.md](hmap.md) | 1.0 | layout verified; engine behaviour from code |
| [obj.md](obj.md), [wpn.md](wpn.md), [ps.md](ps.md), [levels-txt.md](levels-txt.md) | 1.0 | verified from parsers; some sub-field names guessed |
| [engine-behaviour.md](engine-behaviour.md) | 0.1 | verified from code; collision projection is settled in render-pipeline.md; the on-screen test is corrected by issue 120; corrections to sections 4.3, 6.2 and 8.1 to 8.4 are listed in rcsl-builtins-semantics.md, which wins where they differ |
| [render-pipeline.md](render-pipeline.md) | 1.0 | verified from code; lists corrections to obj, ps, mdl and levels-txt specs in section 11.3 |
| [frontend.md](frontend.md) | 1.0 | menus, HUD, mission flow, save and settings, verified from code; wins over engine-behaviour.md section 11 and render-pipeline.md section 8.3 where they differ; answers issue 060 in its section 4.9 |
| [rcsl-vm.md](rcsl-vm.md) | 1.1 | verified from code; C++ VM (`engine/src/script`) reproduces the reference trace of all 339 scripts |
| [rcsl-builtins-table.md](rcsl-builtins-table.md) | 1.0 | signatures verified; behaviour is in rcsl-builtins-semantics.md, which wins where they differ |
| [rcsl-builtins-semantics.md](rcsl-builtins-semantics.md) | 1.0 | all 85 builtins, core behaviour verified from code; 62 are needed for mission 1; also lists corrections to engine-behaviour, rcsl-vm, hmap and wpn specs; checked by `tools/ref/check_builtin_semantics.py` |
| [rcsl-container.md](rcsl-container.md) | 0.2 | verified from code |
| [rcsl-opcodes-v0.md](rcsl-opcodes-v0.md) | 0.2 | opcodes verified from code |

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
| Main menu branding of the 2010 re-release | "GameTonic.com" in the copyright line and the portal's logo | removed on the owner's request (`removeRereleaseBranding`); the DivoGames intro page and copyright stay |
| Online high scores, CD check | present | dropped |

## Games and which spec applies

The engine runs several games of the same engine family. Each has a key used for data
directories, tests (`AS3D_GAME`), goldens (`testdata/golden/<key>/`) and saves.

Data on disk, all gitignored: the first game's install is `third_party_local/original/` and
its extracted files `assets_extracted/`; a sequel's install is `third_party_local/games/<key>/`
and its extracted files `assets_extracted_games/<key>/` (`tools/setup_data.sh <key>`). Ghidra
exports are `re/out/v170`, `re/out/as2`, `re/out/gulf` (`re/run_ghidra.sh --game <key>`).

| Key | Game | Executable | Specs |
|---|---|---|---|
| `as3d` | AirStrike 3D v1.70 | `AirStrike3D.exe` | the base specs in this directory |
| `as2` | AirStrike 2 v2.51 | `AirStrike3D II.exe` | base specs amended by `as2/*.delta.md` |
| `gulf` | AirStrike II: Gulf Thunder v2.71 | `AirStrike3D II - Gulf.exe` | base specs amended by `as2/*.delta.md`, then by `gulf/*.delta.md` |

Rules for delta specs:

- A delta file mirrors a base spec by name and section number: `as2/engine-behaviour.delta.md`
  section 6.3 amends section 6.3 of `engine-behaviour.md`.
- Precedence for a game: its own delta, then the delta of the game it builds on, then the
  base spec. For `gulf`: `gulf/` over `as2/` over base.
- Every delta ends with a table "Checked sections" listing each section of the base spec as
  `same`, `changed` (described in the delta) or `not checked`. A section that is not listed
  counts as not checked, and an implementer must not assume it is the same.
- `VERIFIED-CODE` in a delta cites the executable and the address, as `as2@0x41f2a0`.
  `INFERRED-SEQUEL` there means: taken from the third-party toolkit's decompilation and not
  confirmed in our own export.
- The base specs describe v1.70 and are not edited to describe a sequel.

## Spec issues

| Issue | Topic |
|---|---|
| [001](issues/001-lcall-nan-timeout.md) | NaN timeout in LCALL; resolved in rcsl-vm.md 1.1 |
| [010](issues/010-particle-prev-origin.md) | particle emitter previous origin and yaw of oriented emitters; check once emitters move in game |
| [030](issues/030-collision-segment-details.md) | collision segment end point; points behind the eye |
| [031](issues/031-entity-pass-details.md) | entity pass details: think bit, leaving test, dormant entities, two-player assignment |
| [032](issues/032-shared-return-register.md) | resolved: one return register shared by all script threads, as in the original |
| [033](issues/033-held-input-bits.md) | accepted: held input bits are re-applied every frame, for keys, mouse buttons and touch alike |
| [034](issues/034-entity-geometry-guesses.md) | bounding radius formula, ground-normal axis, missing tag on a definition child |
| [050](issues/050-game-integration-choices.md) | game integration choices: draw order, controls, audio |
| [060](issues/060-hud-gaps.md) | HUD facts the specs do not give: weapon icon table, two-player layout, stars, upgrades, boss bar, blend modes |
| [070](issues/070-render-extras-choices.md) | shadows, lights and environment map choices |
| [080](issues/080-frontend-texts-in-exe.md) | decided: import step from the user's exe, see deviations table |
| [090](issues/090-touch-mode-additions.md) | touch-mode additions to the menus |
| [091](issues/091-frontend-wide-screens-and-small-choices.md) | wide screens and small front end choices |
| [092](issues/092-information-page-layout.md) | Information page layout; paragraph icons are a guess |
| [100](issues/100-touch-controls.md) | touch control mapping and button placement |
| [110](issues/110-particle-damage.md) | particle damage: meaning of the three `damage` numbers is our reading; supersedes 050 sections 2 and 3 |
| [111](issues/111-health-bar-drawing.md) | how the two sprites split the health bar |
| [112](issues/112-shadow-key-and-music-jump.md) | shadow rotation key for spawned entities, game-over music jump |
| [113](issues/113-all-missions-findings.md) | findings of the all-missions run: missing files in the shipped data, boss part leaving in mission 20, pinned roots |
| [120](issues/120-player-fire-at-screen-edge.md) | the on-screen bit is set when the rectangle overlaps the window, not when it is contained; corrects engine-behaviour 3.3, 5.1, 7.3, render-pipeline 9.3 and `Shoot` step 1; settles 034 (radius, pivot) |
| [130](issues/130-frontend-game-integration.md) | front end integration choices |
| [140](issues/140-android-polish.md) | launcher icon, Wide / 4:3 setting, touch overlay, touch speed (drag gain only; the helicopter's own speed is untouched), FPS counter |
| [141](issues/141-next-item-preview.md) | next buttons preview the item they select, from the game's own cycling rule (`as3d/player_select.h`) |
| [150](issues/150-web-version.md) | web version: full screen, touch detection, storage, web key bindings, bundled and bring-your-own builds |
| [160](issues/160-save-format-v2.md) | save format 2: game key, counts as data, migration of the first game's save |
| [161](issues/161-game-detection.md) | how a game's data is found and identified |
| [162](issues/162-packaging-by-game.md) | packaging by game on Android and the web |
| [163](issues/163-game-selector.md) | the game selector, "Change game", switching games in one process |

Issues of a sequel are in its own directory, `as2/issues/` (numbers from 200).

## Implementation status

| Area | Where | State |
|---|---|---|
| Formats, VFS, definitions | `engine/src/{vfs,formats,game}` | `as3d`: all shipped files load |
| Script VM | `engine/src/script` | `as3d`: reproduces the reference trace of all 339 scripts |
| World, builtins, collision, level runtime | `engine/src/game`, `apps/sim_tool` | `as3d`: all 85 builtins implemented; `EndLevel` and `ShowTutorialHint` hand over to the front end; all 20 missions reach the end under the pilot in god mode (`docs/missions-status.md`) |
| Rendering | `engine/src/render`, `apps/viewer` | meshes, terrain, water, particles, shadows, ground marks, sprites, dynamic lights, environment maps, lightning, health bars, driven by the live world |
| 2D layer, font, HUD | `engine/src/ui` | follows frontend.md section 4 |
| Audio | `engine/src/audio`, `apps/game/audio_bridge.*` | mixer and music, driven by the world's sound queue |
| Game app | `apps/game`, `engine/src/input`, `engine/src/render/world_render*` | full flow on desktop: intro, menus, missions, mission complete, game over, high scores, options, two players, saved profile; no cheat codes; video options and 3D sound hidden |
| Menus, progression, save | `engine/src/ui`, `engine/src/game/profile*`, `apps/game/game_flow.*`, `tools/extract_exe_texts.py` | wired into the game on all targets |
| Android | `android`, `apps/game`, `engine/src/input/touch_mapper.cpp`, `docs/android.md` | runs on the owner's phone; touch overlay with item previews, Wide / 4:3, touch speed, FPS counter |
| Web | `apps/web`, `tools/web_build.sh`, `tools/web_serve.sh`, `docs/web.md` | the whole game in the browser, one player; verified in headless Chromium and by the owner on Android Chrome and Edge; Safari, iPhone and Firefox untested |
| Several games | `engine/include/as3d/{game_profile,game_data}.h`, `tools/games.json`, `tools/regress_as3d.sh` | groundwork done: game chosen at start (`--game`, `AS3D_GAME`, `?game=`), rules from `GameRules`, one save per game (format 2), tests, goldens and tools per game, packaging by game |
| AirStrike 2 (`as2`) | `docs/spec/as2/`, `docs/missions-status-as2.md` | playable on all three targets: all 18 missions end under the pilot, its own HUD, water and lava checked against the original under Wine (`tools/run_original.sh`); with its own front end: intro comic, menus, helicopter selection, dialogues, campaign checkpoint, compared screen by screen with the original (`docs/spec/as2/issues/300-frontend-implementation-findings.md`); co-op not done |
| Gulf Thunder (`gulf`) | `docs/spec/gulf/`, `docs/missions-status-gulf.md` | playable on all three targets: all 24 operations end under the pilot, its own loadout and HUD tables, texts; started from the plain front end (its own menus are not done); co-op not done (`docs/missions-status-gulf.md`) |
