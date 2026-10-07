#!/usr/bin/env bash
# tools/3ds_ci.sh: compile-only check of the 3DS build (configure + build in the pinned Docker
# image). Exits non-zero on any error; for agents and CI, no console or emulator needed.
set -euo pipefail
"$(dirname "${BASH_SOURCE[0]}")/3ds_build.sh" all > /dev/null
echo "3ds_ci: ok"
