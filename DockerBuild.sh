#!/bin/bash
PROJECT_ROOT=$(cd -P -- "$(dirname -- "$0")" && printf '%s\n' "$(pwd -P)")

set -e

function usage() {
    echo "Usage: ./DockerBuild.sh [-c][-d][-i][-v]"
    echo "   -c: Build a self-contained Docker image that can be run directly"
    echo "   -d: disable safe parallel number limit(By default, the maximum number of parallels is set to free memory/2.5)"
    echo "   -i: Build and export an AppImage"
    echo "   -v: Build System Version:ubu22 or ubu24"
    echo "   -h: this help output"
    echo "If you only need to run the program on a built Docker container, just use './DockerBuild.sh -c'"
    echo "If you need to build an AppImage using Docker, first run './DockerBuild.sh -d', then run './DockerBuild.sh -s'."
}

unset name
while getopts "hcdiv:" opt; do
  case ${opt} in
    c )
        BUILD_RUNNER=1
        ;;
    d )
        BUILD_DEPS=1
        ;;
    i )
        BUILD_APPIMAGE=1
        ;;
    v )
        SYSTEM_VERSION="$OPTARG"
        ;;
    h ) usage
        exit 0
        ;;
  esac
done

if [ -z "$SYSTEM_VERSION" ]; then
  SYSTEM_VERSION="ubu22"
fi

if [[ -n "${BUILD_DEPS}" ]]; then
  if [ "$SYSTEM_VERSION" == "ubu22" ]; then
    echo "Building dependencies for Ubuntu 22.04..."
    docker build -f docker/BuildDepsDockerfile -t studio_dep_22:1.0 .
  else
    docker build -f docker/BuildDepsDockerfile24 -t studio_dep_24:1.0 .
  fi
fi

if [[ -n "${BUILD_APPIMAGE}" ]]; then
  if [ "$SYSTEM_VERSION" == "ubu22" ]; then
    docker build -f docker/BuildAppimageDockerfile --build-arg VERSION=studio_dep_22 -o type=local,dest=./build .
    mv build/BambuStudio_ubu64.AppImage build/BambuStudio_ubu22.AppImage
  else
    docker build -f docker/BuildAppimageDockerfile --build-arg VERSION=studio_dep_24 -o type=local,dest=./build .
    mv build/BambuStudio_ubu64.AppImage build/BambuStudio_ubu24.AppImage
  fi
fi

if [[ -n "${BUILD_RUNNER}" ]]
then
  # Wishlist hint:  For developers, creating a Docker Compose 
  # setup with persistent volumes for the build & deps directories
  # would speed up recompile times significantly.  For end users,
  # the simplicity of a single Docker image and a one-time compilation
  # seems better.
  docker build -t bambustudio \
    --build-arg USER=${USER:-root} \
    --build-arg UID=$(id -u) \
    --build-arg GID=$(id -g) \
    $PROJECT_ROOT
fi