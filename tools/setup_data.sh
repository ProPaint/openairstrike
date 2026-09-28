#!/usr/bin/env bash
# Extracts the AirStrike 3D v1.70 files from the user's own zip into the
# gitignored third_party_local/original/ directory. Nothing here is committed.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ZIP="${1:-$ROOT/binaries/AirStrike.zip}"
DEST="$ROOT/third_party_local/original"
[ -f "$ZIP" ] || { echo "setup_data: zip not found: $ZIP" >&2; exit 1; }
mkdir -p "$DEST"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
unzip -q -o "$ZIP" 'AirStrike/AirStrike_3D/*' -d "$TMP"
cp -r "$TMP/AirStrike/AirStrike_3D/." "$DEST/"
echo "setup_data: extracted to $DEST"
ls "$DEST" "$DEST/data"
