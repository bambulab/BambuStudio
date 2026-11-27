#!/usr/bin/env bash
# Regenerates resources/Assets.car from the Icon Composer source
# resources/images/BambuStudio.icon.
#
# Assets.car carries the layered app icon used by macOS 26 and later
# (light, dark, tinted and clear appearances). Older macOS versions keep
# using resources/Icon.icns via CFBundleIconFile.
#
# Requires Xcode 26 or later (actool must understand .icon files), so the
# compiled Assets.car is checked in and CI does not need a newer Xcode.
# Run this after editing BambuStudio.icon in Icon Composer.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ICON="$ROOT/resources/images/BambuStudio.icon"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

xcrun actool "$ICON" \
    --compile "$OUT" \
    --app-icon BambuStudio \
    --include-all-app-icons \
    --enable-on-demand-resources NO \
    --development-region en \
    --target-device mac \
    --minimum-deployment-target 10.15 \
    --platform macosx \
    --output-partial-info-plist "$OUT/partial.plist" \
    --errors --warnings --output-format human-readable-text

cp "$OUT/Assets.car" "$ROOT/resources/Assets.car"
echo "Wrote resources/Assets.car"
