<#
    Bundle the Release build into a Windows MSI installer.

    Compatible with Windows PowerShell 5.1 (the engine that the
    `powershell -NoProfile -ExecutionPolicy Bypass -File ...` invoker
    in pixi.toml resolves to). Avoid PS7-only features.

    1. Stage payload via `cmake --install` driving the existing WIN32
       install() rules in src/CMakeLists.txt:294-298 + CMakeLists.txt:807.
    2. Bootstrap WiX 4+ (`wix.exe` from dotnet tool) into the pixi env on
       first run — conda-forge has no `wix` package as of 2025.
    3. Run `wix build` against scripts/pixi/windows/BambuStudio.wxs.

    Output: build/<type>/dist/BambuStudio-<version>-x64.msi.
#>
[CmdletBinding()]
param(
    [ValidateSet('release','debug')]
    [string]$BuildType = 'release'
)
$ErrorActionPreference = 'Stop'

$root        = (Resolve-Path "$PSScriptRoot/../../..").Path
$buildDir    = Join-Path $root "build/$BuildType"
$payloadDir  = Join-Path $buildDir 'dist/payload/BambuStudio'
$msiOut      = Join-Path $buildDir 'dist'
$wxs         = Join-Path $PSScriptRoot 'BambuStudio.wxs'

if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
    throw "build not configured at $buildDir — run 'pixi run build-$BuildType' first"
}

# 1) Stage payload via cmake --install (drives WIN32 install() rules)
if (Test-Path $payloadDir) { Remove-Item -Recurse -Force $payloadDir }
New-Item -ItemType Directory -Force -Path $payloadDir | Out-Null

Write-Host "Staging payload -> $payloadDir"
& cmake --install $buildDir --prefix $payloadDir --config Release
if ($LASTEXITCODE) { throw "cmake --install failed ($LASTEXITCODE)" }

foreach ($f in 'bambu-studio.exe', 'BambuStudio.dll', 'resources') {
    if (-not (Test-Path (Join-Path $payloadDir $f))) {
        throw "payload missing: $f (cmake --install did not stage expected output)"
    }
}

# 2) Bootstrap WiX into the pixi env if not already present
$wixExe = Join-Path $env:CONDA_PREFIX 'Library/bin/wix.exe'
if (-not (Test-Path $wixExe)) {
    Write-Host "Installing WiX 4+ via dotnet tool -> $wixExe"
    & dotnet tool install --tool-path (Join-Path $env:CONDA_PREFIX 'Library/bin') wix
    if ($LASTEXITCODE) { throw "dotnet tool install wix failed ($LASTEXITCODE)" }
}

# 3) Resolve version: version.inc -> git describe -> 0.0.0
$version = $null
$versionInc = Join-Path $root 'version.inc'
if (Test-Path $versionInc) {
    $m = Select-String -Path $versionInc -Pattern 'SLIC3R_VERSION\s+"([^"]+)"' |
         Select-Object -First 1
    if ($m) { $version = $m.Matches[0].Groups[1].Value }
}
if (-not $version) {
    $version = (& git -C $root describe --tags --always 2>$null) -replace '^v',''
}
if (-not $version) { $version = '0.0.0' }

# Windows Installer requires N.N.N.N (each part 0-65535)
$digits = ($version -replace '[^0-9.]','').Trim('.').Split('.') |
          ForEach-Object { [int]$_ }
while ($digits.Count -lt 4) { $digits += 0 }
$msiVersion = ($digits[0..3] -join '.')

# 4) Build MSI
New-Item -ItemType Directory -Force -Path $msiOut | Out-Null
$msiPath = Join-Path $msiOut "BambuStudio-$msiVersion-x64.msi"

Write-Host "Building MSI -> $msiPath"
& $wixExe build $wxs `
    -arch x64 `
    -d "Payload=$payloadDir" `
    -d "Version=$msiVersion" `
    -o $msiPath
if ($LASTEXITCODE) { throw "wix build failed ($LASTEXITCODE)" }

$sizeMB = (Get-Item $msiPath).Length / 1MB
Write-Host ""
Write-Host "MSI: $msiPath" -ForegroundColor Green
Write-Host ("Size: {0:N1} MB" -f $sizeMB)
