# Slicing tests of priming into infill and the idle-time prime on tool changers. They slice small prints with the fff_print test
# fixtures; libslic3r references symbols that live in the GUI library, so the tests link libslic3r_gui
# (test_gui), as fila_manager does.

get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

add_executable(${_TEST_NAME}_tests
    ${_TEST_NAME}_tests_main.cpp
    test_prime_into_infill.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../fff_print/test_data.cpp
)
target_include_directories(${_TEST_NAME}_tests PRIVATE ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_LIST_DIR}/../fff_print)
target_link_libraries(${_TEST_NAME}_tests test_gui)
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

# test_data.cpp brings the fff_print fixture scenarios along; run only this suite's own cases.
add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests ${CATCH_EXTRA_ARGS} "[PrimeIntoInfill],[IdlePrime]")
