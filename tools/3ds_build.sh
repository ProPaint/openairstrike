#!/usr/bin/env bash
# tools/3ds_build.sh: builds build-3ds/apps/3ds/openairstrike.3dsx with devkitARM and 3ds-cmake
# in the pinned Docker image (no native devkitPro needed). Usage:
#   tools/3ds_build.sh [all|clean]
#   tools/3ds_build.sh netload <3ds-ip>   (build, then 3dslink; press Y in the Homebrew
#                                          Launcher first. Retries every 3 s for 10 minutes.)
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="devkitpro/devkitarm@sha256:116afba8df8453961de2936ffab20dd441edf4d682856c1ec8b0e53d7ed0bbf5"
ACTION="${1:-all}"
OUT="build-3ds/apps/3ds/openairstrike.3dsx"

run() {
  docker run --rm --user "$(id -u):$(id -g)" -v "${REPO_ROOT}:/repo" -w /repo "${IMAGE}" \
    bash -c "export DEVKITARM=/opt/devkitpro/devkitARM DEVKITPRO=/opt/devkitpro HOME=/tmp; $*"
}

build() {
  run "cmake -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/3DS.cmake -S /repo -B /repo/build-3ds -DCMAKE_BUILD_TYPE=Release \
       && cmake --build /repo/build-3ds -j${AS3D_BUILD_JOBS:-\$(nproc)}"
  echo "${OUT}"
}

case "${ACTION}" in
  all) build ;;
  clean) rm -rf "${REPO_ROOT}/build-3ds" ;;
  netload)
    ip="${2:?usage: 3ds_build.sh netload <3ds-ip>}"
    build
    # One 3dslink attempt per loop and no separate port probe: a TCP probe of port 17491
    # consumes the netloader session.
    end=$((SECONDS + 600))
    while (( SECONDS < end )); do
      if docker run --rm --network host -v "${REPO_ROOT}/build-3ds/apps/3ds:/out:ro" "${IMAGE}" \
           /opt/devkitpro/tools/bin/3dslink -a "${ip}" /out/openairstrike.3dsx; then
        exit 0
      fi
      sleep 3
    done
    echo "3dslink: no netloader answered within 10 minutes" >&2
    exit 1
    ;;
  *) echo "usage: $0 [all|clean|netload <ip>]" >&2; exit 2 ;;
esac
