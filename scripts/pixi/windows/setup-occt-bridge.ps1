# Bridge conda-forge's OCCT DLL layout to what bambustudio_copy_dlls() expects.
#
# CMakeLists.txt:652-677 hardcodes file(COPY ${CMAKE_PREFIX_PATH}/bin/occt/TK*.dll ...)
# (legacy from the source-tree deps build). conda-forge installs OCCT DLLs to
# ${CONDA_PREFIX}/Library/bin/ with no occt/ subdirectory, so create a
# directory junction Library/bin/occt -> Library/bin once. Idempotent.

$ErrorActionPreference = 'Stop'

if (-not $env:CONDA_PREFIX) { throw "CONDA_PREFIX must be set; run via 'pixi run'" }

$binDir = Join-Path $env:CONDA_PREFIX 'Library/bin'
$occtDir = Join-Path $binDir 'occt'

if (Test-Path $occtDir) {
    Write-Host "Library/bin/occt already exists, skipping"
    exit 0
}

if (-not (Test-Path (Join-Path $binDir 'TKernel.dll'))) {
    throw "OCCT not found at $binDir/TKernel.dll — is `occt` resolved by pixi?"
}

# Junction (works without admin / dev mode, unlike SymbolicLink).
New-Item -ItemType Junction -Path $occtDir -Target $binDir | Out-Null
Write-Host "Created junction: $occtDir -> $binDir"
