# STUDIO-17977 / openspec 20260506耗材管理器AMS同步渐变多拼色:
# fila_manager unit tests target.
#
# wgtFilaManagerStore.cpp is compiled directly into the test binary. It used to
# need only nlohmann::json, std, boost and libslic3r/Utils.hpp, so the target did
# not link the GUI. It now includes slic3r/GUI/GUI_App.hpp and calls
# wxGetApp().transition_tridid() for the tray label, so it links libslic3r_gui,
# which also provides wxWidgets' include paths.
#
# When the parallel change "20260506耗材管理器AMS自动同步云端" lands its
# AmsAutoPushThrottle / wgtFilaManagerCloudSync tests, append the
# corresponding source files below; the same target services every
# fila_manager-rooted unit test.

get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

add_executable(${_TEST_NAME}_tests
    ${_TEST_NAME}_tests_main.cpp
    ${PROJECT_SOURCE_DIR}/src/slic3r/GUI/fila_manager/wgtFilaManagerStore.cpp
)

target_include_directories(${_TEST_NAME}_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(${_TEST_NAME}_tests
    test_common
    libslic3r_gui
    libslic3r
)

set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

if (WIN32)
    # See tests/libslic3r/CMakeLists.txt for the full rationale; the legacy
    # 1-arg invocation no longer matches bambustudio_copy_dlls's 4-arg signature.
    # bambustudio_copy_dlls(${_TEST_NAME}_tests <config> <postfix> <out>)
endif()

add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests "${CATCH_EXTRA_ARGS} exclude:[NotWorking]")
