# We have to check for OpenGL to compile GLEW
set(OpenGL_GL_PREFERENCE "LEGACY") # to prevent a nasty warning by cmake
find_package(OpenGL QUIET REQUIRED)

bambustudio_add_cmake_project(
  GLEW
  URL https://github.com/nigels-com/glew/releases/download/glew-2.3.0/glew-2.3.0.zip
  URL_HASH SHA256=fe8fdbaa77cfa354ff400da323ea5e32b3641ad58a218607de74d2998b872e66
  SOURCE_SUBDIR build/cmake
  CMAKE_ARGS
    -DBUILD_UTILS=OFF
)

if (MSVC)
    add_debug_dep(dep_GLEW)
endif ()
