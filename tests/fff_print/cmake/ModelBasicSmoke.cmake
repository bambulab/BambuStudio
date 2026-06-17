add_executable(model_basic_smoke_tests
	${_TEST_NAME}_tests.cpp
	test_model_basic_smoke.cpp
	test_trianglemesh_basic_smoke.cpp
)
target_link_libraries(model_basic_smoke_tests PRIVATE test_common libslic3r_model_basic_core)
set_property(TARGET model_basic_smoke_tests PROPERTY FOLDER "tests")
add_test(model_basic_smoke_tests model_basic_smoke_tests ${CATCH_EXTRA_ARGS})
