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

# Music (docs/3ds-port.md M2.4): libopenmpt is compiled from the source tree that
# tools/fetch_third_party.sh downloads once (pinned version and SHA-256) into
# ${AS3D_DATA_ROOT:-repo}/third_party_local/, outside the build and never committed. It is
# mounted read-only into the container. AS3D_3DS_MUSIC=0 builds the stub decoder (no music);
# AS3D_3DS_FETCH=0 never downloads (tools/3ds_ci.sh): without the tree, the stub, with a warning.
OPENMPT_VERSION="0.8.9"
OPENMPT_DIR="${AS3D_DATA_ROOT:-${REPO_ROOT}}/third_party_local/libopenmpt-${OPENMPT_VERSION}+release"
MOUNTS=(-v "${REPO_ROOT}:/repo")
OPENMPT_ARG="-DAS3D_3DS_OPENMPT_SRC="

prepare_music() {
  [ "${AS3D_3DS_MUSIC:-1}" = "0" ] && return 0
  if [ ! -f "${OPENMPT_DIR}/libopenmpt/libopenmpt.h" ] && [ "${AS3D_3DS_FETCH:-1}" != "0" ]; then
    "${REPO_ROOT}/tools/fetch_third_party.sh" openmpt >&2
  fi
  if [ -f "${OPENMPT_DIR}/libopenmpt/libopenmpt.h" ]; then
    MOUNTS+=(-v "${OPENMPT_DIR}:/openmpt:ro")
    OPENMPT_ARG="-DAS3D_3DS_OPENMPT_SRC=/openmpt"
  else
    echo "3ds_build: no libopenmpt source at ${OPENMPT_DIR}: building without music" >&2
  fi
}

run() {
  docker run --rm --user "$(id -u):$(id -g)" "${MOUNTS[@]}" -w /repo "${IMAGE}" \
    bash -c "export DEVKITARM=/opt/devkitpro/devkitARM DEVKITPRO=/opt/devkitpro HOME=/tmp; $*"
}

# The commit the build is logged with (build= in log.txt, docs/3ds-port.md "Soak mode and the log
# push"): taken on the host, the image has no git.
commit_arg() {
  local c
  c="$(git -C "${REPO_ROOT}" rev-parse --short HEAD 2>/dev/null || echo unknown)"
  if [ "${c}" != unknown ] && ! git -C "${REPO_ROOT}" diff --quiet HEAD -- 2>/dev/null; then c="${c}-dirty"; fi
  echo "-DAS3D_GIT_COMMIT=${c}"
}

build() {
  prepare_music
  run "cmake -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/3DS.cmake -S /repo -B /repo/build-3ds -DCMAKE_BUILD_TYPE=Release ${OPENMPT_ARG} $(commit_arg) \
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
