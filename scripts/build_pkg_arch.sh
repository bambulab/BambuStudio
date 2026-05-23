#!/bin/bash
# Build an Arch Linux .pkg.tar.zst from the already-compiled build/package/ directory.
# Run after BuildLinux.sh -s has completed.
set -e

ROOT=$(dirname "$(readlink -f "$0")")/..

VERSION=$(grep 'set(SLIC3R_VERSION' "$ROOT/version.inc" | cut -d '"' -f2)
if [ -z "$VERSION" ]; then
    echo "Error: could not read version from version.inc" >&2
    exit 1
fi

ARCH=$(uname -m)
APPDIR="$ROOT/build/package"
PKGNAME="bambustudio"
PKGDIR="$ROOT/build/pkgbuild"

if [ ! -f "$APPDIR/bin/bambu-studio" ]; then
    echo "Error: build/package/bin/bambu-studio not found." >&2
    echo "Run './BuildLinux.sh -sf' first." >&2
    exit 1
fi

echo "Building .pkg.tar.zst for BambuStudio ${VERSION}..."
rm -rf "$PKGDIR"
# Use a staging dir OUTSIDE of pkg/ — makepkg clears pkg/<pkgname>/ before
# calling package(), so pre-populating that path would leave it empty.
STAGING="$PKGDIR/staging"
mkdir -p "$STAGING/usr/bin"
mkdir -p "$STAGING/usr/lib/${PKGNAME}"
mkdir -p "$STAGING/usr/share/applications"
for SIZE in 32x32 128x128 192x192; do
    mkdir -p "$STAGING/usr/share/icons/hicolor/${SIZE}/apps"
done

install -m 755 "$APPDIR/bin/bambu-studio" "$STAGING/usr/lib/${PKGNAME}/"
find "$APPDIR/bin" -name "*.so*" -exec install -m 755 {} "$STAGING/usr/lib/${PKGNAME}/" \;
cp -r "$APPDIR/resources" "$STAGING/usr/lib/${PKGNAME}/"

cat > "$STAGING/usr/bin/${PKGNAME}" << 'WRAPPER'
#!/bin/bash
export LD_LIBRARY_PATH="/usr/lib/bambustudio${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
exec /usr/lib/bambustudio/bambu-studio "$@"
WRAPPER
chmod 755 "$STAGING/usr/bin/${PKGNAME}"

sed 's|^Exec=.*|Exec=/usr/bin/'"${PKGNAME}"' %U|' \
    "$APPDIR/BambuStudio.desktop" \
    > "$STAGING/usr/share/applications/${PKGNAME}.desktop"

for SIZE in 32x32 128x128 192x192; do
    SRC="$APPDIR/usr/share/icons/hicolor/${SIZE}/apps/BambuStudio.png"
    DST="$STAGING/usr/share/icons/hicolor/${SIZE}/apps/${PKGNAME}.png"
    [ -f "$SRC" ] && install -m 644 "$SRC" "$DST"
done

# Write PKGBUILD — package() copies from our staging dir into makepkg's pkgdir
STAGING_ABS="$(realpath "$STAGING")"
cat > "$PKGDIR/PKGBUILD" << EOF
pkgname=${PKGNAME}
pkgver=${VERSION//-/_}
pkgrel=1
pkgdesc="BambuStudio 3D printing slicer"
arch=('${ARCH}')
url="https://bambulab.com"
license=('AGPL3')
depends=('gtk3' 'glib2' 'dbus' 'libsecret' 'webkit2gtk-4.1' 'libxkbcommon' 'mesa')

package() {
    cp -r "${STAGING_ABS}/." "\${pkgdir}/"
}
EOF

cd "$PKGDIR"
makepkg --noextract --nodeps --nocheck -f 2>&1

PKG=$(find "$PKGDIR" -name "${PKGNAME}-*.pkg.tar.zst" | head -1)
if [ -z "$PKG" ]; then
    echo "Error: no .pkg.tar.zst found after makepkg" >&2
    exit 1
fi
cp "$PKG" "$ROOT/build/"
echo "Created: $ROOT/build/$(basename "$PKG")"
