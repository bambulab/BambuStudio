# device_manager unit tests target. MachineObject lives in the GUI device layer,
# so the tests link libslic3r_gui via test_gui (same as nozzle_inventory).

get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

add_executable(${_TEST_NAME}_tests
    ${_TEST_NAME}_tests_main.cpp
)

target_link_libraries(${_TEST_NAME}_tests test_gui)

set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests "${CATCH_EXTRA_ARGS} exclude:[NotWorking]")
