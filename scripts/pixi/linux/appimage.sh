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

# Prune the env down before layering our binary on top. Doing it now (vs after)
# avoids the bin/ whitelist rebuild blowing away the bambu-studio we'd just
# copied in.
echo "Pruning conda artifacts…"

# Whitelist-replace bin/: keep only runtime tools bambu-studio shells out to.
# bwrap is the AppRun fallback when host bwrap is missing; ffmpeg/ffprobe are
# spawned by MediaPlayCtrl for live-camera playback. Everything else (cmake,
# mold, sccache, gcc/g++/clang/ld/ar/nm/strip, conda compiler wrappers,
# gettext utilities, python3.*, demo apps like JxrEncApp / WebKitWebDriver) is
# unreachable at runtime.
mv "$appdir/usr/bin" "$appdir/usr/bin_tmp"
mkdir "$appdir/usr/bin"
for tool in bwrap ffmpeg ffprobe; do
    [[ -e "$appdir/usr/bin_tmp/$tool" ]] && cp -a "$appdir/usr/bin_tmp/$tool" "$appdir/usr/bin/"
done
rm -rf "$appdir/usr/bin_tmp"

# Build-only directories. FreeCAD-Bundle (conda/linux/create_bundle.sh) prunes
# a smaller set; this list goes further because pixi env carries the full
# toolchain (gcc/cmake/mold/sccache/LLVM) that FreeCAD's runtime-only
# `mamba create` doesn't pull in.
rm -rf \
    "$appdir/usr/conda-meta" \
    "$appdir/usr/include" \
    "$appdir/usr/lib/cmake" \
    "$appdir/usr/lib/pkgconfig" \
    "$appdir/usr/lib/aclocal" \
    "$appdir/usr/libexec/gcc" \
    "$appdir/usr/libexec/gettext" \
    "$appdir/usr/x86_64-conda-linux-gnu" \
    "$appdir/usr/man" \
    "$appdir/usr/share/man" \
    "$appdir/usr/share/doc" \
    "$appdir/usr/share/gtk-doc" \
    "$appdir/usr/share/info" \
    "$appdir/usr/share/aclocal"

# LLVM/Clang are absent from bambu-studio's ldd closure (pulled in only by
# rust/clang-based build tools we just deleted). Same for the Python runtime.
rm -f  "$appdir"/usr/lib/libLLVM*.so* \
       "$appdir"/usr/lib/libclang*.so* \
       "$appdir"/usr/lib/libLTO*.so* \
       "$appdir"/usr/lib/libRemarks*.so* \
       "$appdir"/usr/lib/libpython3.*.so*
rm -rf "$appdir"/usr/lib/python3.*

# Remaining dev artifacts.
find "$appdir/usr" \( -name '*.a' -o -name '*.h' -o -name '*.hpp' -o -name '*.la' -o -name '*.cmake' \) -delete
find "$appdir/usr" -name '__pycache__' -type d -exec rm -rf {} + 2>/dev/null || true

# Dangling symlinks left by everything above.
find "$appdir/usr" -xtype l -delete

# TODO: pruning candidates worth ~250 MB more, deferred pending runtime checks.
# Each is absent from ldd's static closure but could still be dlopen'd; verify
# with `strace -f -e openat ./bambu-studio` over a full session (slice, send to
# printer, play camera feed) before enabling.
#
#   share/gir-1.0   ~46M  .gir XML for GObject Introspection. C++ apps don't
#                         read these directly, but check whether any bundled
#                         lib loads them via g_irepository_require().
#   lib/perl5       ~48M  Conda's Perl runtime (autoconf/gettext build-dep).
#                         No Perl in bambu-studio sources -- almost certainly
#                         safe, but confirm no .so wraps a perl interpreter.
#   lib/ruby        ~36M  Same story for Ruby. Safe pending confirmation.
#   lib/qt6         ~73M  Qt6 runtime; bambu-studio is wxWidgets-based and
#                         qt6 isn't in ldd. Risk: webkit2gtk's WebInspector
#                         tooling, or some pulled-in lib, could dlopen Qt.
#   lib/openvino*   ~114M Intel OpenVINO. ffmpeg may dlopen the OpenVINO
#                         filter at runtime if --enable-libopenvino was set
#                         at ffmpeg build time. Check `ffmpeg -filters | grep
#                         openvino` first; if empty, safe to drop.
#
# rm -rf "$appdir/usr/share/gir-1.0"
# rm -rf "$appdir/usr/lib/perl5"
# rm -rf "$appdir/usr/lib/ruby"
# rm -rf "$appdir/usr/lib/qt6"
# rm -rf "$appdir/usr/lib/openvino"*

# Record build-host $CONDA_PREFIX so AppRun can bind-mount AppDir/usr onto it at
# runtime. Required because conda-forge libs (webkit2gtk, libcurl, fontconfig)
# bake this absolute path into their .so files with no env-var override.
printf '%s\n' "$CONDA_PREFIX" > "$appdir/conda-prefix-at-build.txt"

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

echo "Running appimagetool (zstd compression)…"
out="$build_dir/BambuStudio-${version}-x86_64.AppImage"
ARCH=x86_64 VERSION="$version" "$appimagetool" --comp zstd "$appdir" "$out"
chmod +x "$out"

echo
echo "AppImage: $out"
ls -lh "$out"
