# PresetBundle per-filament map size tests (#13326). A separate target, so the tests build without the older libslic3r suites.

get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

add_executable(${_TEST_NAME}_tests
    ${_TEST_NAME}_tests.cpp
)

target_include_directories(${_TEST_NAME}_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(${_TEST_NAME}_tests test_common libslic3r)
# set_num_filaments() reads resources/info/multi_nozzle_rules.json
target_compile_definitions(${_TEST_NAME}_tests PRIVATE TEST_RESOURCES_DIR="${PROJECT_SOURCE_DIR}/resources")

set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests ${CATCH_EXTRA_ARGS})
