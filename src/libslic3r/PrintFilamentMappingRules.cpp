#include "PrintFilamentMappingRules.hpp"

namespace Slic3r::FilamentMappingRules {

bool is_dynamic_group_reorder_enabled(
    bool enable_dynamic_map,
    FilamentMapMode map_mode,
    size_t nozzle_count,
    const std::vector<unsigned char> &filament_is_mixed,
    const std::vector<unsigned int> &used_filaments)
{
    if (!enable_dynamic_map || map_mode != FilamentMapMode::fmmAutoForFlush || nozzle_count <= 1)
        return false;

    for (unsigned int filament_id : used_filaments) {
        if (filament_id < filament_is_mixed.size() && filament_is_mixed[filament_id])
            return false;
    }

    return true;
}

std::vector<FilamentMapMode> available_modes_for_extruder_types(const std::vector<int> &extruder_type_values)
{
    std::vector<FilamentMapMode> available_modes;
    available_modes.push_back(fmmAutoForFlush);
    available_modes.push_back(fmmAutoForMatch);

    if (extruder_type_values.size() > 1) {
        const int first_type = extruder_type_values.front();
        bool has_different_types = false;
        for (size_t i = 1; i < extruder_type_values.size(); ++i) {
            if (extruder_type_values[i] != first_type) {
                has_different_types = true;
                break;
            }
        }
        if (has_different_types)
            available_modes.push_back(fmmAutoForQuality);
    }

    available_modes.push_back(fmmManual);
    return available_modes;
}

} // namespace Slic3r::FilamentMappingRules
