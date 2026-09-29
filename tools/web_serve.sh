#!/usr/bin/env bash
# Serves the web spike's page on this machine only (docs/web-spike.md).
#
#   tools/web_serve.sh [PORT]     default 8080, then open http://127.0.0.1:PORT/
#
# The directory is $AS3D_WEB_SITE, else $AS3D_DATA_ROOT/out/web/site, else out/web/site of
# this checkout. It holds the original game data: it is bound to 127.0.0.1 and must never be
# exposed to a network.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SITE="${AS3D_WEB_SITE:-${AS3D_DATA_ROOT:-$ROOT}/out/web/site}"
PORT="${1:-8080}"
if [ ! -f "$SITE/index.html" ]; then
  echo "web_serve: no page in $SITE (run tools/web_build.sh first)" >&2
  exit 1
fi
echo "web_serve: http://127.0.0.1:$PORT/  (Ctrl+C to stop)"
exec python3 -m http.server "$PORT" --bind 127.0.0.1 --directory "$SITE"
