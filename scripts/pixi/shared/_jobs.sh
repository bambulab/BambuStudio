#!/usr/bin/env bash
# Print a memory-aware parallel job count, matching BuildLinux.sh:
#   MAX_THREADS = floor(FREE_MEM_GB / 2.5)
# capped at nproc and at least 1.
# Honors CMAKE_BUILD_PARALLEL_LEVEL if set in env.
#
# Source-and-call:  source "$(dirname "$0")/_jobs.sh" && jobs=$(pixi_parallel_jobs)

pixi_parallel_jobs() {
    if [[ -n "${CMAKE_BUILD_PARALLEL_LEVEL:-}" ]]; then
        echo "$CMAKE_BUILD_PARALLEL_LEVEL"
        return
    fi
    local free_gb nproc_count threads
    if [[ "$(uname)" == "Darwin" ]]; then
        # macOS: no /proc, no `free`. `memory_pressure` would give "free"
        # mem but reliably misreports under load; total RAM is the closest
        # stable proxy and keeps the formula consistent with Linux.
        free_gb=$(( $(sysctl -n hw.memsize) / 1024 / 1024 / 1024 ))
        nproc_count=$(sysctl -n hw.ncpu)
    else
        free_gb=$(free -g | awk '/^Mem:/ {print $7}')
        nproc_count=$(nproc)
    fi
    threads=$((free_gb * 10 / 25))
    if [[ $threads -lt 1 ]]; then
        threads=1
    elif [[ $threads -gt $nproc_count ]]; then
        threads=$nproc_count
    fi
    echo "$threads"
}
