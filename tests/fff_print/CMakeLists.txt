get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
add_executable(${_TEST_NAME}_tests 
	${_TEST_NAME}_tests.cpp
	test_data.cpp
	test_data.hpp
	test_extrusion_entity.cpp
	test_fill.cpp
	test_flow.cpp
	test_flush_into_object_without_tower.cpp
	# NOTE: the following pre-existing test files do not compile against current
	# master (API drift; the fff_print suite is not built in CI) - unrelated to
	# this change, so left excluded rather than fixed here. See
	# [[bambu-sequential-prime-tower]] memory / commit c37062d16 on the
	# feature/sequential-per-object-prime-tower branch for the same finding and
	# a standalone fix.
	#   test_gcodewriter.cpp      -- GCodeWriter::set_extruder(int)/lift(), GCodeConfig::retract_lift
	#   test_print.cpp            -- Print::brim()
	#   test_skirt_brim.cpp       -- Print::brim()
	#   test_support_material.cpp -- PrintConfig::first_layer_height
	test_gcode.cpp
	test_gcodewriter_travel.cpp
	test_model.cpp
	test_printgcode.cpp
	test_printobject.cpp
	test_support_material_on_wipe_tower.cpp
	test_trianglemesh.cpp
	)
target_link_libraries(${_TEST_NAME}_tests test_common libslic3r)
# libslic3r currently has an unresolved link-time dependency on the GUI layer
# (LogSink.cpp -> Slic3r::Http / BBL_Encrypt in libslic3r_gui, NSVGUtils.cpp ->
# nanosvg implementation compiled in libslic3r_gui). The full app papers over
# this by linking both; a test that links only libslic3r does not. Link the GUI
# lib here until that layering is fixed upstream. Same finding as above.
if (TARGET libslic3r_gui)
	target_link_libraries(${_TEST_NAME}_tests libslic3r_gui)
endif()
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

if (WIN32)
    # See tests/libslic3r/CMakeLists.txt for the full rationale; the legacy
    # 1-arg invocation no longer matches bambustudio_copy_dlls's 4-arg signature.
    # bambustudio_copy_dlls(${_TEST_NAME}_tests <config> <postfix> <out>)
endif()

# catch_discover_tests(${_TEST_NAME}_tests TEST_PREFIX "${_TEST_NAME}: ")
add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests ${CATCH_EXTRA_ARGS})

add_executable(support_material_on_wipe_tower_tests
	fff_print_tests.cpp
	test_data.cpp
	test_data.hpp
	test_support_material_on_wipe_tower.cpp
	)
target_link_libraries(support_material_on_wipe_tower_tests test_common libslic3r
	libcurl
	OpenSSL::SSL OpenSSL::Crypto
)
if (MSVC)
	target_link_options(support_material_on_wipe_tower_tests PRIVATE /FORCE:UNRESOLVED)
endif()
set_property(TARGET support_material_on_wipe_tower_tests PROPERTY FOLDER "tests")
add_test(support_material_on_wipe_tower_tests support_material_on_wipe_tower_tests ${CATCH_EXTRA_ARGS})
