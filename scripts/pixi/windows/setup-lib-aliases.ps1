# Copy libfoo.lib -> foo.lib for the deps where conda-forge's binary name
# differs from what pkg-config emits as -lfoo. ffmpeg/cairo/glib pull bare
# -lmp3lame / -lx265 references; without these copies link.exe errors with:
#
#     LINK : fatal error LNK1181: cannot open input file 'mp3lame.lib'
#     LINK : fatal error LNK1181: cannot open input file 'x265.lib'
#
# Idempotent. Extend the @aliases list as new mismatches surface.

$ErrorActionPreference = 'Stop'

if (-not $env:CONDA_PREFIX) { throw "CONDA_PREFIX must be set; run via 'pixi run'" }

$libDir = Join-Path $env:CONDA_PREFIX 'Library/lib'

# from -> to (relative to $libDir)
$aliases = @(
    @{ src = 'libmp3lame.lib'; dst = 'mp3lame.lib' },
    @{ src = 'libx265.lib';    dst = 'x265.lib'    }
)

foreach ($a in $aliases) {
    $src = Join-Path $libDir $a.src
    $dst = Join-Path $libDir $a.dst
    if (Test-Path $dst) {
        continue
    }
    if (-not (Test-Path $src)) {
        Write-Warning "$($a.src) not found; skipping alias to $($a.dst)"
        continue
    }
    Copy-Item -Path $src -Destination $dst
    Write-Host "Aliased $($a.dst) -> $($a.src)"
}
