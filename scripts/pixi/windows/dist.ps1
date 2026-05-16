<#
    Bundle the Release build into a Windows MSI installer:
      1. cmake --install drives the WIN32 install() rules.
      2. Bundle every DLL from the conda env (the source of truth for
         runtime deps).
      3. Generate _payload.wxs (one explicit Component per file).
      4. wix build emits the MSI.

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
# Pin to WiX 5: WiX 7 (the dotnet-tool default since 2025) gates
# production use behind the OSMF EULA. WiX 5 retains the modern
# bind-path + Files Include syntax we rely on, no EULA prompt.
$wixVersion = '5.0.2'
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

# 5) Generate _payload.wxs: explicit Directory tree + one Component per
# file. WiX 5's `<Files Include="**" />` emits duplicate sibling
# <Directory Name="X"> entries when a folder mixes loose files with
# subdirectories, which breaks install-time file extraction. Declaring
# every Directory once with a stable Id sidesteps the auto-merge.
$iconDir = Join-Path $root 'resources/images'
New-Item -ItemType Directory -Force -Path $msiOut | Out-Null
$msiPath     = Join-Path $msiOut "BambuStudio-$msiVersion-x64.msi"
$payloadWxs  = Join-Path $msiOut '_payload.wxs'

function New-WixId([string]$prefix, [string]$relPath) {
    # Stable 12-hex-char id derived from the relative path. MSI
    # Identifier rules: <=72 chars, must start with letter/underscore.
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $hash = [System.BitConverter]::ToString($sha.ComputeHash(
        [System.Text.Encoding]::UTF8.GetBytes($relPath.ToLowerInvariant())
    )).Replace('-','').Substring(0, 12).ToLowerInvariant()
    return "${prefix}_${hash}"
}

Write-Host "Generating $payloadWxs"
$payloadRoot = (Get-Item $payloadDir).FullName.TrimEnd('\')

$dirInfo = @{}     # FullName -> @{ Id, Name, ParentFullName, RelPath }
$dirInfo[$payloadRoot] = @{ Id = 'INSTALLFOLDER'; Name = ''; ParentFullName = $null; RelPath = '' }
foreach ($d in (Get-ChildItem -LiteralPath $payloadRoot -Recurse -Directory)) {
    $rel = $d.FullName.Substring($payloadRoot.Length).TrimStart('\')
    $dirInfo[$d.FullName] = @{
        Id             = (New-WixId 'd' $rel)
        Name           = $d.Name
        ParentFullName = $d.Parent.FullName
        RelPath        = $rel
    }
}

$dirChildren = @{}
foreach ($k in $dirInfo.Keys) {
    $parent = $dirInfo[$k].ParentFullName
    if ($null -eq $parent) { continue }
    if (-not $dirChildren.ContainsKey($parent)) { $dirChildren[$parent] = @() }
    $dirChildren[$parent] += $k
}

$treeLines = New-Object System.Collections.Generic.List[string]
function Emit-DirTree($parentFullName, $depth) {
    if (-not $dirChildren.ContainsKey($parentFullName)) { return }
    $indent = '  ' * $depth
    foreach ($childFullName in ($dirChildren[$parentFullName] | Sort-Object)) {
        $info = $dirInfo[$childFullName]
        $hasGrandchildren = $dirChildren.ContainsKey($childFullName)
        if ($hasGrandchildren) {
            $treeLines.Add("$indent<Directory Id=`"$($info.Id)`" Name=`"$($info.Name)`">")
            Emit-DirTree $childFullName ($depth + 1)
            $treeLines.Add("$indent</Directory>")
        } else {
            $treeLines.Add("$indent<Directory Id=`"$($info.Id)`" Name=`"$($info.Name)`" />")
        }
    }
}
Emit-DirTree $payloadRoot 4

$compLines = New-Object System.Collections.Generic.List[string]
$fileCount = 0
foreach ($f in (Get-ChildItem -LiteralPath $payloadRoot -Recurse -File)) {
    $rel    = $f.FullName.Substring($payloadRoot.Length).TrimStart('\')
    $dirId  = $dirInfo[$f.Directory.FullName].Id
    $cid    = New-WixId 'c' $rel
    [void]$compLines.Add("      <Component Id=`"$cid`" Directory=`"$dirId`" Guid=`"*`" Bitness=`"always64`">")
    [void]$compLines.Add("        <File Source=`"!(bindpath.payload)\$rel`" KeyPath=`"yes`" />")
    [void]$compLines.Add('      </Component>')
    $fileCount++
}

$payloadWxsContent = @(
    '<?xml version="1.0" encoding="UTF-8"?>'
    '<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs">'
    '  <Fragment>'
    '    <DirectoryRef Id="INSTALLFOLDER">'
    $treeLines
    '    </DirectoryRef>'
    '    <ComponentGroup Id="BambuStudioFiles">'
    $compLines
    '    </ComponentGroup>'
    '  </Fragment>'
    '</Wix>'
) -join "`r`n"
[System.IO.File]::WriteAllText($payloadWxs, $payloadWxsContent, [System.Text.UTF8Encoding]::new($false))
Write-Host "  $($dirInfo.Count) directories, $fileCount components written"

Write-Host "Building MSI -> $msiPath"
& $wixExe build $wxs $payloadWxs `
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
