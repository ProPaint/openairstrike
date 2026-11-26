#!/usr/bin/env bash
# tools/3ds_package.sh: the installable 3DS package (docs/3ds-port.md "Packaging and launch
# (round 2, P6)"). Builds with tools/3ds_build.sh (AS3D_BUILD_JOBS, default 2), then writes
#
#   out/3ds/package/<version>/
#     3ds/openairstrike/openairstrike.3dsx   the Homebrew Launcher app (icon and titles in its SMDH)
#     3ds/openairstrike/run.ini              the defaults, every line a comment (the file is optional)
#     openairstrike.cia                      the HOME menu title 000400000FA35D00 (FBI, or Azahar's Install CIA)
#     INSTALL.txt                            the SD layout, where each game's files go, how to get them
#   out/3ds/package/openairstrike-3ds-<version>.zip
#
# No game data goes into the package (the script checks the file list). The CIA needs makerom
# and bannertool, which tools/fetch_third_party.sh ctrtools downloads (pinned SHA-256) into
# ${AS3D_DATA_ROOT:-repo}/third_party_local/; they run inside the pinned devkitPro image without
# network. Usage:
#   tools/3ds_package.sh [--no-build] [--no-cia]
#   AS3D_VERSION=0.3.0 tools/3ds_package.sh      (default: 0.3.0-dev; the commit is appended)
#   AS3D_TITLE_VERSION=N                         (the CIA's title version; default: the commit count)
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="$(sed -n 's/^IMAGE=\(.*\)$/\1/p' "${REPO_ROOT}/tools/3ds_build.sh" | head -1 | tr -d '"')"
BUILD=1
CIA=1
for a in "$@"; do
  case "$a" in
    --no-build) BUILD=0 ;;
    --no-cia) CIA=0 ;;
    *) echo "usage: $0 [--no-build] [--no-cia]" >&2; exit 2 ;;
  esac
done

COMMIT="$(git -C "${REPO_ROOT}" rev-parse --short HEAD)"
VERSION="${AS3D_VERSION:-0.3.0-dev}-${COMMIT}"
# The CIA's title version (makerom -ver, a 16-bit number in the TMD and the NCCH): the commit count,
# so every build installed over the HOME menu title carries a higher version than the one before
# (the installed title's version then says which build it is, and FBI never sees a downgrade).
TITLE_VER="${AS3D_TITLE_VERSION:-$(git -C "${REPO_ROOT}" rev-list --count HEAD)}"
OUT_ROOT="${REPO_ROOT}/out/3ds/package"
OUT="${OUT_ROOT}/${VERSION}"
BUILD_DIR="${REPO_ROOT}/build-3ds/apps/3ds"

if [ "${BUILD}" = 1 ]; then
  AS3D_BUILD_JOBS="${AS3D_BUILD_JOBS:-2}" "${REPO_ROOT}/tools/3ds_build.sh" all
fi
for f in openairstrike.3dsx openairstrike.elf openairstrike.smdh; do
  [ -f "${BUILD_DIR}/${f}" ] || { echo "3ds_package: ${BUILD_DIR}/${f} is missing (build first)" >&2; exit 1; }
done

rm -rf "${OUT}"
mkdir -p "${OUT}/3ds/openairstrike"
cp "${BUILD_DIR}/openairstrike.3dsx" "${OUT}/3ds/openairstrike/openairstrike.3dsx"

cat > "${OUT}/3ds/openairstrike/run.ini" <<'EOF'
# OpenAirStrike on the 3DS: run.ini is optional. Without it (or with every line a comment, as
# here) the game starts: the game selector when more than one game is on the card, the menus,
# sound and music on. Uncomment a line to change it; the full list of keys is in
# apps/3ds/README.md of the source.
#
# mode=play          # the game (default)
# game=as3d          # skip the selector: as3d, as2 or gulf
# audio=1            # 0: no sound
# music=1            # 0: no music
# quality=auto       # auto, new (New 3DS look), old (Old 3DS profile: 30 Hz, fewer effects)
# logo=1             # 0: no intro pages
# name_entry=applet  # touch: no system keyboard for the high-score name, the on-screen one
# max_far_parallax_px=8   # 3D depth behind the screen at full slider (pixels); lower it (6, 4) if the 3D tires the eyes
# max_parallax_px=6       # 3D depth in front of the screen at full slider (pixels)
# perf=1             # write perf.csv (frame times) next to log.txt
# mode=soak          # the self-test: bot runs of every installed game, soak.txt, back to the launcher
# report_to=192.168.1.10:7777  # push log.txt and the results to tools/3ds_receive.py on that PC
EOF

# A file list for INSTALL.txt, from the game profiles (engine/src/game/game_profiles.cpp).
cat > "${OUT}/INSTALL.txt" <<EOF
OpenAirStrike for the Nintendo 3DS, version ${VERSION}
=====================================================

Play AirStrike 3D, AirStrike 2 and AirStrike II: Gulf Thunder on your 3DS. This package holds
only the app, no game: you need your own copy of at least one of the games (the PC version, the
installer or the folder it installed). Project page: https://github.com/ProPaint/openairstrike


What you need
-------------

  - A 3DS (any model; a New 3DS runs it best) with custom firmware: Luma3DS, the Homebrew
    Launcher and FBI. If you do not have it yet, follow https://3ds.hacks.guide first.
  - Your copy of the game(s) on a computer.
  - A way to copy files to the 3DS's SD card (a card reader, or ftpd over Wi-Fi).


Step 1: make your game files (on the computer, in the browser)
--------------------------------------------------------------

  1. Open https://propaint.github.io/openairstrike/ in Chrome, Edge or Firefox.
     (If you do not see "Files for the 3DS" in step 3, use the beta page instead:
     https://propaint.github.io/openairstrike/beta/)
  2. Give the page your game: the game's folder, its installer or the archive you downloaded.
     Nothing is uploaded anywhere; it all stays in your browser.
  3. Click "Files for the 3DS", keep every box ticked, click "Make the zip" and wait (about
     half a minute per game), then "Download the zip".

You get openairstrike-3ds.zip with everything for the SD card: the game files, the texts and the
music, already in the right folders.


Step 2: copy everything to the SD card
--------------------------------------

  1. Put the 3DS's SD card in the computer.
  2. Unzip THIS package at the root of the SD card (the top, where the "3ds" folder already is).
  3. Unzip openairstrike-3ds.zip from step 1 at the root of the SD card too.
     If the computer asks whether to merge the folders, say yes.

The SD card now has a folder 3ds/openairstrike/ with openairstrike.3dsx and one folder per game
(as3d, as2, gulf).


Step 3: sound (once per console)
--------------------------------

The 3DS needs its sound firmware file, sdmc:/3ds/dspfirm.cdc. If you followed 3ds.hacks.guide
you probably have it already. If not: get the homebrew "DSP1", start it once from the Homebrew
Launcher, and it writes the file. Without it the game works but has no sound.


Step 4: play
------------

Put the SD card back in the 3DS and choose one:

  - Homebrew Launcher: start it and pick OpenAirStrike.
  - HOME menu icon: open FBI, go to SD, choose openairstrike.cia (step 2 put it at the root of
    the card) > Install CIA. OpenAirStrike is then on the HOME menu; the .cia file can be
    deleted afterwards.

With more than one game, a game selector shows first ("Change game" in the main menu brings it
back).

Controls: the menus use the D-pad, A and B, or touch. In the air: circle pad to fly, A fire,
B missile, X power-up, Y next weapon, R next missile, START pause; the bottom screen's buttons
work by touch too. START + SELECT quits. Your progress and high scores are saved by themselves.


If something is wrong
---------------------

  - A game says "Missing: ..." in the selector: a file of that game is not where it should be.
    Redo step 1 and step 2 for that game.
  - No sound: sdmc:/3ds/dspfirm.cdc is missing (step 3).
  - The app goes straight back to the launcher: no game was found. Check that the folders are
    sdmc:/3ds/openairstrike/as3d (or as2, gulf), not one folder deeper.
  - Anything else: the app writes sdmc:/3ds/openairstrike/log.txt. Attach it to a new issue on
    the project page.


For experts: the files by hand
------------------------------

  sdmc:/3ds/openairstrike/
      openairstrike.3dsx        the app (Homebrew Launcher)
      run.ini                   optional settings (see the comments inside)
      as3d/                     AirStrike 3D
          pak0.apk  pak1.apk  pak2.apk  Settings.xml
          texts_v170.txt        or AirStrike3D.exe
          music/*.ogg           optional, recommended
      as2/                      AirStrike 2
          pak0.apk  pak1.apk  pak2.apk  Settings.xml
          texts_as2.txt         or "AirStrike3D II.exe"
          music/*.ogg
      gulf/                     AirStrike II: Gulf Thunder
          pak0.apk  pak1.apk  pak2.apk  pak4.apk  Settings.xml
          texts_gulf.txt        or "AirStrike3D II - Gulf.exe"
          music/*.ogg
  sdmc:/3ds/dspfirm.cdc         the sound firmware

  - pakN.apk and Settings.xml: the data/ folder of the installed game.
  - The texts file (Information pages, ranks, Game Complete text): put the game's .exe in its
    folder instead and the app makes the texts file from it at the next start (log.txt:
    "install: <game>: ... made from ..."); the .exe can be deleted afterwards. Or with the
    source repository: python3 tools/extract_exe_texts.py --game as3d|as2|gulf --exe <exe>
    --out <texts file>. The Android app's games/<game>/ folder holds the same files.
  - The music: the 3DS plays pre-rendered Ogg files (the Old 3DS cannot play the original
    tracker modules in time; a New 3DS can, so the music is optional there). From the source
    repository: build/apps/audio_tool/audio_tool [--paks <game data folder>] render-music --all
    <out dir>, then copy the .ogg files to <game>/music/.
  - The app also writes its own files there: log.txt, launcher.bin, <game>/profile.bin (options,
    high scores, unlocked missions) and cache/ (rebuilt when deleted).
  - HOME menu title ID 000400000FA35D00 (title version ${TITLE_VER}).
EOF

if [ "${CIA}" = 1 ]; then
  TPL="${AS3D_DATA_ROOT:-${REPO_ROOT}}/third_party_local"
  MAKEROM="${TPL}/makerom-0.18.4/makerom"
  BANNERTOOL="${TPL}/bannertool-1.2.3/bannertool"
  if [ ! -x "${MAKEROM}" ] || [ ! -x "${BANNERTOOL}" ]; then
    "${REPO_ROOT}/tools/fetch_third_party.sh" ctrtools >&2
  fi
  WORK="$(mktemp -d "${REPO_ROOT}/out/3ds/package/.cia.XXXXXX")"
  trap 'rm -rf "${WORK}"' EXIT
  cp "${REPO_ROOT}/apps/3ds/banner.png" "${REPO_ROOT}/apps/3ds/openairstrike.rsf" "${BUILD_DIR}/openairstrike.elf" \
     "${BUILD_DIR}/openairstrike.smdh" "${WORK}/"
  # The banner's sound: 0.5 s of silence (no game audio), 16-bit mono at 22050 Hz.
  python3 -I -c '
import sys, wave
w = wave.open(sys.argv[1], "wb")
w.setnchannels(1); w.setsampwidth(2); w.setframerate(22050)
w.writeframes(b"\0\0" * 11025)
w.close()' "${WORK}/silence.wav"
  # The tools run in the pinned image, without network, on copies in a scratch directory.
  docker run --rm --network none --user "$(id -u):$(id -g)" -v "${WORK}:/work" -w /work \
      -v "${MAKEROM}:/tools/makerom:ro" -v "${BANNERTOOL}:/tools/bannertool:ro" "${IMAGE}" bash -c '
    set -e
    /tools/bannertool makebanner -i banner.png -a silence.wav -o banner.bnr
    /tools/makerom -f cia -o openairstrike.cia -target t -exefslogo -rsf openairstrike.rsf \
        -ver '"${TITLE_VER}"' -elf openairstrike.elf -icon openairstrike.smdh -banner banner.bnr'
  cp "${WORK}/openairstrike.cia" "${OUT}/openairstrike.cia"
fi

# Nothing of a game may be in the package: only the files this script writes.
unexpected="$(cd "${OUT}" && find . -type f | sort | grep -v -x -e './3ds/openairstrike/openairstrike.3dsx' \
  -e './3ds/openairstrike/run.ini' -e './INSTALL.txt' -e './openairstrike.cia' || true)"
if [ -n "${unexpected}" ]; then
  echo "3ds_package: unexpected files in the package:" >&2
  echo "${unexpected}" >&2
  exit 1
fi

ZIP="${OUT_ROOT}/openairstrike-3ds-${VERSION}.zip"
rm -f "${ZIP}"
(cd "${OUT}" && zip -q -r -X "${ZIP}" 3ds INSTALL.txt $( [ -f openairstrike.cia ] && echo openairstrike.cia ))
echo "3ds_package: ${OUT}"
(cd "${OUT}" && find . -type f -printf '  %p (%s bytes)\n' | sort)
echo "3ds_package: ${ZIP} ($(stat -c %s "${ZIP}") bytes)"
