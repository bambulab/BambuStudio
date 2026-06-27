bambustudio_add_cmake_project(libharu
  SOURCE_DIR          ${CMAKE_CURRENT_LIST_DIR}/libharu
  DEPENDS             ${ZLIB_PKG} ${PNG_PKG}
  CMAKE_ARGS
    -DBUILD_SHARED_LIBS:BOOL=OFF
    -DLIBHPDF_EXAMPLES:BOOL=OFF
)

if (MSVC)
    add_debug_dep(dep_libharu)
endif ()
