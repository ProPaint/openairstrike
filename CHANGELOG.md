# Changelog

The release notes take a version's section from here (tools/release_notes.sh); without one
they list the commits since the previous tag.

## 0.4.0

- **Nintendo 3DS port**: the three games on a New 3DS, as a Homebrew Launcher app or a CIA for
  the HOME menu, with stereoscopic 3D and the menus and HUD on the touch screen. An Old 3DS
  runs at 30 Hz with fewer effects. One player (`apps/3ds/README.md`).
- **Game files from any copy**: give the archive or installer as you downloaded it (zip, RAR,
  7z, CAB, Inno Setup, self-extracting executables) on the web, Android or `as3d_game
  --import`. Any build of the games is recognised by its contents, including copies without
  `pak0.apk`. NSIS and Gentee installers are refused with a note: install them on a PC first.
- **Cheat codes**: the original words typed during play on the desktop and the web
  (`iwannabe`, `invulnerability`, ...). A cheat gives the rank Cheater and no high score.
- **Web**: AirStrike 3D's flaming banner on the game selector when you bring your own files;
  "Add another game's files" takes any game's files; the page can be embedded in a launcher.
- **Performance**: missions preload everything they use, and the sequels' big fights cost a
  fraction of what they did.
- Saves are written atomically; the sequels' helicopter and name screens work with a game pad.

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
