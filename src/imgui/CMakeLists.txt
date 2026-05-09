cmake_minimum_required(VERSION 2.8.12)
project(imgui)

add_library(imgui STATIC
    imconfig.h
    imgui.h
    imgui_internal.h
    imstb_rectpack.h
    imstb_textedit.h
    imstb_truetype.h
    imgui_tables.cpp
    imgui.cpp
    imgui_demo.cpp
    imgui_draw.cpp
    imgui_widgets.cpp
    imgui_stdlib.cpp
    imgui_stdlib.h
)

if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    # imgui_draw.cpp was patched to call BOOST_LOG_TRIVIAL — propagate the
    # Boost::log requirement so consumers link it after libimgui.a.
    if(TARGET Boost::log)
        target_link_libraries(imgui PUBLIC Boost::log)
    endif()
endif()