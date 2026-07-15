# Build OpenMeshCraft boolean backend for Studio (win / mac / linux).
# Layout (minimal, OpenCSG-style):
#   OpenMeshCraft.cmake              - ExternalProject entry + prepare overlays
#   CMakeLists.txt.in                - upstream root overlay + wrapper install
#   OpenMeshCraftConfig.cmake.in     - Config installed into dependency prefix
#   wrapper/                         - C ABI sources
#
# Install tree (CMAKE_PREFIX_PATH):
#   include/OpenMeshCraft/OpenMeshCraftBoolean.h
#   lib/OpenMeshCraftBoolean.lib|.a (+ upstream OpenMeshCraft + shewchuk)
#   lib/cmake/OpenMeshCraft/OpenMeshCraftConfig.cmake
#
# Source is fetched as a GitHub commit zip (not git+submodules):
# upstream .gitmodules pulls full CGAL, whose deep doc paths exceed Windows
# MAX_PATH under the deps ExternalProject tree. Boolean build (OMC_BUILD_TEST=OFF)
# does not need CGAL/googletest; prepare only fills empty submodule placeholders
# (eigen / parallel-hashmap / oneTBB) and vendors {fmt} for GCC9 / old Apple libc++.
# Linux/mac/AVX compatibility lives in the fetched OpenMeshCraft zip
# (bambulab/OpenMeshCraft @0e8d12c3, merged PR #2 OMC::format);
# do not mutate unzipped .h/.cpp here.

set(_OMC_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(_OMC_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/omc_gen")
file(MAKE_DIRECTORY "${_OMC_GEN_DIR}")

# ---- prepare_source.cmake: fill zip submodule placeholders (+ fmt) ----
file(WRITE "${_OMC_GEN_DIR}/prepare_source.cmake" [=[
cmake_minimum_required(VERSION 3.13)
if(NOT DEFINED OMC_SOURCE_DIR)
  message(FATAL_ERROR "OMC_SOURCE_DIR must be set")
endif()

function(omc_fetch_zip url sha256 out_dir strip_prefix marker_file)
  if(EXISTS "${out_dir}/${marker_file}")
    message(STATUS "[OpenMeshCraft] Already present: ${out_dir}")
    return()
  endif()
  set(_workdir "${OMC_SOURCE_DIR}/.omc_fetch")
  file(MAKE_DIRECTORY "${_workdir}")
  set(_archive "${_workdir}/${strip_prefix}.zip")
  message(STATUS "[OpenMeshCraft] Downloading ${url}")
  file(DOWNLOAD "${url}" "${_archive}" EXPECTED_HASH SHA256=${sha256} SHOW_PROGRESS STATUS _st)
  list(GET _st 0 _code)
  if(NOT _code EQUAL 0)
    list(GET _st 1 _msg)
    message(FATAL_ERROR "[OpenMeshCraft] Download failed: ${_msg}")
  endif()
  set(_extract "${_workdir}/${strip_prefix}_extract")
  file(REMOVE_RECURSE "${_extract}")
  file(MAKE_DIRECTORY "${_extract}")
  execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf "${_archive}"
    WORKING_DIRECTORY "${_extract}" RESULT_VARIABLE _tar_rc)
  if(NOT _tar_rc EQUAL 0)
    message(FATAL_ERROR "[OpenMeshCraft] Failed to extract ${_archive}")
  endif()
  set(_src "${_extract}/${strip_prefix}")
  if(NOT EXISTS "${_src}/${marker_file}")
    message(FATAL_ERROR "[OpenMeshCraft] Extracted tree missing ${marker_file}: ${_src}")
  endif()
  get_filename_component(_parent "${out_dir}" DIRECTORY)
  file(MAKE_DIRECTORY "${_parent}")
  file(REMOVE_RECURSE "${out_dir}")
  file(RENAME "${_src}" "${out_dir}")
  message(STATUS "[OpenMeshCraft] Installed ${out_dir}")
endfunction()

# Zip only has empty submodule placeholders for these; never pull CGAL.
omc_fetch_zip(
  "https://gitlab.com/libeigen/eigen/-/archive/68f4e58cfacc686583d16cff90361f0b43bc2c1b/eigen-68f4e58cfacc686583d16cff90361f0b43bc2c1b.zip"
  "41D12F1FC8E18606F39312681C6E1B957A4746ED97676C576E23E73554C6A662"
  "${OMC_SOURCE_DIR}/external/eigen"
  "eigen-68f4e58cfacc686583d16cff90361f0b43bc2c1b"
  "CMakeLists.txt")
omc_fetch_zip(
  "https://github.com/greg7mdp/parallel-hashmap/archive/154c63489e84d5569d3b466342a2ae8fd99e4734.zip"
  "29D0C510018D9BDAA5FBECAF95C2EAA616027D568E4A631D363B27620DE4E07B"
  "${OMC_SOURCE_DIR}/external/parallel-hashmap"
  "parallel-hashmap-154c63489e84d5569d3b466342a2ae8fd99e4734"
  "CMakeLists.txt")
omc_fetch_zip(
  "https://github.com/oneapi-src/oneTBB/archive/d75ea937f7e480bd7775d7dab224e0b38451bb40.zip"
  "FE22388FB9435DCEFA66C8189D4A8FBD8D968FDA3A3DC4F578F78A3DD201C68E"
  "${OMC_SOURCE_DIR}/external/oneTBB"
  "oneTBB-d75ea937f7e480bd7775d7dab224e0b38451bb40"
  "CMakeLists.txt")

# Header-only {fmt} 10.2.1. OpenMeshCraft ConfigureFmt.cmake uses external/fmt
# when present and sets FMT_CONSTEVAL=; do not patch fmt headers here.
omc_fetch_zip(
  "https://github.com/fmtlib/fmt/archive/refs/tags/10.2.1.zip"
  "D368F9C39A33A3AEF800F5BE372EC1DF1C12AD57ADA1F60ADC62F24C0E348469"
  "${OMC_SOURCE_DIR}/external/fmt"
  "fmt-10.2.1"
  "include/fmt/format.h")

message(STATUS "[OpenMeshCraft] External placeholders ready (no source patches)")
]=])

# ---- external/CMakeLists.txt overlay (GMP/MPFR/TBB) ----
file(WRITE "${_OMC_GEN_DIR}/external_CMakeLists.txt" [=[
# Studio overlay for OpenMeshCraft/external/CMakeLists.txt
if(WIN32)
  set(GMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/gmp/windows")
  list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/gmp/windows/lib/cmake")
  if(NOT GMP_FOUND AND NOT TARGET GMP)
    find_package(GMP REQUIRED NO_MODULE)
    if(NOT TARGET GMP)
      message(FATAL_ERROR "[OpenMeshCraft] GMP target not found.")
    endif()
  elseif(GMP_FOUND AND NOT TARGET GMP)
    add_library(GMP SHARED IMPORTED GLOBAL)
    set_target_properties(GMP PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${GMP_INCLUDE_DIR}"
      IMPORTED_LOCATION "${GMP_SHARED_LIBRARIES}"
      IMPORTED_IMPLIB "${GMP_LIBRARIES}")
  endif()
else()
  if(NOT TARGET GMP)
    find_path(OMC_STUDIO_GMP_INCLUDE_DIR NAMES gmp.h)
    find_library(OMC_STUDIO_GMP_LIBRARY NAMES gmp libgmp)
    if(NOT OMC_STUDIO_GMP_INCLUDE_DIR OR NOT OMC_STUDIO_GMP_LIBRARY)
      message(FATAL_ERROR "[OpenMeshCraft] Studio GMP not found in CMAKE_PREFIX_PATH.")
    endif()
    add_library(GMP STATIC IMPORTED GLOBAL)
    set_target_properties(GMP PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${OMC_STUDIO_GMP_INCLUDE_DIR}"
      IMPORTED_LOCATION "${OMC_STUDIO_GMP_LIBRARY}")
  endif()
endif()

if(WIN32)
  set(MPFR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mpfr/windows")
  list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/mpfr/windows/lib/cmake")
  if(NOT MPFR_FOUND AND NOT TARGET MPFR)
    find_package(MPFR REQUIRED NO_MODULE)
    if(NOT TARGET MPFR)
      message(FATAL_ERROR "[OpenMeshCraft] MPFR target not found.")
    endif()
  elseif(MPFR_FOUND AND NOT TARGET MPFR)
    add_library(MPFR SHARED IMPORTED GLOBAL)
    set_target_properties(MPFR PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${MPFR_INCLUDE_DIR}"
      IMPORTED_LOCATION "${MPFR_SHARED_LIBRARIES}"
      IMPORTED_IMPLIB "${MPFR_LIBRARIES}")
  endif()
else()
  if(NOT TARGET MPFR)
    find_path(OMC_STUDIO_MPFR_INCLUDE_DIR NAMES mpfr.h)
    find_library(OMC_STUDIO_MPFR_LIBRARY NAMES mpfr libmpfr)
    if(NOT OMC_STUDIO_MPFR_INCLUDE_DIR OR NOT OMC_STUDIO_MPFR_LIBRARY)
      message(FATAL_ERROR "[OpenMeshCraft] Studio MPFR not found in CMAKE_PREFIX_PATH.")
    endif()
    add_library(MPFR STATIC IMPORTED GLOBAL)
    set_target_properties(MPFR PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${OMC_STUDIO_MPFR_INCLUDE_DIR}"
      IMPORTED_LOCATION "${OMC_STUDIO_MPFR_LIBRARY}")
  endif()
endif()

if(OMC_BUILD_TEST)
  if(NOT CGAL_FOUND)
    set(CGAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cgal/lib/cmake/CGAL)
    set(Boost_DEBUG OFF)
    set(Boost_NO_WARN_NEW_VERSIONS ON)
    set(Boost_USE_STATIC_LIBS ON)
    find_package(Boost 1.78.0 CONFIG REQUIRED)
    set(CGAL_Boost_USE_STATIC_LIBS ON)
    find_package(CGAL REQUIRED)
  endif()
endif()

if(NOT TARGET Eigen3)
  add_library(Eigen3 INTERFACE IMPORTED GLOBAL)
  set_target_properties(Eigen3 PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/eigen")
endif()

if(OMC_BUILD_TEST)
  if(NOT TARGET gtest_main)
    if(CMAKE_HOST_WIN32)
      set(gtest_force_shared_crt ON CACHE BOOL "")
    endif()
    set(BUILD_GMOCK OFF)
    set(INSTALL_GTEST OFF)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/googletest EXCLUDE_FROM_ALL)
  endif()
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/parallel-hashmap EXCLUDE_FROM_ALL)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/shewchuk-predicates EXCLUDE_FROM_ALL)

if(TBB_FOUND AND TARGET tbb)
elseif(NOT TBB_FOUND AND TARGET tbb)
elseif(TARGET TBB::tbb AND NOT TARGET tbb)
  add_library(tbb INTERFACE IMPORTED GLOBAL)
  set_target_properties(tbb PROPERTIES INTERFACE_LINK_LIBRARIES TBB::tbb)
elseif(TBB_FOUND AND NOT TARGET tbb)
  add_library(tbb SHARED IMPORTED GLOBAL)
  set_target_properties(tbb PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${TBB_INCLUDE_DIR}"
    IMPORTED_LOCATION "${TBB_SHARED_LIBRARIES}"
    IMPORTED_IMPLIB "${TBB_LIBRARIES}")
else()
  find_package(TBB QUIET CONFIG)
  if(TARGET TBB::tbb AND NOT TARGET tbb)
    add_library(tbb INTERFACE IMPORTED GLOBAL)
    set_target_properties(tbb PROPERTIES INTERFACE_LINK_LIBRARIES TBB::tbb)
  else()
    set(TBB_TEST OFF CACHE BOOL "")
    set(TBBMALLOC_BUILD OFF CACHE BOOL "")
    if(WIN32 AND "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
      set(TBB_WARNING_SUPPRESS -Wno-stringop-overflow -Wno-unused-value -Wno-array-bounds)
    endif()
    if(NOT BUILD_SHARED_LIBS)
      set(BUILD_SHARED_LIBS ON)
      add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/oneTBB EXCLUDE_FROM_ALL)
      set(BUILD_SHARED_LIBS OFF)
    else()
      add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/oneTBB EXCLUDE_FROM_ALL)
    endif()
    set_property(TARGET tbb PROPERTY VERSION)
    set_property(TARGET tbb PROPERTY SOVERSION)
  endif()
endif()

set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} PARENT_SCOPE)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} PARENT_SCOPE)
]=])

set(_OMC_DEPENDS DEPENDS)
if(BOOST_PKG)
  list(APPEND _OMC_DEPENDS ${BOOST_PKG})
endif()
list(APPEND _OMC_DEPENDS dep_TBB)
if(GMP_PKG)
  list(APPEND _OMC_DEPENDS ${GMP_PKG})
endif()
if(MPFR_PKG)
  list(APPEND _OMC_DEPENDS ${MPFR_PKG})
endif()

set(_OMC_SIMD_ARGS "")
if(APPLE)
  set(_OMC_SIMD_ARGS
    -DOMC_CMAKE_ENABLE_SSE2=OFF
    -DOMC_CMAKE_ENABLE_AVX=OFF
    -DOMC_CMAKE_ENABLE_AVX2=OFF
    -DOMC_CMAKE_ENABLE_FMA=OFF
  )
endif()

# MSVC has std::format; GCC 9 / older Apple libc++ use vendored {fmt}.
set(_OMC_FORMAT_ARGS "")
if(WIN32)
  set(_OMC_FORMAT_ARGS -DOMC_USE_STD_FORMAT=ON)
else()
  set(_OMC_FORMAT_ARGS -DOMC_USE_STD_FORMAT=OFF)
endif()

# Zip of bambulab/OpenMeshCraft @0e8d12c3 (main, merged PR #2:
# FormatCompat OMC::format, no std inject, no <format> unless CMake
# probe passed). Empty submodule dirs; Windows never CGAL.
bambustudio_add_cmake_project(OpenMeshCraft
  URL https://github.com/bambulab/OpenMeshCraft/archive/0e8d12c3df54804393593ab5d86c05caa340cbee.zip
  URL_HASH SHA256=632CD806CE932D6A1D76DF0E86ECF8BFC22480E76F80A638A28474F5262E2B9E
  ${_OMC_DEPENDS}
  PATCH_COMMAND ${CMAKE_COMMAND}
    -DOMC_SOURCE_DIR=<SOURCE_DIR>
    -P ${_OMC_GEN_DIR}/prepare_source.cmake
  COMMAND ${CMAKE_COMMAND} -E copy
    ${_OMC_DIR}/CMakeLists.txt.in
    <SOURCE_DIR>/CMakeLists.txt
  COMMAND ${CMAKE_COMMAND} -E copy
    ${_OMC_GEN_DIR}/external_CMakeLists.txt
    <SOURCE_DIR>/external/CMakeLists.txt
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${_OMC_DIR}/wrapper
    <SOURCE_DIR>/bambu_wrapper
  COMMAND ${CMAKE_COMMAND} -E copy
    ${_OMC_DIR}/OpenMeshCraftConfig.cmake.in
    <SOURCE_DIR>/OpenMeshCraftConfig.cmake.in
  CMAKE_ARGS
    -DOMC_BUILD_TEST=OFF
    -DOMC_MASTER_PROJECT=OFF
    -DOMC_BUILD_PREDICATES_GENERATOR=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    ${_OMC_FORMAT_ARGS}
    ${_OMC_SIMD_ARGS}
)

if(MSVC)
  add_debug_dep(dep_OpenMeshCraft)
endif()
