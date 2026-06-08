get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

add_executable(${_TEST_NAME}_tests 
	${_TEST_NAME}_tests.cpp
	test_3mf.cpp
	test_aabbindirect.cpp
	test_clipper_offset.cpp
	test_clipper_utils.cpp
	test_config.cpp
	test_config_runtime.cpp
	test_elephant_foot_compensation.cpp
	test_geometry.cpp
	test_placeholder_parser.cpp
	test_polygon.cpp
	test_mutable_polygon.cpp
	test_mutable_priority_queue.cpp
	test_stl.cpp
	test_meshboolean.cpp
	test_marchingsquares.cpp
	test_timeutils.cpp
	test_voronoi.cpp
    test_optimizers.cpp
    test_png_io.cpp
    test_timeutils.cpp
    test_indexed_triangle_set.cpp
    test_debounce.cpp
    ../libnest2d/printer_parts.cpp
	)

if (TARGET OpenVDB::openvdb)
    target_sources(${_TEST_NAME}_tests PRIVATE test_hollowing.cpp)
endif()
    
target_link_libraries(${_TEST_NAME}_tests test_common libslic3r)
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

if (WIN32)
    # bambustudio_copy_dlls now requires 4 args (target, config, postfix,
    # output_dlls); legacy 1-arg call sites in tests/* predate that signature
    # change.  Test executables link libslic3r and do not need the heavyweight
    # OCCT / GMP / MPFR / FFmpeg DLL bundle, so skip the copy step here.
    # Run tests with PATH pointing at deps build dir if extra DLLs are needed.
    # bambustudio_copy_dlls(${_TEST_NAME}_tests <config> <postfix> <out>)
endif()

# catch_discover_tests(${_TEST_NAME}_tests TEST_PREFIX "${_TEST_NAME}: ")
add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests ${CATCH_EXTRA_ARGS})

add_executable(libslic3r_config_smoke
    ${_TEST_NAME}_tests.cpp
    test_config_smoke.cpp
)
target_link_libraries(libslic3r_config_smoke test_common libslic3r)
target_link_libraries(libslic3r_config_smoke test_smoke_support)
set_property(TARGET libslic3r_config_smoke PROPERTY FOLDER "tests")
add_test(libslic3r_config_smoke libslic3r_config_smoke ${CATCH_EXTRA_ARGS})

add_executable(libslic3r_config_tests
    config_tests_main.cpp
    test_config.cpp
    test_print_filament_mapping_config_sync.cpp
    test_print_filament_mapping_rules.cpp
)
target_link_libraries(libslic3r_config_tests PRIVATE test_common libslic3r_config_core)
target_include_directories(libslic3r_config_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/libslic3r
    ${PROJECT_BINARY_DIR}/src/libslic3r
)
set_property(TARGET libslic3r_config_tests PROPERTY FOLDER "tests")
add_test(libslic3r_config_tests libslic3r_config_tests ${CATCH_EXTRA_ARGS})

add_executable(placeholder_parser_smoke_tests
    config_tests_main.cpp
    test_placeholder_parser_smoke.cpp
)
target_link_libraries(placeholder_parser_smoke_tests PRIVATE test_common libslic3r_placeholder_parser_core)
target_include_directories(placeholder_parser_smoke_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/libslic3r
    ${PROJECT_BINARY_DIR}/src/libslic3r
)
set_property(TARGET placeholder_parser_smoke_tests PROPERTY FOLDER "tests")
add_test(placeholder_parser_smoke_tests placeholder_parser_smoke_tests ${CATCH_EXTRA_ARGS})
