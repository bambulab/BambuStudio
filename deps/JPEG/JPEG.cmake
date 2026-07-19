if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if (JPEG_VERSION STREQUAL "6")
        message("Using Jpeg Lib 62")
        set(jpeg_flag "")
    elseif (JPEG_VERSION STREQUAL "7")
        message("Using Jpeg Lib 70")
        set(jpeg_flag "-DWITH_JPEG7=ON")
    else ()
        message("Using Jpeg Lib 80")
        set(jpeg_flag "-DWITH_JPEG8=ON")
    endif ()
endif()

if (MSVC)
    # Generator platform takes precedence over the host processor; SIMD is off
    # for ARM64 (no x86 asm) and ARM64EC (deterministic pure-C build).
    set(_jpeg_plat "${CMAKE_GENERATOR_PLATFORM}")
    if (NOT _jpeg_plat)
        set(_jpeg_plat "${CMAKE_SYSTEM_PROCESSOR}")
    endif ()
    string(TOUPPER "${_jpeg_plat}" _jpeg_plat)
    if (_jpeg_plat MATCHES "^(ARM64|AARCH64|ARM64EC)$")
        set(_use_SIMD "-DWITH_SIMD=OFF")
    endif ()
endif ()

bambustudio_add_cmake_project(JPEG
    URL https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/3.0.1.zip
    URL_HASH SHA256=d6d99e693366bc03897677650e8b2dfa76b5d6c54e2c9e70c03f0af821b0a52f
    DEPENDS ${ZLIB_PKG}
    CMAKE_ARGS
        -DENABLE_SHARED=OFF
        -DENABLE_STATIC=ON
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        ${jpeg_flag}
        ${_use_SIMD}
)
