
include(ProcessorCount)
ProcessorCount(NPROC)

if(DEFINED OPENSSL_ARCH)
    set(_cross_arch ${OPENSSL_ARCH})
else()
    if(WIN32)
        set(_cross_arch "VC-WIN64A")
    elseif(APPLE)
        set(_cross_arch "darwin64-arm64-cc")
	endif()
endif()

if(WIN32)
    set(_conf_cmd perl Configure )
    set(_cross_comp_prefix_line "")
    set(_make_cmd nmake)
    set(_install_cmd nmake install_sw )
else()
    if(APPLE)
        set(_conf_cmd ./Configure )
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
