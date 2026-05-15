# Bridge conda-forge's OCCT DLL layout to what bambustudio_copy_dlls() expects.
#
# Two mismatches to paper over:
#  1. CMakeLists.txt:652-677 reads ${CMAKE_PREFIX_PATH}/bin/occt/TK*.dll, but
#     conda-forge installs OCCT DLLs to ${CONDA_PREFIX}/Library/bin/ with no
#     occt/ subdirectory. Fixed with a directory junction Library/bin/occt
#     -> Library/bin (junction, not SymbolicLink -- works without admin / dev
#     mode).
#  2. OCCT 7.8+ collapsed the per-format Data Exchange DLLs (TKSTEP,
#     TKSTEP209, TKSTEPAttr, TKSTEPBase, TKXDESTEP) into a single TKDESTEP.dll.
#     bambustudio_copy_dlls() still expects the legacy names. Stage copies of
#     TKDESTEP.dll under those names so file(COPY) succeeds -- they're inert
#     at runtime since the import libs link against TKDESTEP.
#
# Idempotent.

$ErrorActionPreference = 'Stop'

if (-not $env:CONDA_PREFIX) { throw "CONDA_PREFIX must be set; run via 'pixi run'" }

$binDir = Join-Path $env:CONDA_PREFIX 'Library/bin'
$occtDir = Join-Path $binDir 'occt'

if (-not (Test-Path (Join-Path $binDir 'TKernel.dll'))) {
    throw "OCCT not found at $binDir/TKernel.dll -- is `occt` resolved by pixi?"
}

if (-not (Test-Path $occtDir)) {
    New-Item -ItemType Junction -Path $occtDir -Target $binDir | Out-Null
    Write-Host "Created junction: $occtDir -> $binDir"
}

# Stage TKDESTEP.dll copies under the legacy split-DLL names that
# bambustudio_copy_dlls() still references. Junction makes them appear under
# bin/occt/ too -- single source of truth.
$tkDestep = Join-Path $binDir 'TKDESTEP.dll'
$aliases = @('TKSTEP.dll', 'TKSTEP209.dll', 'TKSTEPAttr.dll', 'TKSTEPBase.dll', 'TKXDESTEP.dll')
foreach ($a in $aliases) {
    $dst = Join-Path $binDir $a
    if (-not (Test-Path $dst)) {
        Copy-Item -Path $tkDestep -Destination $dst
        Write-Host "Aliased $a -> TKDESTEP.dll"
    }
}
