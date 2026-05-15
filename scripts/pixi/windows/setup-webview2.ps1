# Download the Microsoft.Web.WebView2 NuGet redistributable and install its
# headers/import-lib/runtime DLL into the pixi env layout
# ($CONDA_PREFIX/Library/{include,lib,bin}). BambuStudio's GUI code includes
# <WebView2.h> directly (src/slic3r/GUI/Widgets/WebView.cpp:21) and
# bambustudio_copy_dlls() expects WebView2Loader.dll alongside the binary.
#
# This SDK was vendored under deps/WebView2/ before the pixi migration; now
# fetched from the official redistributable so deps/ only holds bambulab forks
# (wxWidgets, libnoise) that aren't on conda-forge and that we patch.
#
# Run with `pixi run setup-webview2`.

$ErrorActionPreference = 'Stop'

if (-not $env:CONDA_PREFIX) { throw "CONDA_PREFIX must be set; run via 'pixi run'" }

# Pin to the version that was vendored last (a953442c9: "update WebView2 to
# 1.0.1418.22"). Bumping is a deliberate change -- newer SDKs occasionally
# drop interfaces BambuStudio's WebView.cpp relies on (e.g. ICoreWebView2_13).
$webView2Version = '1.0.1418.22'

$libRoot    = Join-Path $env:CONDA_PREFIX 'Library'
$dstInclude = Join-Path $libRoot 'include'
$dstLib     = Join-Path $libRoot 'lib'
$dstBin     = Join-Path $libRoot 'bin'

# Idempotency check: presence of WebView2.h with our pinned version content
# would be ideal, but a simple existence check is enough -- bumping the version
# requires deleting the marker manually or `pixi run --force-reinstall`.
$markerFile = Join-Path $dstInclude 'WebView2.h'
if (Test-Path $markerFile) {
    Write-Host "WebView2 SDK already installed in pixi env, skipping"
    exit 0
}

$tempDir    = Join-Path $env:TEMP "pixi-webview2-$webView2Version"
$nupkgPath  = Join-Path $tempDir "Microsoft.Web.WebView2.$webView2Version.nupkg"
$extractDir = Join-Path $tempDir 'extracted'

New-Item -ItemType Directory -Force -Path $tempDir | Out-Null
if (Test-Path $extractDir) { Remove-Item -Recurse -Force $extractDir }

$url = "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/$webView2Version"
Write-Host "Downloading Microsoft.Web.WebView2 $webView2Version ..."
# TLS 1.2 needed on older PowerShell; nuget.org rejects SSL3/TLS1.0.
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Invoke-WebRequest -Uri $url -OutFile $nupkgPath -UseBasicParsing

Write-Host "Extracting to $extractDir"
Expand-Archive -Path $nupkgPath -DestinationPath $extractDir -Force

# NuGet layout: build/native/{include,x64/}
$srcInclude = Join-Path $extractDir 'build/native/include'
$srcX64     = Join-Path $extractDir 'build/native/x64'

New-Item -ItemType Directory -Force -Path $dstInclude, $dstLib, $dstBin | Out-Null

Copy-Item -Force (Join-Path $srcInclude 'WebView2.h')                   $dstInclude
Copy-Item -Force (Join-Path $srcInclude 'WebView2EnvironmentOptions.h') $dstInclude
Copy-Item -Force (Join-Path $srcX64     'WebView2Loader.dll')           $dstBin
Copy-Item -Force (Join-Path $srcX64     'WebView2Loader.dll.lib')       $dstLib
Copy-Item -Force (Join-Path $srcX64     'WebView2LoaderStatic.lib')     $dstLib

Write-Host "WebView2 SDK $webView2Version installed to $libRoot"
