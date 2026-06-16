#include "Print.hpp"
#include "PrintFilamentMappingConfigSync.hpp"
#include "PrintFilamentMappingRules.hpp"

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

namespace Slic3r {

bool Print::is_dynamic_group_reorder() const
{
    return FilamentMappingRules::is_dynamic_group_reorder_enabled(
        config().enable_filament_dynamic_map,
        config().filament_map_mode,
        config().nozzle_diameter.size(),
        config().filament_is_mixed.values,
        extruders());
}

void Print::update_filament_maps_to_config(std::vector<int> f_maps, std::vector<int> f_volume_maps, std::vector<int> f_nozzle_maps)
{
    const auto sync_result = FilamentMappingConfigSync::sync_maps_to_config(
        m_config,
        m_ori_full_print_config,
        m_full_print_config,
        f_maps,
        f_volume_maps,
        f_nozzle_maps);

    if (sync_result.maps_changed)
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": filament maps changed after pre-slicing.");

    if (!sync_result.print_diff.empty())
        m_placeholder_parser.apply_config(sync_result.filament_overrides);

    update_filament_self_index_cache();
    m_has_auto_filament_map_result = sync_result.has_auto_filament_map_result;
}

std::vector<FilamentMapMode> Print::get_available_filament_map_modes() const
{
    auto opt_extruder_type = dynamic_cast<const ConfigOptionEnumsGeneric*>(m_config.option("extruder_type"));
    return FilamentMappingRules::available_modes_for_extruder_types(opt_extruder_type ? opt_extruder_type->values : std::vector<int>{});
}

} // namespace Slic3r
