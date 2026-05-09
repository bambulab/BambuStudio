# Launch the bambu-studio.exe built by `pixi run build`. Args pass through.
#
# The build itself stages a build/src/Release/resources junction via the
# post-build step in src/CMakeLists.txt, so no extra work is needed here —
# unlike the Linux flow which has to bridge a build/resources symlink for
# its in-tree resource lookup.

$ErrorActionPreference = 'Stop'

if (-not $env:PIXI_PROJECT_ROOT) { throw "PIXI_PROJECT_ROOT must be set; run via 'pixi run'" }

$bin = Join-Path $env:PIXI_PROJECT_ROOT 'build/src/Release/bambu-studio.exe'
if (-not (Test-Path $bin)) {
    throw "$bin not found. Run 'pixi run build' first."
}

& $bin @args
exit $LASTEXITCODE
