# TODO Add individual tests as executables in separate directories
# add_subirectory(<testcase>)

set(TEST_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)
file(TO_NATIVE_PATH "${TEST_DATA_DIR}" TEST_DATA_DIR)

add_library(Catch2 INTERFACE)
list (APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/modules/Catch2)
target_include_directories(Catch2 INTERFACE ${CMAKE_CURRENT_LIST_DIR})
add_library(Catch2::Catch2 ALIAS Catch2)
if (APPLE)
	# OSX builds targeting OSX 10.9 do not support new std::uncought_exception()
	# see https://github.com/catchorg/Catch2/issues/1218
	target_compile_definitions(Catch2 INTERFACE -DCATCH_CONFIG_NO_CPP17_UNCAUGHT_EXCEPTIONS)
endif()
include(Catch)

set(CATCH_EXTRA_ARGS "" CACHE STRING "Extra arguments for catch2 test suites.")

add_library(test_common INTERFACE)
target_compile_definitions(test_common INTERFACE TEST_DATA_DIR=R"\(${TEST_DATA_DIR}\)" CATCH_CONFIG_FAST_COMPILE)
target_link_libraries(test_common INTERFACE Catch2::Catch2)

# Resolve the same Http, encryption and NanoSVG providers as the application.
# Interface links carry usage requirements but not OBJECT library contents.
target_link_libraries(test_common INTERFACE libcurl OpenSSL::SSL OpenSSL::Crypto libslic3r_gui)
get_target_property(_test_gui_library_type libslic3r_gui TYPE)
if(_test_gui_library_type STREQUAL "OBJECT_LIBRARY")
    target_sources(test_common INTERFACE $<TARGET_OBJECTS:libslic3r_gui>)
endif()
if(WIN32)
    target_link_libraries(test_common INTERFACE bcrypt)
endif()

if (APPLE)
    target_link_libraries(test_common INTERFACE "-liconv -framework IOKit" "-framework CoreFoundation" -lc++)
endif()

# Tests of GUI-layer code. wxWidgets include paths and definitions are applied at directory scope
# in src/, so they are not propagated by linking libslic3r_gui alone.
add_library(test_gui INTERFACE)
get_directory_property(_src_include_dirs DIRECTORY ${PROJECT_SOURCE_DIR}/src INCLUDE_DIRECTORIES)
get_directory_property(_src_compile_defs DIRECTORY ${PROJECT_SOURCE_DIR}/src COMPILE_DEFINITIONS)
target_include_directories(test_gui INTERFACE ${_src_include_dirs} ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(test_gui INTERFACE ${_src_compile_defs})
target_link_libraries(test_gui INTERFACE test_common libslic3r_gui libslic3r)
if (MSVC)
    target_link_libraries(test_gui INTERFACE Setupapi.lib)
endif ()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_subdirectory(slic3rutils)
add_subdirectory(fff_print)
add_subdirectory(fila_manager)
add_subdirectory(filament_group)
add_subdirectory(nozzle_inventory)
add_subdirectory(libslic3r_utils)
add_subdirectory(gpu_geometry)

# These suites were not kept in sync with libslic3r after the fork and no longer compile.
# They are left out of the default build; build a target explicitly to work on reviving it.
add_subdirectory(libnest2d EXCLUDE_FROM_ALL)
add_subdirectory(libslic3r EXCLUDE_FROM_ALL)
add_subdirectory(sla_print EXCLUDE_FROM_ALL)

add_subdirectory(cpp17 EXCLUDE_FROM_ALL)    # does not have to be built all the time
# add_subdirectory(example)
