<#
    Bundle the Release build into a Windows MSI installer:
      1. cmake --install drives the WIN32 install() rules.
      2. Bundle every DLL from the conda env (the source of truth for
         runtime deps).
      3. wix build emits the MSI.

    Output: build/<type>/dist/BambuStudio-<version>-x64.msi.

    Targets Windows PowerShell 5.1 (the engine pixi.toml's `powershell`
    invoker resolves to). Avoid PS7-only features.
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
    throw "build not configured at $buildDir -- run 'pixi run build-$BuildType' first"
}

# 1) Stage payload via cmake --install.
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

# 2) Bundle every .dll from the conda env's Library/bin into the payload.
# Static import walkers (dumpbin / objdump) miss DLLs that conda libs
# (notably OpenCV) pull in via runtime LoadLibrary. The env is the
# curated source of truth -- copy it whole. ~80 MB raw, no PE parser
# required, future-proof against dependency drift.
Write-Host "Copying env DLLs into payload..."
Copy-Item -Path (Join-Path $env:CONDA_PREFIX 'Library/bin/*.dll') -Destination $payloadDir -Force

# 3) Bootstrap WiX into the pixi env if not already present.
# WiX 7 fixed the v5 <Files Include="**"> Directory-dedup bug (issue
# #8608); accept the OSMF EULA non-interactively per FireGiant guide.
$wixVersion = '7.0.0'
$wixExe = Join-Path $env:CONDA_PREFIX 'Library/bin/wix.exe'
$installedVer = $null
if (Test-Path $wixExe) {
    $installedVer = (& $wixExe --version 2>$null) -split '\+' | Select-Object -First 1
}
if ($installedVer -ne $wixVersion) {
    Write-Host "Installing WiX $wixVersion via dotnet tool -> $wixExe"
    if (Test-Path $wixExe) {
        & dotnet tool uninstall --tool-path (Join-Path $env:CONDA_PREFIX 'Library/bin') wix
    }
    & dotnet tool install --tool-path (Join-Path $env:CONDA_PREFIX 'Library/bin') --version $wixVersion wix
    if ($LASTEXITCODE) { throw "dotnet tool install wix failed ($LASTEXITCODE)" }
    # Idempotent EULA acceptance (writes a per-user marker).
    & $wixExe eula accept wix7 | Out-Null
}

# 4) Resolve version: version.inc -> git describe -> 0.0.0
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

# 5) Build MSI.
$iconDir = Join-Path $root 'resources/images'
New-Item -ItemType Directory -Force -Path $msiOut | Out-Null
$msiPath = Join-Path $msiOut "BambuStudio-$msiVersion-x64.msi"

Write-Host "Building MSI -> $msiPath"
& $wixExe build $wxs `
    -arch x64 `
    -bindpath "payload=$payloadDir" `
    -bindpath "icon=$iconDir" `
    -d "Version=$msiVersion" `
    -o $msiPath
if ($LASTEXITCODE) { throw "wix build failed ($LASTEXITCODE)" }

# 6) Sanity-check the built MSI: assert File entries exist and no
# duplicate Directory siblings sneaked back in.
$decompiled = Join-Path $msiOut '_verify.wxs'
& $wixExe msi decompile $msiPath -o $decompiled | Out-Null
if ($LASTEXITCODE) { throw "wix msi decompile failed ($LASTEXITCODE)" }

$decompiledText = Get-Content $decompiled -Raw
$fileCount = ([regex]::Matches($decompiledText, '<File\s')).Count
if ($fileCount -lt 100) {
    throw "MSI verification failed: only $fileCount File entries (expected thousands). Bind path likely did not resolve."
}

[xml]$msiXml = $decompiledText
$ns = New-Object System.Xml.XmlNamespaceManager($msiXml.NameTable)
$ns.AddNamespace('w', 'http://wixtoolset.org/schemas/v4/wxs')
$dirParents = $msiXml.SelectNodes("//w:Directory[w:Directory] | //w:StandardDirectory[w:Directory]", $ns)
$siblingDupes = @()
foreach ($parent in $dirParents) {
    $childNames = @($parent.Directory) | ForEach-Object { $_.Name }
    $localDupes = $childNames | Group-Object | Where-Object { $_.Count -gt 1 } | ForEach-Object { $_.Name }
    foreach ($d in $localDupes) {
        $parentId = if ($parent.Id) { $parent.Id } else { '<root>' }
        $siblingDupes += "$parentId/$d"
    }
}
if ($siblingDupes) {
    throw "MSI verification failed: duplicate sibling Directory Names (parent/name): $($siblingDupes -join ', ')"
}

Remove-Item $decompiled -Force

$sizeMB = (Get-Item $msiPath).Length / 1MB
Write-Host ""
Write-Host "MSI: $msiPath" -ForegroundColor Green
Write-Host ("Size: {0:N1} MB ({1} files)" -f $sizeMB, $fileCount)
