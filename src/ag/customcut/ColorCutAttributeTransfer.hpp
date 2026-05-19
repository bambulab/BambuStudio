#ifndef slic3r_ColorCutAttributeTransfer_hpp_
#define slic3r_ColorCutAttributeTransfer_hpp_

#include "ColorCutAppearanceTypes.hpp"
#include "ColorCutGeometryBackend.hpp"
#include "ColorCutTypes.hpp"

#include "libslic3r/Format/OBJ.hpp"
#include "libslic3r/TriangleMesh.hpp"

namespace Slic3r {

class ModelVolume;

namespace ColorCut {

class ColorCutAttributeTransfer
{
public:
    void apply(
        const ObjectAppearanceSnapshot &source_appearance,
        GeometryCutOutput &geometry_output,
        const ColorCutRequest &request) const;

    void reapply_from_single_source(
        const ObjectAppearanceSnapshot &source_appearance,
        const ModelObject &source_object,
        const ModelVolume &source_volume,
        int instance_index,
        const ModelObjectPtrs &target_objects,
        bool uniform_cap_color = false,
        const CutSurfaceClassifier &cut_surface_classifier = {},
        bool planar_cut_surface = false,
        bool groove_cut_surface = false) const;

    void reapply_after_mesh_repair(
        const VolumeAppearanceSnapshot &source_appearance,
        const TriangleMesh &source_mesh,
        ModelVolume &target_volume,
        bool uniform_cap_color = false,
        const CutSurfaceClassifier &cut_surface_classifier = {},
        bool planar_cut_surface = false,
        bool groove_cut_surface = false) const;

    void apply_uniform_color_to_surface_planes(
        const ObjectAppearanceSnapshot &source_appearance,
        const ModelVolume &source_volume,
        const ModelObjectPtrs &target_objects,
        const std::vector<SurfacePlaneDefinition> &surface_planes,
        double plane_tolerance = 0.25,
        double min_abs_normal_dot = 0.92) const;

    void apply_uniform_color_to_marked_triangles(
        const ObjectAppearanceSnapshot &source_appearance,
        const ModelVolume &source_volume,
        const ModelObjectPtrs &target_objects) const;
};

} // namespace ColorCut
} // namespace Slic3r

#endif
