add_executable(gcodewriter_smoke_tests
	${_TEST_NAME}_tests.cpp
	test_gcode_origin_smoke.cpp
	test_gcodewriter_smoke.cpp
	test_gcodewriter_state_smoke.cpp
)
target_link_libraries(gcodewriter_smoke_tests PRIVATE test_common libslic3r_gcodewriter_core)
set_property(TARGET gcodewriter_smoke_tests PROPERTY FOLDER "tests")
add_test(gcodewriter_smoke_tests gcodewriter_smoke_tests ${CATCH_EXTRA_ARGS})
