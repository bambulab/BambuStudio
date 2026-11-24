get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

add_executable(${_TEST_NAME}_tests 
	${_TEST_NAME}_tests.cpp
	test_3mf.cpp
	test_aabbindirect.cpp
	test_clipper_offset.cpp
	test_clipper_utils.cpp
	test_config.cpp
	test_config_enum_maps.cpp
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
    test_trianglemesh_slicer.cpp
    test_triangle_selector.cpp
    test_debounce.cpp
    test_emboss.cpp
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

# Narrow target: cut_mesh tests only (full libslic3r_tests has unrelated compile breaks).
add_executable(libslic3r_cut_mesh_tests
    cut_mesh_catch_main.cpp
    test_trianglemesh_slicer.cpp)
target_include_directories(libslic3r_cut_mesh_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(libslic3r_cut_mesh_tests PRIVATE test_common libslic3r)
set_property(TARGET libslic3r_cut_mesh_tests PROPERTY FOLDER "tests")
if (MSVC)
    target_link_options(libslic3r_cut_mesh_tests PRIVATE /FORCE:UNRESOLVED)
endif()
add_test(libslic3r_cut_mesh_tests libslic3r_cut_mesh_tests ${CATCH_EXTRA_ARGS})

# Keep the mesh boolean regressions runnable without the legacy full suite.
add_executable(libslic3r_meshboolean_tests cut_mesh_catch_main.cpp test_meshboolean.cpp)
target_link_libraries(libslic3r_meshboolean_tests PRIVATE test_common libslic3r)
set_property(TARGET libslic3r_meshboolean_tests PROPERTY FOLDER "tests")
add_test(NAME libslic3r_meshboolean_tests COMMAND libslic3r_meshboolean_tests "[MeshBoolean]" ${CATCH_EXTRA_ARGS})
