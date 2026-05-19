#ifndef slic3r_ColorCutRepositoryBridge_hpp_
#define slic3r_ColorCutRepositoryBridge_hpp_

#include "ColorCutAttributeRepository.hpp"

#include <unordered_map>

namespace Slic3r {
namespace ColorCut {
namespace RepositoryBridge {

const std::unordered_map<int, std::vector<std::string>> &export_color_group_map(
    const ColorCutAttributeRepository *repository = nullptr);

std::optional<ExternalVolumeColorData> export_volume_color_data(
    int volume_id,
    const ColorCutAttributeRepository *repository = nullptr);

void register_standard_3mf_colors(
    const std::unordered_map<int, std::vector<std::string>> &color_group_map,
    const std::unordered_map<int, VolumeColorInfo> &volume_color_data);

void register_standard_3mf_colors(
    ColorCutAttributeRepository &repository,
    const std::unordered_map<int, std::vector<std::string>> &color_group_map,
    const std::unordered_map<int, VolumeColorInfo> &volume_color_data);

} // namespace RepositoryBridge
} // namespace ColorCut
} // namespace Slic3r

#endif
