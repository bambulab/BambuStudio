# Launch the bambu-studio.exe built by `pixi run build`. Args pass through.
#
# The build itself stages a resources junction next to the exe via the
# post-build step in src/CMakeLists.txt, so no extra work is needed here —
# unlike the Linux flow which has to bridge a build/resources symlink for
# its in-tree resource lookup.

$ErrorActionPreference = 'Stop'

if (-not $env:PIXI_PROJECT_ROOT) { throw "PIXI_PROJECT_ROOT must be set; run via 'pixi run'" }

# build/release/src/bambu-studio.exe under the Ninja single-config win-pixi
# preset. The legacy VS Generator path (win-pixi-vs2019) puts the exe at
# build/src/Release/bambu-studio.exe instead — fall back to that.
$bin = Join-Path $env:PIXI_PROJECT_ROOT 'build/release/src/bambu-studio.exe'
if (-not (Test-Path $bin)) {
    $alt = Join-Path $env:PIXI_PROJECT_ROOT 'build/src/Release/bambu-studio.exe'
    if (Test-Path $alt) { $bin = $alt }
    else { throw "$bin not found. Run 'pixi run build' first." }
}

& $bin @args
exit $LASTEXITCODE
