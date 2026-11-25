# Interactive AlertDialog gallery. Not part of ctest; build it explicitly:
#   cmake --build <build dir> --target alert_dialog_gallery

add_executable(alert_dialog_gallery alert_dialog_gallery.cpp)
# libslic3r_gui may be an OBJECT library, whose objects only reach targets that link it directly
target_link_libraries(alert_dialog_gallery libslic3r_gui test_gui)
target_compile_definitions(alert_dialog_gallery PRIVATE GALLERY_RESOURCES_DIR="${PROJECT_SOURCE_DIR}/resources")
set_property(TARGET alert_dialog_gallery PROPERTY FOLDER "tests")

if (MSVC)
    # Windowed app with a standard main(): the GUI library already defines its own WinMain
    target_link_options(alert_dialog_gallery PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
endif ()
