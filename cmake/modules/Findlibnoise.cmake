# Findlibnoise.cmake - Find libnoise for structured fuzzy skin
#
# Use CMAKE_PREFIX_PATH to point to the dep root (e.g. .../dep_win_new/usr/local).
#   include/libnoise/noise.h, lib/libnoise_static.lib (.lib on Windows, .a on Linux)
#
# Ref: https://github.com/bambulab/libnoise
#
# Provides: noise::noise (imported static library)

if(libnoise_FOUND)
  return()
endif()

find_path(LIBNOISE_INCLUDE_DIR NAMES noise.h
  PATHS ${CMAKE_PREFIX_PATH}
  PATH_SUFFIXES include/libnoise include
  NO_DEFAULT_PATH
)

# bambulab/libnoise produces libnoise_static (.lib on Windows, liblibnoise_static.a on Linux)
find_library(LIBNOISE_LIBRARY
  NAMES libnoise_static noise_static
  PATHS ${CMAKE_PREFIX_PATH}
  PATH_SUFFIXES lib
  NO_DEFAULT_PATH
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(libnoise
  REQUIRED_VARS LIBNOISE_INCLUDE_DIR LIBNOISE_LIBRARY
)

if(libnoise_FOUND AND NOT TARGET noise::noise)
  add_library(noise::noise STATIC IMPORTED GLOBAL)
  set_target_properties(noise::noise PROPERTIES
    IMPORTED_LOCATION "${LIBNOISE_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${LIBNOISE_INCLUDE_DIR}"
  )
  mark_as_advanced(LIBNOISE_INCLUDE_DIR LIBNOISE_LIBRARY)
endif()
