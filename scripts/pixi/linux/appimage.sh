#!/usr/bin/env bash
# Bundle bambu-studio into a portable AppImage.
#
# Approach borrowed from FreeCAD-Bundle: copy the pixi env into AppDir/usr/,
# layer in our binary + resources + metadata, then run appimagetool. No
# linuxdeploy: conda already resolves all .so deps.
#
# Argument (optional, default "release"): which build/<type>/ tree to bundle.

set -euo pipefail

build_type="${1:-release}"
case "$build_type" in
    debug|release) ;;
    *) echo "Unknown build type: $build_type (expected debug or release)" >&2; exit 1 ;;
esac

: "${PIXI_PROJECT_ROOT:?run via 'pixi run'}"
: "${CONDA_PREFIX:?run via 'pixi run'}"

build_dir="$PIXI_PROJECT_ROOT/build/$build_type"
binary="$build_dir/src/bambu-studio"
[[ -x "$binary" ]] || { echo "$binary not found. Run 'pixi run build-$build_type' first." >&2; exit 1; }

version=$(awk -F'"' '/SLIC3R_VERSION /{print $2}' "$PIXI_PROJECT_ROOT/version.inc")

# Cache appimagetool under .pixi/cache/ (downloaded once).
tools="$PIXI_PROJECT_ROOT/.pixi/cache/appimage-tools"
mkdir -p "$tools"
appimagetool="$tools/appimagetool-x86_64.AppImage"
if [[ ! -x "$appimagetool" ]]; then
    echo "Downloading appimagetool…"
    curl -fL -o "$appimagetool" \
        https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
    chmod +x "$appimagetool"
fi

appdir="$build_dir/AppDir"
rm -rf "$appdir"
mkdir -p "$appdir/usr"

echo "Copying pixi env to AppDir/usr (~1.5 GB)…"
cp -a "$CONDA_PREFIX/." "$appdir/usr/"

# Drop bambu-studio in (overwrites if pixi env had a stub).
cp -f "$binary" "$appdir/usr/bin/bambu-studio"

# conda's compiler activation injects -Wl,-rpath,$CONDA_PREFIX/lib + --disable-new-dtags
# into LDFLAGS, baking the build host's absolute path into bambu-studio's DT_RPATH.
# Strip it: --set-rpath also flips DT_RPATH -> DT_RUNPATH, so AppRun's LD_LIBRARY_PATH
# (and any host LD_PRELOAD overrides) take precedence over the bundled libs again.
patchelf --set-rpath '$ORIGIN:$ORIGIN/../lib' "$appdir/usr/bin/bambu-studio"

# bambu-studio looks for resources at <exec_dir>/../resources/ (matches the
# in-tree layout build/<type>/resources/ where the symlink is generated).
mkdir -p "$appdir/usr/resources"
cp -rf "$PIXI_PROJECT_ROOT/resources/." "$appdir/usr/resources/"

# Top-level metadata for appimagetool.
install -m 755 "$PIXI_PROJECT_ROOT/packaging/appimage/AppRun" "$appdir/AppRun"
install -m 644 "$PIXI_PROJECT_ROOT/packaging/appimage/BambuStudio.desktop" "$appdir/BambuStudio.desktop"
install -m 644 "$PIXI_PROJECT_ROOT/resources/images/BambuStudio_192px.png" "$appdir/BambuStudio.png"

# Icon also at hicolor location for desktop integration after extraction.
for size in 32 128 192; do
    install -Dm 644 "$PIXI_PROJECT_ROOT/resources/images/BambuStudio_${size}px.png" \
        "$appdir/usr/share/icons/hicolor/${size}x${size}/apps/BambuStudio.png"
done

# Drop conda metadata + dev artifacts to shrink the AppImage (FreeCAD-Bundle
# does the same). Skip pruning anything bambu-studio may dlopen at runtime.
echo "Pruning conda artifacts…"
rm -rf \
    "$appdir/usr/conda-meta" \
    "$appdir/usr/lib/cmake" \
    "$appdir/usr/lib/pkgconfig" \
    "$appdir/usr/share/man" \
    "$appdir/usr/share/doc"
find "$appdir/usr" \( -name '*.a' -o -name '*.h' -o -name '*.hpp' \) -delete
find "$appdir/usr" -name '__pycache__' -type d -exec rm -rf {} + 2>/dev/null || true

echo "Running appimagetool (zstd compression)…"
out="$build_dir/BambuStudio-${version}-x86_64.AppImage"
ARCH=x86_64 VERSION="$version" "$appimagetool" --comp zstd "$appdir" "$out"
chmod +x "$out"

echo
echo "AppImage: $out"
ls -lh "$out"
