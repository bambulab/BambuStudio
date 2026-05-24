# On Windows (MSVC) there is no guaranteed system zlib when the dep cache is
# cold. dep_ZLIB and dep_Assimp build in parallel, so find_package(ZLIB) can
# race and fail. Let Assimp use its own bundled zlib on Windows.
# On macOS / Linux the system or dep_ZLIB is already available and the bundled
# copy conflicts with SDK headers (macOS 15.5 _stdio.h macro collision).
if (MSVC)
    set(_assimp_zlib -DASSIMP_BUILD_ZLIB=ON)
else()
    set(_assimp_zlib -DASSIMP_BUILD_ZLIB=OFF)
endif()

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
        ${_assimp_zlib}
        -DASSIMP_WARNINGS_AS_ERRORS=OFF
        -DBUILD_WITH_STATIC_CRT=OFF
)

if (MSVC)
    add_debug_dep(dep_Assimp)
endif ()
