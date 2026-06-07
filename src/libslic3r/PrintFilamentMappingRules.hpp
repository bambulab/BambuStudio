#ifndef slic3r_PrintFilamentMappingRules_hpp_
#define slic3r_PrintFilamentMappingRules_hpp_

#include "PrintConfig.hpp"

#include <vector>

namespace Slic3r::FilamentMappingRules {

bool is_dynamic_group_reorder_enabled(
    bool enable_dynamic_map,
    FilamentMapMode map_mode,
    size_t nozzle_count,
    const std::vector<unsigned char> &filament_is_mixed,
    const std::vector<unsigned int> &used_filaments);

std::vector<FilamentMapMode> available_modes_for_extruder_types(const std::vector<int> &extruder_type_values);

} // namespace Slic3r::FilamentMappingRules

#endif // slic3r_PrintFilamentMappingRules_hpp_
