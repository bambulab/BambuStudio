#!/usr/bin/env bash
# Configure (via CMakePresets) and build BambuStudio.
#
# Argument (optional, default "debug"): build type, one of {debug, release}.
# Each gets its own build/<type>/ tree; both can coexist.
#
# Parallelism formula matches BuildLinux.sh / DockerBuild.sh:
#   MAX_THREADS = floor(FREE_MEM_GB / 2.5)
# i.e. budget ~2.5 GB per cc1plus (Boost.Spirit / CGAL TUs are the spike).
# Override with `CMAKE_BUILD_PARALLEL_LEVEL=N pixi run build`.

set -euo pipefail

build_type="${1:-debug}"
case "$build_type" in
    debug|release) ;;
    *) echo "Unknown build type: $build_type (expected debug or release)" >&2; exit 1 ;;
esac
preset="linux-pixi-${build_type}"

# Resolve relative to this script regardless of caller's cwd.
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_dir/_jobs.sh"
export CMAKE_BUILD_PARALLEL_LEVEL=$(pixi_parallel_jobs)
echo "Setting CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL} (mem-aware, matches BuildLinux.sh)"

# Only reconfigure on first build (or after `rm -rf build/<type>`). Re-running
# `cmake --preset` on every build invalidates the PCH and cascades into a
# rebuild of all libslic3r / libslic3r_gui TUs, even when nothing changed.
# `cmake --build` re-invokes configure internally if CMakeLists.txt changed.
build_dir="${PIXI_PROJECT_ROOT:-$script_dir/../..}/build/$build_type"
if [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
    cmake --preset "$preset"
fi
cmake --build --preset "$preset" --parallel "$CMAKE_BUILD_PARALLEL_LEVEL"
