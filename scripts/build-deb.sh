#!/bin/bash
# Builds a Debian package of AnGscheidrDiffer.
# Usage:  ./scripts/build-deb.sh [version]   (default: version from CMakeLists.txt)
# Result: release/angscheidrdiffer_<version>_<arch>.deb

set -e

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# The version is kept in CMakeLists.txt. An argument overrides it.
CMAKE_VERSION="$(sed -n 's/^project(.*VERSION \([0-9.]*\).*/\1/p' \
                 "$ROOT_DIR/CMakeLists.txt" | head -1)"
VERSION="${1:-${CMAKE_VERSION:-1.0.0}}"
ARCH="$(dpkg --print-architecture)"
PKG_NAME="angscheidrdiffer"

BUILD_DIR="$ROOT_DIR/build-deb/cmake"
PKG_DIR="$ROOT_DIR/build-deb/pkg"

echo "=== $PKG_NAME $VERSION ($ARCH) ==="

# --- 1. Release build with the /usr prefix ---
echo "[1/4] Compiling (Release)..."
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr > /dev/null
cmake --build "$BUILD_DIR" -j"$(nproc)" > /dev/null

# --- 2. Install into the staging tree using the CMake install rules ---
echo "[2/4] Staging package contents..."
rm -rf "$PKG_DIR"
DESTDIR="$PKG_DIR" cmake --install "$BUILD_DIR" > /dev/null
mkdir -p "$PKG_DIR/DEBIAN"

# --- 3. Determine the dependencies ---
echo "[3/4] Determining dependencies..."
# dpkg-shlibdeps is preferred: it reports not only the package names but also
# the minimum versions derived from the symbols the binary actually uses.
# Without those lower bounds the package would install on a system with an
# older Qt and only fail at startup.
DEPENDS=""
if command -v dpkg-shlibdeps > /dev/null; then
    SHLIB_DIR="$ROOT_DIR/build-deb/shlibdeps"
    rm -rf "$SHLIB_DIR"
    mkdir -p "$SHLIB_DIR/debian"
    printf 'Source: %s\n\nPackage: %s\nArchitecture: %s\nDepends: ${shlibs:Depends}\nDescription: placeholder\n placeholder\n' \
        "$PKG_NAME" "$PKG_NAME" "$ARCH" > "$SHLIB_DIR/debian/control"
    cp "$PKG_DIR/usr/bin/$PKG_NAME" "$SHLIB_DIR/"
    DEPENDS="$(cd "$SHLIB_DIR" && dpkg-shlibdeps -O "./$PKG_NAME" 2>/dev/null \
               | sed 's/^shlibs:Depends=//')"
    rm -rf "$SHLIB_DIR"
fi

# Fallback when dpkg-shlibdeps is missing or yields nothing: the package names
# of the linked libraries only, without version bounds.
# realpath is needed because ldd reports /lib/... while the dpkg database
# knows /usr/lib/... after the usr merge.
if [ -z "$DEPENDS" ]; then
    echo "    Note: dpkg-shlibdeps unusable, falling back to package names only."
    DEPENDS="$(ldd "$PKG_DIR/usr/bin/angscheidrdiffer" 2>/dev/null \
        | awk '/=>/ { print $3 }' \
        | sort -u \
        | while read -r lib; do
              [ -n "$lib" ] && dpkg -S "$(realpath "$lib")" 2>/dev/null | cut -d: -f1
          done \
        | sort -u | paste -sd, - | sed 's/,/, /g')"
fi

cat > "$PKG_DIR/DEBIAN/control" <<EOF
Package: $PKG_NAME
Version: $VERSION
Section: devel
Priority: optional
Architecture: $ARCH
Depends: $DEPENDS
Maintainer: formic <apps@unifyzer.de>
Homepage: https://github.com/form1c/angscheidrdiffer
Description: Side-by-side diff and merge tool (Qt6)
 Fully editable two-pane file comparison with merge operations,
 ignore options (whitespace, comments, case, first N lines),
 WinMerge-style folder comparison and Dolphin service-menu
 integration. Includes a batch mode with GNU-diff-like exit codes.
EOF

# --- 4. Build the package ---
echo "[4/4] Building package..."
mkdir -p "$ROOT_DIR/release"
DEB_FILE="$ROOT_DIR/release/${PKG_NAME}_${VERSION}_${ARCH}.deb"
dpkg-deb --root-owner-group --build "$PKG_DIR" "$DEB_FILE" > /dev/null
rm -rf "$PKG_DIR"  # the staging tree is no longer needed

echo ""
echo "Package created: $DEB_FILE"
echo ""
echo "Install:   sudo apt install \"$DEB_FILE\""
echo "Remove:    sudo apt remove $PKG_NAME"
echo ""
echo "Note: remove an earlier manual installation under /usr/local first,"
echo "it would shadow the package in the search path:"
echo "  ./scripts/uninstall-local.sh"
