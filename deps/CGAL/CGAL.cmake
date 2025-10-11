bambustudio_add_cmake_project(
    CGAL
    URL      https://github.com/CGAL/cgal/archive/refs/tags/v6.1.zip
    URL_HASH SHA256=ac8f61bef8a7d8732d041d0953db3203c93427b1346389c57fa4c567e36672d4
    DEPENDS ${BOOST_PKG} dep_GMP dep_MPFR
)

include(GNUInstallDirs)
