#!/usr/bin/env bash
# Build the bambulab/libnoise fork via deps/ ExternalProject and install
# the result into the active pixi env so the main configure can find it
# via a single-valued CMAKE_PREFIX_PATH=$CONDA_PREFIX.
#
# Run with `pixi run setup-libnoise` (the task wires PIXI_PROJECT_ROOT and CONDA_PREFIX).

set -euo pipefail

: "${PIXI_PROJECT_ROOT:?PIXI_PROJECT_ROOT must be set; run this via 'pixi run'}"
: "${CONDA_PREFIX:?CONDA_PREFIX must be set; run this via 'pixi run'}"

if [[ -f "$CONDA_PREFIX/lib/liblibnoise_static.a" && -d "$CONDA_PREFIX/include/libnoise" ]]; then
  echo "libnoise already installed in pixi env, skipping"
  exit 0
fi

build_dir="$PIXI_PROJECT_ROOT/build/deps-build"
destdir="$build_dir/destdir"
mkdir -p "$build_dir"
cd "$build_dir"

# Configure deps with everything that pixi already provides switched off.
# Only dep_libnoise will actually be built below.
cmake "$PIXI_PROJECT_ROOT/deps" \
  -DDEP_BUILD_BOOST=OFF -DDEP_BUILD_TBB=OFF -DDEP_BUILD_CURL=OFF \
  -DDEP_BUILD_OPENSSL=OFF -DDEP_BUILD_GLFW=OFF -DDEP_BUILD_BLOSC=OFF \
  -DDEP_BUILD_FREETYPE=OFF -DDEP_BUILD_WXWIDGETS=OFF -DDEP_BUILD_FFMPEG=OFF \
  -DDEP_BUILD_PNG=OFF -DDEP_BUILD_JPEG=OFF -DDEP_BUILD_TIFF=OFF \
  -DDESTDIR="$destdir" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5

source "$(dirname "$0")/_jobs.sh"
jobs=$(pixi_parallel_jobs)
echo "Building dep_libnoise with -j${jobs}"
cmake --build . --target dep_libnoise -j"$jobs"

src="$destdir/usr/local"
mkdir -p "$CONDA_PREFIX/include" "$CONDA_PREFIX/lib"
cp -r "$src/include/libnoise" "$CONDA_PREFIX/include/"
cp "$src/lib/liblibnoise_static.a" "$CONDA_PREFIX/lib/"
echo "libnoise installed to $CONDA_PREFIX"
