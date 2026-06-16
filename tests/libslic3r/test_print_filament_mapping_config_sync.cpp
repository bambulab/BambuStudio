#include <catch2/catch.hpp>

#include "libslic3r/PrintFilamentMappingConfigSync.hpp"

using namespace Slic3r;

namespace {

void append_nozzle_variants(std::vector<std::string> &variants, std::vector<int> &ids, int id, ExtruderType extruder_type)
{
    variants.push_back(get_extruder_variant_string(extruder_type, nvtStandard));
    ids.push_back(id);
    variants.push_back(get_extruder_variant_string(extruder_type, nvtHighFlow));
    ids.push_back(id);
}

DynamicPrintConfig make_filament_mapping_config(ExtruderType second_extruder_type = etDirectDrive)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        { "fill_density", 0 },
        { "skirts", 0 },
        { "brim_width", 0 },
        { "enable_filament_dynamic_map", true },
        { "filament_map_mode", "Auto For Flush" }
    });

    config.option<ConfigOptionFloats>("nozzle_diameter")->values          = { 0.4, 0.4 };
    config.option<ConfigOptionFloats>("filament_diameter")->values        = { 1.75, 1.75 };
    config.option<ConfigOptionStrings>("filament_type")->values           = { "PLA", "PLA" };
    config.option<ConfigOptionStrings>("filament_colour")->values         = { "#FFFFFF", "#000000" };
    config.option<ConfigOptionEnumsGeneric>("extruder_type")->values      = { etDirectDrive, second_extruder_type };
    config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values = { nvtStandard, nvtHighFlow };

    std::vector<std::string> extruder_variants;
    std::vector<int>         extruder_ids;
    append_nozzle_variants(extruder_variants, extruder_ids, 1, etDirectDrive);
    append_nozzle_variants(extruder_variants, extruder_ids, 2, second_extruder_type);
    config.option<ConfigOptionStrings>("printer_extruder_variant")->values = extruder_variants;
    config.option<ConfigOptionInts>("printer_extruder_id")->values         = extruder_ids;
    config.option<ConfigOptionStrings>("print_extruder_variant")->values   = extruder_variants;
    config.option<ConfigOptionInts>("print_extruder_id")->values           = extruder_ids;

    std::vector<std::string> filament_variants;
    std::vector<int>         filament_ids;
    for (int filament_id = 1; filament_id <= 2; ++filament_id) {
        append_nozzle_variants(filament_variants, filament_ids, filament_id, etDirectDrive);
        if (second_extruder_type != etDirectDrive)
            append_nozzle_variants(filament_variants, filament_ids, filament_id, second_extruder_type);
    }
    config.option<ConfigOptionStrings>("filament_extruder_variant")->values = filament_variants;
    config.option<ConfigOptionInts>("filament_self_index")->values          = filament_ids;

    return config;
}

PrintConfig make_print_config_from(const DynamicPrintConfig &full_config)
{
    PrintConfig config;
    config.apply(full_config, true);
    return config;
}

} // namespace

SCENARIO("Filament mapping config sync keeps explicit map updates stable", "[ConfigCore][FilamentMappingConfigSync]")
{
    GIVEN("a two-extruder config with standard and high-flow nozzle variants") {
        DynamicPrintConfig original_full_config = make_filament_mapping_config();
        DynamicPrintConfig full_config          = original_full_config;
        PrintConfig        print_config         = make_print_config_from(original_full_config);

        WHEN("explicit filament, volume, and nozzle maps are synchronized") {
            const auto result = FilamentMappingConfigSync::sync_maps_to_config(
                print_config,
                original_full_config,
                full_config,
                { 2, 1 },
                { nvtHighFlow, nvtStandard },
                { 2, 1 });

            THEN("the config-facing maps match the synchronized values") {
                REQUIRE(result.has_auto_filament_map_result);
                REQUIRE(result.filament_overrides.empty());
                REQUIRE(print_config.filament_map.values == std::vector<int>{ 2, 1 });
                REQUIRE(print_config.filament_volume_map.values == std::vector<int>{ nvtHighFlow, nvtStandard });
                REQUIRE(print_config.filament_nozzle_map.values == std::vector<int>{ 2, 1 });
                REQUIRE(original_full_config.option<ConfigOptionInts>("filament_map")->values == std::vector<int>{ 2, 1 });
                REQUIRE(original_full_config.option<ConfigOptionInts>("filament_volume_map")->values == std::vector<int>{ nvtHighFlow, nvtStandard });
                REQUIRE(full_config.option<ConfigOptionInts>("filament_map")->values == std::vector<int>{ 2, 1 });
                REQUIRE(full_config.option<ConfigOptionInts>("filament_volume_map")->values == std::vector<int>{ nvtHighFlow, nvtStandard });
            }
        }
    }
}

SCENARIO("Filament mapping config sync derives volume maps from mapped nozzles", "[ConfigCore][FilamentMappingConfigSync]")
{
    GIVEN("a two-extruder config with standard and high-flow nozzle variants") {
        DynamicPrintConfig original_full_config = make_filament_mapping_config();
        DynamicPrintConfig full_config          = original_full_config;
        PrintConfig        print_config         = make_print_config_from(original_full_config);

        WHEN("filament maps are synchronized without an explicit volume map") {
            const auto result = FilamentMappingConfigSync::sync_maps_to_config(
                print_config,
                original_full_config,
                full_config,
                { 2, 1 },
                {},
                { 2, 1 });

            THEN("the volume map follows the mapped nozzle volume types") {
                REQUIRE(result.has_auto_filament_map_result);
                REQUIRE(result.filament_overrides.empty());
                REQUIRE(print_config.filament_map.values == std::vector<int>{ 2, 1 });
                REQUIRE(print_config.filament_volume_map.values == std::vector<int>{ nvtHighFlow, nvtStandard });
                REQUIRE(print_config.filament_nozzle_map.values == std::vector<int>{ 2, 1 });
                REQUIRE(original_full_config.option<ConfigOptionInts>("filament_map")->values == std::vector<int>{ 2, 1 });
                REQUIRE(original_full_config.option<ConfigOptionInts>("filament_volume_map")->values == std::vector<int>{ nvtHighFlow, nvtStandard });
                REQUIRE(full_config.option<ConfigOptionInts>("filament_map")->values == std::vector<int>{ 2, 1 });
                REQUIRE(full_config.option<ConfigOptionInts>("filament_volume_map")->values == std::vector<int>{ nvtHighFlow, nvtStandard });
            }
        }
    }
}
