# This production candidate visitor is independent of optional GPU toolchains.
add_executable(gpu_rectilinear_candidates_tests ../libslic3r/test_gpu_rectilinear_candidates.cpp)
set_property(TARGET gpu_rectilinear_candidates_tests PROPERTY CXX_STANDARD 17)
add_test(NAME gpu_rectilinear_candidates_tests COMMAND gpu_rectilinear_candidates_tests)


add_executable(gpu_tree_candidates_tests ../libslic3r/test_gpu_tree_candidates.cpp)
set_property(TARGET gpu_tree_candidates_tests PROPERTY CXX_STANDARD 17)
add_test(NAME gpu_tree_candidates_tests COMMAND gpu_tree_candidates_tests)

# Keep maintained GPU geometry targets in the IDE/default graph. The obsolete
# libslic3r suite remains excluded in its original directory.
if(SLIC3R_ENABLE_CUDA_SLICER OR SLIC3R_ENABLE_VULKAN_SLICER)
    add_executable(gpu_geometry_tests
        ../libslic3r/test_gpu_geometry_main.cpp
        ../libslic3r/test_gpu_exact_geometry.cpp
        ../libslic3r/test_gpu_batch_preflight.cpp
        ../libslic3r/test_gpu_rectilinear_equivalence.cpp)
    target_compile_features(gpu_geometry_tests PRIVATE cxx_std_17)
    target_include_directories(gpu_geometry_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(gpu_geometry_tests PRIVATE test_common libslic3r)
    if(MSVC)
        target_compile_definitions(gpu_geometry_tests PRIVATE DO_NOT_USE_WMAIN)
    endif()
    set_property(TARGET gpu_geometry_tests PROPERTY FOLDER "tests")
    add_test(NAME gpu_geometry_tests COMMAND gpu_geometry_tests ${CATCH_EXTRA_ARGS})
endif()
