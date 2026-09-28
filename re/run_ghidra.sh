#!/usr/bin/env bash
# Imports the AirStrike3D v1.70 executable into a headless Ghidra project, runs auto-analysis,
# applies the naming pass, and exports functions.json / strings.json / imports.json /
# data_tables.json / decompiled/*.c / disasm/*.asm / SUMMARY.md.
#
# Usage:
#   re/run_ghidra.sh [--export-only] [exe_path] [out_dir]
#
#   (no flags)      Import (or re-import, via -overwrite) the exe and run full auto-analysis,
#                   then export. This is the "clean state" path; safe to re-run.
#   --export-only   Reuse the existing Ghidra project (skip -import/-noanalysis re-analysis)
#                   and just re-run the naming pass + export post-scripts. Fails with a clear
#                   message if the project does not exist yet.
#   exe_path        Defaults to third_party_local/original/AirStrike3D.exe. Pass a different
#                   path to analyse another build; it gets its own Ghidra project (named after
#                   the exe's basename) and its own default output dir / symbols CSV name, so
#                   it never collides with the v1.70 outputs.
#   out_dir         Defaults to re/out/v170 for the default exe, or re/out/<exe_basename> for
#                   any other exe.
#
# Ghidra location resolution order: $GHIDRA_HOME env var, else $HOME/tools/ghidra/current,
# else `analyzeHeadless` on PATH. Java location resolution order: $JAVA_HOME env var, else
# $HOME/Android/jdk, else `java` on PATH.
#
# Keeps the JVM heap modest (3G) since the machine may be shared with other agents, and caps
# analysis to a handful of CPU cores. Only run one analysis (i.e. one non---export-only
# invocation) at a time on this machine.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

EXPORT_ONLY=0
POSITIONAL=()
for arg in "$@"; do
    case "$arg" in
        --export-only)
            EXPORT_ONLY=1
            ;;
        -h|--help)
            sed -n '2,25p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            POSITIONAL+=("$arg")
            ;;
    esac
done

DEFAULT_EXE="$REPO_ROOT/third_party_local/original/AirStrike3D.exe"
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

OUT_DIR="${POSITIONAL[1]:-$REPO_ROOT/re/out/$TAG}"
PROJECT_LOC="$REPO_ROOT/third_party_local/ghidra_project"
SCRIPT_PATH="$REPO_ROOT/re/ghidra_scripts"
SYMBOLS_CSV="$REPO_ROOT/re/symbols_${TAG}_auto.csv"
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
echo "Output dir: $OUT_DIR"
echo "Project:    $PROJECT_LOC/$PROJECT_NAME"
echo "Symbols CSV: $SYMBOLS_CSV"

mkdir -p "$OUT_DIR" "$PROJECT_LOC"

export MAXMEM="${MAXMEM:-3G}"

COMMON_ARGS=(
    "$PROJECT_LOC" "$PROJECT_NAME"
    -scriptPath "$SCRIPT_PATH"
    -postScript RenameByErrorStrings.java "$SYMBOLS_CSV"
    -postScript ExportAll.java "$OUT_DIR"
    -max-cpu 4
    -log "$OUT_DIR/ghidra_analyze.log"
    -scriptlog "$OUT_DIR/ghidra_scripts.log"
)

if [ "$EXPORT_ONLY" = 1 ]; then
    if [ ! -f "$GPR_FILE" ]; then
        echo "ERROR: --export-only requested but no existing project at $GPR_FILE" >&2
        echo "       Run '$0 ${POSITIONAL[*]:-}' once without --export-only first." >&2
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
