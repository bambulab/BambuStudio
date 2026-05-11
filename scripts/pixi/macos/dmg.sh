#!/usr/bin/env bash
# Wrap build/<type>/BambuStudio.app into a distributable DMG via dmgbuild.
#
# Argument (optional, default "release"): which build/<type>/ tree to wrap.

set -euo pipefail

build_type="${1:-release}"
case "$build_type" in
    debug|release) ;;
    *) echo "Unknown build type: $build_type (expected debug or release)" >&2; exit 1 ;;
esac

: "${PIXI_PROJECT_ROOT:?run via 'pixi run'}"

build_dir="$PIXI_PROJECT_ROOT/build/$build_type"
app="$build_dir/BambuStudio.app"
[[ -d "$app" ]] || { echo "$app not found. Run 'pixi run package-$build_type' first." >&2; exit 1; }

# shellcheck disable=SC1091
source "$PIXI_PROJECT_ROOT/packaging/macos/plist-vars.sh"

out="$build_dir/BambuStudio-${SLIC3R_VERSION}-arm64.dmg"
rm -f "$out"

DMG_APP_PATH="$app" dmgbuild \
    --settings "$PIXI_PROJECT_ROOT/packaging/macos/dmg_settings.py" \
    "BambuStudio" \
    "$out"

echo
ls -lh "$out"
