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

& cmake --preset $preset
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }

& cmake --build --preset $preset --parallel $jobs
if ($LASTEXITCODE -ne 0) { throw "cmake build failed ($LASTEXITCODE)" }
