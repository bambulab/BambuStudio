set(ag_sources
    customcut/ColorCutAppearanceSnapshot.cpp
    customcut/ColorCutAttributeRepository.cpp
    customcut/ColorCutAttributeTransfer.cpp
    customcut/ColorCutCoordinator.cpp
    customcut/ColorCutDiagnostics.cpp
    customcut/ColorCutIntegrationBridge.cpp
    customcut/ColorCutLegacyPlaneBackend.cpp
    customcut/ColorCutMcutBackend.cpp
    customcut/ColorCutProvenance.cpp
    customcut/ColorCutRepositoryBridge.cpp
    customcut/ColorCutValidation.cpp
    customcut/ColorCutVolumeBuilder.cpp
    ballerase/BallEraserBoolean.cpp
    warpcut/WarpCutCoordinator.cpp
    warpcut/WarpCutGeometryBackend.cpp
    warpcut/WarpCutSurfaceClassifier.cpp
    warpcut/WarpCutTransferBridge.cpp
)

add_library(ag STATIC ${ag_sources})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${ag_sources})

target_include_directories(ag PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_BINARY_DIR}/../libslic3r
)
target_include_directories(ag PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../mcut/include
    ${OpenCASCADE_INCLUDE_DIR}
)

target_link_libraries(ag PUBLIC mcut boost_headeronly cereal)
