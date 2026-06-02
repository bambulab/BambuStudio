if(WIN32)
    set(library_build_shared "1")
else()
    set(library_build_shared "0")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_ft_disable_zlib "-D FT_DISABLE_ZLIB=FALSE")
else()
    set(_ft_disable_zlib "-D FT_DISABLE_ZLIB=TRUE")
endif()

bambustudio_add_cmake_project(FREETYPE
    URL https://github.com/freetype/freetype/archive/refs/tags/VER-2-12-1.tar.gz
    URL_HASH SHA256=0E72CAE32751598D126CFD4BCEDA909F646B7231AB8C52E28ABB686C20A2BEA1
    #DEPENDS ${ZLIB_PKG}
    #"${_patch_step}"
    CMAKE_ARGS
	-D BUILD_SHARED_LIBS=${library_build_shared}
	${_ft_disable_zlib}
        -D FT_DISABLE_BZIP2=TRUE
        -D FT_DISABLE_PNG=TRUE
        -D FT_DISABLE_HARFBUZZ=TRUE
        -D FT_DISABLE_BROTLI=TRUE
)

if(MSVC)
    add_debug_dep(dep_FREETYPE)
endif()

# On macOS the FT_DISABLE_ZLIB option above does NOT prevent FreeType from
# linking the bundled zlib code into ftgzip.c.o - the inline copies of
# inflate / inflateInit_ / zcalloc / adler32 etc. stay as global T symbols.
# When BambuStudio is statically linked, those globals satisfy Assimp's
# `inflateInit_` reference with FreeType's bundled 1.2.12 code while Assimp's
# `inflate` still binds dynamically to the macOS system libz (1.2.11). The
# resulting z_stream/state ABI mismatch makes Assimp's FBX 7.4 binary parser
# fail with "CompressionFailure decompressing this file using gzip." on any
# FBX that uses compressed property arrays. Strip the zlib externals after
# install so only FT_Gzip_Uncompress / FT_Stream_OpenGzip remain visible.
if(APPLE)
    ExternalProject_Add_Step(dep_FREETYPE strip_zlib_globals
        COMMENT "Stripping FreeType bundled-zlib globals from libfreetype.a (macOS)"
        DEPENDEES install
        ALWAYS 1
        COMMAND ${CMAKE_COMMAND} -E env bash
                "${CMAKE_CURRENT_LIST_DIR}/strip_libfreetype_zlib.sh"
                "${DESTDIR}/usr/local/lib/libfreetype.a"
    )
endif()
