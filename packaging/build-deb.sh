#!/usr/bin/env bash
# Build a .deb package for petto using dpkg-deb.
# Output: dist/petto_<version>_<arch>.deb
set -euo pipefail

cd "$(dirname "$0")/.."

VERSION="${VERSION:-$(cat VERSION 2>/dev/null || echo 0.1.0)}"
ARCH="$(dpkg --print-architecture)"
PKG="petto"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
chmod 0755 "$STAGE"   # mktemp -d is 0700; package root must be world-readable

echo "Building ${PKG} ${VERSION} (${ARCH})"

# 1. compile
make clean
make

# 2. stage files into the package root
make install DESTDIR="$STAGE" PREFIX=/usr
install -d "$STAGE/DEBIAN"

# 3. control file
INSTALLED_KB=$(du -ks "$STAGE/usr" | cut -f1)
cat > "$STAGE/DEBIAN/control" <<EOF
Package: ${PKG}
Version: ${VERSION}
Section: utils
Priority: optional
Architecture: ${ARCH}
Depends: libc6, libstdc++6, libx11-6, libxext6, libxfixes3, libxrender1, libxtst6, libcairo2
Installed-Size: ${INSTALLED_KB}
Maintainer: Indra Gunanda <indra.gunanda@ciptadusa.com>
Homepage: https://github.com/cds-id/petto
Description: Desktop pet that reacts as you type
 petto is a lightweight X11 desktop pet. Choose a rocket, cat, or Jarvis-style
 HUD that animates and reacts to your keystrokes. It includes a built-in
 Pomodoro timer with a full-screen break overlay. Double-click the pet to open
 settings.
 .
 An open source project by Cipta Dua Saudara (CDS) - https://open.ciptadusa.com
EOF

# 4. build the package
mkdir -p dist
DEB="dist/${PKG}_${VERSION}_${ARCH}.deb"
dpkg-deb --build --root-owner-group "$STAGE" "$DEB"

echo "Built $DEB"
dpkg-deb --info "$DEB" || true
