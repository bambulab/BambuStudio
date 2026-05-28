#include "ColorCutAttributeRepository.hpp"

namespace Slic3r {
namespace ColorCut {

ColorCutAttributeRepository &global_color_cut_attribute_repository()
{
    static ColorCutAttributeRepository repository;
    return repository;
}

void ColorCutAttributeRepository::register_volume_color_data(int volume_id, ExternalVolumeColorData data)
{
    m_volume_color_data[volume_id] = std::move(data);
}

std::optional<ExternalVolumeColorData> ColorCutAttributeRepository::get_volume_color_data(int volume_id) const
{
    auto iterator = m_volume_color_data.find(volume_id);
    if (iterator == m_volume_color_data.end())
        return std::nullopt;
    return iterator->second;
}

void ColorCutAttributeRepository::register_color_group_map(std::unordered_map<int, std::vector<std::string>> color_group_map)
{
    m_color_group_map = std::move(color_group_map);
}

const std::unordered_map<int, std::vector<std::string>> &ColorCutAttributeRepository::get_color_group_map() const
{
    return m_color_group_map;
}

void ColorCutAttributeRepository::register_texture_data(int volume_id, ExternalTextureData data)
{
    m_texture_data[volume_id] = std::move(data);
}

std::optional<ExternalTextureData> ColorCutAttributeRepository::get_texture_data(int volume_id) const
{
    auto iterator = m_texture_data.find(volume_id);
    if (iterator == m_texture_data.end())
        return std::nullopt;
    return iterator->second;
}

} // namespace ColorCut
} // namespace Slic3r
