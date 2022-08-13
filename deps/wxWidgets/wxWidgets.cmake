# set(_wx_git_tag v3.1.5) # Does not link -ldep-name-NOTFOUND (see deps/build/dep_wxWidgets-prefix/src/dep_wxWidgets-build/wx-config --libs all | grep NOTFOUND)
# set(_wx_git_tag v3.1.4-patched) # Links, but Bambu's slic3r_gui depends on wxWebView methods in >=3.1.5 like SetUserAgent
# set(_wx_git_tag v3.1.6) # wx includes nanosvg, which creates link conflicts (multiple definition) with slic3r which also uses nanosvg, wx nanosvg can't be disabled
# set(_wx_git_tag v3.1.7) # wx includes nanosvg, which creates link conflicts ( multiple definition) with slic3r which also uses nanosvg, segfaults if nanosvg disabled
set(_wx_git_tag v3.2.0) # Starts, but gives unhandled AppConfig::Save() exception

set(_wx_toolkit "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_gtk_ver 2)
    if (DEP_WX_GTK3)
        set(_gtk_ver 3)
    endif ()
    set(_wx_toolkit "-DwxBUILD_TOOLKIT=gtk${_gtk_ver}")
endif()

if (MSVC)
    set(_wx_edge "-DwxUSE_WEBVIEW_EDGE=ON")
else ()
    set(_wx_edge "-DwxUSE_WEBVIEW_EDGE=OFF")
endif ()

bambustudio_add_cmake_project(wxWidgets
    GIT_REPOSITORY "https://github.com/wxWidgets/wxWidgets"
    GIT_TAG ${_wx_git_tag}
    # PATCH_COMMAND ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/0001-wxWidget-fix.patch

    DEPENDS ${PNG_PKG} ${ZLIB_PKG} ${EXPAT_PKG} dep_TIFF dep_JPEG
    CMAKE_ARGS
        -DwxBUILD_PRECOMP=ON
        ${_wx_toolkit}
        "-DCMAKE_DEBUG_POSTFIX:STRING="
        -DwxBUILD_DEBUG_LEVEL=0
        -DwxBUILD_SAMPLES=OFF
        -DwxBUILD_SHARED=OFF
        -DwxUSE_MEDIACTRL=ON
        -DwxUSE_DETECT_SM=OFF
        -DwxUSE_UNICODE=ON
        -DwxUSE_OPENGL=ON
        -DwxUSE_WEBVIEW=ON
        ${_wx_edge}
        -DwxUSE_WEBVIEW_IE=OFF
        -DwxUSE_REGEX=builtin
        -DwxUSE_LIBXPM=builtin
        -DwxUSE_LIBSDL=OFF
        -DwxUSE_XTEST=OFF
        -DwxUSE_STC=OFF
        -DwxUSE_AUI=ON
        -DwxUSE_LIBPNG=sys
        -DwxUSE_ZLIB=sys
        -DwxUSE_LIBJPEG=sys
        -DwxUSE_LIBTIFF=sys
        -DwxUSE_EXPAT=sys
        -DwxUSE_NANOSVG=OFF # Required for wx >=3.1.7 # TODO: refactor to use /src/nanosvg for wx and libSlic3r_gui
)

if (MSVC)
    add_debug_dep(dep_wxWidgets)
endif ()
