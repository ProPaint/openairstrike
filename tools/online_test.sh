#!/usr/bin/env bash
# Online co-op integration checks that need no browser and no second machine
# (docs/spec/issues/180-online-coop.md section 14, items 1 to 3). Prints "online: OK" and exits 0
# when everything passed, else says what failed and exits 1.
#
#   tools/online_test.sh [--games as3d,as2,gulf] [--frames N] [--long] [--web URL] [--quick]
#
#   (default)  item 1 for AirStrike 3D: two as3d_game processes over the real rtc transport
#              (--stun none, loopback ICE through the machine's own address), host and guest in
#              their own processes started a second apart, the bot flying and the lobby playing
#              itself; both must exit 0, the two --dump-state files must be identical, no
#              AS3D_DESYNC, the AS3D_NET start lines agree on token, seed and delay, the stalls
#              total under a second. Then item 2: the same with --net-fault-frame 900 on the guest:
#              both sides must report AS3D_DESYNC within 40 frames of it, write a desync dump and
#              exit 0 (a clean end).
#   --games    the games of item 1 (as3d, as2, gulf; the other items run once).
#   --frames   frames of item 1 (default 6000, about 100 s per game).
#   --long     adds the 3-level run of item 1: --frames 40000 --god (about 11 minutes): the host's
#              automatic Next is followed by the guest, AS3D_LEVEL_LOADED counts equal, dumps equal.
#   --web URL  adds item 3: the wasm determinism checks against a served web build (tools/web_build.sh
#              then tools/web_serve.sh): ?simmath=1 must log AS3D_SIMMATH hash=c38a83fb2d80117b (the
#              desktop's, as3d_tests -tc="sim_math hash*" says the same), and a bot run of 3600
#              frames with ?dumpstate=1 must equal as3d_sim's dump. Needs Playwright
#              (PYTHONPATH=~/tools/web-pw) and the game data (AS3D_DATA_ROOT).
#   --quick    1200 frames: a dry run of the script itself.
#
# Env: AS3D_GAME_BIN (default build/apps/game/as3d_game), AS3D_SIM_BIN (build/apps/sim_tool/as3d_sim),
# AS3D_TESTS_BIN (build/apps/as3d_tests), AS3D_DATA_ROOT. Every process runs under ulimit -v 4000000.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GAME_BIN="${AS3D_GAME_BIN:-$ROOT/build/apps/game/as3d_game}"
SIM_BIN="${AS3D_SIM_BIN:-$ROOT/build/apps/sim_tool/as3d_sim}"
TESTS_BIN="${AS3D_TESTS_BIN:-$ROOT/build/apps/as3d_tests}"
GAMES=as3d
FRAMES=6000
LONG=0
WEB="${AS3D_ONLINE_WEB_URL:-}"
while [ $# -gt 0 ]; do
  case "$1" in
    --games) GAMES="$2"; shift 2 ;;
    --frames) FRAMES="$2"; shift 2 ;;
    --long) LONG=1; shift ;;
    --web) WEB="$2"; shift 2 ;;
    --quick) FRAMES=1200; shift ;;
    -h|--help) sed -n '2,32p' "$0"; exit 0 ;;
    *) echo "online_test: unknown argument $1" >&2; exit 2 ;;
  esac
done
[ -x "$GAME_BIN" ] || { echo "online_test: no $GAME_BIN (build the desktop game, or set AS3D_GAME_BIN)" >&2; exit 2; }
T="$(mktemp -d "${TMPDIR:-/tmp}/as3d_online_test.XXXXXX")"
FAILS=0
PIDS=()
cleanup() { for p in "${PIDS[@]:-}"; do [ -n "$p" ] && kill "$p" 2>/dev/null; done; }
trap cleanup EXIT
fail() { echo "online: FAIL: $*" >&2; FAILS=$((FAILS + 1)); }
note() { echo "online: $*"; }

# run_pair NAME HOST_ARGS... -- GUEST_ARGS...: starts both, waits, leaves $T/NAME/{host,guest}.{log,json}
# and the exit codes in $T/NAME/{host,guest}.rc.
run_pair() {
  local name="$1"; shift
  local hargs=() gargs=() cur=h
  for a in "$@"; do
    if [ "$a" = "--" ]; then cur=g; continue; fi
    if [ $cur = h ]; then hargs+=("$a"); else gargs+=("$a"); fi
  done
  local d="$T/$name"
  mkdir -p "$d"
  local common=(--headless --bot --stun none --markers --net-hash-full --level 1 --seed 7)
  (
    ulimit -v 4000000
    exec timeout 900 "$GAME_BIN" --online host "${common[@]}" "${hargs[@]}" --code-out "$d/invite" --reply-in "$d/reply" \
      --dump-state "$d/host.json" >"$d/host.log" 2>&1
  ) &
  local hp=$!
  PIDS+=("$hp")
  local i
  for i in $(seq 1 300); do [ -s "$d/invite" ] && break; sleep 0.1; done
  [ -s "$d/invite" ] || { fail "$name: the host wrote no invite code"; kill "$hp" 2>/dev/null; return 1; }
  sleep 1
  (
    ulimit -v 4000000
    exec timeout 900 "$GAME_BIN" --online join "${common[@]}" "${gargs[@]}" --invite-in "$d/invite" --code-out "$d/reply" \
      --dump-state "$d/guest.json" >"$d/guest.log" 2>&1
  ) &
  local gp=$!
  PIDS+=("$gp")
  wait "$hp"; echo $? >"$d/host.rc"
  wait "$gp"; echo $? >"$d/guest.rc"
  return 0
}

stall_total() { grep -o 'AS3D_NET stall ms=[0-9]*' "$1" | sed 's/.*=//' | awk '{s += $1} END {print s + 0}'; }

check_clean_run() { # NAME: exit codes, no desync, equal dumps, equal start lines, short stalls
  local name="$1" d="$T/$1"
  [ "$(cat "$d/host.rc" 2>/dev/null)" = 0 ] || fail "$name: the host exited $(cat "$d/host.rc" 2>/dev/null); see $d/host.log"
  [ "$(cat "$d/guest.rc" 2>/dev/null)" = 0 ] || fail "$name: the guest exited $(cat "$d/guest.rc" 2>/dev/null); see $d/guest.log"
  if grep -q AS3D_DESYNC "$d/host.log" "$d/guest.log"; then fail "$name: AS3D_DESYNC in the logs"; fi
  if [ -s "$d/host.json" ] && [ -s "$d/guest.json" ]; then
    cmp -s "$d/host.json" "$d/guest.json" || fail "$name: the dumps differ (cmp $d/host.json $d/guest.json)"
  else
    fail "$name: a dump is missing"
  fi
  local hs gs
  hs="$(grep -o 'AS3D_NET start token=[0-9]* seed=[0-9]* delay=[0-9]*' "$d/host.log" | head -1)"
  gs="$(grep -o 'AS3D_NET start token=[0-9]* seed=[0-9]* delay=[0-9]*' "$d/guest.log" | head -1)"
  [ -n "$hs" ] && [ "$hs" = "$gs" ] || fail "$name: the start lines differ: '$hs' / '$gs'"
  local st
  st="$(( $(stall_total "$d/host.log") + $(stall_total "$d/guest.log") ))"
  [ "$st" -lt 1000 ] || fail "$name: stalls total $st ms (limit 1000)"
  note "$name: exits and dumps checked ($hs, stalls $st ms)"
}

# ---- item 1: a clean run per game ---------------------------------------------------------------
for g in ${GAMES//,/ }; do
  gargs=()
  [ "$g" = as3d ] || gargs=(--game "$g")
  note "item 1: $g, $FRAMES frames"
  if run_pair "clean_$g" "${gargs[@]}" --frames "$FRAMES" -- "${gargs[@]}" --frames "$FRAMES"; then check_clean_run "clean_$g"; fi
done

# ---- item 2: the forced desync ----------------------------------------------------------------
note "item 2: a bit flipped on the guest at frame 900"
if run_pair desync --frames "$FRAMES" -- --frames "$FRAMES" --net-fault-frame 900; then
  d="$T/desync"
  before=$FAILS
  for side in host guest; do
    [ "$(cat "$d/$side.rc" 2>/dev/null)" = 0 ] || fail "desync: the $side exited $(cat "$d/$side.rc" 2>/dev/null) (a desync is a clean end)"
    line="$(grep -m1 AS3D_DESYNC "$d/$side.log")"
    if [ -z "$line" ]; then
      fail "desync: no AS3D_DESYNC on the $side"
    else
      fr="$(echo "$line" | sed -n 's/.* frame=\([0-9]*\).*/\1/p')"
      { [ -n "$fr" ] && [ "$fr" -ge 900 ] && [ "$fr" -le 940 ]; } || fail "desync: the $side saw it at frame '$fr' (expected 900 to 940)"
    fi
    dump="$(sed -n 's/.*AS3D_NET desync dump \(.*\)$/\1/p' "$d/$side.log" | head -1)"
    if [ -n "$dump" ] && [ -s "$dump" ]; then
      rm -f "$dump"   # written into the game's data directory
    else
      fail "desync: the $side wrote no desync dump ($dump)"
    fi
  done
  [ "$FAILS" = "$before" ] && note "desync: both sides caught it and ended cleanly"
fi

# ---- the long run: three levels --------------------------------------------------------------
if [ "$LONG" = 1 ]; then
  note "item 1 (long): 40000 frames, god mode, Mission Complete and the host's Next twice"
  if run_pair long --frames 40000 --god -- --frames 40000; then
    check_clean_run long
    hl="$(grep -c AS3D_LEVEL_LOADED "$T/long/host.log")"; gl="$(grep -c AS3D_LEVEL_LOADED "$T/long/guest.log")"
    { [ "$hl" = "$gl" ] && [ "$hl" -ge 2 ]; } || fail "long: AS3D_LEVEL_LOADED host $hl guest $gl (equal and at least 2 expected)"
    note "long: AS3D_LEVEL_LOADED $hl on both"
  fi
fi

# ---- item 3: wasm against desktop -----------------------------------------------------------------
if [ -n "$WEB" ]; then
  note "item 3: the wasm build at $WEB"
  EXPECT=c38a83fb2d80117b
  if [ -x "$TESTS_BIN" ]; then
    got="$( (ulimit -v 4000000; "$TESTS_BIN" -tc='sim_math hash*' 2>&1) | sed -n 's/.*simMathCheckHash = \([0-9a-f]*\).*/\1/p' | head -1)"
    [ "$got" = "$EXPECT" ] || fail "desktop simMathCheckHash is '$got', expected $EXPECT"
  else
    note "no $TESTS_BIN: the desktop's hash is taken as $EXPECT"
  fi
  if [ -x "$SIM_BIN" ]; then
    (ulimit -v 4000000; "$SIM_BIN" --game as3d --level 1 --frames 3600 --bot --dump-state "$T/sim.json" >"$T/sim.log" 2>&1) \
      || fail "as3d_sim failed (see $T/sim.log)"
  else
    fail "no $SIM_BIN (build the sim tool, or set AS3D_SIM_BIN)"
  fi
  PYTHONPATH="${PYTHONPATH:-$HOME/tools/web-pw}" python3 -I - "$WEB" "$T" "$EXPECT" <<'PY' || fail "the wasm checks failed"
import base64, json, os, re, sys
sys.path.insert(0, os.path.expanduser(os.environ.get("PYTHONPATH", "~/tools/web-pw")))
from playwright.sync_api import sync_playwright
url, T, expect = sys.argv[1:4]
with sync_playwright() as pw:
    br = pw.chromium.launch(args=["--use-angle=swiftshader", "--enable-unsafe-swiftshader"])
    def run(query, pattern, timeout):
        pg = br.new_context(viewport={"width": 640, "height": 400}).new_page()
        lines = []
        pg.on("console", lambda m: lines.append(m.text))
        pg.goto(url + "?game=as3d&noaudio=1&autostart=1&" + query)
        pg.wait_for_function("window.as3dState && (window.as3dState.ready || window.as3dState.failed)", timeout=120000)
        if not any(re.search(pattern, l) for l in lines):
            try:
                pg.click("#play", timeout=3000)
            except Exception:
                pass
        end = timeout * 10
        for _ in range(end):
            for l in lines:
                m = re.search(pattern, l)
                if m:
                    pg.context.close()
                    return m
            pg.wait_for_timeout(100)
        raise SystemExit("timeout waiting for %s; last lines: %s" % (pattern, lines[-5:]))
    m = run("simmath=1&level=1&bot=1&frames=60", r"AS3D_SIMMATH hash=([0-9a-f]{16})", 120)
    print("wasm simmath", m.group(1))
    if m.group(1) != expect:
        raise SystemExit("wasm hash %s, expected %s" % (m.group(1), expect))
    m = run("level=1&bot=1&frames=3600&dumpstate=1", r"AS3D_DUMP_STATE b64=(\S+)", 600)
    open(os.path.join(T, "web.json"), "wb").write(base64.b64decode(m.group(1)))
    br.close()
PY
  if [ -s "$T/web.json" ] && [ -s "$T/sim.json" ]; then
    python3 -I - "$T/web.json" "$T/sim.json" <<'PY' || fail "the wasm bot dump differs from as3d_sim's (cmp $T/web.json $T/sim.json)"
import json, sys
a, b = (json.load(open(p)) for p in sys.argv[1:3])
sys.exit(0 if a == b else 1)
PY
    note "item 3: the simmath hash and the 3600-frame bot dump agree"
  else
    fail "item 3: a dump is missing"
  fi
fi

if [ "$FAILS" -eq 0 ]; then
  echo "online: OK  (logs in $T)"
  exit 0
fi
echo "online: $FAILS check(s) failed (logs in $T)" >&2
exit 1
