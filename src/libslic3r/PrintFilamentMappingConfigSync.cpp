#include "PrintFilamentMappingConfigSync.hpp"

#include <set>

namespace Slic3r::FilamentMappingConfigSync {

SyncResult sync_maps_to_config(
    PrintConfig &print_config,
    DynamicPrintConfig &original_full_print_config,
    DynamicPrintConfig &full_print_config,
    const std::vector<int> &filament_maps,
    const std::vector<int> &filament_volume_maps,
    const std::vector<int> &filament_nozzle_maps)
{
    SyncResult result;
    result.maps_changed =
        print_config.filament_map.values != filament_maps ||
        print_config.filament_volume_map.values != filament_volume_maps ||
        print_config.filament_nozzle_map.values != filament_nozzle_maps;

    if (result.maps_changed) {
        original_full_print_config.option<ConfigOptionInts>("filament_map", true)->values = filament_maps;
        print_config.filament_map.values = filament_maps;

        if (!filament_volume_maps.empty()) {
            original_full_print_config.option<ConfigOptionInts>("filament_volume_map", true)->values = filament_volume_maps;
            print_config.filament_volume_map.values = filament_volume_maps;
        } else {
            original_full_print_config.option<ConfigOptionInts>("filament_volume_map", true)->values.resize(filament_maps.size(), nvtStandard);
            print_config.filament_volume_map.values.resize(filament_maps.size(), nvtStandard);
        }

        if (!filament_nozzle_maps.empty()) {
            original_full_print_config.option<ConfigOptionInts>("filament_nozzle_map", true)->values = filament_nozzle_maps;
            print_config.filament_nozzle_map.values = filament_nozzle_maps;
        }
    }

    int extruder_count = 0;
    int extruder_volume_type_count = 0;
    const bool support_multi = original_full_print_config.support_different_extruders(extruder_count);
    std::vector<std::vector<NozzleVolumeType>> nozzle_volume_types;
    extruder_volume_type_count = original_full_print_config.get_extruder_nozzle_volume_count(extruder_count, nozzle_volume_types);

    print_config.filament_map_2.values = filament_maps;
    auto opt_extruder_type = dynamic_cast<const ConfigOptionEnumsGeneric*>(original_full_print_config.option("extruder_type"));
    auto opt_nozzle_volume_type = dynamic_cast<const ConfigOptionEnumsGeneric*>(original_full_print_config.option("nozzle_volume_type"));
    for (int index = 0; index < int(filament_maps.size()); index++) {
        ExtruderType extruder_type = ExtruderType(opt_extruder_type->get_at(filament_maps[index] - 1));
        NozzleVolumeType nozzle_volume_type = NozzleVolumeType(opt_nozzle_volume_type->get_at(filament_maps[index] - 1));
        if (filament_volume_maps.empty()) {
            print_config.filament_volume_map.values[index] = nozzle_volume_type;
            original_full_print_config.option<ConfigOptionInts>("filament_volume_map")->values[index] = nozzle_volume_type;
        } else if ((extruder_volume_type_count > extruder_count) && (print_config.filament_volume_map.values.size() > size_t(index))) {
            nozzle_volume_type = NozzleVolumeType(print_config.filament_volume_map.values[index]);
        }
        print_config.filament_map_2.values[index] = original_full_print_config.get_index_for_extruder(
            filament_maps[index],
            "print_extruder_id",
            extruder_type,
            nozzle_volume_type,
            "print_extruder_variant");
    }
    full_print_config = original_full_print_config;

    std::set<std::string> filament_keys = filament_options_with_variant;
    filament_keys.insert("filament_self_index");
    if ((extruder_count > 1) || support_multi) {
        full_print_config.update_values_to_printer_extruders_for_multiple_filaments(
            full_print_config,
            extruder_count,
            extruder_volume_type_count,
            filament_keys,
            "filament_self_index",
            "filament_extruder_variant");
    }

    const std::vector<std::string> &extruder_retract_keys = print_config_def.extruder_retract_keys();
    const std::string filament_prefix = "filament_";
    for (auto &opt_key : extruder_retract_keys) {
        const ConfigOption *opt_new_filament = full_print_config.option(filament_prefix + opt_key);
        const ConfigOption *opt_new_machine = full_print_config.option(opt_key);
        const ConfigOption *opt_old_machine = print_config.option(opt_key);

        if (opt_new_filament)
            compute_filament_override_value(
                opt_key,
                opt_old_machine,
                opt_new_machine,
                opt_new_filament,
                full_print_config,
                result.print_diff,
                result.filament_overrides,
                print_config.filament_map_2.values);
    }

    if ((extruder_count > 1) || support_multi) {
        t_config_option_keys keys(filament_options_with_variant.begin(), filament_options_with_variant.end());
        keys.push_back("filament_self_index");
        print_config.apply_only(full_print_config, keys, true);
    }
    if (!result.print_diff.empty())
        print_config.apply(result.filament_overrides);

    result.has_auto_filament_map_result = true;
    return result;
}

} // namespace Slic3r::FilamentMappingConfigSync
