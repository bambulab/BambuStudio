if(WIN32)
    set(library_build_type "Shared")
else()
    set(library_build_type "Static")
endif()

bambustudio_add_cmake_project(OCCT
    URL https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V7_8_1.zip
    URL_HASH SHA256=05e1fc2d8d14acecb3c2fcfd962255f1eb35b384d5b5619d516eef329131f3db
    PATCH_COMMAND git apply --directory deps/build/dep_OCCT-prefix/src/dep_OCCT --verbose --ignore-space-change --whitespace=fix ${CMAKE_CURRENT_LIST_DIR}/0001-OCCT-fix.patch
    CMAKE_ARGS
        -DBUILD_LIBRARY_TYPE=${library_build_type}
        -DUSE_TK=OFF
        -DUSE_TBB=OFF
        -DUSE_FFMPEG=OFF
        -DUSE_VTK=OFF
        -DBUILD_MODULE_ApplicationFramework=OFF
        #-DBUILD_MODULE_DataExchange=OFF
        -DBUILD_MODULE_Draw=OFF
        -DBUILD_MODULE_FoundationClasses=OFF
        -DBUILD_MODULE_ModelingAlgorithms=OFF
        -DBUILD_MODULE_ModelingData=OFF
        -DBUILD_MODULE_Visualization=OFF
)

add_dependencies(dep_OCCT dep_FREETYPE)
