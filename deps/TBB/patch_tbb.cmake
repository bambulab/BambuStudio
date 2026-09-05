# Applied as PATCH_COMMAND for oneTBB. Two fixes, both idempotent:
#
# 1. Remove the hard-coded /GL (LTCG) from the MSVC release flags. oneTBB
#    2021.5 has no option for this (TBB_ENABLE_IPO came later). x64 LTCG
#    objects cannot participate in an ARM64EC link (C1905 front end/back end
#    mismatch), and this static lib is consumed by the ARM64EC BambuStudio
#    build.
#
# 2. Guard the TSX (hardware transactional memory) intrinsics against
#    ARM64EC. ARM64EC defines _M_X64, so TBB assumes _xbegin/_xend/_xabort
#    exist, but softintrin cannot emulate hardware transactions and the
#    symbols fail to link.

set(_msvc_cmake "cmake/compilers/MSVC.cmake")
if(EXISTS "${_msvc_cmake}")
    file(READ "${_msvc_cmake}" _content)
    set(_old "set(TBB_IPO_COMPILE_FLAGS $<$<NOT:$<CONFIG:Debug>>:/GL>)")
    if(_content MATCHES "TBB_IPO_COMPILE_FLAGS \"\"")
        message(STATUS "[TBB patch] /GL already removed")
    else()
        string(REPLACE "${_old}" "set(TBB_IPO_COMPILE_FLAGS \"\")" _patched "${_content}")
        if(_patched STREQUAL _content)
            message(FATAL_ERROR "[TBB patch] failed to match /GL line in ${_msvc_cmake}")
        endif()
        file(WRITE "${_msvc_cmake}" "${_patched}")
        message(STATUS "[TBB patch] removed /GL from MSVC release flags")
    endif()
else()
    message(FATAL_ERROR "[TBB patch] not found: ${_msvc_cmake}")
endif()

# 3. Provide link-time definitions of the TSX intrinsics for ARM64EC builds.
#    rtm_mutex.cpp/rtm_rw_mutex.cpp call _xbegin/_xend/_xabort unconditionally;
#    on ARM64EC those lower to extern calls with no softintrin implementation.
#    The code paths are unreachable at runtime (the governor only enables RTM
#    when CPUID reports TSX, which the x64 emulator never does), but the
#    symbols must resolve. A separate stub TU avoids touching the RTM sources.
set(_stub "src/tbb/arm64ec_tsx_stub.cpp")
set(_tbb_cml "src/tbb/CMakeLists.txt")
if(NOT EXISTS "${_stub}")
    file(WRITE "${_stub}" [=[
// Link-time stubs for Intel TSX intrinsics on ARM64EC (no hardware TSX, no
// softintrin fallback). Unreachable at runtime: TBB only takes RTM paths when
// CPUID reports TSX support, which the x64/EC emulator never does.
#if defined(_M_ARM64EC)
extern "C" unsigned int _xbegin(void) { return 0u; /* abort, no retry */ }
extern "C" void _xend(void) {}
extern "C" void _xabort(unsigned int) {}
#endif
]=])
    message(STATUS "[TBB patch] wrote ${_stub}")
endif()
if(EXISTS "${_tbb_cml}")
    file(READ "${_tbb_cml}" _content)
    if(_content MATCHES "arm64ec_tsx_stub")
        message(STATUS "[TBB patch] stub already registered")
    else()
        string(REPLACE "rtm_mutex.cpp" "rtm_mutex.cpp\n    arm64ec_tsx_stub.cpp" _patched "${_content}")
        if(_patched STREQUAL _content)
            message(FATAL_ERROR "[TBB patch] failed to register stub in ${_tbb_cml}")
        endif()
        file(WRITE "${_tbb_cml}" "${_patched}")
        message(STATUS "[TBB patch] registered arm64ec_tsx_stub.cpp")
    endif()
else()
    message(FATAL_ERROR "[TBB patch] not found: ${_tbb_cml}")
endif()

set(_config_h "include/oneapi/tbb/detail/_config.h")
if(EXISTS "${_config_h}")
    file(READ "${_config_h}" _content)
    set(_old "#define __TBB_TSX_INTRINSICS_PRESENT (__RTM__ || __INTEL_COMPILER || (_MSC_VER>=1700 && (__TBB_x86_64 || __TBB_x86_32)))")
    set(_new "#define __TBB_TSX_INTRINSICS_PRESENT ((__RTM__ || __INTEL_COMPILER || (_MSC_VER>=1700 && (__TBB_x86_64 || __TBB_x86_32))) && !defined(_M_ARM64EC))")
    if(_content MATCHES "_M_ARM64EC")
        message(STATUS "[TBB patch] TSX guard already applied")
    else()
        string(REPLACE "${_old}" "${_new}" _patched "${_content}")
        if(_patched STREQUAL _content)
            message(FATAL_ERROR "[TBB patch] failed to match TSX guard in ${_config_h}")
        endif()
        file(WRITE "${_config_h}" "${_patched}")
        message(STATUS "[TBB patch] guarded TSX intrinsics against ARM64EC")
    endif()
else()
    message(FATAL_ERROR "[TBB patch] not found: ${_config_h}")
endif()
