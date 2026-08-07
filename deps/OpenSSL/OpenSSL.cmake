
include(ProcessorCount)
ProcessorCount(NPROC)

if(DEFINED OPENSSL_ARCH)
    set(_cross_arch ${OPENSSL_ARCH})
else()
    if(WIN32)
        # Detect ARM64 whether the platform arrived as -A ARM64 (CI) or -A arm64
        # (build_win.bat lowercases it). The generator platform takes precedence
        # over the host processor so an ARM64 host can still build x64 deps.
        set(_win_target_plat "${CMAKE_GENERATOR_PLATFORM}")
        if(NOT _win_target_plat)
            set(_win_target_plat "${CMAKE_SYSTEM_PROCESSOR}")
        endif()
        string(TOUPPER "${_win_target_plat}" _win_target_plat)
        if(_win_target_plat MATCHES "^(ARM64|AARCH64)$")
            set(_cross_arch "VC-WIN64-ARM")
        else()
            set(_cross_arch "VC-WIN64A")
        endif()
    elseif(APPLE)
        set(_cross_arch "darwin64-arm64-cc")
	endif()
endif()

if(WIN32)
    set(_conf_cmd perl Configure )
    set(_cross_comp_prefix_line "")
    set(_make_cmd nmake)
    set(_install_cmd nmake install_sw )
    if(_win_target_plat STREQUAL "ARM64EC")
        # ARM64EC links the x64 OpenSSL static libs (EC and x64 objects mix),
        # but VC-WIN64A requires an x64-targeting cl. The deps environment
        # targets arm64, so run the OpenSSL steps under the x64 cross tools
        # via a generated wrapper batch file.
        set(_vsdevcmd "$ENV{VSINSTALLDIR}Common7\\Tools\\vsdevcmd.bat")
        if(NOT EXISTS "${_vsdevcmd}")
            execute_process(
                COMMAND "$ENV{ProgramFiles\(x86\)}\\Microsoft Visual Studio\\Installer\\vswhere.exe"
                        -latest -products * -property installationPath
                OUTPUT_VARIABLE _vs_root OUTPUT_STRIP_TRAILING_WHITESPACE)
            set(_vsdevcmd "${_vs_root}\\Common7\\Tools\\vsdevcmd.bat")
        endif()
        if(NOT EXISTS "${_vsdevcmd}")
            message(FATAL_ERROR "OpenSSL ARM64EC: cannot locate vsdevcmd.bat for the x64 cross environment")
        endif()
        set(_x64env_bat "${CMAKE_CURRENT_BINARY_DIR}/openssl_x64env.bat")
        file(WRITE "${_x64env_bat}" "@echo off\r\ncall \"${_vsdevcmd}\" -arch=x64 -host_arch=arm64 -no_logo\r\n%*\r\n")
        set(_cross_arch "VC-WIN64A")
        set(_conf_cmd "${_x64env_bat}" perl Configure )
        set(_make_cmd "${_x64env_bat}" nmake)
        set(_install_cmd "${_x64env_bat}" nmake install_sw )
    endif()
else()
    if(APPLE)
        set(_conf_cmd export MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET} && ./Configure -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET} )
    else()
        set(_conf_cmd "./config")
    endif()
    set(_cross_comp_prefix_line "")
    set(_make_cmd make -j${NPROC})
    set(_install_cmd make -j${NPROC} install_sw)
    if (CMAKE_CROSSCOMPILING)
        set(_cross_comp_prefix_line "--cross-compile-prefix=${TOOLCHAIN_PREFIX}-")

        if (${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64" OR ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "arm64")
            set(_cross_arch "linux-aarch64")
        elseif (${CMAKE_SYSTEM_PROCESSOR} STREQUAL "armhf") # For raspbian
            # TODO: verify
            set(_cross_arch "linux-armv4")
        endif ()
    endif ()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
set(url_str "https://github.com/openssl/openssl/archive/OpenSSL_1_1_1k.tar.gz")
set(url_hash "SHA256=b92f9d3d12043c02860e5e602e50a73ed21a69947bcc74d391f41148e9f6aa95")
else()
set(url_str "https://github.com/openssl/openssl/archive/refs/tags/openssl-3.1.2.tar.gz")
set(url_hash "SHA256=8c776993154652d0bb393f506d850b811517c8bd8d24b1008aef57fbe55d3f31")
endif()
# set(url_str "https://github.com/openssl/openssl/archive/OpenSSL_1_1_1w.tar.gz")
# set(url_hash "SHA256=2130E8C2FB3B79D1086186F78E59E8BC8D1A6AEDF17AB3907F4CB9AE20918C41")
ExternalProject_Add(dep_OpenSSL
    #EXCLUDE_FROM_ALL ON
    URL ${url_str}
    URL_HASH ${url_hash}
    DOWNLOAD_DIR ${DEP_DOWNLOAD_DIR}/OpenSSL
	CONFIGURE_COMMAND ${_conf_cmd} ${_cross_arch}
        "--openssldir=${DESTDIR}/usr/local"
        "--prefix=${DESTDIR}/usr/local"
        ${_cross_comp_prefix_line}
        no-shared
        no-asm
        no-tests
        no-ssl3-method
        no-dynamic-engine
    BUILD_IN_SOURCE ON
    BUILD_COMMAND ${_make_cmd}
    INSTALL_COMMAND ${_install_cmd}
)

ExternalProject_Add_Step(dep_OpenSSL install_cmake_files
    DEPENDEES install

    COMMAND ${CMAKE_COMMAND} -E copy_directory openssl "${DESTDIR}/usr/local/${CMAKE_INSTALL_LIBDIR}/cmake/openssl"
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
)
