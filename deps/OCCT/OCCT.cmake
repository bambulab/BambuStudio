if(WIN32)
    set(library_build_type "Shared")
else()
    set(library_build_type "Static")
endif()

if (BINARY_DIR_REL)
    set(OCCT_DIRECTORY_FLAG --directory ${BINARY_DIR_REL}/dep_OCCT-prefix/src/dep_OCCT)
endif ()

bambustudio_add_cmake_project(OCCT
    URL https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V7_6_0.zip
    URL_HASH SHA256=28334f0e98f1b1629799783e9b4d21e05349d89e695809d7e6dfa45ea43e1dbc
    PATCH_COMMAND git apply ${OCCT_DIRECTORY_FLAG} --verbose --ignore-space-change --whitespace=fix ${CMAKE_CURRENT_LIST_DIR}/0001-OCCT-fix.patch
    #DEPENDS dep_Boost
    #DEPENDS dep_FREETYPE
    CMAKE_ARGS
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        # OCCT 7.6 only sets -std=c++0x when the compiler id is "Clang"; Apple Xcode reports
        # "AppleClang", so it falls through with no C++ standard and fails to parse nested
        # template closes (>>). Force C++11 explicitly to match OCCT's intended standard.
        -DCMAKE_CXX_STANDARD=11
        -DCMAKE_CXX_STANDARD_REQUIRED=ON
        -DBUILD_LIBRARY_TYPE=${library_build_type}
        -DUSE_TK=OFF
        -DUSE_TBB=OFF
	#-DUSE_FREETYPE=OFF
        -DUSE_FFMPEG=OFF
        -DUSE_VTK=OFF
        -DBUILD_DOC_Overview=OFF
        -DBUILD_MODULE_ApplicationFramework=OFF
        #-DBUILD_MODULE_DataExchange=OFF
        -DBUILD_MODULE_Draw=OFF
        -DBUILD_MODULE_FoundationClasses=OFF
        -DBUILD_MODULE_ModelingAlgorithms=OFF
        -DBUILD_MODULE_ModelingData=OFF
        -DBUILD_MODULE_Visualization=OFF
)

if (DEP_BUILD_FREETYPE)
    add_dependencies(dep_OCCT ${FREETYPE_PKG})
endif ()