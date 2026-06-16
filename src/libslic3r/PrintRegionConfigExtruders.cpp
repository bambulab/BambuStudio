#include "Print.hpp"

#include <algorithm>

namespace Slic3r {

void PrintRegion::collect_object_printing_extruders(const PrintConfig &print_config, const PrintRegionConfig &region_config, const bool has_brim, std::vector<unsigned int> &object_extruders)
{
    // These checks reflect the same logic used in the GUI for enabling/disabling extruder selection fields.
    // BBS
    auto num_extruders = (int)print_config.filament_diameter.size();
    auto emplace_extruder = [num_extruders, &object_extruders](int extruder_id) {
        int i = std::max(0, extruder_id - 1);
        object_extruders.emplace_back((i >= num_extruders) ? 0 : i);
    };
    if (region_config.wall_loops.value > 0 || has_brim)
        emplace_extruder(region_config.wall_filament);
    if (region_config.sparse_infill_density.value > 0)
        emplace_extruder(region_config.sparse_infill_filament);
    if (region_config.top_shell_layers.value > 0 || region_config.bottom_shell_layers.value > 0)
        emplace_extruder(region_config.solid_infill_filament);
}

} // namespace Slic3r
