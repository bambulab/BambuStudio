# Configure (via CMakePresets) and build BambuStudio on Windows.
#
# Uses the win-pixi-{debug,release} preset (Ninja + sccache). Ninja calls
# cl.exe / link.exe directly, so the script first sources Launch-VsDevShell.ps1
# to bring the MSVC toolchain onto PATH and set INCLUDE / LIB / LIBPATH from
# vcvars.
#
# Parallelism formula matches BuildLinux.sh:
#   MAX_THREADS = floor(FREE_MEM_GB / 2.5)
# Override with `$env:CMAKE_BUILD_PARALLEL_LEVEL = N` before `pixi run build`.
[CmdletBinding()]
param(
    [ValidateSet('release','relwithdebinfo')]
    [string]$BuildType = 'relwithdebinfo'
)

$ErrorActionPreference = 'Stop'
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# Locate VS Developer environment. -products '*' is required to pick up VS
# Build Tools installations, not just the IDE.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path $vswhere)) { throw "vswhere not found at $vswhere" }
$vsroot = & $vswhere -latest -products '*' -property installationPath
if (-not $vsroot) { throw "no Visual Studio installation found via vswhere" }
$devShell = Join-Path $vsroot 'Common7/Tools/Launch-VsDevShell.ps1'
if (-not (Test-Path $devShell)) { throw "Launch-VsDevShell.ps1 not found at $devShell" }
& $devShell -Arch amd64 -SkipAutomaticLocation | Out-Null
Write-Host "Using VS install: $vsroot"

. (Join-Path $scriptDir '_jobs.ps1')
$jobs = Get-PixiParallelJobs
$env:CMAKE_BUILD_PARALLEL_LEVEL = $jobs
Write-Host "Setting CMAKE_BUILD_PARALLEL_LEVEL=$jobs (mem-aware)"

$preset   = "win-pixi-$BuildType"
$buildDir = Join-Path $env:PIXI_PROJECT_ROOT "build/$BuildType"

# Only reconfigure on first build (or after rm -rf build/<type>). Re-running
# `cmake --preset` on every build can invalidate caches even when nothing
# changed; `cmake --build` re-invokes configure internally if CMakeLists.txt
# or CMakePresets.json actually changed. Mirror of scripts/pixi/build.sh.
if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
    & cmake --preset $preset
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }
}

& cmake --build --preset $preset --parallel $jobs
if ($LASTEXITCODE -ne 0) { throw "cmake build failed ($LASTEXITCODE)" }
