get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

set(FG_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)
file(TO_NATIVE_PATH "${FG_DATA_DIR}" FG_DATA_DIR)

set(FG_GOLDEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/golden)
file(TO_NATIVE_PATH "${FG_GOLDEN_DIR}" FG_GOLDEN_DIR)

# Test data generator (standalone executable, not a test)
add_executable(fg_test_data_generator
    test_data_generator.cpp
)
target_include_directories(fg_test_data_generator PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(fg_test_data_generator libslic3r)
target_compile_definitions(fg_test_data_generator PRIVATE
    FG_TEST_DATA_DIR=R"\(${FG_DATA_DIR}\)"
)
set_property(TARGET fg_test_data_generator PROPERTY FOLDER "tests")

# Regression test runner
add_executable(${_TEST_NAME}_tests
    filament_group_regression_main.cpp
)
target_include_directories(${_TEST_NAME}_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(${_TEST_NAME}_tests test_common libslic3r
    libcurl
    OpenSSL::SSL OpenSSL::Crypto
)
target_compile_definitions(${_TEST_NAME}_tests PRIVATE
    FG_TEST_DATA_DIR=R"\(${FG_DATA_DIR}\)"
    FG_TEST_GOLDEN_DIR=R"\(${FG_GOLDEN_DIR}\)"
)
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

# Allow unresolved externals from nanosvg (header-only lib compiled in libslic3r GUI utils)
if (MSVC)
    target_link_options(${_TEST_NAME}_tests PRIVATE /FORCE:UNRESOLVED)
endif()

add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests ${CATCH_EXTRA_ARGS})
