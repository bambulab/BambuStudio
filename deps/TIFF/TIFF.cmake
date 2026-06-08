find_package(OpenGL QUIET REQUIRED)

bambustudio_add_cmake_project(TIFF
    URL https://download.osgeo.org/libtiff/tiff-4.1.0.zip
    URL_HASH SHA256=6F3DBED9D2ECFED33C7192B5C01884078970657FA21B4AD28E3CDF3438EB2419
    DEPENDS ${ZLIB_PKG} ${PNG_PKG} ${JPEG_PKG}
    CMAKE_ARGS
        -Dlzma:BOOL=OFF
        -Dwebp:BOOL=OFF
        -Djbig:BOOL=OFF
        -Dzstd:BOOL=OFF
        -Dpixarlog:BOOL=OFF
)
