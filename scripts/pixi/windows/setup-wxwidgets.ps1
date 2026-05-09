# Build the bambulab/wxWidgets fork via deps/ ExternalProject and install
# the result into the active pixi env so the main BambuStudio configure can
# link against wxWidgets via FindwxWidgets (WXWIN=$CONDA_PREFIX/Library).
#
# WebView is enabled (DEP_WX_WEBVIEW=ON) — on MSVC this picks WebView Edge
# (wxUSE_WEBVIEW_EDGE=ON in wxWidgets.cmake:15). BambuStudio.cpp pulls in
# slic3r/GUI/* unconditionally so there's no real "GUI=OFF" build.
#
# Run with `pixi run setup-wxwidgets`.

$ErrorActionPreference = 'Stop'
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

if (-not $env:PIXI_PROJECT_ROOT) { throw "PIXI_PROJECT_ROOT must be set; run via 'pixi run'" }
if (-not $env:CONDA_PREFIX)      { throw "CONDA_PREFIX must be set; run via 'pixi run'" }

$libRoot = Join-Path $env:CONDA_PREFIX 'Library'
# wxWidgets MSW install layout: include/wx/wx.h directly (no wx-3.x/ on Windows).
if (Test-Path (Join-Path $libRoot 'include/wx/wx.h')) {
    Write-Host "wxWidgets already installed in pixi env, skipping"
    exit 0
}

# Pick the newest installed Visual Studio via vswhere; fall back to VS 2022.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsGen = 'Visual Studio 17 2022'
if (Test-Path $vswhere) {
    $ver = & $vswhere -latest -products '*' -property installationVersion
    if ($ver -match '^16\.') { $vsGen = 'Visual Studio 16 2019' }
}

# Separate build dir from libnoise so CMake caches don't collide.
$buildDir = Join-Path $env:PIXI_PROJECT_ROOT 'build/deps-build-wx'
$destDir  = Join-Path $buildDir 'destdir'
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $buildDir 'CMakeCache.txt')
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue (Join-Path $buildDir 'CMakeFiles')
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

# wxWidgets's vendored cotire helper has try_compile tests with
# cmake_minimum_required(VERSION 2.x). Export the policy floor as env so
# the nested try_compile inherits it.
$env:CMAKE_POLICY_VERSION_MINIMUM = '3.5'

$cmakeArgs = @(
    (Join-Path $env:PIXI_PROJECT_ROOT 'deps'),
    '-G', $vsGen, '-A', 'x64',
    '-DDEP_BUILD_BOOST=OFF', '-DDEP_BUILD_TBB=OFF', '-DDEP_BUILD_CURL=OFF',
    '-DDEP_BUILD_OPENSSL=OFF', '-DDEP_BUILD_GLFW=OFF', '-DDEP_BUILD_BLOSC=OFF',
    '-DDEP_BUILD_FREETYPE=OFF', '-DDEP_BUILD_FFMPEG=OFF',
    '-DDEP_BUILD_PNG=OFF', '-DDEP_BUILD_JPEG=OFF', '-DDEP_BUILD_TIFF=OFF',
    '-DDEP_BUILD_WXWIDGETS=ON',
    '-DDEP_WX_WEBVIEW=ON',
    "-DDESTDIR=$destDir"
)

Push-Location $buildDir
try {
    & cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }

    . (Join-Path $scriptDir '_jobs.ps1')
    $jobs = Get-PixiParallelJobs
    Write-Host "Building dep_wxWidgets with -j$jobs"
    & cmake --build . --target dep_wxWidgets --config Release -j $jobs
    if ($LASTEXITCODE -ne 0) { throw "cmake build dep_wxWidgets failed ($LASTEXITCODE)" }
}
finally {
    Pop-Location
}

# Merge destdir/usr/local/* into $CONDA_PREFIX/Library. wxWidgets MSW lays out
# bin/, include/wx-3.x/, lib/vc_x64_lib/, lib/cmake/wxWidgets/ — all under usr/local.
$src = Join-Path $destDir 'usr/local'
Copy-Item -Path (Join-Path $src '*') -Destination $libRoot -Recurse -Force
Write-Host "wxWidgets installed to $libRoot"
