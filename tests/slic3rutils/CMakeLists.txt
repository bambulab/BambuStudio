get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
add_executable(${_TEST_NAME}_tests
    ${_TEST_NAME}_tests_main.cpp
    )

target_link_libraries(${_TEST_NAME}_tests test_common libslic3r_gui libslic3r)
if (MSVC)
    target_link_libraries(${_TEST_NAME}_tests Setupapi.lib)
endif ()
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

if (WIN32)
    # See tests/libslic3r/CMakeLists.txt for the full rationale; the legacy
    # 1-arg invocation no longer matches bambustudio_copy_dlls's 4-arg signature.
    # bambustudio_copy_dlls(${_TEST_NAME}_tests <config> <postfix> <out>)
endif()

# catch_discover_tests(${_TEST_NAME}_tests TEST_PREFIX "${_TEST_NAME}: ")
add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests "${CATCH_EXTRA_ARGS} exclude:[NotWorking]")
