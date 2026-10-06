# Changelog

The release notes take a version's section from here (tools/release_notes.sh); without one
they list the commits since the previous tag.

## 0.3.2

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
