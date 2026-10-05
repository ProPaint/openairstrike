#!/usr/bin/env bash
# Prints the release notes of a version: .github/release-body.md with __VERSION__ filled in
# and __CHANGES__ replaced by that version's section of CHANGELOG.md ("## <version>" up to the
# next "## "), or, when the changelog has no such section, by the commit subjects since the
# previous tag. Used by .github/workflows/release.yml and by hand (gh release edit --notes-file).
#
#   tools/release_notes.sh <version without v> [previous tag]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="${1:?version}"
PREV="${2:-$(git -C "$ROOT" describe --tags --abbrev=0 "v${VERSION}^" 2>/dev/null || true)}"
changes="$(awk -v v="## ${VERSION}" '$0 == v {on=1; next} /^## / {on=0} on' "$ROOT/CHANGELOG.md" 2>/dev/null | sed -e :a -e '/^\n*$/{$d;N;ba' -e '}')"
if [ -z "${changes// /}" ]; then
  if [ -n "$PREV" ]; then changes="$(git -C "$ROOT" log --format='- %s' --no-merges "${PREV}..HEAD")"
  else changes="$(git -C "$ROOT" log --format='- %s' --no-merges -20)"; fi
fi
python3 - "$ROOT/.github/release-body.md" "$VERSION" "$changes" <<'PY'
import sys
body = open(sys.argv[1]).read().replace("__VERSION__", sys.argv[2]).replace("__CHANGES__", sys.argv[3].strip() + "\n")
sys.stdout.write(body)
PY
