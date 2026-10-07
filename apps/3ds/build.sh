#!/usr/bin/env bash
# apps/3ds/build.sh: builds apps/3ds/as3d_spike.3dsx with devkitARM in Docker (no native
# devkitPro needed). Usage:
#   apps/3ds/build.sh [all|clean]          (default all)
#   apps/3ds/build.sh netload <3ds-ip>     (build, then 3dslink to a real 3DS: press Y in
#                                           the Homebrew Launcher first)
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${HERE}/../.." && pwd)"
IMAGE="devkitpro/devkitarm@sha256:116afba8df8453961de2936ffab20dd441edf4d682856c1ec8b0e53d7ed0bbf5"
ACTION="${1:-all}"
JOBS="$(nproc)"

run() {
  docker run --rm --user "$(id -u):$(id -g)" -v "${REPO_ROOT}:/repo" -w /repo/apps/3ds "${IMAGE}" \
    bash -c "export DEVKITARM=/opt/devkitpro/devkitARM DEVKITPRO=/opt/devkitpro HOME=/tmp; $*"
}

case "${ACTION}" in
  all) run "make -j${JOBS} all" ;;
  clean) run "make clean" ;;
  netload)
    ip="${2:?usage: build.sh netload <3ds-ip>}"
    run "make -j${JOBS} all"
    docker run --rm --network host -v "${HERE}:/out" "${IMAGE}" \
      /opt/devkitpro/tools/bin/3dslink -a "${ip}" /out/as3d_spike.3dsx
    ;;
  *) echo "usage: $0 [all|clean|netload <ip>]" >&2; exit 2 ;;
esac
