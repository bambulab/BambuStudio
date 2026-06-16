add_executable(gcode_reader_smoke_tests
    ${_TEST_NAME}_tests.cpp
    test_gcode_reader_smoke.cpp
)
target_link_libraries(gcode_reader_smoke_tests PRIVATE test_common libslic3r_gcode_reader_core)
set_property(TARGET gcode_reader_smoke_tests PROPERTY FOLDER "tests")
add_test(gcode_reader_smoke_tests gcode_reader_smoke_tests ${CATCH_EXTRA_ARGS})
