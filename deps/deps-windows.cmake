# https://cmake.org/cmake/help/latest/variable/MSVC_VERSION.html
message(STATUS "MSVC_VERSION: ${MSVC_VERSION}")
if (MSVC_VERSION EQUAL 1800)
# 1800      = VS 12.0 (v120 toolset)
    set(DEP_VS_VER "12")
    set(DEP_BOOST_TOOLSET "msvc-12.0")
elseif (MSVC_VERSION EQUAL 1900)
# 1900      = VS 14.0 (v140 toolset)    
    set(DEP_VS_VER "14")
    set(DEP_BOOST_TOOLSET "msvc-14.0")
elseif (MSVC_VERSION LESS 1920)
# 1910-1919 = VS 15.0 (v141 toolset)
    set(DEP_VS_VER "15")
    set(DEP_BOOST_TOOLSET "msvc-14.1")
elseif (MSVC_VERSION LESS 1930)
# 1920-1929 = VS 16.0 (v142 toolset)
    set(DEP_VS_VER "16")
    set(DEP_BOOST_TOOLSET "msvc-14.2")
elseif (MSVC_VERSION LESS 1950)
# 1930-1949 = VS 17.0 (v143 toolset)
    set(DEP_VS_VER "17")
    set(DEP_BOOST_TOOLSET "msvc-14.3")
elseif (MSVC_VERSION LESS 1970)
# 1950-1969 = VS 18.0 (v144 toolset)
    set(DEP_VS_VER "18")
    set(DEP_BOOST_TOOLSET "msvc-14.4")
else ()
    message(FATAL_ERROR "Unsupported MSVC version")
endif ()

if (CMAKE_CXX_COMPILER_ID STREQUAL Clang)
    set(DEP_BOOST_TOOLSET "clang-win")
endif ()

set(DEP_ARCH "")
if (CMAKE_GENERATOR_PLATFORM)
    set(DEP_ARCH "${CMAKE_GENERATOR_PLATFORM}")
elseif (CMAKE_SYSTEM_PROCESSOR)
    set(DEP_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
endif ()
string(TOUPPER "${DEP_ARCH}" DEP_ARCH)

if (${DEPS_BITS} EQUAL 32)
    set(DEP_MSVC_GEN "Visual Studio ${DEP_VS_VER}")
    set(DEP_PLATFORM "Win32")
else ()
    if (DEP_VS_VER LESS 16 AND NOT DEP_ARCH STREQUAL "ARM64")
        set(DEP_MSVC_GEN "Visual Studio ${DEP_VS_VER} Win64")
    else ()
        set(DEP_MSVC_GEN "Visual Studio ${DEP_VS_VER}")
    endif ()

    if (DEP_ARCH STREQUAL "ARM64")
        set(DEP_PLATFORM "ARM64")
    elseif (DEP_ARCH STREQUAL "ARM64EC")
        # ARM64EC: native ARM64 code with an x64-compatible ABI, so an ARM64EC
        # app can link these deps AND load x64-only DLLs (e.g. the Bambu
        # network plugin) in-process. CMake-based deps build with -A ARM64EC.
        set(DEP_PLATFORM "ARM64EC")
    else ()
        set(DEP_PLATFORM "x64")
    endif ()
endif ()

set(DEP_CMAKE_OPTS "-DCMAKE_POLICY_VERSION_MINIMUM=3.5")
if (DEP_PLATFORM STREQUAL "ARM64")
    list(APPEND DEP_CMAKE_OPTS "-DCMAKE_SYSTEM_PROCESSOR=ARM64")
endif ()
if (DEP_PLATFORM STREQUAL "ARM64EC")
    # ARM64EC defines _M_X64, and the x86 SIMD headers (emmintrin.h etc.)
    # refuse direct inclusion there - they must arrive via <intrin.h> so the
    # softintrin emulation layer is set up first; /FIintrin.h forces that for
    # third-party code that includes them directly. /FI in turn breaks the
    # common "#define _CRT_SECURE_NO_WARNINGS before includes" idiom (the CRT
    # headers are already in), so silence C4996/C4005 which several deps
    # (boost.nowide among them) promote to errors. The flag strings must
    # restate CMake's default MSVC flags: CMAKE_ARGS replaces, not appends,
    # and losing /EHsc breaks any dep that requires exception handling.
    list(APPEND DEP_CMAKE_OPTS
        "-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS /W3 /wd4996 /wd4005 /FIintrin.h"
        "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /W3 /GR /EHsc /wd4996 /wd4005 /FIintrin.h")
endif ()

if (${DEP_DEBUG})
    set(DEP_BOOST_DEBUG "debug")
else ()
    set(DEP_BOOST_DEBUG "")
endif ()

macro(add_debug_dep _dep)
if (${DEP_DEBUG})
    ExternalProject_Get_Property(${_dep} BINARY_DIR)
    ExternalProject_Add_Step(${_dep} build_debug
        DEPENDEES build
        DEPENDERS install
        COMMAND msbuild /m /P:Configuration=Debug INSTALL.vcxproj
        WORKING_DIRECTORY "${BINARY_DIR}"
    )
endif ()
endmacro()

if (${DEPS_BITS} EQUAL 32)
    set(DEP_WXWIDGETS_TARGET "")
    set(DEP_WXWIDGETS_LIBDIR "vc_lib")
else ()
    if (DEP_ARCH STREQUAL "ARM64")
        set(DEP_WXWIDGETS_TARGET "TARGET_CPU=ARM64")
        set(DEP_WXWIDGETS_LIBDIR "vc_arm64_lib")
    else ()
        set(DEP_WXWIDGETS_TARGET "TARGET_CPU=X64")
        set(DEP_WXWIDGETS_LIBDIR "vc_x64_lib")
    endif ()
endif ()

find_package(Git REQUIRED)
