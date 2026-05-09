#!/usr/bin/env bash
# Configure (via CMakePresets) and build BambuStudio.
#
# Parallelism formula matches BuildLinux.sh / DockerBuild.sh:
#   MAX_THREADS = floor(FREE_MEM_GB / 2.5)
# i.e. budget ~2.5 GB per cc1plus (Boost.Spirit / CGAL TUs are the spike).
# Override with `CMAKE_BUILD_PARALLEL_LEVEL=N pixi run build`.

set -euo pipefail

source "$(dirname "$0")/_jobs.sh"
export CMAKE_BUILD_PARALLEL_LEVEL=$(pixi_parallel_jobs)
echo "Setting CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL} (mem-aware, matches BuildLinux.sh)"

cmake --preset linux-pixi
cmake --build --preset linux-pixi --parallel "$CMAKE_BUILD_PARALLEL_LEVEL"
