if(CMAKE_VERSION VERSION_LESS 3.22)
    set(_assimp_url "https://github.com/assimp/assimp/archive/refs/tags/v5.3.1.tar.gz")
    set(_assimp_hash "SHA256=a07666be71afe1ad4bc008c2336b7c688aca391271188eb9108d0c6db1be53f1")
else()
    set(_assimp_url "https://github.com/assimp/assimp/archive/refs/tags/v5.4.3.tar.gz")
    set(_assimp_hash "SHA256=66dfbaee288f2bc43172440a55d0235dfc7bf885dda6435c038e8000e79582cb")
endif()

bambustudio_add_cmake_project(Assimp
    URL ${_assimp_url}
    URL_HASH ${_assimp_hash}
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
        -DASSIMP_BUILD_ZLIB=OFF
        -DASSIMP_WARNINGS_AS_ERRORS=OFF
        -DBUILD_WITH_STATIC_CRT=OFF
)

if (MSVC)
    add_debug_dep(dep_Assimp)
endif ()
