#!/usr/bin/env bash
# Installs Ghidra machine-wide under $HOME/tools/ghidra, shared across projects.
#
# - Downloads the official release zip from the NationalSecurityAgency/ghidra GitHub
#   releases page (unless it is already present next to the target).
# - Verifies its SHA-256 against the value hardcoded below (copied from the release notes).
# - Unzips it into $HOME/tools/ghidra/<version>_PUBLIC/ and points the `current` symlink
#   at it.
# - Does nothing (idempotent) if that exact version is already installed and verified.
# - Writes nothing into this repository.
#
# Usage: re/install_ghidra.sh
#
# To install a different version, edit VERSION/BUILD_DATE/SHA256 below (copy the new
# value from the release's "SHA-256:" line on GitHub) and re-run; this will install
# alongside the existing version without touching it unless you also repoint `current`.

set -euo pipefail

VERSION="11.0.3"
BUILD_DATE="20240410"
ZIP_NAME="ghidra_${VERSION}_PUBLIC_${BUILD_DATE}.zip"
TAG="Ghidra_${VERSION}_build"
URL="https://github.com/NationalSecurityAgency/ghidra/releases/download/${TAG}/${ZIP_NAME}"
SHA256="2462a2d0ab11e30f9e907cd3b4aa6b48dd2642f325617e3d922c28e752be6761"

TOOLS_DIR="${GHIDRA_TOOLS_DIR:-$HOME/tools/ghidra}"
INSTALL_DIR="$TOOLS_DIR/ghidra_${VERSION}_PUBLIC"
ZIP_PATH="$TOOLS_DIR/$ZIP_NAME"

mkdir -p "$TOOLS_DIR"

verify_checksum() {
    local file="$1"
    local actual
    actual="$(sha256sum "$file" | awk '{print $1}')"
    if [ "$actual" != "$SHA256" ]; then
        echo "ERROR: checksum mismatch for $file" >&2
        echo "  expected: $SHA256" >&2
        echo "  actual:   $actual" >&2
        return 1
    fi
}

if [ -x "$INSTALL_DIR/support/analyzeHeadless" ]; then
    echo "Ghidra $VERSION already installed at $INSTALL_DIR"
else
    if [ ! -f "$ZIP_PATH" ]; then
        echo "Downloading Ghidra $VERSION from $URL"
        curl -L --fail -o "$ZIP_PATH.part" "$URL"
        mv "$ZIP_PATH.part" "$ZIP_PATH"
    else
        echo "Found existing archive $ZIP_PATH, skipping download"
    fi

    echo "Verifying SHA-256..."
    verify_checksum "$ZIP_PATH"

    echo "Unzipping into $TOOLS_DIR ..."
    tmp_extract="$(mktemp -d "$TOOLS_DIR/.extract_XXXXXX")"
    unzip -q "$ZIP_PATH" -d "$tmp_extract"
    extracted_dir="$tmp_extract/ghidra_${VERSION}_PUBLIC"
    if [ ! -d "$extracted_dir" ]; then
        echo "ERROR: expected $extracted_dir after unzip, not found" >&2
        rm -rf "$tmp_extract"
        exit 1
    fi
    mv "$extracted_dir" "$INSTALL_DIR"
    rmdir "$tmp_extract"
    echo "Installed Ghidra $VERSION at $INSTALL_DIR"
fi

ln -sfn "ghidra_${VERSION}_PUBLIC" "$TOOLS_DIR/current"
echo "current -> $(readlink "$TOOLS_DIR/current")"

echo "Verifying it runs..."
# analyzeHeadless's launch.sh insists on finding 'java' on PATH (separately from JAVA_HOME),
# so make sure a JDK 17-20 is reachable the same way re/run_ghidra.sh resolves one.
if [ -z "${JAVA_HOME:-}" ]; then
    if [ -x "$HOME/Android/jdk/bin/java" ]; then
        JAVA_HOME="$HOME/Android/jdk"
    fi
fi
if [ -n "${JAVA_HOME:-}" ] && [ -x "$JAVA_HOME/bin/java" ]; then
    export PATH="$JAVA_HOME/bin:$PATH"
fi
"$INSTALL_DIR/support/analyzeHeadless" 2>&1 | head -5 || true

echo "Done. GHIDRA_HOME=$TOOLS_DIR/current"
