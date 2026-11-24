#!/bin/bash
# Builds the installer from compiled Audio Units (macOS only):
#   dist/VoxBundle-<version>.pkg  - double-click installer
#   dist/VoxBundle-<version>.dmg  - disk image with the installer inside
# Usage: packaging/make-dmg.sh [build-dir]
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD="${1:-build}"
VERSION=$(sed -n 's/^project(VoxBundle VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
WORK="dist/work"
ROOT="$WORK/root/Library/Audio/Plug-Ins/Components"

rm -rf "$WORK"
mkdir -p "$ROOT" "$WORK/scripts" "$WORK/resources" "$WORK/pkgs" "$WORK/dmg"

PLISTBUDDY="${PLISTBUDDY:-/usr/libexec/PlistBuddy}"
shopt -s nullglob
comps=("$BUILD"/*_artefacts/Release/AU/*.component)
if [ ${#comps[@]} -eq 0 ]; then
    echo "No .component files in $BUILD - build the plugins first." >&2
    exit 1
fi
for c in "${comps[@]}"; do
    cp -R "$c" "$ROOT/"
done
for c in "$ROOT"/*.component; do
    codesign --force --deep --sign - "$c"
done

# Component package. Bundles must not be "relocatable", otherwise Installer may
# drop the update wherever an old copy with the same bundle ID happens to live.
cp packaging/installer/postinstall "$WORK/scripts/postinstall"
chmod +x "$WORK/scripts/postinstall"
pkgbuild --analyze --root "$WORK/root" "$WORK/components.plist"
i=0
while "$PLISTBUDDY" -c "Print :$i" "$WORK/components.plist" >/dev/null 2>&1; do
    # The key may or may not be present: drop it, then add it as false.
    "$PLISTBUDDY" -c "Delete :$i:BundleIsRelocatable" "$WORK/components.plist" >/dev/null 2>&1 || true
    "$PLISTBUDDY" -c "Add :$i:BundleIsRelocatable bool false" "$WORK/components.plist"
    i=$((i + 1))
done
pkgbuild --root "$WORK/root" \
         --component-plist "$WORK/components.plist" \
         --identifier com.voxbundle.au \
         --version "$VERSION" \
         --install-location / \
         --scripts "$WORK/scripts" \
         "$WORK/pkgs/VoxBundle-AU.pkg"

# Product archive with the welcome / done pages.
cp packaging/installer/welcome.html packaging/installer/conclusion.html "$WORK/resources/"
sed "s/__VERSION__/$VERSION/" packaging/installer/distribution.xml > "$WORK/distribution.xml"
PKG="dist/VoxBundle-$VERSION.pkg"
productbuild --distribution "$WORK/distribution.xml" \
             --resources "$WORK/resources" \
             --package-path "$WORK/pkgs" \
             "$PKG"

# Disk image: installer + readme.
cp "$PKG" "$WORK/dmg/Install Vox Bundle.pkg"
cp packaging/README-INSTALL.txt "$WORK/dmg/README.txt"
DMG="dist/VoxBundle-$VERSION.dmg"
rm -f "$DMG"
hdiutil create -volname "Vox Bundle" -srcfolder "$WORK/dmg" -fs HFS+ -format UDZO -ov "$DMG"

echo
echo "Installer: $PKG"
echo "DMG:       $DMG  (${#comps[@]} plugins)"
