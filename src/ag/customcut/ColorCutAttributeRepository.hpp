#ifndef slic3r_ColorCutAttributeRepository_hpp_
#define slic3r_ColorCutAttributeRepository_hpp_

#include <array>
#include <cassert>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "libslic3r/Geometry.hpp"
#include "libslic3r/Format/OBJ.hpp"

namespace Slic3r {
namespace ColorCut {

struct ExternalVolumeColorData {
    int                        pid{-1};
    int                        pindex{-1};
    std::vector<TriangleColor> triangle_colors;
};

struct ExternalTextureData {
    bool                                             has_uv_png{false};
    std::string                                      object_directory;
    std::unordered_map<int, std::string>             uv_map_pngs;
    std::vector<std::array<Vec2f, 3>>                uvs;
};

class ColorCutAttributeRepository
{
public:
    void register_volume_color_data(int volume_id, ExternalVolumeColorData data);
    std::optional<ExternalVolumeColorData> get_volume_color_data(int volume_id) const;

    void register_color_group_map(std::unordered_map<int, std::vector<std::string>> color_group_map);
    const std::unordered_map<int, std::vector<std::string>> &get_color_group_map() const;

    void register_texture_data(int volume_id, ExternalTextureData data);
    std::optional<ExternalTextureData> get_texture_data(int volume_id) const;

private:
    std::unordered_map<int, ExternalVolumeColorData> m_volume_color_data;
    std::unordered_map<int, std::vector<std::string>> m_color_group_map;
    std::unordered_map<int, ExternalTextureData>     m_texture_data;
};

ColorCutAttributeRepository &global_color_cut_attribute_repository();

} // namespace ColorCut
} // namespace Slic3r

#endif
