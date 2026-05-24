#!/bin/bash
# Build a .deb package from the already-compiled build/package/ directory.
# Run after BuildLinux.sh -si (or -sif) has completed.
set -e

ROOT=$(dirname "$(readlink -f "$0")")/..

BASE=$(grep 'set(SLIC3R_VERSION_BASE' "$ROOT/version.inc" | cut -d '"' -f2)
if [ -z "$BASE" ]; then
    echo "Error: could not read version from version.inc" >&2
    exit 1
fi
COUNT=$(git -C "$ROOT" rev-list --count HEAD 2>/dev/null || echo "0")
VERSION="${BASE}.${COUNT}"

PKGNAME="bambustudio"
ARCH="amd64"
APPDIR="$ROOT/build/package"
PKGDIR="$ROOT/build/${PKGNAME}_${VERSION}_${ARCH}"
OUTFILE="$ROOT/build/${PKGNAME}_${VERSION}_${ARCH}.deb"

if [ ! -f "$APPDIR/bin/bambu-studio" ]; then
    echo "Error: build/package/bin/bambu-studio not found." >&2
    echo "Run './BuildLinux.sh -si' first." >&2
    exit 1
fi

echo "Building .deb for BambuStudio ${VERSION}..."
rm -rf "$PKGDIR"

# Directory structure
mkdir -p "$PKGDIR/DEBIAN"
mkdir -p "$PKGDIR/usr/bin"
mkdir -p "$PKGDIR/usr/lib/$PKGNAME/bin"   # binary lives here so parent().parent() == /usr/lib/bambustudio
mkdir -p "$PKGDIR/usr/share/applications"
mkdir -p "$PKGDIR/usr/share/icons/hicolor/192x192/apps"
mkdir -p "$PKGDIR/usr/share/icons/hicolor/128x128/apps"
mkdir -p "$PKGDIR/usr/share/icons/hicolor/32x32/apps"

# Binary goes into bin/ so the resource-path calculation
# (parent_path().parent_path() / "resources") resolves to
# /usr/lib/bambustudio/resources — matching where we install resources.
cp "$APPDIR/bin/bambu-studio" "$PKGDIR/usr/lib/$PKGNAME/bin/"

# Bundled app-specific libs — exclude GTK/GLib/X11/Wayland/DBus/udev
# because those are system libraries declared in Depends; bundling them
# causes LD_LIBRARY_PATH to override the system versions and crash on GTK init.
find "$APPDIR/bin" -name "*.so*" \
    ! -name "libgtk*"     ! -name "libgdk*"      ! -name "libgdk-pixbuf*" \
    ! -name "libglib*"    ! -name "libgobject*"   ! -name "libgio*" \
    ! -name "libgmodule*" ! -name "libgthread*"   ! -name "libgthread*" \
    ! -name "libX*"       ! -name "libxcb*"       ! -name "libxkbcommon*" \
    ! -name "libwayland*" \
    ! -name "libdbus*"    ! -name "libudev*"      \
    ! -name "libsecret*"  ! -name "libfontconfig*" \
    ! -name "libfreetype*" ! -name "libpango*"    ! -name "libcairo*" \
    ! -name "libatk*"     ! -name "libharfbuzz*"  \
    -exec cp {} "$PKGDIR/usr/lib/$PKGNAME/" \;

# App resources (alongside the bin/ directory, not inside it)
cp -r "$APPDIR/resources" "$PKGDIR/usr/lib/$PKGNAME/"

# Launcher wrapper
cat > "$PKGDIR/usr/bin/$PKGNAME" <<'WRAPPER'
#!/bin/bash
export LD_LIBRARY_PATH="/usr/lib/bambustudio${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec /usr/lib/bambustudio/bin/bambu-studio "$@"
WRAPPER
chmod 755 "$PKGDIR/usr/bin/$PKGNAME"

# Desktop entry — fix AppImage-specific Exec= path for system install
sed 's|^Exec=.*|Exec=/usr/bin/bambustudio %U|' \
    "$APPDIR/BambuStudio.desktop" \
    > "$PKGDIR/usr/share/applications/$PKGNAME.desktop"

# Icons
for SIZE in 192x192 128x128 32x32; do
    SRC="$APPDIR/usr/share/icons/hicolor/$SIZE/apps/BambuStudio.png"
    DST="$PKGDIR/usr/share/icons/hicolor/$SIZE/apps/$PKGNAME.png"
    [ -f "$SRC" ] && cp "$SRC" "$DST"
done

# Installed-Size in KiB (must be computed after usr/ is populated)
INSTALLED_KB=$(du -sk "$PKGDIR/usr" | cut -f1)

# DEBIAN/control
cat > "$PKGDIR/DEBIAN/control" <<EOF
Package: $PKGNAME
Version: $VERSION
Architecture: $ARCH
Maintainer: Bambu Lab <https://bambulab.com>
Installed-Size: $INSTALLED_KB
Depends: libgl1, libgtk-3-0, libglib2.0-0, libdbus-1-3, libudev1, libsecret-1-0, libwebkit2gtk-4.1-0 | libwebkit2gtk-4.0-37, libxkbcommon0, libgstreamer1.0-0, libgstreamer-plugins-base1.0-0, gstreamer1.0-plugins-good, gstreamer1.0-gl
Section: graphics
Priority: optional
Homepage: https://bambulab.com
Description: BambuStudio 3D printing slicer
 BambuStudio is a cutting-edge, feature-rich slicing software.
 Supports Bambu Lab 3D printers. Based on PrusaSlicer.
EOF

# DEBIAN/postinst
cat > "$PKGDIR/DEBIAN/postinst" <<'EOF'
#!/bin/bash
update-desktop-database /usr/share/applications 2>/dev/null || true
update-mime-database /usr/share/mime 2>/dev/null || true
EOF
chmod 755 "$PKGDIR/DEBIAN/postinst"

# DEBIAN/postrm
cat > "$PKGDIR/DEBIAN/postrm" <<'EOF'
#!/bin/bash
update-desktop-database /usr/share/applications 2>/dev/null || true
update-mime-database /usr/share/mime 2>/dev/null || true
EOF
chmod 755 "$PKGDIR/DEBIAN/postrm"

dpkg-deb --build --root-owner-group "$PKGDIR" "$OUTFILE"
echo "Created: $OUTFILE"
