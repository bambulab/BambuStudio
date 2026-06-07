add_executable(support_material_layers_smoke_tests
	${_TEST_NAME}_tests.cpp
	test_support_material_layers_smoke.cpp
)
target_include_directories(support_material_layers_smoke_tests PRIVATE
	${CMAKE_SOURCE_DIR}/src/libslic3r
	${CMAKE_BINARY_DIR}/src/libslic3r
)
target_link_libraries(support_material_layers_smoke_tests PRIVATE test_common boost_libs TBB::tbb TBB::tbbmalloc)
set_property(TARGET support_material_layers_smoke_tests PROPERTY FOLDER "tests")
add_test(support_material_layers_smoke_tests support_material_layers_smoke_tests ${CATCH_EXTRA_ARGS})
