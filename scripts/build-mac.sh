#!/bin/bash
# One-shot build on a Mac: universal AU plugins -> dist/VoxBundle-<version>.pkg / .dmg
#   bash scripts/build-mac.sh            build the installer
#   bash scripts/build-mac.sh --install  build it and run it (asks for your password)
set -euo pipefail
cd "$(dirname "$0")/.."

if ! xcode-select -p >/dev/null 2>&1; then
    echo "Xcode Command Line Tools are missing. Run:  xcode-select --install   then re-run this script." >&2
    exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
    echo "CMake is missing. Install it with Homebrew:  brew install cmake   (or from cmake.org), then re-run." >&2
    exit 1
fi

JOBS=$(sysctl -n hw.ncpu)
cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release
cmake --build build-mac --config Release -j "$JOBS"
bash packaging/make-dmg.sh build-mac

if [ "${1:-}" = "--install" ]; then
    sudo installer -pkg dist/VoxBundle-*.pkg -target /
fi
