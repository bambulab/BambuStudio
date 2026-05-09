# Build the bambulab/libnoise fork via deps/ ExternalProject and install
# the result into the active pixi env, so the main configure can find it
# via a single-valued CMAKE_PREFIX_PATH=$CONDA_PREFIX/Library
# (Windows mirror of setup-libnoise.sh).
#
# Run with `pixi run setup-libnoise`.

$ErrorActionPreference = 'Stop'
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

if (-not $env:PIXI_PROJECT_ROOT) { throw "PIXI_PROJECT_ROOT must be set; run via 'pixi run'" }
if (-not $env:CONDA_PREFIX)      { throw "CONDA_PREFIX must be set; run via 'pixi run'" }

$libRoot = Join-Path $env:CONDA_PREFIX 'Library'
$existing = Get-ChildItem -Path (Join-Path $libRoot 'lib') -Filter 'libnoise_static*' -ErrorAction SilentlyContinue
if ($existing -and (Test-Path (Join-Path $libRoot 'include/libnoise'))) {
    Write-Host "libnoise already installed in pixi env, skipping"
    exit 0
}

# Pick the newest installed Visual Studio via vswhere; fall back to VS 2022.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsGen = 'Visual Studio 17 2022'
if (Test-Path $vswhere) {
    $ver = & $vswhere -latest -property installationVersion
    if ($ver -match '^16\.') { $vsGen = 'Visual Studio 16 2019' }
}

$buildDir = Join-Path $env:PIXI_PROJECT_ROOT 'build/deps-build'
$destDir  = Join-Path $buildDir 'destdir'
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

# Splat as an array so dotted version values (e.g. =3.5) survive PowerShell's
# external-command argument tokenization unambiguously.
$cmakeArgs = @(
    (Join-Path $env:PIXI_PROJECT_ROOT 'deps'),
    '-G', $vsGen, '-A', 'x64',
    '-DDEP_BUILD_BOOST=OFF', '-DDEP_BUILD_TBB=OFF', '-DDEP_BUILD_CURL=OFF',
    '-DDEP_BUILD_OPENSSL=OFF', '-DDEP_BUILD_GLFW=OFF', '-DDEP_BUILD_BLOSC=OFF',
    '-DDEP_BUILD_FREETYPE=OFF', '-DDEP_BUILD_WXWIDGETS=OFF', '-DDEP_BUILD_FFMPEG=OFF',
    '-DDEP_BUILD_PNG=OFF', '-DDEP_BUILD_JPEG=OFF', '-DDEP_BUILD_TIFF=OFF',
    "-DDESTDIR=$destDir",
    '-DCMAKE_POLICY_VERSION_MINIMUM=3.5'
)

Push-Location $buildDir
try {
    & cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }

    . (Join-Path $scriptDir '_jobs.ps1')
    $jobs = Get-PixiParallelJobs
    Write-Host "Building dep_libnoise with -j$jobs"
    & cmake --build . --target dep_libnoise --config Release -j $jobs
    if ($LASTEXITCODE -ne 0) { throw "cmake build dep_libnoise failed ($LASTEXITCODE)" }
}
finally {
    Pop-Location
}

# Merge destdir/usr/local/* into $CONDA_PREFIX/Library (conda-forge layout on win).
$src = Join-Path $destDir 'usr/local'
Copy-Item -Path (Join-Path $src '*') -Destination $libRoot -Recurse -Force
Write-Host "libnoise installed to $libRoot"
