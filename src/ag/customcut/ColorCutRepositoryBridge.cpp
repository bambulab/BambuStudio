#include "ColorCutRepositoryBridge.hpp"

namespace Slic3r {
namespace ColorCut {
namespace RepositoryBridge {

namespace {

const ColorCutAttributeRepository &resolve_repository(const ColorCutAttributeRepository *repository)
{
    return repository != nullptr ? *repository : global_color_cut_attribute_repository();
}

} // namespace

const std::unordered_map<int, std::vector<std::string>> &export_color_group_map(const ColorCutAttributeRepository *repository)
{
    return resolve_repository(repository).get_color_group_map();
}

std::optional<ExternalVolumeColorData> export_volume_color_data(int volume_id, const ColorCutAttributeRepository *repository)
{
    return resolve_repository(repository).get_volume_color_data(volume_id);
}

void register_standard_3mf_colors(
    const std::unordered_map<int, std::vector<std::string>> &color_group_map,
    const std::unordered_map<int, VolumeColorInfo> &volume_color_data)
{
    register_standard_3mf_colors(global_color_cut_attribute_repository(), color_group_map, volume_color_data);
}

void register_standard_3mf_colors(
    ColorCutAttributeRepository &repository,
    const std::unordered_map<int, std::vector<std::string>> &color_group_map,
    const std::unordered_map<int, VolumeColorInfo> &volume_color_data)
{
    repository.register_color_group_map(color_group_map);

    for (const auto &entry : volume_color_data) {
        ExternalVolumeColorData data;
        data.pid             = entry.second.pid;
        data.pindex          = entry.second.pindex;
        data.triangle_colors = entry.second.triangle_colors;
        repository.register_volume_color_data(entry.first, std::move(data));
    }
}

} // namespace RepositoryBridge
} // namespace ColorCut
} // namespace Slic3r
