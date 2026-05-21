#!/bin/bash
# Build a .rpm package from the already-compiled build/package/ directory.
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
RPMNAME="bambustudio"
RELEASE="1"
RPMTOP="$ROOT/build/rpmbuild"

if [ ! -f "$APPDIR/bin/bambu-studio" ]; then
    echo "Error: build/package/bin/bambu-studio not found." >&2
    echo "Run './BuildLinux.sh -sf' first." >&2
    exit 1
fi

echo "Building .rpm for BambuStudio ${VERSION}..."
mkdir -p "$RPMTOP"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

# Generate spec file via Python to avoid bash/RPM macro escaping conflicts
python3 - <<PYEOF
import os, textwrap

rpmname   = "${RPMNAME}"
version   = "${VERSION}"
release   = "${RELEASE}"
arch      = "${ARCH}"
appdir    = "${APPDIR}"
rpmtop    = "${RPMTOP}"

spec = textwrap.dedent(f"""
    Name:       {rpmname}
    Version:    {version}
    Release:    {release}%{{?dist}}
    Summary:    BambuStudio 3D printing slicer
    License:    AGPLv3
    URL:        https://bambulab.com
    BuildArch:  {arch}
    Requires:   mesa-libGL, gtk3, glib2, dbus-libs, systemd-libs, libsecret, webkitgtk6.0, libxkbcommon

    %description
    BambuStudio is a cutting-edge, feature-rich slicing software.
    Supports Bambu Lab 3D printers. Based on PrusaSlicer.

    %install
    APPDIR="{appdir}"

    install -d %{{buildroot}}/usr/bin
    install -d %{{buildroot}}/usr/lib/{rpmname}
    install -d %{{buildroot}}/usr/share/applications
    for SIZE in 32x32 128x128 192x192; do
        install -d %{{buildroot}}/usr/share/icons/hicolor/\${{SIZE}}/apps
    done

    install -m 755 "\${{APPDIR}}/bin/bambu-studio" %{{buildroot}}/usr/lib/{rpmname}/
    find "\${{APPDIR}}/bin" -name "*.so*" -exec install -m 755 {{}} %{{buildroot}}/usr/lib/{rpmname}/ \\;
    cp -r "\${{APPDIR}}/resources" %{{buildroot}}/usr/lib/{rpmname}/

    cat > %{{buildroot}}/usr/bin/{rpmname} << 'WRAPPER'
    #!/bin/bash
    export LD_LIBRARY_PATH="/usr/lib/{rpmname}${{LD_LIBRARY_PATH:+:${{LD_LIBRARY_PATH}}}}"
    exec /usr/lib/{rpmname}/bambu-studio "$@"
    WRAPPER
    chmod 755 %{{buildroot}}/usr/bin/{rpmname}
    sed -i '1s/^    //' %{{buildroot}}/usr/bin/{rpmname}

    sed 's|^Exec=.*|Exec=/usr/bin/{rpmname} %%U|' \\
        "\${{APPDIR}}/BambuStudio.desktop" \\
        > %{{buildroot}}/usr/share/applications/{rpmname}.desktop

    for SIZE in 32x32 128x128 192x192; do
        SRC="\${{APPDIR}}/usr/share/icons/hicolor/\${{SIZE}}/apps/BambuStudio.png"
        DST="%{{buildroot}}/usr/share/icons/hicolor/\${{SIZE}}/apps/{rpmname}.png"
        [ -f "\${{SRC}}" ] && install -m 644 "\${{SRC}}" "\${{DST}}"
    done

    %files
    /usr/bin/{rpmname}
    /usr/lib/{rpmname}/
    /usr/share/applications/{rpmname}.desktop
    /usr/share/icons/hicolor/32x32/apps/{rpmname}.png
    /usr/share/icons/hicolor/128x128/apps/{rpmname}.png
    /usr/share/icons/hicolor/192x192/apps/{rpmname}.png

    %post
    update-desktop-database /usr/share/applications 2>/dev/null || true

    %postun
    update-desktop-database /usr/share/applications 2>/dev/null || true
""").lstrip()

# Remove leading indentation from spec body (textwrap.dedent strips common indent)
with open(f"{rpmtop}/SPECS/{rpmname}.spec", "w") as f:
    f.write(spec)

print(f"Spec written to {rpmtop}/SPECS/{rpmname}.spec")
PYEOF

rpmbuild -bb \
    --define "_topdir $RPMTOP" \
    "$RPMTOP/SPECS/$RPMNAME.spec"

RPM=$(find "$RPMTOP/RPMS" -name "${RPMNAME}-*.rpm" | head -1)
if [ -z "$RPM" ]; then
    echo "Error: no .rpm found after rpmbuild" >&2
    exit 1
fi
cp "$RPM" "$ROOT/build/"
echo "Created: $ROOT/build/$(basename "$RPM")"
