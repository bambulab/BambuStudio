#!/bin/bash
set -euo pipefail
# The caller signs (and optionally notarizes) the built universal bundle first.
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 /path/to/BambuStudio.app /path/to/BambuStudio_macos-universal.dmg" >&2
    exit 2
fi
app=$1
output=$2
/usr/bin/codesign --verify --deep --strict "$app"
/usr/bin/lipo "$app/Contents/MacOS/BambuStudio" -verify_arch x86_64
/usr/bin/lipo "$app/Contents/MacOS/BambuStudio" -verify_arch arm64
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
/usr/bin/ditto "$app" "$stage/BambuStudio.app"
ln -s /Applications "$stage/Applications"
/usr/bin/hdiutil create -volname BambuStudio -srcfolder "$stage" -ov -format UDZO "$output"
