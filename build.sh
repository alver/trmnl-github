#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-.build}"

# Ensure we're in a git repo with the required branches
if ! git rev-parse --git-dir &>/dev/null; then
    echo "Error: not a git repository" >&2
    exit 1
fi

if ! git rev-parse --verify origin/upstream &>/dev/null; then
    echo "Error: origin/upstream branch not found" >&2
    exit 1
fi

# Clean previous build dir if exists
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# Layer 1: upstream firmware
echo "Extracting upstream firmware..."
git archive origin/upstream | tar -x -C "$BUILD_DIR"

# Layer 2: overlay files from current branch (overwrites upstream where needed)
echo "Applying overlay files..."
# git archive HEAD -- src/ include/ test/ platformio.ini | tar -x -C "$BUILD_DIR"
cp -rf src/ "$BUILD_DIR"/src/
cp -rf include/ "$BUILD_DIR"/include/
cp -rf test/ "$BUILD_DIR"/test
cp -f platformio.ini "$BUILD_DIR"/platformio.ini

echo ""
echo "Build directory ready: $BUILD_DIR"
echo ""
echo "  Build:   cd $BUILD_DIR && pio run -e github_pages"
echo "  Test:    cd $BUILD_DIR && pio test -e native-crypto"
echo "  Flash:   cd $BUILD_DIR && pio run -e github_pages -t upload"
echo "  Clean:   rm -rf $BUILD_DIR"
