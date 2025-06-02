bambustudio_add_cmake_project(
    CGAL
    URL      https://github.com/CGAL/cgal/archive/refs/tags/v5.4.zip
    URL_HASH SHA256=d7605e0a5a5ca17da7547592f6f6e4a59430a0bc861948974254d0de43eab4c0
    DEPENDS ${BOOST_PKG} dep_GMP dep_MPFR
)

include(GNUInstallDirs)
