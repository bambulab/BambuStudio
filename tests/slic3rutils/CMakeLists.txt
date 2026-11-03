get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
add_executable(${_TEST_NAME}_tests
    ${_TEST_NAME}_tests_main.cpp
    helio_retry_policy_tests.cpp
    helio_support_data_tests.cpp
    helio_recent_runs_tests.cpp
    http_lifetime_tests.cpp
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
# The lifetime cases require the loopback server supplied by the separate runner.
add_test(NAME ${_TEST_NAME}_tests
    COMMAND $<TARGET_FILE:${_TEST_NAME}_tests> "${CATCH_EXTRA_ARGS} exclude:[NotWorking]~[HttpLifetime]")

find_package(Python3 QUIET COMPONENTS Interpreter)
if (Python3_Interpreter_FOUND)
    add_test(NAME http_lifetime_tests
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run_http_lifetime_tests.py
                $<TARGET_FILE:${_TEST_NAME}_tests>)
endif ()
