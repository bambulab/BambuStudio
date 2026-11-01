# The ARM64EC fixes (drop the hard-coded /GL that blocks EC links, guard/stub
# the TSX intrinsics) are MSVC-only. Keep them off non-MSVC toolchains so
# macOS/Linux build TBB exactly as before: in particular TBB_STRICT=OFF makes
# oneTBB rewrite the compile flags, which mangles the macOS -Werror=<availability>
# flags into bare '=<availability>' tokens (LNK/compile failure).
set(_tbb_patch "")
set(_tbb_strict "")
if (MSVC)
    # Removes the hard-coded /GL (blocks ARM64EC links) and guards TSX
    # intrinsics against ARM64EC. On x64 this only drops LTCG (same code).
    set(_tbb_patch PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_LIST_DIR}/patch_tbb.cmake)
    # ARM64EC: /FIintrin.h makes cl warn C4163 for x86-only intrinsics
    # (__rdtsc, _mm_mfence) that softintrin handles at link time; TBB's default
    # warnings-as-errors turns those fatal.
    set(_tbb_strict -DTBB_STRICT=OFF)
endif ()

bambustudio_add_cmake_project(
    TBB
    URL "https://github.com/oneapi-src/oneTBB/archive/refs/tags/v2021.5.0.zip"
    URL_HASH SHA256=83ea786c964a384dd72534f9854b419716f412f9d43c0be88d41874763e7bb47
    #PATCH_COMMAND ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/0001-TBB-GCC13.patch
    ${_tbb_patch}
    CMAKE_ARGS
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        ${_tbb_strict}
        -DTBB_BUILD_SHARED=OFF
        -DTBB_BUILD_TESTS=OFF
        -DTBB_TEST=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_DEBUG_POSTFIX=_debug
)

if (MSVC)
    add_debug_dep(dep_TBB)
endif ()


