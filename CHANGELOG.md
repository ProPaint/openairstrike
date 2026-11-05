# Changelog

The release notes take a version's section from here (tools/release_notes.sh); without one
they list the commits since the previous tag.

## 0.4.0

- Nintendo 3DS port: the three games on a New 3DS (an Old 3DS runs a 30 Hz profile with fewer
  effects), as a Homebrew Launcher app and as a CIA for the HOME menu. Stereoscopic 3D with
  the slider, the game selector, the menus and the HUD on the touch screen with the game's
  own font at readable sizes, the comic pages of the sequels read at 1:1, music from the
  original modules or pre-rendered Ogg files, a self-test mode that plays every installed game
  (`docs/3ds-port.md`, `apps/3ds/README.md`). One player; the game files go on the SD card.
- Cheat codes: the original words typed on the keyboard during play, on the desktop and the
  web (`iwannabe`, `endisnear`, ... for AirStrike 3D; `invulnerability`,
  `igonnaliveforever`, ... for the sequels). A cheat marks the session (rank Cheater, no high
  score, no checkpoint in the sequels) and shows its message.
- Engine: a level-start precache loads every model, texture, script and sound a mission can use
  before it starts, so nothing stalls on first use; the simulation step costs a fraction of
  what it did in the sequels' big fights (hashed name lookups, an inlined script VM, per-entity
  passes without allocations); dynamic lights, decals and particles are culled earlier.
- Audio: the music is decoded outside the mixer's lock (no stalls on a module change), with an
  interpolation setting.
- Saves are written atomically (a temporary file, then a rename), so a crash or a power loss
  never leaves a truncated profile.
- Sequels' front end with a game pad: Left and Right browse the helicopters, the name entry's
  keyboard works from the D-pad.
- Web: the page can run inside a launcher page (`?autostart=1` with a `launcherSaves` hook):
  a loading dialog with the real progress, full screen on the first input, the profile handed
  to the host page, and a way back to it when the game ends. Standalone pages are unchanged.
- Tools: `as3d_sim --time` and `--step-times` (per-step costs), `tools/sim_bench.sh`, and the
  3DS build in CI (`.github/workflows/ci.yml`) with the package in the release workflow.

## 0.3.2

- Game controllers on the web and the desktop (Xbox / PlayStation style pads): fly, fire and
  go through the menus and the web start screen without the keyboard (docs/web.md
  "Controllers").
- Web: "Add another game's files" on the start page; before, a second game could only be
  added through `?game=`.

## 0.3.1

First release.

- The three games play to the end on Linux, Android and the web, with their own front ends:
  AirStrike 3D (20 missions), AirStrike 2 (18 missions) and Gulf Thunder (24 operations).
- Android: the public app imports your own game files on first start (loose files or the
  installer zip) and lives beside a personal build with bundled data.
- Web: the page asks for your game files and keeps them in the browser.
- Engine: a faulted script no longer freezes its entity, and a helicopter that never comes
  back after a death is respawned by the engine after 12 s; the FPS counter reports script
  errors.
- Release pipeline: signed APK, Linux tarball and web zip from a version tag.
