set(_srcdir ${CMAKE_CURRENT_LIST_DIR}/mpfr)
set(_dstdir ${DESTDIR}/usr/local)

if (MSVC)
    if ((CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|aarch64)$") OR (CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64"))
        set(_output  ${_dstdir}/include/mpfr.h
                 ${_dstdir}/include/mpf2mpfr.h
                 ${_dstdir}/lib/libmpfr-6.lib 
                 ${_dstdir}/bin/mpfr-6.dll)
        set(_mpfrheader win_arm64)
        set(_mpfrlib win_arm64/libmpfr-6.lib)
        set(_mpfrdll win_arm64/mpfr-6.dll)
    else ()
        set(_output  ${_dstdir}/include/mpfr.h
                 ${_dstdir}/include/mpf2mpfr.h
                 ${_dstdir}/lib/libmpfr-4.lib 
                 ${_dstdir}/bin/libmpfr-4.dll)
        set(_mpfrheader win64)
        set(_mpfrlib win${DEPS_BITS}/libmpfr-4.lib)
        set(_mpfrdll win${DEPS_BITS}/libmpfr-4.dll)
    endif ()

    add_custom_command(
        OUTPUT  ${_output}
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/include/${_mpfrheader}/mpfr.h ${_dstdir}/include/
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/include/${_mpfrheader}/mpf2mpfr.h ${_dstdir}/include/
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/lib/${_mpfrlib} ${_dstdir}/lib/
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/lib/${_mpfrdll} ${_dstdir}/bin/
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
