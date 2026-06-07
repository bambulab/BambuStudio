get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
add_executable(${_TEST_NAME}_tests 
	${_TEST_NAME}_tests.cpp
	# Residual maintained legacy cases. Files already migrated to responsibility
	# smoke targets stay out of this full target to avoid obsolete API noise.
	test_extrusion_entity.cpp
	)
target_link_libraries(${_TEST_NAME}_tests test_common libslic3r)
target_link_libraries(${_TEST_NAME}_tests test_smoke_support)
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

include(${CMAKE_CURRENT_LIST_DIR}/cmake/GCodeWriterSmoke.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/ModelBasicSmoke.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/PrintFilamentMappingSmoke.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/PrintPerimetersStageSmoke.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/PrintProcessMathSmoke.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/PrintProcessCoreSmoke.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/PrintApplyValidateSmoke.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/PrintObjectLayersSmoke.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/PrintAdhesionSmoke.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/FillSmoke.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/SupportMaterialSmoke.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/SupportMaterialLayersSmoke.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/TriangleMeshGeometrySmoke.cmake)
