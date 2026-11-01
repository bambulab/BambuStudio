set(_srcdir ${CMAKE_CURRENT_LIST_DIR}/mpfr)
set(_dstdir ${DESTDIR}/usr/local)

if (MSVC)
    # Prefer the generator platform (-A ...) over the host processor so that an
    # ARM64 host building an x64 (or ARM64EC) target stages the right blobs.
    set(_mpfr_plat "${CMAKE_GENERATOR_PLATFORM}")
    if (NOT _mpfr_plat)
        set(_mpfr_plat "${CMAKE_SYSTEM_PROCESSOR}")
    endif ()
    string(TOUPPER "${_mpfr_plat}" _mpfr_plat)
    if (_mpfr_plat MATCHES "^(ARM64|AARCH64)$")
        set(_output  ${_dstdir}/include/mpfr.h
                 ${_dstdir}/include/mpf2mpfr.h
                 ${_dstdir}/lib/libmpfr-6.lib
                 ${_dstdir}/lib/libmpfr-4.lib
                 ${_dstdir}/bin/mpfr-6.dll)
        set(_mpfrheader win_arm64)
        set(_mpfrlib win_arm64/libmpfr-6.lib)
        set(_mpfrdll win_arm64/mpfr-6.dll)
        # CGAL's FindMPFR searches a fixed name list (mpfr, libmpfr-4, libmpfr-1)
        # that predates the MPFR 4.x "-6" ABI naming, so it misses libmpfr-6.lib
        # and the app configure fails with "Could NOT find MPFR". Install an
        # aliased copy under the legacy name; the import library's filename does
        # not affect runtime binding (it still references mpfr-6.dll internally).
        set(_mpfrlib_alias ${_dstdir}/lib/libmpfr-4.lib)
    else ()
        set(_output  ${_dstdir}/include/mpfr.h
                 ${_dstdir}/include/mpf2mpfr.h
                 ${_dstdir}/lib/libmpfr-4.lib 
                 ${_dstdir}/bin/libmpfr-4.dll)
        set(_mpfrheader win64)
        set(_mpfrlib win${DEPS_BITS}/libmpfr-4.lib)
        set(_mpfrdll win${DEPS_BITS}/libmpfr-4.dll)
    endif ()

    set(_mpfr_alias_cmd "")
    if (DEFINED _mpfrlib_alias)
        set(_mpfr_alias_cmd COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/lib/${_mpfrlib} ${_mpfrlib_alias})
    endif ()

    add_custom_command(
        OUTPUT  ${_output}
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/include/${_mpfrheader}/mpfr.h ${_dstdir}/include/
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/include/${_mpfrheader}/mpf2mpfr.h ${_dstdir}/include/
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/lib/${_mpfrlib} ${_dstdir}/lib/
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/lib/${_mpfrdll} ${_dstdir}/bin/
        ${_mpfr_alias_cmd}
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
        DEPENDS ${GMP_PKG}
    )
endif ()
