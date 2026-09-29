#!/usr/bin/env bash
# Imports an AirStrike game executable into a headless Ghidra project, runs auto-analysis,
# applies the naming pass, and exports functions.json / strings.json / imports.json /
# data_tables.json / decompiled/*.c / disasm/*.asm / SUMMARY.md.
#
# Usage:
#   re/run_ghidra.sh --game as3d|as2|gulf [--export-only] [--out DIR] [--project-dir DIR]
#   re/run_ghidra.sh [--export-only] [exe_path] [out_dir]          (legacy positional form)
#
#   --game KEY      Game key from tools/games.json. Resolves the exe, the tag (as3d -> v170,
#                   as2, gulf), the output dir $AS3D_DATA_ROOT/re/out/<tag>, the probe file
#                   re/probes/<tag>.json and a SEPARATE Ghidra project per game:
#                     as2, gulf: $AS3D_DATA_ROOT/third_party_local/ghidra_project/<tag>/
#                     as3d:      $AS3D_DATA_ROOT/third_party_local/ghidra_project/ (the
#                                project that already exists there, name AirStrike3D)
#                   Exe: as3d -> third_party_local/original/, sequels ->
#                   third_party_local/games/<key>/ (tools/setup_data.sh <key> puts it there).
#   --out DIR       Override the output directory (e.g. a scratch dir for a comparison run).
#   --project-dir DIR  Override the Ghidra project directory.
#   (no flags)      Import (or re-import, via -overwrite) the exe and run full auto-analysis,
#                   then export. This is the "clean state" path; safe to re-run.
#   --export-only   Reuse the existing Ghidra project (skip -import/-noanalysis re-analysis)
#                   and just re-run the naming pass + export post-scripts. Fails with a clear
#                   message if the project does not exist yet.
#   exe_path        (legacy) Defaults to third_party_local/original/AirStrike3D.exe. A
#                   different path gets its own Ghidra project (named after the exe's
#                   basename), tag = basename, output dir re/out/<basename>.
#   out_dir         (legacy) Defaults to re/out/<tag>.
#
# AS3D_DATA_ROOT (default: the repo root) is where game data, the projects and re/out live;
# nothing is written into a worktree except the symbols CSV re/symbols_<tag>_auto.csv.
#
# Ghidra location resolution order: $GHIDRA_HOME env var, else $HOME/tools/ghidra/current,
# else `analyzeHeadless` on PATH. Java location resolution order: $JAVA_HOME env var, else
# $HOME/Android/jdk, else `java` on PATH.
#
# Keeps the JVM heap modest (3G) since the machine may be shared with other agents, and caps
# analysis to a handful of CPU cores. Only run one Ghidra instance at a time on this machine
# (check `free -m` first: at least 4 GB available), and wrap it in `timeout 1800`.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DATA_ROOT="${AS3D_DATA_ROOT:-$REPO_ROOT}"

EXPORT_ONLY=0
GAME=""
OUT_OVERRIDE=""
PROJECT_OVERRIDE=""
POSITIONAL=()
while [ $# -gt 0 ]; do
    case "$1" in
        --export-only)
            EXPORT_ONLY=1
            ;;
        --game)
            GAME="${2:?--game needs a key (as3d, as2, gulf)}"; shift
            ;;
        --out)
            OUT_OVERRIDE="${2:?--out needs a directory}"; shift
            ;;
        --project-dir)
            PROJECT_OVERRIDE="${2:?--project-dir needs a directory}"; shift
            ;;
        -h|--help)
            sed -n '2,32p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            POSITIONAL+=("$1")
            ;;
    esac
    shift
done

DEFAULT_EXE="$DATA_ROOT/third_party_local/original/AirStrike3D.exe"

if [ -n "$GAME" ]; then
    # exe name from tools/games.json
    EXE_NAME="$(python3 -c '
import json, sys
for g in json.load(open(sys.argv[1]))["games"]:
    if g["key"] == sys.argv[2]:
        print(g["exe"]); sys.exit(0)
sys.exit("ERROR: unknown game key %r (see tools/games.json)" % sys.argv[2])
' "$REPO_ROOT/tools/games.json" "$GAME")"
    case "$GAME" in
        as3d)
            TAG="v170"
            EXE_PATH="$DEFAULT_EXE"
            PROJECT_LOC="$DATA_ROOT/third_party_local/ghidra_project"
            PROJECT_NAME="AirStrike3D"
            ;;
        *)
            TAG="$GAME"
            EXE_PATH="$DATA_ROOT/third_party_local/games/$GAME/$EXE_NAME"
            PROJECT_LOC="$DATA_ROOT/third_party_local/ghidra_project/$TAG"
            PROJECT_NAME="$TAG"
            ;;
    esac
    if [ ! -f "$EXE_PATH" ]; then
        echo "ERROR: executable not found: $EXE_PATH (run tools/setup_data.sh $GAME)" >&2
        exit 1
    fi
    EXE_BASENAME="$(basename "$EXE_PATH")"
else
    EXE_PATH="${POSITIONAL[0]:-$DEFAULT_EXE}"
    if [ ! -f "$EXE_PATH" ]; then
        echo "ERROR: executable not found: $EXE_PATH" >&2
        exit 1
    fi
    EXE_BASENAME="$(basename "$EXE_PATH")"
    PROJECT_NAME="${EXE_BASENAME%.*}"
    if [ "$EXE_PATH" = "$DEFAULT_EXE" ]; then
        TAG="v170"
    else
        TAG="$PROJECT_NAME"
    fi
    PROJECT_LOC="$DATA_ROOT/third_party_local/ghidra_project"
fi

if [ -n "$PROJECT_OVERRIDE" ]; then PROJECT_LOC="$PROJECT_OVERRIDE"; fi
if [ -n "$OUT_OVERRIDE" ]; then
    OUT_DIR="$OUT_OVERRIDE"
elif [ -z "$GAME" ] && [ -n "${POSITIONAL[1]:-}" ]; then
    OUT_DIR="${POSITIONAL[1]}"
else
    OUT_DIR="$DATA_ROOT/re/out/$TAG"
fi
SCRIPT_PATH="$REPO_ROOT/re/ghidra_scripts"
SYMBOLS_CSV="$REPO_ROOT/re/symbols_${TAG}_auto.csv"
PROBE_FILE="$REPO_ROOT/re/probes/$TAG.json"
if [ ! -f "$PROBE_FILE" ]; then
    echo "WARNING: no probe file $PROBE_FILE, using re/probes/v170.json" >&2
    PROBE_FILE="$REPO_ROOT/re/probes/v170.json"
fi
GPR_FILE="$PROJECT_LOC/$PROJECT_NAME.gpr"

# ---------------------------------------------------------------- resolve Java

if [ -z "${JAVA_HOME:-}" ]; then
    if [ -x "$HOME/Android/jdk/bin/java" ]; then
        JAVA_HOME="$HOME/Android/jdk"
    elif command -v java >/dev/null 2>&1; then
        JAVA_BIN="$(command -v java)"
        JAVA_BIN="$(readlink -f "$JAVA_BIN" 2>/dev/null || echo "$JAVA_BIN")"
        JAVA_HOME="$(dirname "$(dirname "$JAVA_BIN")")"
    else
        echo "ERROR: no Java found. Set JAVA_HOME, or install a JDK at $HOME/Android/jdk," >&2
        echo "       or make 'java' available on PATH." >&2
        exit 1
    fi
fi
export JAVA_HOME
if [ ! -x "$JAVA_HOME/bin/java" ]; then
    echo "ERROR: JAVA_HOME=$JAVA_HOME has no bin/java" >&2
    exit 1
fi
# Ghidra's launch.sh insists on finding 'java' on PATH (it uses PATH's java to run its
# LaunchSupport bootstrapper, separately from JAVA_HOME), so make sure it's there too.
export PATH="$JAVA_HOME/bin:$PATH"

# --------------------------------------------------------------- resolve Ghidra

if [ -n "${GHIDRA_HOME:-}" ]; then
    : # use as given
elif [ -x "$HOME/tools/ghidra/current/support/analyzeHeadless" ]; then
    GHIDRA_HOME="$HOME/tools/ghidra/current"
elif command -v analyzeHeadless >/dev/null 2>&1; then
    ANALYZE_ON_PATH="$(command -v analyzeHeadless)"
    ANALYZE_ON_PATH="$(readlink -f "$ANALYZE_ON_PATH" 2>/dev/null || echo "$ANALYZE_ON_PATH")"
    # analyzeHeadless lives at $GHIDRA_HOME/support/analyzeHeadless
    GHIDRA_HOME="$(dirname "$(dirname "$ANALYZE_ON_PATH")")"
else
    cat >&2 <<'EOF'
ERROR: Ghidra not found.

Looked for (in order): $GHIDRA_HOME, $HOME/tools/ghidra/current, 'analyzeHeadless' on PATH.

To install it, run:
    re/install_ghidra.sh

which downloads Ghidra 11.0.3 from:
    https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_11.0.3_build/ghidra_11.0.3_PUBLIC_20240410.zip
verifies it against SHA-256:
    2462a2d0ab11e30f9e907cd3b4aa6b48dd2642f325617e3d922c28e752be6761
and installs it under:
    $HOME/tools/ghidra/ghidra_11.0.3_PUBLIC/   (with a 'current' symlink)
EOF
    exit 1
fi

ANALYZE_HEADLESS="$GHIDRA_HOME/support/analyzeHeadless"
if [ ! -x "$ANALYZE_HEADLESS" ]; then
    echo "ERROR: $ANALYZE_HEADLESS not found or not executable (GHIDRA_HOME=$GHIDRA_HOME)" >&2
    exit 1
fi

echo "Using JAVA_HOME=$JAVA_HOME"
echo "Using GHIDRA_HOME=$GHIDRA_HOME"
echo "Executable: $EXE_PATH"
echo "Tag:        $TAG"
echo "Output dir: $OUT_DIR"
echo "Project:    $PROJECT_LOC/$PROJECT_NAME"
echo "Probes:     $PROBE_FILE"
echo "Symbols CSV: $SYMBOLS_CSV"

mkdir -p "$OUT_DIR" "$PROJECT_LOC"

export MAXMEM="${MAXMEM:-3G}"

COMMON_ARGS=(
    "$PROJECT_LOC" "$PROJECT_NAME"
    -scriptPath "$SCRIPT_PATH"
    -postScript RenameByErrorStrings.java "$SYMBOLS_CSV"
    -postScript ExportAll.java "$OUT_DIR" "$PROBE_FILE"
    -max-cpu 4
    -log "$OUT_DIR/ghidra_analyze.log"
    -scriptlog "$OUT_DIR/ghidra_scripts.log"
)

if [ "$EXPORT_ONLY" = 1 ]; then
    if [ ! -f "$GPR_FILE" ]; then
        echo "ERROR: --export-only requested but no existing project at $GPR_FILE" >&2
        echo "       Run this command once without --export-only first." >&2
        exit 1
    fi
    echo "Mode: export-only (reusing existing analyzed project, no re-analysis)"
    time "$ANALYZE_HEADLESS" "${COMMON_ARGS[@]}" -process "$EXE_BASENAME" -noanalysis
else
    echo "Mode: full import + analysis (idempotent via -overwrite)"
    time "$ANALYZE_HEADLESS" "${COMMON_ARGS[@]}" -import "$EXE_PATH" -overwrite
fi

echo
echo "Done. Outputs in $OUT_DIR"
echo "Symbols mapping: $SYMBOLS_CSV"
