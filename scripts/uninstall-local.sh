#!/bin/bash
# Removes a manual installation (sudo cmake --install build) from /usr/local.
# This is needed before installing the Debian package, because /usr/local/bin
# comes before /usr/bin in the search path and would shadow the package.
# Usage: ./scripts/uninstall-local.sh   (asks for the sudo password if needed)

set -e

PREFIX="/usr/local"

FILES=(
    "$PREFIX/bin/angscheidrdiffer"
    "$PREFIX/share/applications/angscheidrdiffer.desktop"
    "$PREFIX/share/kio/servicemenus/compare-angscheidrdiffer.desktop"
    "$PREFIX/share/metainfo/de.unifyzer.angscheidrdiffer.metainfo.xml"
    "$PREFIX/share/doc/angscheidrdiffer/copyright"
    # Icons: the current set of PNG files plus an SVG from earlier versions
    "$PREFIX/share/icons/hicolor/scalable/apps/angscheidrdiffer.svg"
)
for size in 16 22 24 32 48 64 96 128 192 256 512; do
    FILES+=("$PREFIX/share/icons/hicolor/${size}x${size}/apps/angscheidrdiffer.png")
done

echo "=== AnGscheidrDiffer: removing a manual installation ==="

SUDO=""
if [ "$EUID" -ne 0 ]; then
    SUDO="sudo"
fi

REMOVED=0
for f in "${FILES[@]}"; do
    if [ -e "$f" ]; then
        echo "Removing $f"
        $SUDO rm "$f"
        REMOVED=$((REMOVED + 1))
    fi
done

if [ "$REMOVED" -eq 0 ]; then
    echo "Nothing to do: no manual installation found under $PREFIX."
else
    echo ""
    echo "Removed $REMOVED file(s)."
fi
