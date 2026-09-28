# TODO Add individual tests as executables in separate directories
# add_subirectory(<testcase>)

set(TEST_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)
file(TO_NATIVE_PATH "${TEST_DATA_DIR}" TEST_DATA_DIR)
set(PROFILES_DIR ${PROJECT_SOURCE_DIR}/resources/profiles)
file(TO_NATIVE_PATH "${PROFILES_DIR}" PROFILES_DIR)

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
target_compile_definitions(test_common INTERFACE TEST_DATA_DIR=R"\(${TEST_DATA_DIR}\)" PROFILES_DIR=R"\(${PROFILES_DIR}\)" CATCH_CONFIG_FAST_COMPILE)
target_link_libraries(test_common INTERFACE Catch2::Catch2)

if (APPLE)
    target_link_libraries(test_common INTERFACE "-liconv -framework IOKit" "-framework CoreFoundation" -lc++)
endif()

# libslic3r calls a few functions that are only compiled into the GUI library: Http and BBL_Encrypt
# (LogSink.cpp) and nanosvg (NSVGUtils.cpp). The tests link libslic3r without the GUI, so they
# compile those sources here. None of them depend on wxWidgets.
add_library(test_libslic3r_support STATIC
    ${PROJECT_SOURCE_DIR}/src/slic3r/Utils/Http.cpp
    ${PROJECT_SOURCE_DIR}/src/slic3r/Utils/BBLUtil.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/support/nanosvg_impl.cpp
)
target_include_directories(test_libslic3r_support PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/src/slic3r/Utils)
target_link_libraries(test_libslic3r_support PUBLIC libslic3r libcurl OpenSSL::SSL OpenSSL::Crypto)
if (WIN32)
    # boost::uuids' random generator uses BCryptGenRandom.
    target_link_libraries(test_libslic3r_support PUBLIC bcrypt)
endif()
set_property(TARGET test_libslic3r_support PROPERTY FOLDER "tests")
target_link_libraries(test_common INTERFACE test_libslic3r_support)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_subdirectory(libnest2d)
add_subdirectory(libslic3r)
add_subdirectory(slic3rutils)
add_subdirectory(fff_print)
add_subdirectory(sla_print)
add_subdirectory(fila_manager)
add_subdirectory(filament_group)
add_subdirectory(cpp17 EXCLUDE_FROM_ALL)    # does not have to be built all the time
# add_subdirectory(example)
