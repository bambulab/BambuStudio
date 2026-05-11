#
# Builds the two BambuStudio forks that have no conda-forge package:
#   - libnoise (bambulab fork; no public binary)
#   - wxWidgets (bambulab fork; custom WebView/GUI patches)
#
# Driven by scripts/pixi/{setup-libnoise.sh,setup-wxwidgets.sh,macos/setup-wxwidgets.sh,
# windows/setup-libnoise.ps1,windows/setup-wxwidgets.ps1}, which pass
# -DDEP_BUILD_WXWIDGETS=ON/OFF and the destdir. Everything else (Boost,
# CGAL, OCCT, OpenCV, ...) comes from conda-forge via pixi.
#

project(BambuStudio-deps)
cmake_minimum_required(VERSION 3.13)

include(ExternalProject)
include(ProcessorCount)

ProcessorCount(NPROC)
if(DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL})
    set(NPROC $ENV{CMAKE_BUILD_PARALLEL_LEVEL})
endif()
if (NPROC EQUAL 0)
    set(NPROC 1)
endif ()

option(DEP_BUILD_WXWIDGETS "Compile wxWidgets" ON)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    option(DEP_WX_GTK3 "Build wxWidgets against GTK3" OFF)
endif()
option(DEP_WX_WEBVIEW "Build wxWidgets with WebView" OFF)

set(DESTDIR "${CMAKE_CURRENT_BINARY_DIR}/destdir" CACHE PATH "Destination directory")
set(DEP_DOWNLOAD_DIR ${CMAKE_CURRENT_BINARY_DIR} CACHE PATH "Path for downloaded source packages.")

set(IS_CROSS_COMPILE FALSE)

if (APPLE)
    set(CMAKE_FIND_FRAMEWORK LAST)
    set(CMAKE_FIND_APPBUNDLE LAST)
    list(FIND CMAKE_OSX_ARCHITECTURES ${CMAKE_SYSTEM_PROCESSOR} _arch_idx)
    if (CMAKE_OSX_ARCHITECTURES AND _arch_idx LESS 0)
        set(IS_CROSS_COMPILE TRUE)
        set(CMAKE_CXX_COMPILER_ID "Clang")
        string(REPLACE ";" "$<SEMICOLON>" CMAKE_OSX_ARCHS "${CMAKE_OSX_ARCHITECTURES}")
        set(_cmake_osx_arch -DCMAKE_OSX_ARCHITECTURES:STRING=${CMAKE_OSX_ARCHS})
        message(STATUS "Detect Cross-compilation. Will build for target ${CMAKE_OSX_ARCHS}")
    endif ()
endif ()

message(STATUS "BambuStudio deps DESTDIR: ${DESTDIR}")
message(STATUS "BambuStudio download dir for source packages: ${DEP_DOWNLOAD_DIR}")

find_package(Git REQUIRED)
set(PATCH_CMD ${GIT_EXECUTABLE} apply --verbose --ignore-space-change --whitespace=fix)

get_property(_is_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

if (NOT _is_multi AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
    message(STATUS "Forcing CMAKE_BUILD_TYPE to Release as it was not specified.")
endif ()

execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --is-inside-work-tree
    RESULT_VARIABLE REV_PARSE_RESULT
    OUTPUT_VARIABLE REV_PARSE_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if((REV_PARSE_RESULT EQUAL 0) AND (REV_PARSE_OUTPUT STREQUAL "true"))
    file(RELATIVE_PATH BINARY_DIR_REL  ${CMAKE_SOURCE_DIR}/.. ${CMAKE_BINARY_DIR})
endif ()

function(bambustudio_add_cmake_project projectname)
    cmake_parse_arguments(P_ARGS "" "INSTALL_DIR;BUILD_COMMAND;INSTALL_COMMAND" "CMAKE_ARGS" ${ARGN})

    set(_configs_line -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE})
    if (_is_multi OR MSVC)
        set(_configs_line "")
    endif ()

    set(_gen "")
    set(_build_j "-j${NPROC}")
    if (MSVC)
        set(_gen CMAKE_GENERATOR "${DEP_MSVC_GEN}" CMAKE_GENERATOR_PLATFORM "${DEP_PLATFORM}")
        set(_build_j "/m")
    endif ()

    ExternalProject_Add(
        dep_${projectname}
        EXCLUDE_FROM_ALL    ON
        INSTALL_DIR         ${DESTDIR}/usr/local
        DOWNLOAD_DIR        ${DEP_DOWNLOAD_DIR}/${projectname}
        ${_gen}
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX:STRING=${DESTDIR}/usr/local
            -DCMAKE_MODULE_PATH:STRING=${PROJECT_SOURCE_DIR}/../cmake/modules
            -DCMAKE_PREFIX_PATH:STRING=${DESTDIR}/usr/local
            -DCMAKE_POLICY_VERSION_MINIMUM:STRING=3.5
            -DCMAKE_DEBUG_POSTFIX:STRING=d
            -DCMAKE_C_COMPILER:STRING=${CMAKE_C_COMPILER}
            -DCMAKE_CXX_COMPILER:STRING=${CMAKE_CXX_COMPILER}
            -DCMAKE_TOOLCHAIN_FILE:STRING=${CMAKE_TOOLCHAIN_FILE}
            -DBUILD_SHARED_LIBS:BOOL=OFF
            ${_cmake_osx_arch}
            "${_configs_line}"
            ${DEP_CMAKE_OPTS}
            ${P_ARGS_CMAKE_ARGS}
       ${P_ARGS_UNPARSED_ARGUMENTS}
       BUILD_COMMAND ${CMAKE_COMMAND} --build . --config Release -- ${_build_j}
       INSTALL_COMMAND ${CMAKE_COMMAND} --build . --target install --config Release
    )
endfunction(bambustudio_add_cmake_project)

include(libnoise/libnoise.cmake)

set(WXWIDGETS_PKG "")
if (DEP_BUILD_WXWIDGETS)
    include(wxWidgets/wxWidgets.cmake)
    set(WXWIDGETS_PKG dep_wxWidgets)
endif ()

add_custom_target(deps ALL DEPENDS dep_libnoise ${WXWIDGETS_PKG})
