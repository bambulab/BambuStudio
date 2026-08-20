#ifndef SLIC3R_COLOR_DECOMPOSE_RECIPE_HPP
#define SLIC3R_COLOR_DECOMPOSE_RECIPE_HPP

#include <string>
#include <vector>

namespace Slic3r {

enum class ColorDecomposeRecipeMode {
    MaterialList,
    CMYW,
    RYBW,
    CMYKW
};

struct ColorDecomposeRgb {
    unsigned char r{0};
    unsigned char g{0};
    unsigned char b{0};
};

struct ColorDecomposePhysicalFilament {
    std::string  color_hex;
    std::string  name;
    std::string  type;
    bool         is_mixed{false};
    unsigned int filament_index{0}; // 1-based physical filament index
};

struct ColorDecomposeRecipeComponent {
    std::string  color_hex;
    std::string  base_color;
    int          ratio{0};
    unsigned int filament_index{0}; // 1-based for physical filaments, 0 for standard base colors
};

struct ColorDecomposeRecipeResult {
    bool valid{false};
    ColorDecomposeRecipeMode mode{ColorDecomposeRecipeMode::MaterialList};
    std::string matched_color_hex;
    std::vector<ColorDecomposeRecipeComponent> components;
};

std::string color_decompose_rgb_to_hex(const ColorDecomposeRgb& rgb);
bool color_decompose_hex_to_rgb(const std::string& hex, ColorDecomposeRgb& out);

ColorDecomposeRecipeResult recommend_from_physical_filaments(
    const ColorDecomposeRgb& target,
    const std::vector<ColorDecomposePhysicalFilament>& physical_filaments,
    const std::string& preferred_material_type);

ColorDecomposeRecipeResult lookup_standard_recipe(
    const ColorDecomposeRgb& target,
    ColorDecomposeRecipeMode mode,
    const std::string& preferred_material_type);

// Look up the measured blend color for (component_hexes, ratios) in the
// standard color recipe table. Exact grid hits return measured_rgb; off-grid
// ratios interpolate in Lab (2-color may extrapolate a short way past the
// measured hull, e.g. 14/86 from the 15/85 and 20/80 anchors). Returns empty
// when the component set is not in the table.
std::string lookup_measured_blend_color(const std::vector<std::string>& component_hexes,
                                       const std::vector<int>& ratios);

// Whether two physical filaments are mixable, i.e. belong to the same material
// family. Official series names ("PLA Basic", "PLA Palette", "PETG Basic")
// normalize to their family ("PLA", "PETG"); plain filament_type values contain
// no space and pass through unchanged.
// Note this is deliberately looser than the series matching used to select
// standard recipe entries, where PLA Basic and PLA Palette must stay apart.
bool color_decompose_material_family_matches(const std::string& a, const std::string& b);

} // namespace Slic3r

#endif // SLIC3R_COLOR_DECOMPOSE_RECIPE_HPP
