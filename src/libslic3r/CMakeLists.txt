cmake_minimum_required(VERSION 3.13)
project(libslic3r)

include(PrecompiledHeader)

string(TIMESTAMP COMPILE_TIME %Y%m%d-%H%M%S)
set(SLIC3R_BUILD_TIME ${COMPILE_TIME})

if("${BBL_RELEASE_TO_PUBLIC}" STREQUAL "0")
execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_SHA1
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
set(SLIC3R_COMPILE_VERSION "${GIT_SHA1}")
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/libslic3r_version.h.in ${CMAKE_CURRENT_BINARY_DIR}/libslic3r_version.h @ONLY)

if (MINGW)
    add_compile_options(-Wa,-mbig-obj)
endif ()

set(OpenVDBUtils_SOURCES "")
if (TARGET OpenVDB::openvdb)
    set(OpenVDBUtils_SOURCES OpenVDBUtils.cpp OpenVDBUtils.hpp)
endif()

set(lisbslic3r_sources
    ArcFitter.cpp
    ArcFitter.hpp
    pchheader.cpp
    pchheader.hpp
    AABBTreeIndirect.hpp
    AABBTreeLines.hpp
    AABBMesh.hpp
    AABBMesh.cpp
    Algorithm/LineSegmentation/LineSegmentation.cpp
    Algorithm/LineSegmentation/LineSegmentation.hpp
    AnyPtr.hpp
    AStar.hpp
    LogSink.hpp
    LogSink.cpp
    BoundingBox.cpp
    BoundingBox.hpp
    BridgeDetector.cpp
    BridgeDetector.hpp
    ByObjectPrintData.hpp
    ByObjectPrintData.cpp
    FaceDetector.cpp
    FaceDetector.hpp
    Brim.cpp
    Brim.hpp
    BuildVolume.cpp
    BuildVolume.hpp
	Calib.cpp
    Calib.hpp
    Circle.cpp
    Circle.hpp
    clipper.cpp
    clipper.hpp
    ClipperUtils.cpp
    ClipperUtils.hpp
    Clipper2Utils.cpp
    Clipper2Utils.hpp
    Color.cpp
    Color.hpp
    CommonDefs.hpp
    Config.cpp
    ConfigFileUtils.cpp
    Config.hpp
    CurveAnalyzer.cpp
    CurveAnalyzer.hpp
    CutUtils.cpp
    CutUtils.hpp
    EdgeGrid.cpp
    EdgeGridSvg.cpp
    EdgeGrid.hpp
    ElephantFootCompensation.cpp
    ElephantFootCompensation.hpp
    Emboss.cpp
    Emboss.hpp
    EmbossShape.hpp
    enum_bitmask.hpp
    ExPolygon.cpp
    ExPolygonSecondMoment.cpp
    ExPolygon.hpp
    ExPolygonSerialize.hpp
    ExPolygonsIndex.cpp
    ExPolygonsIndex.hpp
    Extruder.cpp
    Extruder.hpp
    ExtrusionEntity.cpp
    ExtrusionEntity.hpp
    ExtrusionEntityCollection.cpp
    ExtrusionEntityCollection.hpp
    ExtrusionSimulator.cpp
    ExtrusionSimulator.hpp
    FileParserError.hpp
    Fill/Fill.cpp
    Fill/Fill.hpp
    Fill/Fill3DHoneycomb.cpp
    Fill/Fill3DHoneycomb.hpp
    Fill/FillAdaptive.cpp
    Fill/FillAdaptive.hpp
    Fill/FillBase.cpp
    Fill/FillBase.hpp
    Fill/FillConcentric.cpp
    Fill/FillConcentric.hpp
    Fill/FillConcentricInternal.cpp
    Fill/FillConcentricInternal.hpp
    Fill/FillCrossHatch.cpp
    Fill/FillCrossHatch.hpp
    Fill/FillHoneycomb.cpp
    Fill/FillHoneycomb.hpp
    Fill/FillGyroid.cpp
    Fill/FillGyroid.hpp
    Fill/FillPlanePath.cpp
    Fill/FillPlanePath.hpp
    Fill/FillLine.cpp
    Fill/FillLine.hpp
    Fill/FillLightning.cpp
    Fill/FillLightning.hpp
    Fill/Lightning/DistanceField.cpp
    Fill/Lightning/DistanceField.hpp
    Fill/Lightning/Generator.cpp
    Fill/Lightning/Generator.hpp
    Fill/Lightning/Layer.cpp
    Fill/Lightning/Layer.hpp
    Fill/Lightning/TreeNode.cpp
    Fill/Lightning/TreeNode.hpp
    Fill/FillRectilinear.cpp
    Fill/FillRectilinear.hpp
    Fill/FillFloatingConcentric.hpp
    Fill/FillFloatingConcentric.cpp
    Fill/FillSpacing.cpp
    Flow.cpp
    Flow.hpp
    FlowPrint.cpp
    Frustum.cpp
    Frustum.hpp
    FlushVolCalc.cpp
    FlushVolCalc.hpp
    FuzzySkin.cpp
    FuzzySkin.hpp
    format.hpp
    Format/3mf.cpp
    Format/3mf.hpp
    Format/bbs_3mf.cpp
    Format/bbs_3mf.hpp
    Format/AMF.cpp
    Format/AMF.hpp
    Format/OBJ.cpp
    Format/OBJ.hpp
    Format/objparser.cpp
    Format/objparser.hpp
    Format/STEP.cpp
    Format/STEP.hpp
    Format/STL.cpp
    Format/STL.hpp
    Format/SL1.hpp
    Format/SL1.cpp
	Format/svg.hpp
    Format/svg.cpp
    Format/AssimpImport.hpp
    Format/AssimpImport.cpp
    TexturePainting.hpp
    TexturePainting.cpp
    Win10ModelRepair.hpp
    TextureToColor/TextureToColor.hpp
    TextureToColor/TextureToColor.cpp
    TextureToColor/ColorUtils.hpp
    TextureToColor/ColorUtils.cpp
    GCode/ThumbnailData.cpp
    GCode/ThumbnailData.hpp
    GCode/GCodeEditor.cpp
    GCode/GCodeEditor.hpp
    GCode/PostProcessor.cpp
    GCode/PostProcessor.hpp
#    GCode/PressureEqualizer.cpp
#    GCode/PressureEqualizer.hpp
    GCode/PrintExtents.cpp
    GCode/PrintExtents.hpp
    GCode/RetractWhenCrossingPerimeters.cpp
    GCode/RetractWhenCrossingPerimeters.hpp
    GCode/SpiralVase.cpp
    GCode/SpiralVase.hpp
    GCode/SeamPlacer.cpp
    GCode/SeamPlacer.hpp
    GCode/ToolOrdering.cpp
    GCode/ToolOrdering.hpp
    GCode/WipeTower.cpp
    GCode/WipeTower.hpp
    GCode/GCodeProcessor.cpp
    GCode/GCodeProcessor.hpp
    GCode/AvoidCrossingPerimeters.cpp
    GCode/AvoidCrossingPerimeters.hpp
    GCode/ConflictChecker.cpp
    GCode/ConflictChecker.hpp
    GCode/BedExcludeChecker.cpp
    GCode/BedExcludeChecker.hpp
    GCode/GCodeOrigin.hpp
    GCode/Smoothing.cpp
    GCode/Smoothing.hpp
    GCode/CoolingBuffer.cpp
    GCode/CoolingBuffer.hpp
    GCode/TimelapsePosPicker.cpp
    GCode/TimelapsePosPicker.hpp
    GCode.cpp
    GCodeLayerOrdering.cpp
    GCode.hpp
    GCodeReader.cpp
    GCodeReader.hpp
    # GCodeSender.cpp
    # GCodeSender.hpp
    GCodeWriter.cpp
    GCodeWriter.hpp
    Geometry.cpp
    GeometryTransformation.cpp
    Geometry.hpp
    Geometry/Bicubic.hpp
    Geometry/Circle.cpp
    Geometry/Circle.hpp
    Geometry/ConvexHull.cpp
    Geometry/ConvexHull.hpp
    Geometry/Curves.hpp
    Geometry/MedialAxis.cpp
    Geometry/MedialAxis.hpp
	Geometry/Voronoi.cpp
    Geometry/Voronoi.hpp
    Geometry/VoronoiOffset.cpp
    Geometry/VoronoiOffset.hpp
    Geometry/VoronoiUtils.hpp
    Geometry/VoronoiUtils.cpp
    Geometry/VoronoiUtilsCgal.cpp
    Geometry/VoronoiUtilsCgal.hpp
    Geometry/VoronoiVisualUtils.hpp
    I18N.cpp
    Int128.hpp
    InternalBridgeDetector.cpp
    InternalBridgeDetector.hpp
    JumpPointSearch.hpp
    JumpPointSearch.cpp
    KDTreeIndirect.hpp
    Layer.cpp
    LayerLifecycle.cpp
    LayerSvg.cpp
    Layer.hpp
    LayerRegion.cpp
    libslic3r.h
    Line.cpp
    Line.hpp
    BlacklistedLibraryCheck.cpp
    BlacklistedLibraryCheck.hpp
    LocalesUtils.cpp
    LocalesUtils.hpp
    Model.cpp
    ModelAnnotations.cpp
    ModelBounds.cpp
    ModelCopy.cpp
    ModelDiff.cpp
    ModelExtruders.cpp
    ModelGeometry.cpp
    ModelPlacement.cpp
    ModelTransform.cpp
    Model.hpp
    ModelArrange.hpp
    ModelArrange.cpp
    MultiMaterialSegmentation.cpp
    MultiMaterialSegmentation.hpp
    MixedFilamentUtils.cpp
    Measure.hpp
    Measure.cpp
    MeasureUtils.hpp
    CustomGCode.cpp
    CustomGCode.hpp
    Arrange.hpp
    Arrange.cpp
    NormalUtils.cpp
    NormalUtils.hpp
    ObjColorUtils.cpp
    ObjColorUtils.hpp
    Orient.hpp
    Orient.cpp
    MultiPoint.cpp
    MultiPoint.hpp
    MutablePriorityQueue.hpp
    ObjectID.cpp
    ObjectID.hpp
    NSVGUtils.cpp
    NSVGUtils.hpp
	ParameterUtils.cpp
    ParameterUtils.hpp
    PerimeterGenerator.cpp
    PerimeterGenerator.hpp
    OverhangDetector.hpp
    OverhangDetector.cpp
    PlaceholderParserConfig.cpp
    PlaceholderParser.cpp
    PlaceholderParser.hpp
    Platform.cpp
    Platform.hpp
    Point.cpp
    Point.hpp
    Polygon.cpp
    Polygon.hpp
    MutablePolygon.cpp
    MutablePolygon.hpp
    PolygonTrimmer.cpp
    PolygonTrimmer.hpp
    Polyline.cpp
    Polyline.hpp
    Preset.cpp
    Preset.hpp
    PresetBundle.cpp
    PresetBundle.hpp
    ProjectTask.cpp
    ProjectTask.hpp
    PrincipalComponents2D.hpp
    PrincipalComponents2D.cpp
    AppConfig.cpp
    AppConfig.hpp
    PrintSequential.cpp
    PrintState.cpp
    PrintQueries.cpp
    PrintExtruderPrintableArea.cpp
    PrintFilamentPrintability.cpp
    PrintFilamentState.cpp
    PrintFilamentMappingConfigSync.cpp
    PrintFilamentMappingConfigSync.hpp
    PrintFilamentMapping.cpp
    PrintFilamentMappingRules.cpp
    PrintFilamentMappingRules.hpp
    PrintEnvelope.cpp
    PrintState.cpp
    PrintExtruderUtils.cpp
    PrintObjectSharing.cpp
    PrintRuntime.cpp
    PrintAuxiliary.cpp
    PrintFilamentCompatibility.cpp
    PrintCache.cpp
    PrintValidate.cpp
    PrintProcessPerimeters.cpp
    PrintStatistics.cpp
    PrintExport.cpp
    PrintProcess.cpp
    PrintWipeTower.cpp
    PrintProcessSupport.cpp
    Print.hpp
    PrintApply.cpp
    PrintBase.cpp
    PrintBaseOutput.cpp
    PrintBase.hpp
    PrintConfigGeometry.cpp
    PrintConfig.cpp
    PrintConfig.hpp
    PrintConfigRuntime.cpp
    PrintObjectAnalysis.cpp
    PrintObjectInvalidation.cpp
    PrintObjectLayers.cpp
    PrintObjectInfill.cpp
    PrintObjectPerimeters.cpp
    PrintObjectPostProcess.cpp
    PrintObjectSlicingConfig.cpp
    PrintObject.cpp
    PrintObjectExtruders.cpp
    PrintObjectUpdateSlicingParameters.cpp
    PrintObjectSupportGeometry.cpp
    PrintObjectSupport.cpp
    PrintObjectSlice.cpp
    PrintObjectSliceInterlocking.hpp
    PrintObjectSliceInterlocking.cpp
    PrintRegion.cpp
    PrintRegionConfigExtruders.cpp
    PrintRegionExtruders.cpp
    PNGReadWrite.hpp
    PNGReadWrite.cpp
    QuadricEdgeCollapse.cpp
    QuadricEdgeCollapse.hpp
    Semver.cpp
    ShortEdgeCollapse.cpp
    ShortEdgeCollapse.hpp
    ShortestPath.cpp
    ShortestPathExtras.cpp
    ShortestPathInternal.hpp
    ShortestPath.hpp
    SLAPrint.cpp
    SLAPrintSteps.cpp
    SLAPrintSteps.hpp
    SLAPrint.hpp
    Slicing.cpp
    Slicing.hpp
    SlicesToTriangleMesh.hpp
    SlicesToTriangleMesh.cpp
    SlicingAdaptive.cpp
    SlicingAdaptive.hpp
    Support/SupportCommon.cpp
    Support/SupportCommon.hpp
    Support/SupportLayerUtils.hpp
    Support/SupportMaterial.cpp
    Support/SupportMaterial.hpp
    Support/TreeSupport.hpp
    Support/TreeSupport.cpp
    Support/TreeSupport3D.hpp
    Support/TreeSupport3D.cpp
    Support/TreeModelVolumes.hpp
    Support/TreeModelVolumes.cpp
    Support/TreeSupportCommon.hpp
    Support/SupportParameters.hpp
    MinimumSpanningTree.hpp
    MinimumSpanningTree.cpp
    Surface.cpp
    Surface.hpp
    SurfaceCollection.cpp
    SurfaceCollectionSvg.cpp
    SurfaceCollection.hpp
    SurfaceSvg.cpp
    SurfaceMesh.hpp
    SVG.cpp
    SVG.hpp
    Technologies.hpp
    Tesselate.cpp
    Tesselate.hpp
    TriangleMesh.cpp
    TriangleMesh.hpp
    MeshDiagnostics.hpp
    MeshDiagnostics.cpp
    TriangleMeshDeal.cpp
    TriangleMeshDeal.hpp
    TriangleMeshSlicer.cpp
    TriangleMeshSlicer.hpp
    MeshSplitImpl.hpp
    TriangulateWall.hpp
    TriangulateWall.cpp
    utils.cpp
    Utils.hpp
    Time.cpp
    Time.hpp
    Timer.cpp
    Timer.hpp
    Thread.cpp
    Thread.hpp
    TriangleSelector.cpp
    TriangleSelector.hpp
    TriangleSetSampling.cpp
    TriangleSetSampling.hpp
    UtilsData.cpp
    UtilsDebug.cpp
    UtilsFormat.cpp
    UtilsRename.cpp
    MTUtils.hpp
    VariableWidth.cpp
    VariableWidth.hpp
    Zipper.hpp
    Zipper.cpp
    MinAreaBoundingBox.hpp
    MinAreaBoundingBox.cpp
    miniz_extension.hpp
    miniz_extension.cpp
    MarchingSquares.hpp
    Execution/Execution.hpp
    Execution/ExecutionSeq.hpp
    Execution/ExecutionTBB.hpp
    Optimize/Optimizer.hpp
    Optimize/NLoptOptimizer.hpp
    Optimize/BruteforceOptimizer.hpp
    SLA/Pad.hpp
    SLA/Pad.cpp
    SLA/SupportTreeBuilder.hpp
    SLA/SupportTreeMesher.hpp
    SLA/SupportTreeMesher.cpp
    SLA/SupportTreeBuildsteps.hpp
    SLA/SupportTreeBuildsteps.cpp
    SLA/SupportTreeBuilder.cpp
    SLA/Concurrency.hpp
    SLA/SupportTree.hpp
    SLA/SupportTree.cpp
#    SLA/SupportTreeIGL.cpp
    SLA/Rotfinder.hpp
    SLA/Rotfinder.cpp
    SLA/BoostAdapter.hpp
    SLA/SpatIndex.hpp
    SLA/SpatIndex.cpp
    SLA/RasterBase.hpp
    SLA/RasterBase.cpp
    SLA/AGGRaster.hpp
    SLA/RasterToPolygons.hpp
    SLA/RasterToPolygons.cpp
    SLA/ConcaveHull.hpp
    SLA/ConcaveHull.cpp
    SLA/Hollowing.hpp
    SLA/Hollowing.cpp
    SLA/JobController.hpp
    SLA/SupportPoint.hpp
    SLA/SupportPointGenerator.hpp
    SLA/SupportPointGenerator.cpp
    SLA/IndexedMesh.hpp
    SLA/IndexedMesh.cpp
    SLA/Clustering.hpp
    SLA/Clustering.cpp
    SLA/ReprojectPointsOnMesh.hpp
    Arachne/BeadingStrategy/BeadingStrategy.hpp
    Arachne/BeadingStrategy/BeadingStrategy.cpp
    Arachne/BeadingStrategy/BeadingStrategyFactory.hpp
    Arachne/BeadingStrategy/BeadingStrategyFactory.cpp
    Arachne/BeadingStrategy/DistributedBeadingStrategy.hpp
    Arachne/BeadingStrategy/DistributedBeadingStrategy.cpp
    Arachne/BeadingStrategy/LimitedBeadingStrategy.hpp
    Arachne/BeadingStrategy/LimitedBeadingStrategy.cpp
    Arachne/BeadingStrategy/OuterWallInsetBeadingStrategy.hpp
    Arachne/BeadingStrategy/OuterWallInsetBeadingStrategy.cpp
    Arachne/BeadingStrategy/RedistributeBeadingStrategy.hpp
    Arachne/BeadingStrategy/RedistributeBeadingStrategy.cpp
    Arachne/BeadingStrategy/WideningBeadingStrategy.hpp
    Arachne/BeadingStrategy/WideningBeadingStrategy.cpp
    Arachne/BeadingStrategy/OuterWallContourStrategy.hpp
    Arachne/BeadingStrategy/OuterWallContourStrategy.cpp
    Arachne/utils/ExtrusionJunction.hpp
    Arachne/utils/ExtrusionJunction.cpp
    Arachne/utils/ExtrusionLine.hpp
    Arachne/utils/ExtrusionLine.cpp
    Arachne/utils/HalfEdge.hpp
    Arachne/utils/HalfEdgeGraph.hpp
    Arachne/utils/HalfEdgeNode.hpp
    Arachne/utils/SparseGrid.hpp
    Arachne/utils/SparsePointGrid.hpp
    Arachne/utils/SparseLineGrid.hpp
    Arachne/utils/SquareGrid.hpp
    Arachne/utils/SquareGrid.cpp
    Arachne/utils/PolygonsPointIndex.hpp
    Arachne/utils/PolygonsSegmentIndex.hpp
    Arachne/utils/PolylineStitcher.hpp
    Arachne/utils/PolylineStitcher.cpp
    Arachne/SkeletalTrapezoidation.hpp
    Arachne/SkeletalTrapezoidation.cpp
    Arachne/SkeletalTrapezoidationEdge.hpp
    Arachne/SkeletalTrapezoidationGraph.hpp
    Arachne/SkeletalTrapezoidationGraph.cpp
    Arachne/SkeletalTrapezoidationJoint.hpp
    Arachne/WallToolPaths.hpp
    Arachne/WallToolPaths.cpp
    Shape/TextShape.hpp
    Shape/TextShape.cpp
    RegionExpansion.hpp
    RegionExpansion.cpp
    ClipperZUtils.hpp
    FlushVolPredictor.hpp
    FlushVolPredictor.cpp
    FilamentGroup.hpp
    FilamentGroup.cpp
    FilamentGroupUtils.hpp
    FilamentGroupUtils.cpp
    GCode/ToolOrderUtils.hpp
    GCode/ToolOrderUtils.cpp
    FlushVolPredictor.hpp
    FlushVolPredictor.cpp
    Interlocking/InterlockingGenerator.hpp
    Interlocking/InterlockingGenerator.cpp
    Interlocking/VoxelUtils.hpp
    Interlocking/VoxelUtils.cpp
    MultiNozzleUtils.hpp
    MultiNozzleUtils.cpp
    FilamentMixer.hpp
    FilamentMixer.cpp
    FilamentMixerModel.hpp
)

if (APPLE)
    list(APPEND lisbslic3r_sources
			MacUtils.mm
            Format/ModelIO.hpp
            Format/ModelIO.mm
        )
endif ()

add_library(libslic3r STATIC ${lisbslic3r_sources}
    "${CMAKE_CURRENT_BINARY_DIR}/libslic3r_version.h"
    ${OpenVDBUtils_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${lisbslic3r_sources})

set(libslic3r_config_core_sources
    Config.cpp
    ConfigFileUtils.cpp
    LocalesUtils.cpp
    PrintConfig.cpp
    PrintFilamentMappingConfigSync.cpp
    PrintFilamentMappingRules.cpp
    PrintConfigRuntime.cpp
)

add_library(libslic3r_config_core STATIC
    ${libslic3r_config_core_sources}
    "${CMAKE_CURRENT_BINARY_DIR}/libslic3r_version.h")

set(libslic3r_gcodewriter_core_sources
    Config.cpp
    ConfigFileUtils.cpp
    LocalesUtils.cpp
    PrintConfig.cpp
    PrintConfigRuntime.cpp
    Extruder.cpp
    GCodeWriter.cpp
)

add_library(libslic3r_gcodewriter_core STATIC
    ${libslic3r_gcodewriter_core_sources}
    "${CMAKE_CURRENT_BINARY_DIR}/libslic3r_version.h")

set(libslic3r_print_process_math_core_sources
    ArcFitter.cpp
    BoundingBox.cpp
    Circle.cpp
    clipper.cpp
    ClipperUtils.cpp
    Config.cpp
    ConfigFileUtils.cpp
    ExPolygon.cpp
    ExPolygonSecondMoment.cpp
    Extruder.cpp
    ExtrusionEntity.cpp
    ExtrusionEntityCollection.cpp
    Fill/FillSpacing.cpp
    Flow.cpp
    Geometry/MedialAxis.cpp
    Geometry/Voronoi.cpp
    Geometry/VoronoiOffset.cpp
    Geometry/VoronoiUtils.cpp
    Geometry.cpp
    GeometryTransformation.cpp
    I18N.cpp
    Line.cpp
    LocalesUtils.cpp
    MultiPoint.cpp
    Point.cpp
    Polygon.cpp
    Polyline.cpp
    PrintConfig.cpp
    PrintConfigRuntime.cpp
    ShortestPath.cpp
    Surface.cpp
    Utils.cpp
)

add_library(libslic3r_print_process_math_core STATIC
    ${libslic3r_print_process_math_core_sources}
    "${CMAKE_CURRENT_BINARY_DIR}/libslic3r_version.h")

set(libslic3r_fill_core_sources
    ${libslic3r_print_process_math_core_sources}
    Clipper2Utils.cpp
    EdgeGrid.cpp
    PNGReadWrite.cpp
    ShortestPathExtras.cpp
    SVG.cpp
    VariableWidth.cpp
    Arachne/BeadingStrategy/BeadingStrategy.cpp
    Arachne/BeadingStrategy/BeadingStrategyFactory.cpp
    Arachne/BeadingStrategy/DistributedBeadingStrategy.cpp
    Arachne/BeadingStrategy/LimitedBeadingStrategy.cpp
    Arachne/BeadingStrategy/OuterWallContourStrategy.cpp
    Arachne/BeadingStrategy/OuterWallInsetBeadingStrategy.cpp
    Arachne/BeadingStrategy/RedistributeBeadingStrategy.cpp
    Arachne/BeadingStrategy/WideningBeadingStrategy.cpp
    Arachne/utils/ExtrusionJunction.cpp
    Arachne/utils/ExtrusionLine.cpp
    Arachne/utils/PolylineStitcher.cpp
    Arachne/utils/SquareGrid.cpp
    Arachne/SkeletalTrapezoidation.cpp
    Arachne/SkeletalTrapezoidationGraph.cpp
    Arachne/WallToolPaths.cpp
    Fill/Fill.cpp
    Fill/Fill3DHoneycomb.cpp
    Fill/FillAdaptive.cpp
    Fill/FillBase.cpp
    Fill/FillConcentric.cpp
    Fill/FillConcentricInternal.cpp
    Fill/FillCrossHatch.cpp
    Fill/FillFloatingConcentric.cpp
    Fill/FillGyroid.cpp
    Fill/FillHoneycomb.cpp
    Fill/FillLine.cpp
    Fill/FillLightning.cpp
    Fill/FillPlanePath.cpp
    Fill/FillRectilinear.cpp
    Fill/Lightning/DistanceField.cpp
    Fill/Lightning/Generator.cpp
    Fill/Lightning/Layer.cpp
    Fill/Lightning/TreeNode.cpp
)

add_library(libslic3r_fill_core STATIC
    ${libslic3r_fill_core_sources}
    "${CMAKE_CURRENT_BINARY_DIR}/libslic3r_version.h")

set(libslic3r_model_basic_core_sources
    ObjectID.cpp
    PolygonLines.cpp
    TriangleMeshBasic.cpp
    ModelAssembly.cpp
    GeometryTransformation.cpp
    ModelGeometry.cpp
    ModelPlacement.cpp
    ModelCopy.cpp
    ModelTransform.cpp
)

add_library(libslic3r_model_basic_core STATIC
    ${libslic3r_model_basic_core_sources}
    "${CMAKE_CURRENT_BINARY_DIR}/libslic3r_version.h")

set(libslic3r_print_object_layers_core_sources
    ObjectID.cpp
    TriangleMeshBasic.cpp
    ModelAssembly.cpp
    ModelGeometry.cpp
    ModelPlacement.cpp
    ModelCopy.cpp
    ModelTransform.cpp
    Slicing.cpp
    SlicingAdaptive.cpp
    PrintObjectSlicingConfig.cpp
    PrintObjectLayers.cpp
    PrintObjectSlice.cpp
    PrintRegionConfigExtruders.cpp
    Layer.cpp
    LayerLifecycle.cpp
    LayerRegion.cpp
    ModelBounds.cpp
    ModelRawMesh.cpp
    SurfaceCollection.cpp
    TriangleMeshTopology.cpp
    TriangleMeshTransform.cpp
)

add_library(libslic3r_print_object_layers_core STATIC
    ${libslic3r_print_object_layers_core_sources}
    "${CMAKE_CURRENT_BINARY_DIR}/libslic3r_version.h")

set(libslic3r_print_process_core_sources
    ArcFitter.cpp
    BoundingBox.cpp
    BridgeDetector.cpp
    Brim.cpp
    BuildVolume.cpp
    ByObjectPrintData.cpp
    Circle.cpp
    clipper.cpp
    ClipperUtils.cpp
    Clipper2Utils.cpp
    Config.cpp
    ConfigFileUtils.cpp
    ExPolygon.cpp
    ExPolygonSecondMoment.cpp
    Extruder.cpp
    ExtrusionEntity.cpp
    ExtrusionEntityCollection.cpp
    EdgeGrid.cpp
    EdgeGridSvg.cpp
    Fill/Fill.cpp
    Fill/Fill3DHoneycomb.cpp
    Fill/FillAdaptive.cpp
    Fill/FillBase.cpp
    Fill/FillConcentric.cpp
    Fill/FillConcentricInternal.cpp
    Fill/FillCrossHatch.cpp
    Fill/FillFloatingConcentric.cpp
    Fill/FillGyroid.cpp
    Fill/FillHoneycomb.cpp
    Fill/FillLine.cpp
    Fill/FillLightning.cpp
    Fill/FillPlanePath.cpp
    Fill/FillRectilinear.cpp
    Fill/FillSpacing.cpp
    Fill/Lightning/DistanceField.cpp
    Fill/Lightning/Generator.cpp
    Fill/Lightning/Layer.cpp
    Fill/Lightning/TreeNode.cpp
    FuzzySkin.cpp
    Flow.cpp
    FlowPrint.cpp
    FilamentGroup.cpp
    FilamentGroupUtils.cpp
    FilamentMixer.cpp
    FlushVolPredictor.cpp
    EdgeGridSvg.cpp
    GCode/ConflictChecker.cpp
    GCode/GCodeProcessor.cpp
    GCodeReader.cpp
    GCode/ThumbnailData.cpp
    GCode/ToolOrdering.cpp
    GCode/ToolOrderUtils.cpp
    GCode/WipeTower.cpp
    GCodeLayerOrdering.cpp
    Geometry.cpp
    GeometryTransformation.cpp
    Geometry/Circle.cpp
    Geometry/ConvexHull.cpp
    Geometry/MedialAxis.cpp
    Geometry/VoronoiOffset.cpp
    Geometry/Voronoi.cpp
    Geometry/VoronoiUtils.cpp
    I18N.cpp
    InternalBridgeDetector.cpp
    Interlocking/InterlockingGenerator.cpp
    LayerSvg.cpp
    Layer.cpp
    LayerLifecycle.cpp
    LayerSvg.cpp
    LayerRegion.cpp
    Line.cpp
    LocalesUtils.cpp
    ModelAssembly.cpp
    ModelAnnotations.cpp
    ModelBounds.cpp
    ModelDiff.cpp
    ModelExtruders.cpp
    ModelExportPath.cpp
    ModelGeometry.cpp
    ModelPlacement.cpp
    ModelCopy.cpp
    ModelRawMesh.cpp
    ModelThermal.cpp
    ModelTransform.cpp
    MultiMaterialSegmentation.cpp
    MultiNozzleUtils.cpp
    MixedFilamentUtils.cpp
    MultiPoint.cpp
    MutablePolygon.cpp
    ObjectID.cpp
    PrincipalComponents2D.cpp
    OverhangDetector.cpp
    ParameterUtils.cpp
    PerimeterGenerator.cpp
    PlaceholderParserConfig.cpp
    PlaceholderParser.cpp
    Platform.cpp
    Point.cpp
    Polygon.cpp
    PNGReadWrite.cpp
    Polyline.cpp
    PrintSequential.cpp
    PrintState.cpp
    PrintQueries.cpp
    PrintExtruderPrintableArea.cpp
    PrintFilamentPrintability.cpp
    PrintFilamentState.cpp
    PrintFilamentMappingConfigSync.cpp
    PrintFilamentMapping.cpp
    PrintFilamentMappingRules.cpp
    PrintEnvelope.cpp
    PrintExtruderUtils.cpp
    PrintObjectSharing.cpp
    PrintRuntime.cpp
    PrintAuxiliary.cpp
    PrintFilamentCompatibility.cpp
    PrintCache.cpp
    PrintValidate.cpp
    PrintProcessPerimeters.cpp
    PrintBaseOutput.cpp
    PrintOutputFilename.cpp
    PrintStatistics.cpp
    PrintProcess.cpp
    PrintWipeTower.cpp
    PrintProcessSupport.cpp
    PrintApply.cpp
    PrintBase.cpp
    PrintConfig.cpp
    PrintConfigGeometry.cpp
    PrintConfigRuntime.cpp
    PrintObjectAnalysis.cpp
    PrintObjectInvalidation.cpp
    PrintObjectLayers.cpp
    PrintObjectInfill.cpp
    PrintObjectPerimeters.cpp
    PrintObjectPostProcess.cpp
    PrintObjectSlicingConfig.cpp
    PrintObject.cpp
    PrintObjectExtruders.cpp
    PrintObjectUpdateSlicingParameters.cpp
    PrintObjectSupportGeometry.cpp
    PrintObjectSupport.cpp
    PrintObjectSlice.cpp
    PrintObjectSliceInterlocking.cpp
    PrintRegion.cpp
    PrintRegionConfigExtruders.cpp
    PrintRegionExtruders.cpp
    ShortestPath.cpp
    ShortestPathExtras.cpp
    Slicing.cpp
    SlicingAdaptive.cpp
    SVG.cpp
    Surface.cpp
    SurfaceCollection.cpp
    SurfaceCollectionSvg.cpp
    SurfaceSvg.cpp
    Support/SupportCommon.cpp
    Support/SupportMaterial.cpp
    Support/TreeModelVolumes.cpp
    Support/TreeSupport.cpp
    Support/TreeSupport3D.cpp
    MinimumSpanningTree.cpp
    Thread.cpp
    Time.cpp
    TriangleMeshBasic.cpp
    TriangleMeshTopology.cpp
    TriangleMeshTransform.cpp
    TriangleMeshSlicer.cpp
    TriangleSelector.cpp
    UtilsData.cpp
    UtilsMemory.cpp
    UtilsPaths.cpp
    UtilsDebug.cpp
    UtilsFormat.cpp
    UtilsRename.cpp
    VariableWidth.cpp
    RegionExpansion.cpp
    Tesselate.cpp
    MeshDiagnostics.cpp
    ElephantFootCompensation.cpp
    CustomGCode.cpp
    Interlocking/VoxelUtils.cpp
    Arachne/BeadingStrategy/BeadingStrategy.cpp
    Arachne/BeadingStrategy/BeadingStrategyFactory.cpp
    Arachne/BeadingStrategy/DistributedBeadingStrategy.cpp
    Arachne/BeadingStrategy/LimitedBeadingStrategy.cpp
    Arachne/BeadingStrategy/OuterWallContourStrategy.cpp
    Arachne/BeadingStrategy/OuterWallInsetBeadingStrategy.cpp
    Arachne/BeadingStrategy/RedistributeBeadingStrategy.cpp
    Arachne/BeadingStrategy/WideningBeadingStrategy.cpp
    Arachne/utils/ExtrusionJunction.cpp
    Arachne/utils/ExtrusionLine.cpp
    Arachne/utils/PolylineStitcher.cpp
    Arachne/utils/SquareGrid.cpp
    Arachne/SkeletalTrapezoidation.cpp
    Arachne/SkeletalTrapezoidationGraph.cpp
    Arachne/WallToolPaths.cpp
    Algorithm/LineSegmentation/LineSegmentation.cpp
)

set(libslic3r_print_perimeters_core_sources
    ${libslic3r_print_process_core_sources}
)
list(REMOVE_ITEM libslic3r_print_perimeters_core_sources
    Brim.cpp
    ByObjectPrintData.cpp
    Fill/Fill.cpp
    Fill/Fill3DHoneycomb.cpp
    Fill/FillAdaptive.cpp
    Fill/FillBase.cpp
    Fill/FillConcentric.cpp
    Fill/FillConcentricInternal.cpp
    Fill/FillCrossHatch.cpp
    Fill/FillFloatingConcentric.cpp
    Fill/FillGyroid.cpp
    Fill/FillHoneycomb.cpp
    Fill/FillLine.cpp
    Fill/FillLightning.cpp
    Fill/FillPlanePath.cpp
    Fill/FillRectilinear.cpp
    Fill/FillSpacing.cpp
    Fill/Lightning/DistanceField.cpp
    Fill/Lightning/Generator.cpp
    Fill/Lightning/Layer.cpp
    Fill/Lightning/TreeNode.cpp
    FilamentGroup.cpp
    FilamentGroupUtils.cpp
    FilamentMixer.cpp
    FlushVolPredictor.cpp
    GCode/ConflictChecker.cpp
    GCode/GCodeProcessor.cpp
    GCodeReader.cpp
    GCode/ThumbnailData.cpp
    GCode/ToolOrdering.cpp
    GCode/ToolOrderUtils.cpp
    GCode/WipeTower.cpp
    GCodeLayerOrdering.cpp
    PrintBaseOutput.cpp
    PrintCache.cpp
    PrintFilamentCompatibility.cpp
    PrintOutputFilename.cpp
    PrintProcess.cpp
    PrintObjectSliceInterlocking.cpp
    PrintWipeTower.cpp
    PrintProcessSupport.cpp
    PrintValidate.cpp
    PrintObjectAnalysis.cpp
    PrintObjectInfill.cpp
    PrintObjectPostProcess.cpp
    PrintObjectSupportGeometry.cpp
    PrintObjectSupport.cpp
    Interlocking/InterlockingGenerator.cpp
    Interlocking/VoxelUtils.cpp
    ShortestPath.cpp
    ShortestPathExtras.cpp
    SVG.cpp
    Support/SupportCommon.cpp
    Support/SupportMaterial.cpp
    SurfaceCollectionSvg.cpp
    SurfaceSvg.cpp
    Support/TreeModelVolumes.cpp
    Support/TreeSupport.cpp
    Support/TreeSupport3D.cpp
)

list(APPEND libslic3r_print_perimeters_core_sources
    PrintPerimetersCacheCompat.cpp
    PrintProcessInterlockingCompat.cpp
    PrintPerimetersInfillCompat.cpp
    PrintPerimetersOutputCompat.cpp
    PrintPerimetersValidateCompat.cpp
    PrintPerimetersWipeTowerCompat.cpp
    PrintProcessPerimetersCompat.cpp
    ShortestPath.cpp
)

set(libslic3r_print_apply_validate_core_sources
    ${libslic3r_print_perimeters_core_sources}
)
list(REMOVE_ITEM libslic3r_print_apply_validate_core_sources
    Arachne/BeadingStrategy/BeadingStrategy.cpp
    Arachne/BeadingStrategy/BeadingStrategyFactory.cpp
    Arachne/BeadingStrategy/DistributedBeadingStrategy.cpp
    Arachne/BeadingStrategy/LimitedBeadingStrategy.cpp
    Arachne/BeadingStrategy/OuterWallContourStrategy.cpp
    Arachne/BeadingStrategy/OuterWallInsetBeadingStrategy.cpp
    Arachne/BeadingStrategy/RedistributeBeadingStrategy.cpp
    Arachne/BeadingStrategy/WideningBeadingStrategy.cpp
    Arachne/SkeletalTrapezoidation.cpp
    Arachne/SkeletalTrapezoidationGraph.cpp
    Arachne/WallToolPaths.cpp
    Arachne/utils/ExtrusionJunction.cpp
    Arachne/utils/ExtrusionLine.cpp
    Arachne/utils/PolylineStitcher.cpp
    Arachne/utils/SquareGrid.cpp
    BridgeDetector.cpp
    InternalBridgeDetector.cpp
    Layer.cpp
    LayerRegion.cpp
    LayerSvg.cpp
    OverhangDetector.cpp
    PerimeterGenerator.cpp
    PrintFilamentMapping.cpp
    PrintFilamentMappingConfigSync.cpp
    PrintFilamentMappingCompat.cpp
    PrintFilamentMappingRules.cpp
    PrintObjectPerimeters.cpp
    PrintPerimetersCacheCompat.cpp
    PrintPerimetersInfillCompat.cpp
    PrintPerimetersOutputCompat.cpp
    PrintPerimetersValidateCompat.cpp
    PrintPerimetersWipeTowerCompat.cpp
    PrintProcessInterlockingCompat.cpp
    PrintProcessPerimeters.cpp
    PrintProcessPerimetersCompat.cpp
    Surface.cpp
    SurfaceCollection.cpp
    VariableWidth.cpp
)
list(APPEND libslic3r_print_apply_validate_core_sources
    GCodeLayerOrdering.cpp
    LayerLifecycle.cpp
    PrintApplyValidateCompat.cpp
    PrintApplyValidateSupportCompat.cpp
    PrintFilamentCompatibility.cpp
    PrintValidate.cpp
)

set(libslic3r_print_filament_mapping_core_sources
    ${libslic3r_print_apply_validate_core_sources}
)
list(REMOVE_ITEM libslic3r_print_filament_mapping_core_sources
    PrintApplyValidateCompat.cpp
)
list(APPEND libslic3r_print_filament_mapping_core_sources
    PrintFilamentMapping.cpp
    PrintFilamentMappingConfigSync.cpp
    PrintFilamentMappingCompat.cpp
    PrintFilamentMappingRules.cpp
)

add_library(libslic3r_print_process_core STATIC
    ${libslic3r_print_process_core_sources}
    "${CMAKE_CURRENT_BINARY_DIR}/libslic3r_version.h")

add_library(libslic3r_print_perimeters_core STATIC
    ${libslic3r_print_perimeters_core_sources}
    "${CMAKE_CURRENT_BINARY_DIR}/libslic3r_version.h")

add_library(libslic3r_print_filament_mapping_core STATIC
    ${libslic3r_print_filament_mapping_core_sources}
    "${CMAKE_CURRENT_BINARY_DIR}/libslic3r_version.h")

add_library(libslic3r_print_apply_validate_core STATIC
    ${libslic3r_print_apply_validate_core_sources}
    "${CMAKE_CURRENT_BINARY_DIR}/libslic3r_version.h")

if (SLIC3R_STATIC)
    set(CGAL_Boost_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)
endif ()
set(CGAL_DO_NOT_WARN_ABOUT_CMAKE_BUILD_TYPE ON CACHE BOOL "" FORCE)

cmake_policy(PUSH)
cmake_policy(SET CMP0011 NEW)
find_package(CGAL REQUIRED)
if(MSVC AND MSVC_VERSION GREATER_EQUAL 1950 AND NOT DEFINED OpenCV_RUNTIME)
    # Current Windows deps still package OpenCV under the vc16 runtime layout.
    set(OpenCV_RUNTIME vc16)
endif()
find_package(OpenCV REQUIRED core)
find_package(assimp REQUIRED)
cmake_policy(POP)

add_library(libslic3r_cgal STATIC
    CutSurface.hpp CutSurface.cpp
    IntersectionPoints.hpp IntersectionPoints.cpp
    MeshBoolean.hpp MeshBoolean.cpp
    TryCatchSignal.hpp TryCatchSignal.cpp
    Triangulation.hpp Triangulation.cpp)
target_include_directories(libslic3r_cgal PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# Reset compile options of libslic3r_cgal. Despite it being linked privately, CGAL options
# (-frounding-math) still propagate to dependent libs which is not desired.
get_target_property(_cgal_tgt CGAL::CGAL ALIASED_TARGET)
if (NOT TARGET ${_cgal_tgt})
    set (_cgal_tgt CGAL::CGAL)
endif ()
get_target_property(_opts ${_cgal_tgt} INTERFACE_COMPILE_OPTIONS)
if (_opts)
    set(_opts_bad "${_opts}")
    set(_opts_good "${_opts}")
    list(FILTER _opts_bad INCLUDE REGEX frounding-math)
    list(FILTER _opts_good EXCLUDE REGEX frounding-math)
    set_target_properties(${_cgal_tgt} PROPERTIES INTERFACE_COMPILE_OPTIONS "${_opts_good}")
    target_compile_options(libslic3r_cgal PRIVATE "${_opts_bad}")
endif()

if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 14.1)
    set_property(TARGET libslic3r PROPERTY CXX_STANDARD 17)
    set_property(TARGET libslic3r PROPERTY CXX_EXTENSIONS OFF)
    set_property(TARGET libslic3r_cgal PROPERTY CXX_STANDARD 17)
    set_property(TARGET libslic3r_cgal PROPERTY CXX_EXTENSIONS OFF)
endif()

target_link_libraries(libslic3r_cgal PRIVATE ${_cgal_tgt} libigl mcut)

if (MSVC AND "${CMAKE_SIZEOF_VOID_P}" STREQUAL "4") # 32 bit MSVC workaround
    target_compile_definitions(libslic3r_cgal PRIVATE CGAL_DO_NOT_USE_MPZF)
endif ()

encoding_check(libslic3r)

target_compile_definitions(libslic3r PUBLIC -DUSE_TBB -DTBB_USE_CAPTURED_EXCEPTION=0)
target_compile_definitions(libslic3r_config_core PUBLIC -DUSE_TBB -DTBB_USE_CAPTURED_EXCEPTION=0)
target_compile_definitions(libslic3r_gcodewriter_core PUBLIC -DUSE_TBB -DTBB_USE_CAPTURED_EXCEPTION=0)
target_compile_definitions(libslic3r_print_process_math_core PUBLIC -DUSE_TBB -DTBB_USE_CAPTURED_EXCEPTION=0)
target_compile_definitions(libslic3r_print_object_layers_core PUBLIC -DUSE_TBB -DTBB_USE_CAPTURED_EXCEPTION=0)
target_compile_definitions(libslic3r_model_basic_core PUBLIC -DUSE_TBB -DTBB_USE_CAPTURED_EXCEPTION=0)
target_include_directories(libslic3r PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/TextureToColor PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(libslic3r PUBLIC ${EXPAT_INCLUDE_DIRS})
target_include_directories(libslic3r_config_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(libslic3r_config_core PUBLIC ${EXPAT_INCLUDE_DIRS})
target_include_directories(libslic3r_gcodewriter_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(libslic3r_gcodewriter_core PUBLIC ${EXPAT_INCLUDE_DIRS})
target_include_directories(libslic3r_print_process_math_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(libslic3r_print_process_math_core PUBLIC ${EXPAT_INCLUDE_DIRS})
target_include_directories(libslic3r_model_basic_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(libslic3r_model_basic_core PUBLIC ${EXPAT_INCLUDE_DIRS})

# Find the OCCT and related libraries
set(OpenCASCADE_DIR "${CMAKE_PREFIX_PATH}/lib/cmake/occt")
find_package(OpenCASCADE REQUIRED)
target_include_directories(libslic3r PUBLIC ${OpenCASCADE_INCLUDE_DIR})
target_include_directories(libslic3r_model_basic_core PUBLIC ${OpenCASCADE_INCLUDE_DIR})
target_include_directories(libslic3r_print_object_layers_core PUBLIC ${OpenCASCADE_INCLUDE_DIR})

set(OCCT_LIBS
    TKXDESTEP
    TKSTEP
    TKSTEP209
    TKSTEPAttr
    TKSTEPBase
    TKXCAF
    TKXSBase
    TKVCAF
    TKCAF
    TKLCAF
    TKCDF
    TKV3d
    TKService
    TKMesh
    TKBO
    TKPrim
    TKHLR
    TKShHealing
    TKTopAlgo
    TKGeomAlgo
    TKBRep
    TKGeomBase
    TKG3d
    TKG2d
    TKMath
    TKernel
)

function(configure_print_smoke_core target)
    target_compile_definitions(${target} PUBLIC -DUSE_TBB -DTBB_USE_CAPTURED_EXCEPTION=0)
    if (MSVC)
        target_compile_options(${target} PRIVATE /Gy)
    endif()
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/../libnest2d/include PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
    target_include_directories(${target} PUBLIC ${EXPAT_INCLUDE_DIRS})
    target_include_directories(${target} PUBLIC ${OpenCASCADE_INCLUDE_DIR})
    target_link_libraries(${target}
        noise::noise
        admesh
        cereal
        libigl
        miniz
        boost_libs
        clipper
        nowide
        ${EXPAT_LIBRARIES}
        glu-libtess
        qhull
        semver
        TBB::tbb
        TBB::tbbmalloc
        libslic3r_cgal
        ${CMAKE_DL_LIBS}
        PNG::PNG
        ZLIB::ZLIB
        ${OCCT_LIBS}
        Clipper2
        mcut
        opencv_world
        ${_cgal_tgt}
        assimp::assimp
    )
    if (SLIC3R_PCH AND NOT SLIC3R_SYNTAXONLY)
        add_precompiled_header(${target} pchheader.hpp FORCEINCLUDE)
    endif ()
endfunction()

configure_print_smoke_core(libslic3r_print_process_core)
configure_print_smoke_core(libslic3r_print_perimeters_core)
configure_print_smoke_core(libslic3r_print_filament_mapping_core)
configure_print_smoke_core(libslic3r_print_apply_validate_core)
configure_print_smoke_core(libslic3r_fill_core)
target_compile_definitions(libslic3r_print_perimeters_core PUBLIC -DSLIC3R_PERIMETERS_ONLY_BUILD)


target_link_libraries(libslic3r
    noise::noise
    libnest2d
    admesh
    cereal
    libigl
    miniz
    boost_libs
    clipper
    nowide
    ${EXPAT_LIBRARIES}
    glu-libtess
    qhull
    semver
    TBB::tbb
    TBB::tbbmalloc
    libslic3r_cgal
    ${CMAKE_DL_LIBS}
    PNG::PNG
    ZLIB::ZLIB
    ${OCCT_LIBS}
    Clipper2
    mcut
    opencv_world
    ${_cgal_tgt}
    assimp::assimp
    )

target_link_libraries(libslic3r_config_core
    boost_libs
    nowide
    semver
    TBB::tbb
    TBB::tbbmalloc
)

target_link_libraries(libslic3r_gcodewriter_core
    boost_libs
    nowide
    semver
    TBB::tbb
    TBB::tbbmalloc
)

target_link_libraries(libslic3r_print_process_math_core
    boost_libs
    clipper
    nowide
    semver
    TBB::tbb
    TBB::tbbmalloc
)

target_link_libraries(libslic3r_model_basic_core
    libslic3r_config_core
    admesh
    cereal
    libigl
    miniz
    boost_libs
    clipper
    nowide
    ${EXPAT_LIBRARIES}
    glu-libtess
    qhull
    semver
    TBB::tbb
    TBB::tbbmalloc
    ${OCCT_LIBS}
)

target_link_libraries(libslic3r_print_object_layers_core
    libslic3r_config_core
    libslic3r_print_process_math_core
    admesh
    cereal
    libigl
    miniz
    boost_libs
    clipper
    nowide
    ${EXPAT_LIBRARIES}
    glu-libtess
    qhull
    semver
    TBB::tbb
    TBB::tbbmalloc
    ${OCCT_LIBS}
)

if(NOT WIN32)
    target_link_libraries(libslic3r freetype)
    if (NOT APPLE)
        target_link_libraries(libslic3r fontconfig)
    endif()
endif()

if (APPLE)
    find_library(FOUNDATION Foundation REQUIRED)
    find_library(MODELIO ModelIO REQUIRED)
    target_link_libraries(libslic3r ${FOUNDATION} ${MODELIO})
endif ()

if (TARGET OpenVDB::openvdb)
    target_link_libraries(libslic3r OpenVDB::openvdb)
endif()

if(WIN32)
    target_link_libraries(libslic3r Psapi.lib)
endif()

if(SLIC3R_PROFILE)
    target_link_libraries(libslic3r Shiny)
endif()

if (SLIC3R_PCH AND NOT SLIC3R_SYNTAXONLY)
    add_precompiled_header(libslic3r pchheader.hpp FORCEINCLUDE)
endif ()

if (SLIC3R_PCH AND NOT SLIC3R_SYNTAXONLY)
    add_precompiled_header(libslic3r_gcodewriter_core pchheader.hpp FORCEINCLUDE)
endif ()

if (SLIC3R_PCH AND NOT SLIC3R_SYNTAXONLY)
    add_precompiled_header(libslic3r_model_basic_core pchheader.hpp FORCEINCLUDE)
endif ()
