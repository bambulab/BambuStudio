set(_wx_toolkit "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_gtk_ver 2)
    if (DEP_WX_GTK3)
        set(_gtk_ver 3)
    endif ()
    set(_wx_toolkit "-DwxBUILD_TOOLKIT=gtk${_gtk_ver}")
    set(_wx_private_font "-DwxUSE_PRIVATE_FONTS=1")
    set(_wx_egl "-DwxUSE_GLCANVAS_EGL=OFF")
else ()
    set(_wx_egl "")
endif()

if (MSVC)
    set(_wx_edge "-DwxUSE_WEBVIEW_EDGE=ON")
else ()
    set(_wx_edge "-DwxUSE_WEBVIEW_EDGE=OFF")
endif ()

# wx 3.2+ needs its git submodules (pcre, etc.). ExternalProject clones them by
# default when GIT_REPOSITORY is set; do not pass GIT_SUBMODULES "" here.
#
# Temporary: pin daveisfera until bambulab/wxWidgets#11 lands the same tag on
# https://github.com/bambulab/wxWidgets — then switch GIT_REPOSITORY to bambulab
# and keep GIT_TAG bambu-3.2.12-1 (or the next bambu-3.2.x-N).
bambustudio_add_cmake_project(wxWidgets
    GIT_REPOSITORY "https://github.com/daveisfera/wxWidgets"
    GIT_TAG bambu-3.2.12-1
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
        # Studio embeds NanoSVG in BitmapCache; wx 3.2 builtin NanoSVG duplicates those symbols.
        -DwxUSE_NANOSVG=OFF
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
