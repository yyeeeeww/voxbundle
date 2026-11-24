#!/bin/bash
# Installs the Vox Bundle Audio Units for the current user.
set -e
cd "$(dirname "$0")"

DEST="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$DEST"

for c in *.component; do
    echo "Installing $c"
    rm -rf "$DEST/$c"
    cp -R "$c" "$DEST/"
    xattr -dr com.apple.quarantine "$DEST/$c" 2>/dev/null || true
done

# Make macOS forget its cached list of Audio Units.
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo
echo "Done. Restart FL Studio and rescan plugins (Options > Manage plugins > Find plugins)."
