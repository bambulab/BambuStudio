set(_wx_toolkit "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_gtk_ver 2)
    if (DEP_WX_GTK3)
        set(_gtk_ver 3)
    endif ()
    set(_wx_toolkit "-DwxBUILD_TOOLKIT=gtk${_gtk_ver}")
    set(_wx_private_font "-DwxUSE_PRIVATE_FONTS=1")
    set(_wx_egl "-DwxUSE_GLCANVAS_EGL=ON")
else ()
    set(_wx_egl "")
endif()

if (MSVC)
    set(_wx_edge "-DwxUSE_WEBVIEW_EDGE=ON")
else ()
    set(_wx_edge "-DwxUSE_WEBVIEW_EDGE=OFF")
endif ()

# if (MSVC)
#     set(_patch_cmd ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/0001-wxWidget-fix.patch)
# else ()
#     set(_patch_cmd test -f WXWIDGETS_PATCHED || ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/0001-wxWidget-fix.patch && touch WXWIDGETS_PATCHED)
# endif ()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Fix blank wxGLCanvas on native Wayland: reposition the EGL subsurface
    # after layout and keep eglSwapBuffers() from blocking on frame callbacks
    # (backport of the corresponding wxWidgets 3.2 behavior).
    set(_patch_cmd PATCH_COMMAND sh -c "test -f WXWIDGETS_PATCHED || git apply --verbose --ignore-space-change --whitespace=fix ${CMAKE_CURRENT_LIST_DIR}/0002-fix-wayland-egl-subsurface.patch && touch WXWIDGETS_PATCHED")
else ()
    set(_patch_cmd "")
endif ()

bambustudio_add_cmake_project(wxWidgets
    GIT_REPOSITORY "https://github.com/bambulab/wxWidgets"
    GIT_TAG master
    ${_patch_cmd}
    DEPENDS ${PNG_PKG} ${ZLIB_PKG} ${EXPAT_PKG} ${TIFF_PKG} ${JPEG_PKG}
    CMAKE_ARGS
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        -DwxBUILD_PRECOMP=OFF
        ${_wx_toolkit}
        "-DCMAKE_DEBUG_POSTFIX:STRING="
        -DwxBUILD_DEBUG_LEVEL=0
        -DwxBUILD_SAMPLES=OFF
        -DwxBUILD_SHARED=OFF
        -DwxUSE_MEDIACTRL=ON
        -DwxUSE_DETECT_SM=OFF
        -DwxUSE_UNICODE=ON
        ${_wx_private_font}
        -DwxUSE_OPENGL=ON
        -DwxUSE_WEBVIEW=ON
        ${_wx_edge}
        -DwxUSE_WEBVIEW_IE=OFF
        -DwxUSE_REGEX=builtin
        -DwxUSE_LIBMSPACK=OFF
        -DwxUSE_LIBSDL=OFF
        -DwxUSE_XTEST=OFF
        -DwxUSE_STC=OFF
        -DwxUSE_AUI=ON
        -DwxUSE_EXCEPTIONS=OFF
        -DwxUSE_LIBPNG=sys
        -DwxUSE_ZLIB=sys
        -DwxUSE_LIBJPEG=sys
        -DwxUSE_LIBTIFF=sys
        -DwxUSE_EXPAT=sys
        ${_wx_egl}
)

if (MSVC)
    add_debug_dep(dep_wxWidgets)
endif ()
