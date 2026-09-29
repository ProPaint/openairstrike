#!/usr/bin/env bash
# Unpacks a game's directory from the user's own zip into gitignored locations and
# extracts its paks. Nothing here is committed.
#
# Usage: tools/setup_data.sh [as3d|as2|gulf] [zip]        (default game: as3d)
#
# as3d (AirStrike 3D v1.70), the first game, keeps its historic layout:
#   $ROOT/third_party_local/original/     game directory (exe, data/*.apk, ...)
#   (paks are extracted by hand into $ROOT/assets_extracted/, see README.md; this script
#    does not touch that tree)
# as2, gulf (the sequels):
#   $ROOT/third_party_local/games/<key>/  the whole game directory from the zip
#   $ROOT/assets_extracted_games/<key>/   paks extracted in mount order (later paks override)
#
# Why assets_extracted_games/ and not assets_extracted/<key>/: tools/ref/test_tga.py walks
# assets_extracted/ recursively and would pick up the sequels' textures (rcsl tools may
# do the same on scripts), so the sequels' extracted trees live beside it, not inside it.
#
# ROOT is $AS3D_DATA_ROOT if set, else the repo root. Game keys, zip members and pak lists
# come from tools/games.json.
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="${AS3D_DATA_ROOT:-$HERE}"
KEY="${1:-as3d}"
ZIP="${2:-$ROOT/binaries/AirStrike.zip}"
[ -f "$ZIP" ] || { echo "setup_data: zip not found: $ZIP" >&2; exit 1; }

# key -> "zip_member|pak1 pak2 ..." from tools/games.json
INFO="$(python3 - "$HERE/tools/games.json" "$KEY" <<'PY'
import json, sys
for g in json.load(open(sys.argv[1]))["games"]:
    if g["key"] == sys.argv[2]:
        print(g["zip_member"] + "|" + " ".join(g["paks"]))
        sys.exit(0)
sys.exit("setup_data: unknown game key '%s' (see tools/games.json)" % sys.argv[2])
PY
)"
MEMBER="${INFO%%|*}"
PAKS="${INFO#*|}"

if [ "$KEY" = as3d ]; then
    DEST="$ROOT/third_party_local/original"
    EXTRACT=""
else
    DEST="$ROOT/third_party_local/games/$KEY"
    EXTRACT="$ROOT/assets_extracted_games/$KEY"
fi

mkdir -p "$DEST"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
unzip -q -o "$ZIP" "$MEMBER/*" -d "$TMP"
cp -r "$TMP/$MEMBER/." "$DEST/"
echo "setup_data: $KEY: game directory in $DEST"
ls "$DEST" "$DEST/data"

if [ -n "$EXTRACT" ]; then
    PAKARGS=()
    for p in $PAKS; do PAKARGS+=("$DEST/data/$p"); done
    rm -rf "$EXTRACT"; mkdir -p "$EXTRACT"
    ( ulimit -v 6000000; timeout 600 python3 "$HERE/tools/paktool.py" extract "$EXTRACT" "${PAKARGS[@]}" )
    echo "setup_data: $KEY: extracted to $EXTRACT (paks in order: $PAKS)"
    echo "  files per pak (before overrides):"
    for p in $PAKS; do
        n="$(python3 "$HERE/tools/paktool.py" list "$DEST/data/$p" | wc -l)"
        echo "    $p: $n list lines"
    done
    echo "  total files after overrides: $(find "$EXTRACT" -type f | wc -l)"
fi
