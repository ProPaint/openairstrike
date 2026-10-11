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
# max_far_parallax_px=12  # 3D depth behind the screen at full slider (pixels); lower it (8, 6) if the 3D tires the eyes
# max_parallax_px=10      # 3D depth in front of the screen at full slider (pixels)
# perf=1             # write perf.csv (frame times) next to log.txt
# mode=soak          # the self-test: bot runs of every installed game, soak.txt, back to the launcher
# report_to=192.168.1.10:7777  # push log.txt and the results to tools/3ds_receive.py on that PC
EOF

# A file list for INSTALL.txt, from the game profiles (engine/src/game/game_profiles.cpp).
cat > "${OUT}/INSTALL.txt" <<EOF
OpenAirStrike for the Nintendo 3DS, version ${VERSION}
=====================================================

An open source engine for AirStrike 3D (v1.70), AirStrike 2 (v2.51) and AirStrike II: Gulf
Thunder (v2.71). It contains no game data: you need your own copy of at least one of the games.
Source, issues and the full documentation: the openairstrike repository (docs/3ds-port.md).

Needs a 3DS with custom firmware (Luma3DS) and the Homebrew Launcher, or FBI to install the CIA.
A New 3DS is recommended; an Old 3DS runs the Old 3DS profile (30 Hz, fewer effects).


1. The SD card
--------------

Copy the folder 3ds/ of this package to the root of the SD card, then add the game files:

  sdmc:/3ds/openairstrike/
      openairstrike.3dsx        the app (Homebrew Launcher)
      run.ini                   optional (see the comments inside)
      as3d/                     AirStrike 3D
          pak0.apk  pak1.apk  pak2.apk
          Settings.xml
          texts_v170.txt
          music/*.ogg           optional, recommended (see 4)
      as2/                      AirStrike 2 (optional)
          pak0.apk  pak1.apk  pak2.apk
          Settings.xml
          texts_as2.txt
          music/*.ogg
      gulf/                     AirStrike II: Gulf Thunder (optional)
          pak0.apk  pak1.apk  pak2.apk  pak4.apk
          Settings.xml
          texts_gulf.txt
          music/*.ogg
  sdmc:/3ds/dspfirm.cdc         the DSP firmware, for sound (see 5)

One game is enough. With two or more, a game selector shows first ("Change game" in the main
menu brings it back). The app writes its files into the same folder: log.txt, launcher.bin,
<game>/profile.bin (options, high scores, unlocked missions), cache/ (rebuilt when deleted).


2. Where the game files come from
---------------------------------

From your installed copy of each game (Windows; under Wine the same folders):

  pakN.apk, Settings.xml    the game's data/ folder (data/pak0.apk, ..., data/Settings.xml)

The texts file (the Information pages, the Game Complete text, the rank names) is read out of
the game's own executable (AirStrike3D.exe, "AirStrike3D II.exe", "AirStrike3D II - Gulf.exe")
on a computer, with the tool of the source repository:

  python3 tools/extract_exe_texts.py --game as3d --exe /path/to/AirStrike3D.exe --out texts_v170.txt
  python3 tools/extract_exe_texts.py --game as2  --exe "/path/to/AirStrike3D II.exe" --out texts_as2.txt
  python3 tools/extract_exe_texts.py --game gulf --exe "/path/to/AirStrike3D II - Gulf.exe" --out texts_gulf.txt

If you already imported a game into the Android app of OpenAirStrike, its games/<key>/ folder
holds the same files (the paks, Settings.xml and the texts file, made from the executable).

The app checks for these files at start: a game with a file missing shows "Missing: ..." in the
selector; with no game at all the screens show what is expected, then the app returns to the
launcher.


3. Starting it
--------------

Homebrew Launcher: OpenAirStrike is in the list with its icon. Or install openairstrike.cia
with FBI (copy it anywhere on the SD card, FBI > SD > the file > Install CIA): then it is a
HOME menu title (ID 000400000FA35D00) and still reads its data from sdmc:/3ds/openairstrike/.

Keys: menus D-pad / A / B or touch; in play the circle pad flies, A fire, B missile, X power-up,
Y next weapon, R next missile, START pause (the bottom screen's buttons work by touch too).
The high-score name opens the system keyboard (B or Cancel: the on-screen keys; Y opens the
keyboard again). START + SELECT quits (the profile is saved). HOME and closing the lid pause the
game and save the profile.


4. Music (optional, recommended)
--------------------------------

The games' music is in tracker modules; the 3DS plays pre-rendered Ogg files instead (the Old
3DS cannot decode the modules in time). Render them once on a computer from your own paks with
the source repository's audio_tool (built with the desktop version):

  build/apps/audio_tool/audio_tool render-music --all <out-dir>                 (AirStrike 3D)
  build/apps/audio_tool/audio_tool render-music --all <out-dir> --paks <as2 or gulf folder>

and copy <out-dir>/*.ogg to sdmc:/3ds/openairstrike/<game>/music/. Without them the modules are
decoded live: fine on a New 3DS, too slow for an Old 3DS.


5. Sound
--------

The 3DS's DSP needs its firmware dumped from your own console: run "DSP1" (homebrew) once, it
writes sdmc:/3ds/dspfirm.cdc. Without it the game runs silent (log.txt says so).
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
        -elf openairstrike.elf -icon openairstrike.smdh -banner banner.bnr'
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
