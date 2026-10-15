# Unit tests for the Hebrew-restricted bidi reordering used by the ImGui text helpers.
#
# Same slim layout as tests/fila_manager: Bidi.cpp has no wxWidgets / ImGui / libslic3r
# dependency, so it is compiled straight into the test binary (no libslic3r_gui needed).

get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

add_executable(${_TEST_NAME}_tests
    ${_TEST_NAME}_tests_main.cpp
    ${PROJECT_SOURCE_DIR}/src/slic3r/GUI/Bidi.cpp
)

target_include_directories(${_TEST_NAME}_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(${_TEST_NAME}_tests test_common)

set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests "${CATCH_EXTRA_ARGS} exclude:[NotWorking]")
