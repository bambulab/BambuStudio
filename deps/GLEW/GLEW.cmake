# We have to check for OpenGL to compile GLEW
set(OpenGL_GL_PREFERENCE "LEGACY") # to prevent a nasty warning by cmake
find_package(OpenGL QUIET REQUIRED)

bambustudio_add_cmake_project(
  GLEW
  URL https://sourceforge.net/projects/glew/files/glew/2.2.0/glew-2.2.0.zip
  URL_HASH SHA256=a9046a913774395a095edcc0b0ac2d81c3aacca61787b39839b941e9be14e0d4
  SOURCE_SUBDIR build/cmake
  CMAKE_ARGS
    -DBUILD_UTILS=OFF
)

if (MSVC)
    add_debug_dep(dep_GLEW)
endif ()
