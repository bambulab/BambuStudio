#!/usr/bin/env bash
# Build the bambulab/wxWidgets fork via deps/ ExternalProject and install
# the result into the active pixi env so the main BambuStudio executable
# can link against wxWidgets via a single-valued CMAKE_PREFIX_PATH=$CONDA_PREFIX.
#
# WebView is enabled (DEP_WX_WEBVIEW=ON) using webkit2gtk-4.1 from the
# pixi env, since BambuStudio.cpp unconditionally pulls in slic3r/GUI/*
# headers — there is no real "GUI=OFF" build of the executable.
#
# Run with `pixi run setup-wxwidgets`.

set -euo pipefail

# Resolve our own dir BEFORE any cd; we source _jobs.sh from here later.
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

: "${PIXI_PROJECT_ROOT:?PIXI_PROJECT_ROOT must be set; run this via 'pixi run'}"
: "${CONDA_PREFIX:?CONDA_PREFIX must be set; run this via 'pixi run'}"

if [[ -x "$CONDA_PREFIX/bin/wx-config" ]]; then
  echo "wxWidgets already installed in pixi env, skipping"
  exit 0
fi

# Use a wxWidgets-specific build dir so libnoise/wxWidgets caches don't collide.
build_dir="$PIXI_PROJECT_ROOT/build/deps-build-wx"
destdir="$build_dir/destdir"
rm -rf "$build_dir/CMakeCache.txt" "$build_dir/CMakeFiles"
mkdir -p "$build_dir"
cd "$build_dir"

# wxWidgets's vendored cotire helper has a try_compile test with
# cmake_minimum_required(VERSION 2.x). Export the policy default as an
# env var so the nested try_compile inherits it.
export CMAKE_POLICY_VERSION_MINIMUM=3.5

cmake "$PIXI_PROJECT_ROOT/deps" \
  -DDEP_BUILD_BOOST=OFF -DDEP_BUILD_TBB=OFF -DDEP_BUILD_CURL=OFF \
  -DDEP_BUILD_OPENSSL=OFF -DDEP_BUILD_GLFW=OFF -DDEP_BUILD_BLOSC=OFF \
  -DDEP_BUILD_FREETYPE=OFF -DDEP_BUILD_FFMPEG=OFF \
  -DDEP_BUILD_PNG=OFF -DDEP_BUILD_JPEG=OFF -DDEP_BUILD_TIFF=OFF \
  -DDEP_BUILD_WXWIDGETS=ON \
  -DDEP_WX_GTK3=ON \
  -DDEP_WX_WEBVIEW=ON \
  -DDESTDIR="$destdir"

source "$script_dir/../shared/_jobs.sh"
jobs=$(pixi_parallel_jobs)
echo "Building dep_wxWidgets with -j${jobs}"
cmake --build . --target dep_wxWidgets -j"$jobs"

src="$destdir/usr/local"
# wxWidgets installs into bin/ (wx-config), include/wx-X.Y/, lib/, lib/cmake/, share/.
# Use cp -rn to merge into the pixi env without overwriting unrelated files.
cp -rn "$src/." "$CONDA_PREFIX/"

# wxWidgets hard-codes the install prefix into wx-config and its companion
# files (the upstream wxWidgets.cmake even warns about this). Rewrite those
# paths to point inside the pixi env so the copy actually works.
wx_config_real="$CONDA_PREFIX/lib/wx/config/gtk3-unicode-static-3.1"
ln -sfn "../lib/wx/config/gtk3-unicode-static-3.1" "$CONDA_PREFIX/bin/wx-config"
sed -i "s|$src|$CONDA_PREFIX|g" \
    "$wx_config_real" \
    "$CONDA_PREFIX/lib/wx/include/gtk3-unicode-static-3.1/wx/setup.h"

echo "wxWidgets installed to $CONDA_PREFIX"
echo "  wx-config -> $(readlink -f "$CONDA_PREFIX/bin/wx-config")"
echo "  --cxxflags: $("$CONDA_PREFIX/bin/wx-config" --cxxflags | head -c 120)..."
