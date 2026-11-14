# Unit tests of the tool-ordering helpers (GCode/ToolOrderUtils). libslic3r's tool-ordering code
# pulls in symbols that live in the GUI library on some platforms, so the tests link libslic3r_gui
# (as fila_manager does).

get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

add_executable(${_TEST_NAME}_tests
    ${_TEST_NAME}_tests_main.cpp
    test_tool_order_lookahead.cpp
)
target_include_directories(${_TEST_NAME}_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(${_TEST_NAME}_tests test_gui)
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests "${CATCH_EXTRA_ARGS} exclude:[NotWorking]")
