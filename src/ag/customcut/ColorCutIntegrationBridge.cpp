#include "ColorCutIntegrationBridge.hpp"

#include "ColorCutAttributeRepository.hpp"
#include "ColorCutAttributeTransfer.hpp"

#include "libslic3r/Model.hpp"

#include <algorithm>
#include <map>

namespace Slic3r {
namespace ColorCut {
namespace IntegrationBridge {

namespace {

bool volume_snapshot_has_appearance(const VolumeAppearanceSnapshot &snapshot)
{
    for (const TriangleAppearanceRecord &record : snapshot.triangle_records)
        if (!record.payload.empty() || record.triangle_color_pid >= 0)
            return true;
    return false;
}

ColorCutAttributeRepository &resolve_repository(ColorCutAttributeRepository *repository)
{
    return repository != nullptr ? *repository : global_color_cut_attribute_repository();
}

} // namespace

ObjectAppearanceSnapshot build_object_snapshot(const ModelObject &object, ColorCutAttributeRepository *repository)
{
    ColorCutAppearanceSnapshotBuilder snapshot_builder;
    return snapshot_builder.build(object, &resolve_repository(repository));
}

std::optional<VolumeAppearanceSnapshot> capture_volume_appearance_snapshot(const ModelVolume &volume)
{
    ColorCutAppearanceSnapshotBuilder snapshot_builder;
    VolumeAppearanceSnapshot snapshot = snapshot_builder.build_volume(volume);
    if (!volume_snapshot_has_appearance(snapshot))
        return std::nullopt;
    return snapshot;
}

TriangleMesh build_transformed_volume_mesh(const ModelVolume &volume, int instance_index)
{
    TriangleMesh source_mesh(volume.mesh());
    Transform3d  source_transform = volume.get_matrix();
    const ModelObject *source_object = volume.get_object();
    if (source_object != nullptr && instance_index >= 0 && instance_index < static_cast<int>(source_object->instances.size()))
        source_transform = source_object->instances[static_cast<size_t>(instance_index)]->get_transformation().get_matrix_no_offset() * source_transform;
    source_mesh.transform(source_transform, true);
    return source_mesh;
}

void reapply_after_mesh_repair(const VolumeAppearanceSnapshot &snapshot, const TriangleMesh &source_mesh, ModelVolume &target_volume)
{
    if (!volume_snapshot_has_appearance(snapshot))
        return;

    ColorCutAttributeTransfer transfer;
    transfer.reapply_after_mesh_repair(snapshot, source_mesh, target_volume);
}

void reapply_after_mesh_repair(
    const VolumeAppearanceSnapshot &snapshot,
    const ModelVolume &source_volume,
    int instance_index,
    ModelVolume &target_volume)
{
    if (!volume_snapshot_has_appearance(snapshot))
        return;

    reapply_after_mesh_repair(snapshot, build_transformed_volume_mesh(source_volume, instance_index), target_volume);
}

void reapply_after_mesh_repair(const ModelVolume &source_volume, int instance_index, ModelVolume &target_volume)
{
    const std::optional<VolumeAppearanceSnapshot> snapshot = capture_volume_appearance_snapshot(source_volume);
    if (!snapshot.has_value())
        return;

    reapply_after_mesh_repair(*snapshot, source_volume, instance_index, target_volume);
}

void reapply_using_cut_provenance(
    const ModelVolume &source_volume,
    ModelVolume &target_volume,
    const std::vector<CutMeshFacetProvenance> &facet_provenance,
    ColorCutAttributeRepository *repository)
{
    const size_t target_face_count = target_volume.mesh().its.indices.size();
    if (target_face_count == 0 || facet_provenance.size() != target_face_count)
        return;

    ColorCutAttributeRepository &resolved_repository = resolve_repository(repository);

    // --- Compute dominant MMU tag and dominant external color from the SOURCE volume.
    // These are used only for new cap (cut-surface) triangles.
    std::map<std::string, size_t> mmu_counts;
    const size_t source_face_count = source_volume.mesh().its.indices.size();
    for (size_t i = 0; i < source_face_count; ++i) {
        const std::string mmu = source_volume.mmu_segmentation_facets.get_triangle_as_string(static_cast<int>(i));
        if (!mmu.empty())
            ++mmu_counts[mmu];
    }
    std::string dominant_mmu;
    if (!mmu_counts.empty())
        dominant_mmu = std::max_element(mmu_counts.begin(), mmu_counts.end(),
            [](const auto &a, const auto &b) { return a.second < b.second; })->first;

    const std::optional<ExternalVolumeColorData> source_color_data =
        resolved_repository.get_volume_color_data(source_volume.id().id);

    TriangleColor dominant_color;
    dominant_color.pid = -1;
    dominant_color.indices[0] = -1;
    dominant_color.indices[1] = -1;
    dominant_color.indices[2] = -1;
    if (source_color_data.has_value()) {
        dominant_color.pid = source_color_data->pid;
        dominant_color.indices[0] = source_color_data->pindex;
        dominant_color.indices[1] = source_color_data->pindex;
        dominant_color.indices[2] = source_color_data->pindex;
    }

    // --- Reset target MMU and rebuild it index-by-index from provenance.
    target_volume.mmu_segmentation_facets.reset();
    target_volume.mmu_segmentation_facets.reserve(static_cast<int>(target_face_count));

    ExternalVolumeColorData target_color_data;
    bool                     write_color_data = source_color_data.has_value();
    if (write_color_data) {
        target_color_data.pid    = source_color_data->pid;
        target_color_data.pindex = source_color_data->pindex;
        TriangleColor empty_color;
        empty_color.pid = -1;
        empty_color.indices[0] = -1;
        empty_color.indices[1] = -1;
        empty_color.indices[2] = -1;
        target_color_data.triangle_colors.assign(target_face_count, empty_color);
    }

    for (size_t triangle_index = 0; triangle_index < target_face_count; ++triangle_index) {
        const CutMeshFacetProvenance &fp = facet_provenance[triangle_index];

        // 1) New cap triangle introduced by the cut: paint with dominant MMU + color.
        if (fp.is_cap) {
            if (!dominant_mmu.empty())
                target_volume.mmu_segmentation_facets.set_triangle_from_string(static_cast<int>(triangle_index), dominant_mmu);
            if (write_color_data)
                target_color_data.triangle_colors[triangle_index] = dominant_color;
            continue;
        }

        // 2) Body triangle inherited from the source mesh: copy by index. This is the
        //    critical fix - no geometry/KNN comparison, so the result is identical
        //    regardless of the sign of the cut-plane rotation.
        if (fp.source_facet >= 0 && fp.source_facet < static_cast<int>(source_face_count)) {
            const std::string mmu = source_volume.mmu_segmentation_facets.get_triangle_as_string(fp.source_facet);
            if (!mmu.empty())
                target_volume.mmu_segmentation_facets.set_triangle_from_string(static_cast<int>(triangle_index), mmu);

            if (write_color_data && static_cast<size_t>(fp.source_facet) < source_color_data->triangle_colors.size())
                target_color_data.triangle_colors[triangle_index] = source_color_data->triangle_colors[fp.source_facet];
        }
    }

    target_volume.mmu_segmentation_facets.shrink_to_fit();
    if (write_color_data)
        resolved_repository.register_volume_color_data(target_volume.id().id, std::move(target_color_data));
}

void replicate_external_volume_appearance_data(
    const ModelVolume &source_volume,
    ModelVolume &target_volume,
    ColorCutAttributeRepository *repository)
{
    ColorCutAttributeRepository &resolved_repository = resolve_repository(repository);

    if (auto color_data = resolved_repository.get_volume_color_data(source_volume.id().id); color_data.has_value())
        resolved_repository.register_volume_color_data(target_volume.id().id, *color_data);

    if (auto texture_data = resolved_repository.get_texture_data(source_volume.id().id); texture_data.has_value())
        resolved_repository.register_texture_data(target_volume.id().id, *texture_data);
}

void merge_volume_appearance_data(
    const std::vector<const ModelVolume *> &source_volumes,
    ModelVolume &target_volume,
    ColorCutAttributeRepository *repository)
{
    ColorCutAttributeRepository &resolved_repository = resolve_repository(repository);

    target_volume.mmu_segmentation_facets.reset();
    target_volume.mmu_segmentation_facets.reserve(static_cast<int>(target_volume.mesh().its.indices.size()));

    ExternalVolumeColorData merged_color_data;
    bool has_external_color_data = false;

    for (const ModelVolume *volume : source_volumes) {
        if (volume == nullptr)
            continue;

        const std::optional<ExternalVolumeColorData> source_color_data = resolved_repository.get_volume_color_data(volume->id().id);
        if (source_color_data.has_value() && !has_external_color_data) {
            merged_color_data.pid = source_color_data->pid;
            merged_color_data.pindex = source_color_data->pindex;
            has_external_color_data = true;
        }

        for (size_t triangle_index = 0; triangle_index < volume->mesh().its.indices.size(); ++triangle_index) {
            const std::string mmu_tag = volume->mmu_segmentation_facets.get_triangle_as_string(static_cast<int>(triangle_index));
            if (!mmu_tag.empty()) {
                target_volume.mmu_segmentation_facets.set_triangle_from_string(
                    static_cast<int>(merged_color_data.triangle_colors.size()),
                    mmu_tag);
            }

            TriangleColor color_binding;
            color_binding.pid = -1;
            color_binding.indices[0] = -1;
            color_binding.indices[1] = -1;
            color_binding.indices[2] = -1;

            if (source_color_data.has_value()) {
                if (triangle_index < source_color_data->triangle_colors.size() && source_color_data->triangle_colors[triangle_index].pid >= 0) {
                    color_binding = source_color_data->triangle_colors[triangle_index];
                } else if (source_color_data->pid >= 0 && source_color_data->pindex >= 0) {
                    color_binding.pid = source_color_data->pid;
                    color_binding.indices[0] = source_color_data->pindex;
                    color_binding.indices[1] = source_color_data->pindex;
                    color_binding.indices[2] = source_color_data->pindex;
                }
            }

            merged_color_data.triangle_colors.emplace_back(color_binding);
        }
    }

    target_volume.mmu_segmentation_facets.shrink_to_fit();
    if (has_external_color_data)
        resolved_repository.register_volume_color_data(target_volume.id().id, std::move(merged_color_data));
}

} // namespace IntegrationBridge
} // namespace ColorCut
} // namespace Slic3r
