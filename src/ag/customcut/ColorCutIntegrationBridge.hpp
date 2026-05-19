#ifndef slic3r_ColorCutIntegrationBridge_hpp_
#define slic3r_ColorCutIntegrationBridge_hpp_

#include "ColorCutAppearanceSnapshot.hpp"

#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"

#include <optional>
#include <vector>

namespace Slic3r {

class ModelObject;
class ModelVolume;

namespace ColorCut {

class ColorCutAttributeRepository;

namespace IntegrationBridge {

ObjectAppearanceSnapshot build_object_snapshot(
    const ModelObject &object,
    ColorCutAttributeRepository *repository = nullptr);

std::optional<VolumeAppearanceSnapshot> capture_volume_appearance_snapshot(const ModelVolume &volume);

TriangleMesh build_transformed_volume_mesh(const ModelVolume &volume, int instance_index);

void reapply_after_mesh_repair(
    const ModelVolume &source_volume,
    int instance_index,
    ModelVolume &target_volume);

void reapply_after_mesh_repair(
    const VolumeAppearanceSnapshot &snapshot,
    const ModelVolume &source_volume,
    int instance_index,
    ModelVolume &target_volume);

void reapply_after_mesh_repair(
    const VolumeAppearanceSnapshot &snapshot,
    const TriangleMesh &source_mesh,
    ModelVolume &target_volume);

// Index-based appearance replay using cut provenance. Copies MMU tag and per-triangle
// external color from the source volume to the target volume by source-facet index,
// and assigns a dominant cap MMU/color to triangles produced as new cut-surface caps.
// This avoids any geometry-based nearest lookup, so it is sign- and orientation-stable
// (works identically for +N and -N degree planar cuts).
void reapply_using_cut_provenance(
    const ModelVolume &source_volume,
    ModelVolume &target_volume,
    const std::vector<CutMeshFacetProvenance> &facet_provenance,
    ColorCutAttributeRepository *repository = nullptr);

void replicate_external_volume_appearance_data(
    const ModelVolume &source_volume,
    ModelVolume &target_volume,
    ColorCutAttributeRepository *repository = nullptr);

void merge_volume_appearance_data(
    const std::vector<const ModelVolume *> &source_volumes,
    ModelVolume &target_volume,
    ColorCutAttributeRepository *repository = nullptr);

} // namespace IntegrationBridge
} // namespace ColorCut
} // namespace Slic3r

#endif // slic3r_ColorCutIntegrationBridge_hpp_
