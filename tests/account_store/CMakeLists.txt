cmake_minimum_required(VERSION 3.25)

set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)

project(account_store_tests LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "The standalone AccountStore test supports Windows DPAPI only.")
endif()

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_executable(account_store_tests
    account_store_tests.cpp
    "${PROJECT_ROOT}/src/slic3r/GUI/AccountStore.cpp"
)

target_include_directories(account_store_tests PRIVATE
    "${PROJECT_ROOT}/src"
    "${PROJECT_ROOT}/src/slic3r/GUI"
    "${PROJECT_ROOT}/tests"
)
target_compile_features(account_store_tests PRIVATE cxx_std_17)
target_compile_options(account_store_tests PRIVATE /W4 /utf-8)
target_link_libraries(account_store_tests PRIVATE crypt32 bcrypt)

enable_testing()
add_test(NAME account_store_tests COMMAND account_store_tests)
