#ifndef slic3r_PrintFilamentMappingConfigSync_hpp_
#define slic3r_PrintFilamentMappingConfigSync_hpp_

#include "PrintConfig.hpp"

#include <vector>

namespace Slic3r::FilamentMappingConfigSync {

struct SyncResult
{
    bool has_auto_filament_map_result = false;
    bool maps_changed                 = false;
    DynamicPrintConfig filament_overrides;
    t_config_option_keys print_diff;
};

SyncResult sync_maps_to_config(
    PrintConfig &print_config,
    DynamicPrintConfig &original_full_print_config,
    DynamicPrintConfig &full_print_config,
    const std::vector<int> &filament_maps,
    const std::vector<int> &filament_volume_maps,
    const std::vector<int> &filament_nozzle_maps);

} // namespace Slic3r::FilamentMappingConfigSync

#endif // slic3r_PrintFilamentMappingConfigSync_hpp_
