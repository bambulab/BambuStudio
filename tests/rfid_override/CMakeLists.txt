add_executable(rfid_override_tests rfid_override_tests.cpp)
target_link_libraries(rfid_override_tests PRIVATE test_gui)
# PresetBundle pulls core objects which call GUI utility implementations. Rescan
# both static archives so these cross-library references resolve on Linux.
if (UNIX AND NOT APPLE)
    target_link_libraries(rfid_override_tests PRIVATE
        "-Wl,--start-group" libslic3r_gui libslic3r "-Wl,--end-group"
        libcurl OpenSSL::SSL OpenSSL::Crypto)
endif()
add_test(NAME rfid_override_tests COMMAND rfid_override_tests)
