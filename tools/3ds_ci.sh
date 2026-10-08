#!/usr/bin/env bash
# tools/3ds_ci.sh: compile-only check of the 3DS build (configure + build in the pinned Docker
# image). Exits non-zero on any error; for agents and CI, no console or emulator needed.
# No network: libopenmpt (music, docs/3ds-port.md M2.4) is built when its source tree is already
# in ${AS3D_DATA_ROOT:-repo}/third_party_local/, else the build has the stub decoder (a warning).
set -euo pipefail
AS3D_3DS_FETCH=0 "$(dirname "${BASH_SOURCE[0]}")/3ds_build.sh" all > /dev/null
echo "3ds_ci: ok"
