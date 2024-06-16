bambustudio_add_cmake_project(
    CGAL
    # GIT_REPOSITORY https://github.com/CGAL/cgal.git
    # GIT_TAG        caacd806dc55c61cc68adaad99f2240f00493b29 # releases/CGAL-5.3
    # For whatever reason, this keeps downloading forever (repeats downloads if finished)
    URL      https://github.com/CGAL/cgal/archive/refs/tags/v5.6.1.zip
    URL_HASH SHA256=a968cc77b9a2c6cbe5b1680ceee8d8cd8c5369aedb9daced9e5c90b4442dc574
    DEPENDS dep_Boost dep_GMP dep_MPFR
)

include(GNUInstallDirs)
