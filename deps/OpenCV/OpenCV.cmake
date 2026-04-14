set(_opencv_extra_args)
set(_is_arm64 FALSE)
if (MSVC)
    set(_msvc_target_arch "")
    if (CMAKE_GENERATOR_PLATFORM)
        set(_msvc_target_arch "${CMAKE_GENERATOR_PLATFORM}")
    elseif (CMAKE_VS_PLATFORM_NAME)
        set(_msvc_target_arch "${CMAKE_VS_PLATFORM_NAME}")
    elseif (DEFINED DEP_PLATFORM)
        set(_msvc_target_arch "${DEP_PLATFORM}")
    elseif (CMAKE_SYSTEM_PROCESSOR)
        set(_msvc_target_arch "${CMAKE_SYSTEM_PROCESSOR}")
    endif ()
    string(TOUPPER "${_msvc_target_arch}" _msvc_target_arch)
    if (_msvc_target_arch STREQUAL "ARM64" OR _msvc_target_arch STREQUAL "AARCH64")
        set(_is_arm64 TRUE)
    endif ()
else ()
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|aarch64)$")
        set(_is_arm64 TRUE)
    endif ()
endif ()

if (MSVC)
    if (_is_arm64)
        set(_use_IPP "-DWITH_IPP=OFF")
        list(APPEND _opencv_extra_args
            -DWITH_CPU_BASELINE=OFF
            -DWITH_CPU_DISPATCH=OFF
            -DENABLE_SSE=OFF
            -DENABLE_SSE2=OFF
            -DENABLE_SSE3=OFF
            -DENABLE_SSE4_1=OFF
            -DENABLE_AVX=OFF
        )
    else()
        set(_use_IPP "-DWITH_IPP=ON")
    endif ()
else ()
    set(_use_IPP "-DWITH_IPP=OFF")
endif ()

if (BINARY_DIR_REL)
    set(OpenCV_DIRECTORY_FLAG --directory ${BINARY_DIR_REL}/dep_OpenCV-prefix/src/dep_OpenCV)
endif ()

message(STATUS "opencv: _use_IPP is ${_use_IPP}")
bambustudio_add_cmake_project(OpenCV
    URL https://github.com/opencv/opencv/archive/refs/tags/4.6.0.tar.gz
    URL_HASH SHA256=1ec1cba65f9f20fe5a41fda1586e01c70ea0c9a6d7b67c9e13edf0cfe2239277
    PATCH_COMMAND git apply ${OpenCV_DIRECTORY_FLAG} --verbose --ignore-space-change --whitespace=fix ${CMAKE_CURRENT_LIST_DIR}/0001-OpenCV-fix.patch ${CMAKE_CURRENT_LIST_DIR}/0002-clang19-macos.patch
    CMAKE_ARGS
    -DBUILD_SHARED_LIBS=0
       -DBUILD_PERE_TESTS=OFF
       -DBUILD_TESTS=OFF
       -DBUILD_opencv_python_tests=OFF
       -DBUILD_EXAMPLES=OFF
       -DBUILD_JASPER=OFF
       -DBUILD_JAVA=OFF
       -DBUILD_JPEG=ON
       -DBUILD_APPS_LIST=version
       -DBUILD_opencv_apps=OFF
       -DBUILD_opencv_java=OFF
       -DBUILD_OPENEXR=OFF
       -DBUILD_PNG=ON
       -DBUILD_TBB=OFF
       -DBUILD_WEBP=OFF
       -DBUILD_ZLIB=OFF
       -DWITH_1394=OFF
       -DWITH_CUDA=OFF
       -DWITH_EIGEN=OFF
       ${_use_IPP}
       ${_opencv_extra_args}
       -DWITH_ITT=OFF
       -DWITH_FFMPEG=OFF
       -DWITH_GPHOTO2=OFF
       -DWITH_GSTREAMER=OFF
       -DOPENCV_GAPI_GSTREAMER=OFF
       -DWITH_GTK_2_X=OFF
       -DWITH_JASPER=OFF
       -DWITH_LAPACK=OFF
       -DWITH_MATLAB=OFF
       -DWITH_MFX=OFF
       -DWITH_DIRECTX=OFF
       -DWITH_DIRECTML=OFF
       -DWITH_OPENCL=OFF
       -DWITH_OPENCL_D3D11_NV=OFF
       -DWITH_OPENCLAMDBLAS=OFF
       -DWITH_OPENCLAMDFFT=OFF
       -DWITH_OPENEXR=OFF
       -DWITH_OPENJPEG=OFF
       -DWITH_QUIRC=OFF
       -DWITH_VTK=OFF
       -DWITH_JPEG=OFF
       -DWITH_WEBP=OFF
       -DENABLE_PRECOMPILED_HEADERS=OFF
       -DINSTALL_TESTS=OFF
       -DINSTALL_C_EXAMPLES=OFF
       -DINSTALL_PYTHON_EXAMPLES=OFF
       -DOPENCV_GENERATE_SETUPVARS=OFF
       -DOPENCV_INSTALL_FFMPEG_DOWNLOAD_SCRIPT=OFF
       -DBUILD_opencv_python2=OFF
       -DBUILD_opencv_python3=OFF
       -DWITH_OPENVINO=OFF
       -DWITH_INF_ENGINE=OFF
       -DWITH_NGRAPH=OFF
       -DBUILD_WITH_STATIC_CRT=OFF#set /MDd /MD
       -DBUILD_LIST=core,imgcodecs,imgproc,world
       -DBUILD_opencv_highgui=OFF
       -DWITH_ADE=OFF
       -DBUILD_opencv_world=ON
       -DWITH_PROTOBUF=OFF
       -DWITH_WIN32UI=OFF
       -DHAVE_WIN32UI=FALSE
)

