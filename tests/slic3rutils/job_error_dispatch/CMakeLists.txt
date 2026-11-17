cmake_minimum_required(VERSION 3.16)
project(job_error_dispatch_regression LANGUAGES CXX)

# Deliberately standalone: configuring this directory does not regenerate the
# application's version headers or rebuild the application.
get_filename_component(BAMBU_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(BAMBU_GUI_BUILD_DIR "${BAMBU_SOURCE_DIR}/build/gui-check" CACHE PATH
    "Existing GUI build containing generated src/libslic3r/libslic3r_version.h")
if(NOT EXISTS "${BAMBU_GUI_BUILD_DIR}/src/libslic3r/libslic3r_version.h")
    message(FATAL_ERROR "Set BAMBU_GUI_BUILD_DIR to an existing configured GUI build")
endif()

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost CONFIG REQUIRED COMPONENTS log thread chrono)
set(wxWidgets_USE_STATIC ON)
find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})
find_package(Threads REQUIRED)

add_executable(job_error_dispatch
    job_error_dispatch.cpp
    "${BAMBU_SOURCE_DIR}/src/slic3r/GUI/Jobs/Job.cpp")
target_compile_features(job_error_dispatch PRIVATE cxx_std_17)
target_include_directories(job_error_dispatch PRIVATE
    "${BAMBU_SOURCE_DIR}/src"
    "${BAMBU_GUI_BUILD_DIR}/src/libslic3r")
target_compile_definitions(job_error_dispatch PRIVATE
    BOOST_LOG_STATIC_LINK BOOST_THREAD_USE_LIB wxNO_UNSAFE_WXSTRING_CONV
    WXINTL_NO_GETTEXT_MACRO wxDEBUG_LEVEL=0)
# Job.cpp ordinarily gets wx cursor declarations from the GUI target's PCH.
target_precompile_headers(job_error_dispatch PRIVATE <wx/wx.h>)
target_link_libraries(job_error_dispatch PRIVATE
    Boost::log Boost::thread Boost::chrono Threads::Threads ${wxWidgets_LIBRARIES})

enable_testing()
add_test(NAME job_error_dispatch COMMAND job_error_dispatch)
