# Fully-open build driver (Windows).
#
# Builds the open-bamboo-networking plugin from the vendored submodule sources
# and stages it next to the Bambu Studio executable, where the FULLY_OPEN_BUILD
# patched loader picks it up.
#
# Usage:
#   .\build-fully-open.ps1                # build plugin + stage into build/
#   .\build-fully-open.ps1 -BuildStudio   # also run the upstream build_win.bat first
#
# Prerequisites:
#   * Studio: upstream Windows requirements (Visual Studio 2019 / v142).
#   * Plugin: MSVC toolset + vcpkg checkout with VCPKG_ROOT set.

[CmdletBinding()]
param(
    [string]$Configuration = "Release",
    [switch]$BuildStudio
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

# --- derive the plugin version from version.inc (first 8 chars must match) ---
$versionInc = Get-Content (Join-Path $root "version.inc") -Raw
if ($versionInc -notmatch 'SLIC3R_VERSION\s+"([^"]+)"') {
    throw "cannot parse SLIC3R_VERSION from version.inc"
}
$studioVersion = $Matches[1]
Write-Host "Studio version: $studioVersion"

$obn = Join-Path $root "open-bamboo-networking"
if (-not (Test-Path (Join-Path $obn "configure.ps1"))) {
    throw "submodule missing — run: git submodule update --init --recursive"
}

# --- 1) optional: build Studio with the upstream script ---
if ($BuildStudio) {
    Push-Location $root
    try {
        Write-Host "Building Bambu Studio (upstream build_win.bat)..."
        cmd /c build_win.bat
        if ($LASTEXITCODE -ne 0) { throw "build_win.bat failed with exit code $LASTEXITCODE" }
    } finally {
        Pop-Location
    }
}

# --- 2) build the open plugin from source ---
Push-Location $obn
try {
    if (-not (Test-Path "build")) {
        Write-Host "Configuring open-bamboo-networking for ABI $studioVersion..."
        ./configure.ps1 -ClientType bambu_studio -WithVersion $studioVersion
    }
    Write-Host "Building open-bamboo-networking..."
    cmake --build build --config $Configuration
} finally {
    Pop-Location
}

# --- 3) stage the plugin next to the built executable ---
$exe = Get-ChildItem (Join-Path $root "build") -Recurse -Filter "bambu-studio.exe" -ErrorAction SilentlyContinue |
       Select-Object -First 1
if (-not $exe) {
    throw "bambu-studio.exe not found under build\ — build Studio first (or pass -BuildStudio)"
}
$exeDir = $exe.DirectoryName

foreach ($dll in @("bambu_networking.dll", "BambuSource.dll", "live555.dll")) {
    $found = Get-ChildItem (Join-Path $obn "build") -Recurse -Filter $dll -ErrorAction SilentlyContinue |
             Where-Object { $_.FullName -notmatch '\\CMakeFiles\\' } |
             Select-Object -First 1
    if ($found) {
        Copy-Item $found.FullName (Join-Path $exeDir $dll) -Force
        Write-Host "staged $dll -> $exeDir"
    } elseif ($dll -ne "live555.dll") {
        Write-Warning "$dll not found in the plugin build output"
    }
}

Write-Host ""
Write-Host "Fully-open build staged. Run: $exeDir\bambu-studio.exe"
