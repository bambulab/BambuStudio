# Generate intl.lib from intl-8.dll. conda-forge's libintl ships only the
# runtime DLL (no import library), but ffmpeg/cairo/glib's pkg-config files
# reference -lintl, so link.exe errors with:
#
#     LINK : fatal error LNK1181: cannot open input file 'intl.lib'
#
# Walk the DLL exports via dumpbin (from VS tools), build a .def, hand it to
# lib.exe to produce a pure-import-library intl.lib in $CONDA_PREFIX/Library/lib.
# Idempotent.

$ErrorActionPreference = 'Stop'

if (-not $env:CONDA_PREFIX)      { throw "CONDA_PREFIX must be set; run via 'pixi run'" }
if (-not $env:PIXI_PROJECT_ROOT) { throw "PIXI_PROJECT_ROOT must be set; run via 'pixi run'" }

$libDir = Join-Path $env:CONDA_PREFIX 'Library/lib'
$binDir = Join-Path $env:CONDA_PREFIX 'Library/bin'
$dst    = Join-Path $libDir 'intl.lib'
$dll    = Join-Path $binDir 'intl-8.dll'

if (Test-Path $dst) {
    Write-Host "intl.lib already present, skipping"
    exit 0
}
if (-not (Test-Path $dll)) {
    throw "intl-8.dll not found at $dll — is libintl resolved by pixi?"
}

# Locate VS Developer environment (lib.exe + dumpbin.exe live there).
# -products '*' is required to pick up VS Build Tools (otherwise only IDE).
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path $vswhere)) { throw "vswhere not found at $vswhere" }
$vsroot = & $vswhere -latest -products '*' -property installationPath
if (-not $vsroot) { throw "no Visual Studio installation found via vswhere" }
$devShell = Join-Path $vsroot 'Common7/Tools/Launch-VsDevShell.ps1'
if (-not (Test-Path $devShell)) { throw "Launch-VsDevShell.ps1 not found at $devShell" }

# Bring lib.exe + dumpbin.exe onto PATH for this script's process.
& $devShell -Arch amd64 -SkipAutomaticLocation | Out-Null

$workDir = Join-Path $env:PIXI_PROJECT_ROOT 'build/intl-bridge'
$defFile = Join-Path $workDir 'intl.def'
New-Item -ItemType Directory -Force -Path $workDir | Out-Null

# dumpbin /EXPORTS prints lines like:
#     <ord> <hint> <RVA> <name>
# parse the trailing name column.
$exports = @(& dumpbin.exe /EXPORTS $dll | ForEach-Object {
    if ($_ -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)') { $matches[1] }
})
if ($exports.Count -lt 1) { throw "no exports parsed from $dll" }

@('LIBRARY intl-8', 'EXPORTS') + $exports | Set-Content -Encoding ASCII $defFile

& lib.exe /nologo /def:$defFile /machine:x64 /out:$dst
if ($LASTEXITCODE -ne 0) { throw "lib.exe failed ($LASTEXITCODE)" }

Write-Host "Generated $dst from $dll ($($exports.Count) exports)"
