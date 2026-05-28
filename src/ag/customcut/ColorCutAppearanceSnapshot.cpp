#include "ColorCutAppearanceSnapshot.hpp"

#include "ColorCutAttributeRepository.hpp"

#include "libslic3r/Model.hpp"

namespace Slic3r {
namespace ColorCut {

VolumeAppearanceSnapshot ColorCutAppearanceSnapshotBuilder::build_volume(const ModelVolume &volume, const ColorCutAttributeRepository *repository) const
{
    if (repository == nullptr)
        repository = &global_color_cut_attribute_repository();

    VolumeAppearanceSnapshot volume_snapshot;
    volume_snapshot.volume_id        = volume.id().id;
    volume_snapshot.material_id      = volume.material_id();
    volume_snapshot.default_extruder = volume.extruder_id();

    const size_t face_count = volume.mesh().its.indices.size();
    volume_snapshot.triangle_records.reserve(face_count);
    for (size_t index = 0; index < face_count; ++index) {
        TriangleAppearanceRecord record;
        record.source_triangle_index = static_cast<int>(index);

        const std::string mmu_tag = volume.mmu_segmentation_facets.get_triangle_as_string(static_cast<int>(index));
        if (!mmu_tag.empty()) {
            record.kind    = TriangleAppearanceKind::MMUSegmentation;
            record.payload = mmu_tag;
        }

        volume_snapshot.triangle_records.emplace_back(std::move(record));
    }

    if (repository != nullptr) {
        auto external_color_data = repository->get_volume_color_data(volume_snapshot.volume_id);
        if (external_color_data.has_value()) {
            for (size_t index = 0; index < external_color_data->triangle_colors.size(); ++index) {
                TriangleAppearanceRecord record;
                if (index < volume_snapshot.triangle_records.size())
                    record = volume_snapshot.triangle_records[index];
                record.kind                  = record.kind == TriangleAppearanceKind::None ? TriangleAppearanceKind::TriangleColor : record.kind;
                record.source_triangle_index = static_cast<int>(index);
                record.triangle_color_pid    = external_color_data->triangle_colors[index].pid;
                record.color_indices         = {
                    external_color_data->triangle_colors[index].indices[0],
                    external_color_data->triangle_colors[index].indices[1],
                    external_color_data->triangle_colors[index].indices[2]
                };
                if (index < volume_snapshot.triangle_records.size())
                    volume_snapshot.triangle_records[index] = std::move(record);
                else
                    volume_snapshot.triangle_records.emplace_back(std::move(record));
            }
        }
    }

    return volume_snapshot;
}

ObjectAppearanceSnapshot ColorCutAppearanceSnapshotBuilder::build(const ModelObject &object, const ColorCutAttributeRepository *repository) const
{
    if (repository == nullptr)
        repository = &global_color_cut_attribute_repository();

    ObjectAppearanceSnapshot snapshot;
    snapshot.object_id = object.id().id;

    for (const ModelVolume *volume : object.volumes) {
        if (volume == nullptr)
            continue;
        snapshot.volumes.emplace_back(this->build_volume(*volume, repository));
    }

    return snapshot;
}

} // namespace ColorCut
} // namespace Slic3r
