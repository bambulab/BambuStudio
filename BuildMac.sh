#!/bin/bash

set -e
set -o pipefail

while getopts "1dpa:st:xbc:h" opt; do
  case "${opt}" in
    d )
        export BUILD_TARGET="deps"
        ;;
    p )
        export PACK_DEPS="1"
        ;;
    a )
        export ARCH="$OPTARG"
        ;;
    s )
        export BUILD_TARGET="slicer"
        ;;
    t )
        export OSX_DEPLOYMENT_TARGET="$OPTARG"
        ;;
    x )
        export SLICER_CMAKE_GENERATOR="Ninja"
        export SLICER_BUILD_TARGET="all"
        export DEPS_CMAKE_GENERATOR="Ninja"
        ;;
    b )
        export BUILD_ONLY="1"
        ;;
    c )
        export BUILD_CONFIG="$OPTARG"
        ;;
    1 )
        export CMAKE_BUILD_PARALLEL_LEVEL=1
        ;;
    h ) echo "Usage: ./BuildMac.sh [-1][-d][-s][-x][-b][-c]"
        echo "   -d: Build deps"
        echo "   -a: Set ARCHITECTURE (arm64 or x86_64 or universal)"
        echo "   -s: Build slicer only"
        echo "   -t: Specify minimum version of the target platform, default is 10.15"
        echo "   -x: Use Ninja CMake generator, default is Xcode"
        echo "   -b: Build without reconfiguring CMake"
        echo "   -c: Set CMake build configuration, default is Release"
        echo "   -1: limit builds to 1 core (where possible)"
        exit 0
        ;;
    * )
        ;;
  esac
done

if [ -z "$ARCH" ]; then
    ARCH="$(uname -m)"
    export ARCH
fi

if [ -z "$BUILD_CONFIG" ]; then
  export BUILD_CONFIG="Release"
fi

if [ -z "$BUILD_TARGET" ]; then
  export BUILD_TARGET="all"
fi

if [ -z "$SLICER_CMAKE_GENERATOR" ]; then
  export SLICER_CMAKE_GENERATOR="Xcode"
fi

if [ -z "$SLICER_BUILD_TARGET" ]; then
  export SLICER_BUILD_TARGET="ALL_BUILD"
fi

if [ -z "$DEPS_CMAKE_GENERATOR" ]; then
  export DEPS_CMAKE_GENERATOR="Unix Makefiles"
fi

if [ -z "$OSX_DEPLOYMENT_TARGET" ]; then
  export OSX_DEPLOYMENT_TARGET="10.15"
fi

echo "Build params:"
echo " - ARCH: $ARCH"
echo " - BUILD_CONFIG: $BUILD_CONFIG"
echo " - BUILD_TARGET: $BUILD_TARGET"
echo " - CMAKE_GENERATOR: $SLICER_CMAKE_GENERATOR for Slicer, $DEPS_CMAKE_GENERATOR for deps"
echo " - OSX_DEPLOYMENT_TARGET: $OSX_DEPLOYMENT_TARGET"
echo " - CMAKE_BUILD_PARALLEL_LEVEL: $CMAKE_BUILD_PARALLEL_LEVEL" 
echo

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_BUILD_DIR="$PROJECT_DIR/build/$ARCH"
DEPS_DIR="$PROJECT_DIR/deps"
DEPS_BUILD_DIR="$DEPS_DIR/build/$ARCH"
DEPS="$DEPS_BUILD_DIR/BambuStudio_deps"

if [ "$SLICER_CMAKE_GENERATOR" == "Xcode" ]; then
    export BUILD_DIR_CONFIG_SUBDIR="/$BUILD_CONFIG"
else
    export BUILD_DIR_CONFIG_SUBDIR=""
fi

function build_deps() {
    # iterate over two architectures: x86_64 and arm64
    for _ARCH in x86_64 arm64; do
        # if ARCH is universal or equal to _ARCH
        if [ "$ARCH" == "universal" ] || [ "$ARCH" == "$_ARCH" ]; then

            PROJECT_BUILD_DIR="$PROJECT_DIR/build/$_ARCH"
            DEPS_BUILD_DIR="$DEPS_DIR/build/$_ARCH"
            DEPS="$DEPS_BUILD_DIR/BambuStudio_deps"

            echo "Building deps..."
            (
                set -x
                mkdir -p "$DEPS"
                cd "$DEPS_BUILD_DIR"
                if [ "1." != "$BUILD_ONLY". ]; then
                    cmake "${DEPS_DIR}" \
                        -G "${DEPS_CMAKE_GENERATOR}" \
                        -DDESTDIR="$DEPS" \
                        -DOPENSSL_ARCH="darwin64-${_ARCH}-cc" \
                        -DCMAKE_BUILD_TYPE="$BUILD_CONFIG" \
                        -DCMAKE_OSX_ARCHITECTURES:STRING="${_ARCH}" \
                        -DCMAKE_OSX_DEPLOYMENT_TARGET="${OSX_DEPLOYMENT_TARGET}"
                fi
                cmake --build . --parallel ${CMAKE_BUILD_PARALLEL_LEVEL} --config "$BUILD_CONFIG" --target deps
            )
        fi
    done
}

function pack_deps() {
    echo "Packing deps..."
    (
        set -x
        cd "$DEPS_DIR"
        tar -zcvf "BambuStudio_dep_mac_${ARCH}_$(date +"%Y%m%d").tar.gz" "build"
    )
}

function build_slicer() {
    # iterate over two architectures: x86_64 and arm64
    for _ARCH in x86_64 arm64; do
        # if ARCH is universal or equal to _ARCH
        if [ "$ARCH" == "universal" ] || [ "$ARCH" == "$_ARCH" ]; then

            PROJECT_BUILD_DIR="$PROJECT_DIR/build/$_ARCH"
            DEPS_BUILD_DIR="$DEPS_DIR/build/$_ARCH"
            DEPS="$DEPS_BUILD_DIR/BambuStudio_deps"

            echo "Building slicer for $_ARCH..."
            (
                set -x
            mkdir -p "$PROJECT_BUILD_DIR"
            cd "$PROJECT_BUILD_DIR"
            if [ "1." != "$BUILD_ONLY". ]; then
                cmake "${PROJECT_DIR}" \
                    -G "${SLICER_CMAKE_GENERATOR}" \
                    -DBBL_RELEASE_TO_PUBLIC=1 \
                    -DBBL_INTERNAL_TESTING=0 \
                    -DCMAKE_PREFIX_PATH="$DEPS/usr/local" \
                    -DCMAKE_INSTALL_PREFIX="$PWD/BambuStudio" \
                    -DCMAKE_BUILD_TYPE="$BUILD_CONFIG" \
                    -DCMAKE_MACOSX_RPATH=ON \
                    -DCMAKE_INSTALL_RPATH="${DEPS}/usr/local" \
                    -DCMAKE_MACOSX_BUNDLE=ON \
                    -DCMAKE_OSX_ARCHITECTURES="${_ARCH}" \
                    -DCMAKE_OSX_DEPLOYMENT_TARGET="${OSX_DEPLOYMENT_TARGET}"
            fi
            cmake --build . --config "$BUILD_CONFIG" --target "$SLICER_BUILD_TARGET"
        )


        echo "Fix macOS app package..."
        (
            cd "$PROJECT_BUILD_DIR"
            mkdir -p BambuStudio
            cd BambuStudio
            # remove previously built app
            rm -rf ./BambuStudio.app
            # fully copy newly built app
            cp -pR "../src$BUILD_DIR_CONFIG_SUBDIR/BambuStudio.app" ./BambuStudio.app
            # fix resources
            resources_path=$(readlink ./BambuStudio.app/Contents/Resources)
            rm ./BambuStudio.app/Contents/Resources
            cp -R "$resources_path" ./BambuStudio.app/Contents/Resources
            # delete .DS_Store file
            find ./BambuStudio.app/ -name '.DS_Store' -delete
        )

    fi
    done
}

function build_universal() {
    echo "Building universal binary..."

    PROJECT_BUILD_DIR="$PROJECT_DIR/build/$ARCH"
    
    # Create universal binary
    echo "Creating universal binary..."
    # PROJECT_BUILD_DIR="$PROJECT_DIR/build_Universal"
    mkdir -p "$PROJECT_BUILD_DIR/BambuStudio"
    UNIVERSAL_APP="$PROJECT_BUILD_DIR/BambuStudio/BambuStudio.app"
    rm -rf "$UNIVERSAL_APP"
    cp -R "$PROJECT_DIR/build/arm64/BambuStudio/BambuStudio.app" "$UNIVERSAL_APP"
    
    # Get the binary path inside the .app bundle
    BINARY_PATH="Contents/MacOS/BambuStudio"
    
    # Create universal binary using lipo
    lipo -create \
        "$PROJECT_DIR/build/x86_64/BambuStudio/BambuStudio.app/$BINARY_PATH" \
        "$PROJECT_DIR/build/arm64/BambuStudio/BambuStudio.app/$BINARY_PATH" \
        -output "$UNIVERSAL_APP/$BINARY_PATH"
        
    echo "Universal binary created at $UNIVERSAL_APP"
}

case "${BUILD_TARGET}" in
    all)
        build_deps
        build_slicer
        ;;
    deps)
        build_deps
        ;;
    slicer)
        build_slicer
        ;;
    *)
        echo "Unknown target: $BUILD_TARGET. Available targets: deps, slicer, all."
        exit 1
        ;;
esac

if [ "$ARCH" = "universal" ] && [ "$BUILD_TARGET" != "deps" ]; then
    build_universal
fi

if [ "1." == "$PACK_DEPS". ]; then
    pack_deps
fi