add_executable(print_adhesion_smoke_tests
	${_TEST_NAME}_tests.cpp
	test_print_skirt_brim_smoke.cpp
)
target_link_libraries(print_adhesion_smoke_tests PRIVATE test_common libslic3r_print_process_core)
set_property(TARGET print_adhesion_smoke_tests PROPERTY FOLDER "tests")
add_test(print_adhesion_smoke_tests print_adhesion_smoke_tests ${CATCH_EXTRA_ARGS})
