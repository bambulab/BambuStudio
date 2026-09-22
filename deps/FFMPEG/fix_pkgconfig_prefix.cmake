# The prebuilt FFmpeg archive for MSVC ships pkg-config files generated with --prefix=./dist:
#
#     prefix=./dist
#     libdir=./dist/lib
#     includedir=./dist/include
#
# A pkg-config that relocates prefix to the .pc file's location (pkgconfiglite, as used by CI) copes
# with that, but any other one returns ./dist/include, and CMake then refuses the relative path in
# PkgConfig::LIBAV's INTERFACE_INCLUDE_DIRECTORIES. Point the files at where they were installed.
#
# Usage: cmake -D PC_DIR=<dir with the .pc files> -D PREFIX=<install prefix> -P fix_pkgconfig_prefix.cmake

if (NOT PC_DIR OR NOT PREFIX)
    message(FATAL_ERROR "fix_pkgconfig_prefix.cmake needs PC_DIR and PREFIX")
endif ()

file(TO_CMAKE_PATH "${PREFIX}" _prefix)
file(GLOB _pc_files "${PC_DIR}/*.pc")
foreach (_pc ${_pc_files})
    file(READ "${_pc}" _content)
    # prefix gets the absolute path; every other variable is re-expressed relative to it.
    string(REGEX REPLACE "(^|\n)prefix=[^\n]*" "\\1prefix=${_prefix}" _content "${_content}")
    string(REPLACE "=./dist" "=\${prefix}" _content "${_content}")
    file(WRITE "${_pc}" "${_content}")
endforeach ()
