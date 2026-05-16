# Launch the bambu-studio.exe built by `pixi run build`. Args pass through.
#
# The build itself stages a resources junction next to the exe via the
# post-build step in src/CMakeLists.txt, so no extra work is needed here --
# unlike the Linux flow which has to bridge a build/resources symlink for
# its in-tree resource lookup.

$ErrorActionPreference = 'Stop'

if (-not $env:PIXI_PROJECT_ROOT) { throw "PIXI_PROJECT_ROOT must be set; run via 'pixi run'" }

# Build type selected by the calling task via env (default debug, matching
# `pixi run build`'s default). Mirror of scripts/pixi/run.sh.
$buildType = if ($env:BAMBU_BUILD_TYPE) { $env:BAMBU_BUILD_TYPE } else { 'debug' }
$bin = Join-Path $env:PIXI_PROJECT_ROOT "build/$buildType/src/bambu-studio.exe"
if (-not (Test-Path $bin)) {
    # Legacy VS Generator path (win-pixi-vs2019) puts the exe at
    # build/src/Release/bambu-studio.exe; fall back to it for that flow.
    $alt = Join-Path $env:PIXI_PROJECT_ROOT 'build/src/Release/bambu-studio.exe'
    if (Test-Path $alt) { $bin = $alt }
    else { throw "$bin not found. Run 'pixi run build-$buildType' first." }
}

& $bin @args
exit $LASTEXITCODE
