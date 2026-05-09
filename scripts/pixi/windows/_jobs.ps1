# PowerShell port of _jobs.sh — print a memory-aware parallel job count for
# Windows pixi tasks. Mirrors BuildLinux.sh / DockerBuild.sh:
#   MAX_THREADS = floor(FREE_MEM_GB / 2.5)
# capped at logical CPUs and at least 1. Honors CMAKE_BUILD_PARALLEL_LEVEL.
#
# Dot-source then call:
#   . "$PSScriptRoot/_jobs.ps1"
#   $jobs = Get-PixiParallelJobs

function Get-PixiParallelJobs {
    if ($env:CMAKE_BUILD_PARALLEL_LEVEL) {
        return [int]$env:CMAKE_BUILD_PARALLEL_LEVEL
    }
    # FreePhysicalMemory is in KB; 1 MB-of-KB == 1 GB.
    $os = Get-CimInstance Win32_OperatingSystem
    $freeGb = [math]::Floor($os.FreePhysicalMemory / 1MB)
    $cpus = [int]$env:NUMBER_OF_PROCESSORS
    $threads = [math]::Floor($freeGb * 10 / 25)
    if ($threads -lt 1) { $threads = 1 }
    elseif ($threads -gt $cpus) { $threads = $cpus }
    return [int]$threads
}
