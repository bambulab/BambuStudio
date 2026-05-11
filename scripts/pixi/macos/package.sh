#!/usr/bin/env bash
# Wrap build/<type>/src/BambuStudio into a self-contained BambuStudio.app:
#   - Contents/MacOS/BambuStudio                (copy of the binary)
#   - Contents/Info.plist                       (sed'd from src/platform/osx/Info.plist.in)
#   - Contents/Resources/                       (copy of resources/, incl. Icon.icns)
#   - Contents/Frameworks/*.dylib               (filled by bundle-dylibs.sh)
# Then codesigns (ad-hoc by default; override via BAMBU_CODESIGN_IDENTITY).
#
# Argument (optional, default "release"): which build/<type>/ tree to wrap.

set -euo pipefail

build_type="${1:-release}"
case "$build_type" in
    debug|release) ;;
    *) echo "Unknown build type: $build_type (expected debug or release)" >&2; exit 1 ;;
esac

: "${PIXI_PROJECT_ROOT:?run via 'pixi run'}"
: "${CONDA_PREFIX:?run via 'pixi run'}"

build_dir="$PIXI_PROJECT_ROOT/build/$build_type"
binary="$build_dir/src/BambuStudio"
[[ -x "$binary" ]] || { echo "$binary not found. Run 'pixi run build-$build_type' first." >&2; exit 1; }

# Reject accidental x86_64 inputs early.
arch=$(lipo -archs "$binary" 2>/dev/null || true)
case "$arch" in
    *arm64*) ;;
    *) echo "package: binary is '$arch', expected arm64" >&2; exit 1 ;;
esac

# shellcheck disable=SC1091
source "$PIXI_PROJECT_ROOT/packaging/macos/plist-vars.sh"

app="$build_dir/BambuStudio.app"
rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources" "$app/Contents/Frameworks"

# 1. Binary.
cp "$binary" "$app/Contents/MacOS/BambuStudio"
chmod u+w "$app/Contents/MacOS/BambuStudio"

# 2. Info.plist (sed three @VAR@ placeholders).
sed \
    -e "s/@SLIC3R_APP_NAME@/$SLIC3R_APP_NAME/g" \
    -e "s/@SLIC3R_APP_KEY@/$SLIC3R_APP_KEY/g" \
    -e "s/@SLIC3R_BUILD_ID@/$SLIC3R_BUILD_ID/g" \
    "$PIXI_PROJECT_ROOT/src/platform/osx/Info.plist.in" \
    > "$app/Contents/Info.plist"
plutil -lint "$app/Contents/Info.plist" > /dev/null

# 3. Resources. Path matches BambuStudio.cpp's parent_path().parent_path() / "Resources".
cp -R "$PIXI_PROJECT_ROOT/resources/." "$app/Contents/Resources/"

# 4. CA bundle (defensive; not wired into LSEnvironment in v1 — see plan §risks).
if [[ -f "$CONDA_PREFIX/ssl/cacert.pem" ]]; then
    cp "$CONDA_PREFIX/ssl/cacert.pem" "$app/Contents/Resources/cacert.pem"
fi

# 5. Frameworks — bundle every @rpath dylib + flip binary rpath.
bash "$PIXI_PROJECT_ROOT/scripts/pixi/macos/bundle-dylibs.sh" \
    "$app/Contents/MacOS/BambuStudio" \
    "$app/Contents/Frameworks"

# 6. Codesign. Ad-hoc by default; Developer ID via BAMBU_CODESIGN_IDENTITY.
identity="${BAMBU_CODESIGN_IDENTITY:--}"
if [[ "$identity" == "-" ]]; then
    codesign --force --deep --sign - "$app"
else
    codesign --force --deep --options runtime --sign "$identity" "$app"
fi
codesign --verify --verbose=1 "$app"

echo
echo "BambuStudio.app: $app"
du -sh "$app" | awk '{print "size: " $1}'
