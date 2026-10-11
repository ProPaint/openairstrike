#!/usr/bin/env bash
# tools/3ds_fetch_logs.sh <3ds-ip> [port]: fetches the 3DS app's logs from the console's SD card
# with ftpd (port 5000, anonymous) into out/3ds/<date>-<commit>/ and prints the directory.
#   log.txt        the engine log and the PERF_SUMMARY block (tools/3ds_perf_report.py)
#   perf.csv       one row per frame, with perf=1 in run.ini
#   soak.txt       the verdict of mode=soak (soak.running: a soak launch died inside that stage)
#   spike_log.txt  the spike / probe logs (mode=spike); probe_log.txt too
# Start ftpd on the console first (and quit the Homebrew Launcher netloader). A file that is not
# on the card is skipped. Then: python3 tools/3ds_perf_report.py <that directory>
set -euo pipefail
IP="${1:?usage: 3ds_fetch_logs.sh <3ds-ip> [port]}"
PORT="${2:-5000}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMMIT="$(git -C "${REPO_ROOT}" rev-parse --short HEAD 2>/dev/null || echo nogit)"
OUT="${REPO_ROOT}/out/3ds/$(date +%Y-%m-%d)-${COMMIT}"
mkdir -p "${OUT}"
got=0
for f in log.txt perf.csv soak.txt soak.running spike_log.txt probe_log.txt engine_log.txt; do
  if curl -sS --fail --connect-timeout 5 --max-time 120 -o "${OUT}/${f}" \
       "ftp://${IP}:${PORT}/3ds/openairstrike/${f}" 2>/dev/null; then
    echo "fetched ${f} ($(wc -c < "${OUT}/${f}") bytes)" >&2
    got=$((got + 1))
  else
    rm -f "${OUT}/${f}"
  fi
done
if (( got == 0 )); then
  echo "3ds_fetch_logs: nothing fetched from ${IP}:${PORT} (is ftpd running?)" >&2
  exit 1
fi
echo "${OUT}"
