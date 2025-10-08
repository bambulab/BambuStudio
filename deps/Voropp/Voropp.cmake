bambustudio_add_cmake_project(Voropp
    URL "https://math.lbl.gov/voro++/download/dir/voro++-0.4.6.tar.gz"
    URL_HASH SHA256=ef7970071ee2ce3800daa8723649ca069dc4c71cc25f0f7d22552387f3ea437e
    PATCH_COMMAND ${CMAKE_COMMAND} -E copy 
        ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt.in
        <SOURCE_DIR>/CMakeLists.txt
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_LIST_DIR}/voroppConfig.cmake.in
        <SOURCE_DIR>/voroppConfig.cmake.in
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_DEBUG_POSTFIX=d
)

if (MSVC)
    add_debug_dep(dep_Voropp)
endif ()
