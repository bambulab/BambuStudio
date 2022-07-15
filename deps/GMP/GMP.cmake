
set(_srcdir ${CMAKE_CURRENT_LIST_DIR}/gmp)
set(_dstdir ${DESTDIR}/usr/local)

if (MSVC)
    set(_output  ${_dstdir}/include/gmp.h 
                 ${_dstdir}/lib/libgmp-10.lib 
                 ${_dstdir}/bin/libgmp-10.dll)

    add_custom_command(
        OUTPUT  ${_output}
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/include/gmp.h ${_dstdir}/include/
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/lib/win${DEPS_BITS}/libgmp-10.lib ${_dstdir}/lib/
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/lib/win${DEPS_BITS}/libgmp-10.dll ${_dstdir}/bin/
    )
    
    add_custom_target(dep_GMP SOURCES ${_output})

else ()
    set(_gmp_ccflags "-O2 -DNDEBUG -fPIC -DPIC -Wall -Wmissing-prototypes -Wpointer-arith -pedantic -fomit-frame-pointer -fno-common")
    set(_gmp_build_tgt "${CMAKE_SYSTEM_PROCESSOR}")

    if (APPLE)
        if (${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm")
            set(_gmp_build_tgt aarch64)
        endif()
        set(_gmp_ccflags "${_gmp_ccflags} -mmacosx-version-min=${DEP_OSX_TARGET}")
        set(_gmp_build_tgt "--build=${_gmp_build_tgt}-apple-darwin")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        if (${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm")
            set(_gmp_ccflags "${_gmp_ccflags} -march=armv7-a") # Works on RPi-4
            set(_gmp_build_tgt armv7)
        endif()
        set(_gmp_build_tgt "--build=${_gmp_build_tgt}-pc-linux-gnu")
    else ()
        set(_gmp_build_tgt "") # let it guess
    endif()

    set(_cross_compile_arg "")
    if (CMAKE_CROSSCOMPILING)
        # TOOLCHAIN_PREFIX should be defined in the toolchain file
        set(_cross_compile_arg --host=${TOOLCHAIN_PREFIX})
    endif ()

    ExternalProject_Add(dep_GMP
        URL https://gmplib.org/download/gmp/gmp-6.2.1.tar.bz2
        URL_HASH SHA256=eae9326beb4158c386e39a356818031bd28f3124cf915f8c5b1dc4c7a36b4d7c
        DOWNLOAD_DIR ${DEP_DOWNLOAD_DIR}/GMP
        BUILD_IN_SOURCE ON 
        CONFIGURE_COMMAND  env "CFLAGS=${_gmp_ccflags}" "CXXFLAGS=${_gmp_ccflags}" ./configure ${_cross_compile_arg} --enable-shared=no --enable-cxx=yes --enable-static=yes "--prefix=${DESTDIR}/usr/local" ${_gmp_build_tgt}
        BUILD_COMMAND     make -j
        INSTALL_COMMAND   make install
    )
endif ()
