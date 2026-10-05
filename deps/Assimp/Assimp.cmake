# `git apply` run inside a work tree resolves the patch paths against the
# repository root and silently skips ("Skipped patch ...", exit code 0) anything
# outside the current prefix. Without this flag the security patches below are a
# no-op whenever the deps tree is built inside the BambuStudio checkout, so tell
# git where the extracted sources actually live. deps/CMakeLists.txt defines
# BINARY_DIR_REL only when it detected a work tree, which makes it the marker to
# test here - IN_GIT_REPO, which CGAL.cmake and GMP.cmake test, is never set.
if (BINARY_DIR_REL)
    set(ASSIMP_DIRECTORY_FLAG --directory ${BINARY_DIR_REL}/dep_Assimp-prefix/src/dep_Assimp)
endif ()

if(CMAKE_VERSION VERSION_LESS 3.22)
    set(_assimp_url "https://github.com/assimp/assimp/archive/refs/tags/v5.3.1.tar.gz")
    set(_assimp_hash "SHA256=a07666be71afe1ad4bc008c2336b7c688aca391271188eb9108d0c6db1be53f1")
else()
    set(_assimp_url "https://github.com/assimp/assimp/archive/refs/tags/v5.4.3.tar.gz")
    set(_assimp_hash "SHA256=66dfbaee288f2bc43172440a55d0235dfc7bf885dda6435c038e8000e79582cb")
endif()

# Assimp's bundled zlib (contrib/zlib) is too old to compile against the modern
# macOS SDK: its zutil.h takes the classic-Mac branch under TARGET_OS_MAC and
# does `#define fdopen(fd,mode) NULL`, which then clobbers the SDK's real
# `fdopen` prototype in <stdio.h> and breaks the build. On macOS use the system
# zlib (already found by find_package(ZLIB) in deps-unix-common) instead.
if(APPLE)
    set(_assimp_build_zlib "-DASSIMP_BUILD_ZLIB=OFF")
else()
    set(_assimp_build_zlib "-DASSIMP_BUILD_ZLIB=ON")
endif()

bambustudio_add_cmake_project(Assimp
    URL ${_assimp_url}
    URL_HASH ${_assimp_hash}
    # Both patches apply cleanly to v5.4.3 and v5.3.1: the code they touch is
    # identical in the two tags, so a single copy covers either branch above.
    PATCH_COMMAND git apply ${ASSIMP_DIRECTORY_FLAG} --verbose --ignore-space-change --whitespace=fix ${CMAKE_CURRENT_LIST_DIR}/0001-FBX-fix-negative-TypedIndex-OOB.patch ${CMAKE_CURRENT_LIST_DIR}/0002-glTF1-fix-index-accessor-type-confusion-OOB.patch
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
        ${_assimp_build_zlib}
        -DASSIMP_WARNINGS_AS_ERRORS=OFF
        -DBUILD_WITH_STATIC_CRT=OFF
)

if (MSVC)
    add_debug_dep(dep_Assimp)
endif ()
