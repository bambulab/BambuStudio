add_executable(fill_smoke_tests
	${_TEST_NAME}_tests.cpp
	test_fill_smoke.cpp
)
target_link_libraries(fill_smoke_tests PRIVATE test_common libslic3r_print_process_core)
set_property(TARGET fill_smoke_tests PROPERTY FOLDER "tests")
add_test(fill_smoke_tests fill_smoke_tests ${CATCH_EXTRA_ARGS})
