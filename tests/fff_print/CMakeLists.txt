get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
add_executable(${_TEST_NAME}_tests 
	${_TEST_NAME}_tests.cpp
	test_data.cpp
	test_data.hpp
	test_extrusion_entity.cpp
	test_fill.cpp
	test_flow.cpp
	test_gcode.cpp
	test_gcodewriter.cpp
	test_model.cpp
	test_print.cpp
	test_printgcode.cpp
	test_printobject.cpp
	test_skirt_brim.cpp
	test_support_material.cpp
	test_trianglemesh.cpp
	)
target_link_libraries(${_TEST_NAME}_tests test_common libslic3r)
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

if (WIN32)
    # See tests/libslic3r/CMakeLists.txt for the full rationale; the legacy
    # 1-arg invocation no longer matches bambustudio_copy_dlls's 4-arg signature.
    # bambustudio_copy_dlls(${_TEST_NAME}_tests <config> <postfix> <out>)
endif()

# catch_discover_tests(${_TEST_NAME}_tests TEST_PREFIX "${_TEST_NAME}: ")
add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests ${CATCH_EXTRA_ARGS})

add_executable(fff_print_smoke
	${_TEST_NAME}_tests.cpp
	test_model_smoke.cpp
)
target_link_libraries(fff_print_smoke test_common libslic3r)
target_link_libraries(fff_print_smoke test_smoke_support)
set_property(TARGET fff_print_smoke PROPERTY FOLDER "tests")
add_test(fff_print_smoke fff_print_smoke ${CATCH_EXTRA_ARGS})

add_executable(gcodewriter_smoke_tests
	${_TEST_NAME}_tests.cpp
	test_gcodewriter_smoke.cpp
)
target_link_libraries(gcodewriter_smoke_tests PRIVATE test_common libslic3r_gcodewriter_core)
set_property(TARGET gcodewriter_smoke_tests PROPERTY FOLDER "tests")
add_test(gcodewriter_smoke_tests gcodewriter_smoke_tests ${CATCH_EXTRA_ARGS})

add_executable(model_basic_smoke_tests
	${_TEST_NAME}_tests.cpp
	test_model_basic_smoke.cpp
)
target_link_libraries(model_basic_smoke_tests PRIVATE test_common libslic3r_model_basic_core)
set_property(TARGET model_basic_smoke_tests PROPERTY FOLDER "tests")
add_test(model_basic_smoke_tests model_basic_smoke_tests ${CATCH_EXTRA_ARGS})
