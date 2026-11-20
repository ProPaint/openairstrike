# Changelog

The release notes take a version's section from here (tools/release_notes.sh); without one
they list the commits since the previous tag.

## 0.5.0-beta.1

A beta for two-player co-op and online play.

**Online is capricious outside a local network.** On the same Wi-Fi it plays smoothly. Over the
internet it depends on both players' connections: behind some mobile networks or carrier-grade
NATs the game cannot connect, and a connection that freezes now and then (as some phones' mobile
data does) freezes the game for both players, since each side waits for the other's inputs. If
it cannot connect, try the same Wi-Fi, or a virtual LAN such as Tailscale or ZeroTier.

- **Online co-op** for two players over the internet or a local network, in AirStrike 3D,
  AirStrike 2 and Gulf Thunder, on the desktop, Android and the web, in any mix: Start Game,
  Game mode **Online**, Host a game or Join a game. There is no server: the players send each
  other two codes through any chat and the game connects the machines directly (WebRTC, with
  public STUN servers to find the public address; `--stun none` or `?stun=none` leaves them
  out). Both machines run the same simulation and exchange only the inputs, and the game checks
  that both have the same game files and build; a lobby picks the mission, difficulty and
  helicopters, and the host's Next, Restart and Continue are followed by the guest. The web page
  shows the codes in a dialog with Copy, Share, Paste and a `?join=` link. A code contains your
  network addresses: send it only to the person you play with. See "Online co-op" in the README.
- **The simulation's trigonometry is now the engine's own** (a port of musl's, `as3d/sim_math.h`)
  instead of the C library's, and nothing in the engine fuses multiplications and additions any
  more (`-ffp-contract=off` everywhere): the desktop, Android, the web and the 3DS compute the
  same bits, which online co-op needs. Glibc's results differed from these in the last bit for
  some angles, so the desktop's end states changed once: some missions play out slightly
  differently from before (the long boss fights and some two-player runs end at other frames;
  the bot regression runs of the three games are unchanged).

- **Two players in AirStrike 2 and Gulf Thunder** on the desktop: choose "Cooperative" in Start
  Game, pick a helicopter for each player, and play the campaign together with the two-player
  HUD; the checkpoint, Restart and Next keep both players' lives and scores.
- **Two players on the web and Android**, in all three games: "2 Players" (the sequels'
  "Cooperative") in Start Game whenever a second player has something to fly with, the
  keyboard or a game controller. On a phone, touch is player 1 and a pad player 2.
- **One pad per player**: each game controller now flies its own player instead of every pad
  flying player 1. In AirStrike 3D a new Players screen before a two-player mission shows who
  flies with what; push a pad left or right (or tap it) to give it to the other player.
- **Two players on one keyboard on the web**: player 2 flies with W A S D and fires with V (F
  stays the page's full-screen key), G missile, H item. Older web and Android saves get player
  2's keys when they still had none of their own.

## 0.4.1

- Nintendo 3DS: closing the game from the HOME menu (Close, or starting another title) no longer
  hangs the console; the app left without the GPU waited forever for its last frames.

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
