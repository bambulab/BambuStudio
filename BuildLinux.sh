#!/bin/bash

export ROOT=$(dirname $(readlink -f ${0}))

set -e # exit on first error

function check_available_memory_and_disk() {
    FREE_MEM_GB=$(free -g -t | grep 'Mem' | rev | cut -d" " -f1 | rev)
    MIN_MEM_GB=10

    FREE_DISK_KB=$(df -k . | tail -1 | awk '{print $4}')
    MIN_DISK_KB=$((10 * 1024 * 1024))

    if [ ${FREE_MEM_GB} -le ${MIN_MEM_GB} ]; then
        echo -e "\nERROR: Bambu Studio Builder requires at least ${MIN_MEM_GB}G of 'available' mem (systen has only ${FREE_MEM_GB}G available)"
        echo && free -h && echo
        exit 2
    fi

    if [[ ${FREE_DISK_KB} -le ${MIN_DISK_KB} ]]; then 
        echo -e "\nERROR: Bambu Studio Builder requires at least $(echo $MIN_DISK_KB |awk '{ printf "%.1fG\n", $1/1024/1024; }') (systen has only $(echo ${FREE_DISK_KB} | awk '{ printf "%.1fG\n", $1/1024/1024; }') disk free)"
        echo && df -h . && echo
        exit 1
    fi
}

function usage() {
    echo "Usage: ./BuildLinux.sh [-1][-b][-c][-d][-i][-r][-s][-u]"
    echo "   -1: limit builds to 1 core (where possible)"
    echo "   -f: disable safe parallel number limit(By default, the maximum number of parallels is set to free memory/2.5)"
    echo "   -b: build in debug mode"
    echo "   -c: force a clean build"
    echo "   -d: build deps (optional)"
    echo "   -h: this help output"
    echo "   -i: Generate appimage (optional)"
    echo "   -r: skip ram and disk checks (low ram compiling)"
    echo "   -s: build bambu-studio (optional)"
    echo "   -u: update and build dependencies (optional and need sudo)"
    echo "For a first use, you want to 'sudo ./BuildLinux.sh -u'"
    echo "   and then './BuildLinux.sh -dsi'"
}

unset name
while getopts ":1fbcdghirsu" opt; do
  case ${opt} in
    1 )
        export CMAKE_BUILD_PARALLEL_LEVEL=1
        ;;
    f )
        DISABLE_PARALLEL_LIMIT=1
        ;;
    b )
        BUILD_DEBUG="1"
        ;;
    c )
        CLEAN_BUILD=1
        ;;
    d )
        BUILD_DEPS="1"
        ;;
    h ) usage
        exit 0
        ;;
    i )
        BUILD_IMAGE="1"
        ;;
    r )
	    SKIP_RAM_CHECK="1"
	;;
    s )
        BUILD_BAMBU_STUDIO="1"
        ;;
    u )
        UPDATE_LIB="1"
        ;;
  esac
done

if [ ${OPTIND} -eq 1 ]
then
    usage
    exit 0
fi

DISTRIBUTION=$(awk -F= '/^ID=/ {print $2}' /etc/os-release)
VERSION=$(awk -F= '/^VERSION_ID=/ {print $2}' /etc/os-release)
# OSLIKE is a space-delineated list of similar distributions
OSLIKE=$(awk -F= '/^ID_LIKE=/ {print $2}' /etc/os-release | tr -d '"')

# Iterate over a list of candidate distribution targets, first match is used
for CANDIDATE in ${DISTRIBUTION} ${OSLIKE}; do
    if [ -f ./linux.d/${CANDIDATE} ]
    then 
        TARGET_DISTRO="${CANDIDATE}"
        break
    fi
done

if [ -z ${TARGET_DISTRO} ]
then
    echo "Your distribution does not appear to be currently supported by these build scripts"
    exit 1
fi

echo "OS distribution is '${DISTRIBUTION}'.  Using package dependencies for '${TARGET_DISTRO}'."
source ./linux.d/${TARGET_DISTRO}

echo "FOUND_GTK3=${FOUND_GTK3}"
if [[ -z "${FOUND_GTK3_DEV}" ]]
then
    echo "Error, you must install the dependencies before."
    echo "Use option -u with sudo"
    exit 1
fi

echo "Changing date in version..."
{
    # change date in version
    sed -i "s/+UNKNOWN/_$(date '+%F')/" version.inc
}
echo "done"

if ! [[ -n "${SKIP_RAM_CHECK}" ]]
then
    check_available_memory_and_disk
fi

if ! [[ -n "${DISABLE_PARALLEL_LIMIT}" ]]
then
    FREE_MEM_GB=$(free -g -t | grep 'Mem' | rev | cut -d" " -f1 | rev)
    MAX_THREADS=$((FREE_MEM_GB * 10 / 25))
    if [ "$MAX_THREADS" -lt 1 ]; then
        export CMAKE_BUILD_PARALLEL_LEVEL=1
    else
        export CMAKE_BUILD_PARALLEL_LEVEL=${MAX_THREADS}
    fi
    echo "System free memory: ${FREE_MEM_GB} GB"
    echo "Setting CMAKE_BUILD_PARALLEL_LEVEL: ${CMAKE_BUILD_PARALLEL_LEVEL}"
fi

if [[ -n "${BUILD_DEPS}" ]]
then
    echo "Configuring dependencies..."
    BUILD_ARGS="-DDEP_WX_GTK3=ON"
    if [[ -n "${CLEAN_BUILD}" ]]
    then
        rm -fr deps/build
    fi
    if [ ! -d "deps/build" ]
    then
        mkdir deps/build
    fi
    if [[ -n "${BUILD_DEBUG}" ]]
    then
        # have to build deps with debug & release or the cmake won't find everything it needs
        mkdir deps/build/release
        cmake -S deps -B deps/build/release -G Ninja -DDESTDIR="../destdir" ${BUILD_ARGS}
        cmake --build deps/build/release
        BUILD_ARGS="${BUILD_ARGS} -DCMAKE_BUILD_TYPE=Debug"
    fi

    echo "cmake -S deps -B deps/build -G Ninja ${BUILD_ARGS}"
    cmake -S deps -B deps/build -G Ninja ${BUILD_ARGS}
    cmake --build deps/build
fi

if [[ -n "${BUILD_BAMBU_STUDIO}" ]]
then
    echo "Configuring BambuStudio..."
    if [[ -n "${CLEAN_BUILD}" ]]
    then
        rm -fr build
    fi
    BUILD_ARGS=""
    if [[ -n "${FOUND_GTK3_DEV}" ]]
    then
        BUILD_ARGS="-DSLIC3R_GTK=3"
    fi
    if [[ -n "${BUILD_DEBUG}" ]]
    then
        BUILD_ARGS="${BUILD_ARGS} -DCMAKE_BUILD_TYPE=Debug -DBBL_INTERNAL_TESTING=1"
    else
        BUILD_ARGS="${BUILD_ARGS} -DBBL_RELEASE_TO_PUBLIC=1 -DBBL_INTERNAL_TESTING=0"
    fi
    echo -e "cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="${PWD}/deps/build/destdir/usr/local" -DSLIC3R_STATIC=1 ${BUILD_ARGS}"
    cmake -S . -B build -G Ninja \
        -DCMAKE_PREFIX_PATH="${PWD}/deps/build/destdir/usr/local" \
        -DSLIC3R_STATIC=1 \
        ${BUILD_ARGS}
    echo "done"
    echo "Building BambuStudio ..."
    cmake --build build --target BambuStudio
    echo "done"
fi

if [[ -e ${ROOT}/build/src/BuildLinuxImage.sh ]]; then
# Give proper permissions to script
chmod 755 ${ROOT}/build/src/BuildLinuxImage.sh

echo "[9/9] Generating Linux app..."
    pushd build
        if [[ -n "${BUILD_IMAGE}" ]]
        then
            ${ROOT}/build/src/BuildLinuxImage.sh -i
        fi
    popd
echo "done"
fi
