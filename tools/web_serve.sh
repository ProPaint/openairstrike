#!/usr/bin/env bash
# Serves the web version's site on this machine only (docs/web.md).
#
#   tools/web_serve.sh [PORT] [BIND]   default 8080 on 127.0.0.1, then open http://127.0.0.1:PORT/
#
# The directory is $AS3D_WEB_SITE, else $AS3D_DATA_ROOT/out/web/site, else out/web/site of
# this checkout (the bundled build of tools/web_build.sh; the byo build is in site-byo). The
# bundled site holds the original game data, so the default is this machine only.
# BIND (or $AS3D_WEB_BIND) 0.0.0.0 serves it to the local network, for trying it from a
# phone at home; never do that on a network you do not control, and never publish it.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SITE="${AS3D_WEB_SITE:-${AS3D_DATA_ROOT:-$ROOT}/out/web/site}"
PORT="${1:-8080}"
BIND="${2:-${AS3D_WEB_BIND:-127.0.0.1}}"
if [ ! -f "$SITE/index.html" ]; then
  echo "web_serve: no page in $SITE (run tools/web_build.sh first)" >&2
  exit 1
fi
if [ "$BIND" = "127.0.0.1" ]; then
  echo "web_serve: http://127.0.0.1:$PORT/  (Ctrl+C to stop)"
else
  echo "web_serve: serving the game data to the network on $BIND:$PORT  (Ctrl+C to stop)"
  for ip in $(hostname -I 2>/dev/null); do echo "web_serve:   http://$ip:$PORT/"; done
fi
exec python3 -m http.server "$PORT" --bind "$BIND" --directory "$SITE"
