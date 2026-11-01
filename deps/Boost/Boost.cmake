
set(_context_abi_line "")
set(_context_arch_line "")
set(_context_impl_line "")
if (APPLE AND CMAKE_OSX_ARCHITECTURES)
    if (CMAKE_OSX_ARCHITECTURES MATCHES "x86")
        set(_context_abi_line "-DBOOST_CONTEXT_ABI:STRING=sysv")
    elseif (CMAKE_OSX_ARCHITECTURES MATCHES "arm")
        set (_context_abi_line "-DBOOST_CONTEXT_ABI:STRING=aapcs")
    endif ()
    set(_context_arch_line "-DBOOST_CONTEXT_ARCHITECTURE:STRING=${CMAKE_OSX_ARCHITECTURES}")
    message(STATUS "BOOST param: ${_context_abi_line} ${_context_arch_line}")
endif ()

if (MSVC)
    set(_msvc_target_arch "")
    if (CMAKE_GENERATOR_PLATFORM)
        set(_msvc_target_arch "${CMAKE_GENERATOR_PLATFORM}")
    elseif (CMAKE_VS_PLATFORM_NAME)
        set(_msvc_target_arch "${CMAKE_VS_PLATFORM_NAME}")
    elseif (DEFINED DEP_PLATFORM)
        set(_msvc_target_arch "${DEP_PLATFORM}")
    elseif (CMAKE_SYSTEM_PROCESSOR)
        set(_msvc_target_arch "${CMAKE_SYSTEM_PROCESSOR}")
    endif ()
    string(TOUPPER "${_msvc_target_arch}" _msvc_target_arch)
    if (_msvc_target_arch STREQUAL "ARM64" OR _msvc_target_arch STREQUAL "AARCH64")
        set(_context_abi_line "-DBOOST_CONTEXT_ABI:STRING=aapcs")
        set(_context_arch_line "-DBOOST_CONTEXT_ARCHITECTURE:STRING=arm64")
        set(_context_impl_line "-DBOOST_CONTEXT_IMPLEMENTATION:STRING=winfib")
        message(STATUS "BOOST param: ${_context_abi_line} ${_context_arch_line} ${_context_impl_line}")
    elseif (_msvc_target_arch STREQUAL "ARM64EC")
        # ARM64EC uses the x64-compatible ABI; the Windows Fiber implementation
        # needs no assembler, sidestepping fcontext arch detection entirely.
        set(_context_impl_line "-DBOOST_CONTEXT_IMPLEMENTATION:STRING=winfib")
        # Boost.JSON's ryu needs __shiftright128, which ARM64EC lacks (no
        # native lowering, no softintrin fallback); cobalt is in the same
        # C++20-era boat. Neither is used by BambuStudio.
        set(_boost_ec_extra_excludes "|json|cobalt")
        message(STATUS "BOOST param: ${_context_impl_line}")
    elseif (_msvc_target_arch STREQUAL "X64")
        # Pin arch AND abi: Boost.Context otherwise sniffs the HOST processor,
        # so building x64 deps on an ARM64 host selects armasm/aapcs and fails
        # (x86_64 + aapcs has no asm source; the x64 Windows ABI is 'ms').
        set(_context_abi_line "-DBOOST_CONTEXT_ABI:STRING=ms")
        set(_context_arch_line "-DBOOST_CONTEXT_ARCHITECTURE:STRING=x86_64")
        message(STATUS "BOOST param: ${_context_abi_line} ${_context_arch_line}")
    endif ()
endif ()

bambustudio_add_cmake_project(Boost
    URL "https://github.com/boostorg/boost/releases/download/boost-1.84.0/boost-1.84.0.tar.gz"
    URL_HASH SHA256=4d27e9efed0f6f152dc28db6430b9d3dfb40c0345da7342eaa5a987dde57bd95
    LIST_SEPARATOR |
    PATCH_COMMAND git apply --verbose --ignore-space-change --whitespace=fix ${CMAKE_CURRENT_LIST_DIR}/0001-FIX-OBS-cannot-start-streaming-on-MAC.patch
    CMAKE_ARGS
        -DBOOST_EXCLUDE_LIBRARIES:STRING=contract|fiber|numpy|wave|test${_boost_ec_extra_excludes}
        -DBOOST_LOCALE_ENABLE_ICU:BOOL=OFF # do not link to libicu, breaks compatibility between distros
        -DBUILD_TESTING:BOOL=OFF
        "${_context_abi_line}"
        "${_context_arch_line}"
        "${_context_impl_line}"
)

set(DEP_Boost_DEPENDS ZLIB)
