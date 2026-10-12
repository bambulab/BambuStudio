#!/bin/sh
# Fully-open build driver (Linux / macOS).
#
# Builds the open-bamboo-networking plugin from the vendored submodule sources
# and stages it next to the Bambu Studio executable, where the FULLY_OPEN_BUILD
# patched loader picks it up.
#
# Build Bambu Studio itself first with the upstream instructions
# (BuildLinux.sh / BuildMac.sh), then run this script.
#
# Prerequisites (plugin): CMake >= 3.20, a C++17 compiler, pkg-config, and the
# development packages listed in open-bamboo-networking/BUILDING.md. libmosquitto
# and libcurl are vendored/fetched by its build automatically.

set -eu

root=$(cd "$(dirname "$0")" && pwd)

# --- derive the plugin version from version.inc ---
studio_version=$(sed -n 's/.*SLIC3R_VERSION[[:space:]]*"\([^"]*\)".*/\1/p' "$root/version.inc" | head -n 1)
if [ -z "$studio_version" ]; then
    echo "cannot parse SLIC3R_VERSION from version.inc" >&2
    exit 1
fi
echo "Studio version: $studio_version"

obn="$root/open-bamboo-networking"
if [ ! -f "$obn/configure" ]; then
    echo "submodule missing — run: git submodule update --init --recursive" >&2
    exit 1
fi

# --- 1) build the open plugin from source ---
(
    cd "$obn"
    if [ ! -d build ]; then
        echo "Configuring open-bamboo-networking for ABI $studio_version..."
        ./configure --client-type=bambu_studio --with-version="$studio_version"
    fi
    echo "Building open-bamboo-networking..."
    cmake --build build --parallel
)

# --- 2) locate the Studio executable in the build tree ---
exe=$(find "$root/build" -type f -name "bambu-studio" ! -path "*/open-bamboo-networking/*" 2>/dev/null | head -n 1)
if [ -z "$exe" ]; then
    echo "bambu-studio not found under build/ — build Studio first" >&2
    exit 1
fi
exe_dir=$(dirname "$exe")

# --- 3) stage the plugin next to the executable ---
for lib in libbambu_networking.so libBambuSource.so liblive555.so libbambu_networking.dylib libBambuSource.dylib; do
    found=$(find "$obn/build" -type f -name "$lib" 2>/dev/null | head -n 1)
    if [ -n "$found" ]; then
        cp -f "$found" "$exe_dir/"
        echo "staged $lib -> $exe_dir"
    fi
done

echo ""
echo "Fully-open build staged. Run: $exe_dir/bambu-studio"
