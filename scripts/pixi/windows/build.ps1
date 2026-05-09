# Configure (via CMakePresets) and build BambuStudio on Windows.
#
# Picks the right configure preset based on the detected Visual Studio:
#   VS 2022 -> win-pixi
#   VS 2019 -> win-pixi-vs2019
#
# Parallelism formula matches BuildLinux.sh:
#   MAX_THREADS = floor(FREE_MEM_GB / 2.5)
# Override with `$env:CMAKE_BUILD_PARALLEL_LEVEL = N` before `pixi run build`.

$ErrorActionPreference = 'Stop'
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$preset = 'win-pixi'
if (Test-Path $vswhere) {
    $ver = & $vswhere -latest -products '*' -property installationVersion
    if ($ver -match '^16\.') { $preset = 'win-pixi-vs2019' }
}
Write-Host "Using preset: $preset"

. (Join-Path $scriptDir '_jobs.ps1')
$jobs = Get-PixiParallelJobs
$env:CMAKE_BUILD_PARALLEL_LEVEL = $jobs
Write-Host "Setting CMAKE_BUILD_PARALLEL_LEVEL=$jobs (mem-aware)"

# Only reconfigure on first build (or after `rm -rf build`). Re-running
# `cmake --preset` on every build invalidates the PCH and cascades into a
# rebuild of all libslic3r / libslic3r_gui TUs even when nothing changed
# (MSBuild's tracker compares per-obj recorded command lines, and the
# regenerated .vcxproj alone is enough to bust them). `cmake --build`
# already re-invokes configure internally via ZERO_CHECK if CMakeLists.txt
# or CMakePresets.json changed. Mirror of scripts/pixi/build.sh on Linux.
$buildDir = Join-Path $env:PIXI_PROJECT_ROOT 'build'
if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
    & cmake --preset $preset
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }
}

& cmake --build --preset $preset --parallel $jobs
if ($LASTEXITCODE -ne 0) { throw "cmake build failed ($LASTEXITCODE)" }
