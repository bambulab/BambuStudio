if (WIN32)
    set(_assimp_build_zlib ON)
else ()
    # On macOS the bundled zlib's fdopen() macro conflicts with the macOS 15+ SDK
    # __DARWIN_ALIAS(fdopen) expansion, causing a parse error during dep builds.
    # System zlib is always available on macOS/Linux, so disable the bundled copy.
    set(_assimp_build_zlib OFF)
endif ()

bambustudio_add_cmake_project(Assimp
    URL "https://github.com/assimp/assimp/archive/refs/tags/v5.4.3.tar.gz"
    URL_HASH SHA256=66dfbaee288f2bc43172440a55d0235dfc7bf885dda6435c038e8000e79582cb
    CMAKE_ARGS
        -DASSIMP_BUILD_TESTS=OFF
        -DASSIMP_BUILD_SAMPLES=OFF
        -DASSIMP_BUILD_ASSIMP_TOOLS=OFF
        -DASSIMP_INSTALL_PDB=OFF
        -DASSIMP_NO_EXPORT=ON
        -DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=OFF
        -DASSIMP_BUILD_GLTF_IMPORTER=ON
        -DASSIMP_BUILD_OBJ_IMPORTER=ON
        -DASSIMP_BUILD_FBX_IMPORTER=ON
        -DASSIMP_BUILD_ZLIB=${_assimp_build_zlib}
        -DASSIMP_WARNINGS_AS_ERRORS=OFF
        -DBUILD_WITH_STATIC_CRT=OFF
)

if (MSVC)
    add_debug_dep(dep_Assimp)
endif ()
