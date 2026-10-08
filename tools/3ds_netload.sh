#!/usr/bin/env bash
# Sends a given .3dsx to the console's Homebrew Launcher netloader (press Y there), looping
# 3dslink for up to 10 minutes. Unlike `3ds_build.sh netload`, nothing is rebuilt: this sends
# exactly the file given (an agent's drop, an older build).
#   tools/3ds_netload.sh <3ds-ip> [path/to/openairstrike.3dsx]
set -euo pipefail
ip="${1:?usage: 3ds_netload.sh <3ds-ip> [file.3dsx]}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
file="${2:-${REPO_ROOT}/build-3ds/apps/3ds/openairstrike.3dsx}"
file="$(realpath "$file")"
IMAGE="$(sed -n 's/^IMAGE=\(.*\)$/\1/p' "${REPO_ROOT}/tools/3ds_build.sh" | head -1 | tr -d '"')"
dir="$(dirname "$file")"; base="$(basename "$file")"
echo "3dslink: sending $file ($(stat -c %s "$file") bytes) to $ip"
end=$((SECONDS + 600))
while (( SECONDS < end )); do
  if docker run --rm --network host -v "${dir}:/out:ro" "${IMAGE}" \
       /opt/devkitpro/tools/bin/3dslink -a "${ip}" "/out/${base}"; then
    exit 0
  fi
  sleep 3
done
echo "3dslink: no netloader answered within 10 minutes" >&2
exit 1
