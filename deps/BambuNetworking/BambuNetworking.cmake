bambustudio_add_cmake_project(BambuNetworking
    GIT_REPOSITORY "https://github.com/bambulab/BambuNetworking"
    GIT_TAG v0.0.1
    #DEPENDS dep_Boost
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${DESTDIR}/usr/local
)
