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

# DEP_WX_WEBVIEW=OFF lets non-GUI builds skip webkit2gtk dependency.
if (NOT DEFINED DEP_WX_WEBVIEW)
    set(DEP_WX_WEBVIEW ON)
endif ()

# if (MSVC)
#     set(_patch_cmd ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/0001-wxWidget-fix.patch)
# else ()
#     set(_patch_cmd test -f WXWIDGETS_PATCHED || ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/0001-wxWidget-fix.patch && touch WXWIDGETS_PATCHED)
# endif ()

# Pin wx's "sys" thirdparty libs to the conda env explicitly: wx's
# wx_add_thirdparty_library() silently flips wxUSE_LIBPNG=sys back to
# "builtin" (vendored libpng-1.4.12) when find_package(PNG) misses, and
# that's exactly what happens on macos-14 runners.
set(_wx_prefix_path "${DESTDIR}/usr/local")
set(_wx_extra_args "")
if(APPLE AND DEFINED ENV{CONDA_PREFIX})
    set(_wx_prefix_path "$ENV{CONDA_PREFIX};${_wx_prefix_path}")
    set(_wx_extra_args
        "-DPNG_PNG_INCLUDE_DIR:PATH=$ENV{CONDA_PREFIX}/include"
        "-DPNG_LIBRARY:FILEPATH=$ENV{CONDA_PREFIX}/lib/libpng.dylib"
        "-DJPEG_INCLUDE_DIR:PATH=$ENV{CONDA_PREFIX}/include"
        "-DJPEG_LIBRARY:FILEPATH=$ENV{CONDA_PREFIX}/lib/libjpeg.dylib"
        "-DTIFF_INCLUDE_DIR:PATH=$ENV{CONDA_PREFIX}/include"
        "-DTIFF_LIBRARY:FILEPATH=$ENV{CONDA_PREFIX}/lib/libtiff.dylib"
        "-DZLIB_INCLUDE_DIR:PATH=$ENV{CONDA_PREFIX}/include"
        "-DZLIB_LIBRARY:FILEPATH=$ENV{CONDA_PREFIX}/lib/libz.dylib"
        "-DEXPAT_INCLUDE_DIR:PATH=$ENV{CONDA_PREFIX}/include"
        "-DEXPAT_LIBRARY:FILEPATH=$ENV{CONDA_PREFIX}/lib/libexpat.dylib"
    )
endif()

bambustudio_add_cmake_project(wxWidgets
    GIT_REPOSITORY "https://github.com/bambulab/wxWidgets"
    GIT_TAG master
    DEPENDS ${PNG_PKG} ${ZLIB_PKG} ${EXPAT_PKG} ${TIFF_PKG} ${JPEG_PKG}
    # Drop the vendored libs so the "builtin" fallback can't quietly steal the
    # build from the conda-pinned sys ones above.
    PATCH_COMMAND ${CMAKE_COMMAND} -E rm -rf
        <SOURCE_DIR>/src/png
        <SOURCE_DIR>/src/jpeg
        <SOURCE_DIR>/src/tiff
        <SOURCE_DIR>/src/zlib
        <SOURCE_DIR>/src/expat
    CMAKE_ARGS
        "-DCMAKE_PREFIX_PATH:STRING=${_wx_prefix_path}"
        ${_wx_extra_args}
        -DwxBUILD_PRECOMP=ON
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
        -DwxUSE_WEBVIEW=${DEP_WX_WEBVIEW}
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
