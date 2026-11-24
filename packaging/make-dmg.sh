#!/bin/bash
# Packs built Vox Bundle Audio Units into dist/VoxBundle-<version>-AU.dmg (macOS only).
# Usage: packaging/make-dmg.sh [build-dir]
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD="${1:-build}"
VERSION=$(sed -n 's/^project(VoxBundle VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
STAGE="dist/dmg/VoxBundle"

rm -rf dist/dmg
mkdir -p "$STAGE"

shopt -s nullglob
comps=("$BUILD"/*_artefacts/Release/AU/*.component)
if [ ${#comps[@]} -eq 0 ]; then
    echo "No .component files in $BUILD - build the plugins first." >&2
    exit 1
fi

for c in "${comps[@]}"; do
    cp -R "$c" "$STAGE/"
done
for c in "$STAGE"/*.component; do
    codesign --force --deep --sign - "$c"
done

cp packaging/install.command packaging/README-INSTALL.txt "$STAGE/"
chmod +x "$STAGE/install.command"

OUT="dist/VoxBundle-$VERSION-AU.dmg"
rm -f "$OUT"
hdiutil create -volname "VoxBundle" -srcfolder "$STAGE" -fs HFS+ -format UDZO -ov "$OUT"
codesign --force --sign - "$OUT" 2>/dev/null || true

echo
echo "DMG ready: $OUT  (${#comps[@]} plugins)"
