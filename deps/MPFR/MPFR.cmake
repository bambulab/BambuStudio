set(_srcdir ${CMAKE_CURRENT_LIST_DIR}/mpfr)
set(_dstdir ${DESTDIR}/usr/local)

if (MSVC)
    set(_output  ${_dstdir}/include/mpfr.h
                 ${_dstdir}/include/mpf2mpfr.h
                 ${_dstdir}/lib/libmpfr-4.lib 
                 ${_dstdir}/bin/libmpfr-4.dll)

    add_custom_command(
        OUTPUT  ${_output}
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/include/mpfr.h ${_dstdir}/include/
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/include/mpf2mpfr.h ${_dstdir}/include/
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/lib/win${DEPS_BITS}/libmpfr-4.lib ${_dstdir}/lib/
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/lib/win${DEPS_BITS}/libmpfr-4.dll ${_dstdir}/bin/
    )

    add_custom_target(dep_MPFR SOURCES ${_output})

else ()

    set(_cross_compile_arg "")
    if (CMAKE_CROSSCOMPILING)
        # TOOLCHAIN_PREFIX should be defined in the toolchain file
        set(_cross_compile_arg --host=${TOOLCHAIN_PREFIX})
    endif ()

    ExternalProject_Add(dep_MPFR
        URL http://ftp.vim.org/ftp/gnu/mpfr/mpfr-3.1.6.tar.bz2 https://www.mpfr.org/mpfr-3.1.6/mpfr-3.1.6.tar.bz2  # mirrors are allowed
        URL_HASH SHA256=cf4f4b2d80abb79e820e78c8077b6725bbbb4e8f41896783c899087be0e94068
        DOWNLOAD_DIR ${DEP_DOWNLOAD_DIR}/MPFR
        BUILD_IN_SOURCE ON
        CONFIGURE_COMMAND env "CFLAGS=${_gmp_ccflags}" "CXXFLAGS=${_gmp_ccflags}" ./configure ${_cross_compile_arg} --prefix=${DESTDIR}/usr/local --enable-shared=no --enable-static=yes --with-gmp=${DESTDIR}/usr/local ${_gmp_build_tgt}
        BUILD_COMMAND make -j
        INSTALL_COMMAND make install
        DEPENDS dep_GMP
    )
endif ()
