#include <catch2/catch.hpp>

#include "libslic3r/Config.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/libslic3r.h"

using namespace Slic3r;

namespace {

void append_nozzle_variants(std::vector<std::string> &variants, std::vector<int> &ids, int id, ExtruderType extruder_type)
{
    variants.push_back(get_extruder_variant_string(extruder_type, nvtStandard));
    ids.push_back(id);
    variants.push_back(get_extruder_variant_string(extruder_type, nvtHighFlow));
    ids.push_back(id);
}

DynamicPrintConfig make_filament_mapping_config(bool mixed_first_filament = false, ExtruderType second_extruder_type = etDirectDrive)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        { "fill_density", 0 },
        { "skirts", 0 },
        { "brim_width", 0 },
        { "enable_filament_dynamic_map", true },
        { "filament_map_mode", "Auto For Flush" }
    });

    config.option<ConfigOptionFloats>("nozzle_diameter")->values                = { 0.4, 0.4 };
    config.option<ConfigOptionFloats>("filament_diameter")->values              = { 1.75, 1.75 };
    config.option<ConfigOptionStrings>("filament_type")->values                 = { "PLA", "PLA" };
    config.option<ConfigOptionStrings>("filament_colour")->values               = { "#FFFFFF", "#000000" };
    config.option<ConfigOptionBools>("filament_is_mixed")->values               = { mixed_first_filament, false };
    config.option<ConfigOptionEnumsGeneric>("extruder_type")->values            = { etDirectDrive, second_extruder_type };
    config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values       = { nvtStandard, nvtHighFlow };

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

void init_dual_extruder_print(Print &print, const DynamicPrintConfig &config)
{
    Model model;

    ModelObject *model_object = model.add_object();
    model_object->name += "object.stl";

    TriangleMesh large_cube = make_cube(20, 20, 20);
    TriangleMesh small_cube = make_cube(10, 10, 10);

    ModelVolume *primary_volume   = model_object->add_volume(large_cube);
    ModelVolume *secondary_volume = model_object->add_volume(small_cube);
    primary_volume->config.set("extruder", 1);
    secondary_volume->config.set("extruder", 2);

    model_object->add_instance();
    model_object->ensure_on_bed();

    print.set_status_silent();
    REQUIRE_NOTHROW(print.apply(model, config));
}

} // namespace

SCENARIO("Print filament mapping smoke keeps dynamic reorder derivation stable", "[PrintFilamentMapping]") {
    GIVEN("A two-extruder print with dynamic filament mapping enabled") {
        WHEN("used filaments are not mixed") {
            Print print;
            init_dual_extruder_print(print, make_filament_mapping_config(false));

            THEN("dynamic group reorder remains enabled") {
                REQUIRE(print.is_dynamic_group_reorder());
            }
        }

        WHEN("a used filament is marked as mixed") {
            Print print;
            init_dual_extruder_print(print, make_filament_mapping_config(true));

            THEN("dynamic group reorder is disabled") {
                REQUIRE_FALSE(print.is_dynamic_group_reorder());
            }
        }
    }
}

SCENARIO("Print filament mapping smoke keeps config-facing map updates stable", "[PrintFilamentMapping]") {
    GIVEN("A two-extruder print with standard and high-flow nozzle variants") {
        Print print;
        init_dual_extruder_print(print, make_filament_mapping_config(false));

        WHEN("filament map results are pushed back into the print config") {
            print.update_filament_maps_to_config({ 2, 1 }, { nvtHighFlow, nvtStandard }, { 2, 1 });

            THEN("the exposed mapping getters reflect the synchronized values") {
                REQUIRE(print.get_filament_maps() == std::vector<int>{ 2, 1 });
                REQUIRE(print.get_filament_volume_maps() == std::vector<int>{ nvtHighFlow, nvtStandard });
                REQUIRE(print.get_filament_nozzle_maps() == std::vector<int>{ 2, 1 });
            }
        }
    }
}

SCENARIO("Print filament mapping smoke exposes available map modes by extruder type", "[PrintFilamentMapping]") {
    GIVEN("A two-extruder print with matching extruder types") {
        Print print;
        init_dual_extruder_print(print, make_filament_mapping_config(false));

        THEN("quality mapping is not offered") {
            const auto modes = print.get_available_filament_map_modes();
            REQUIRE(modes == std::vector<FilamentMapMode>{ fmmAutoForFlush, fmmAutoForMatch, fmmManual });
        }
    }

    GIVEN("A two-extruder print with different extruder types") {
        Print print;
        init_dual_extruder_print(print, make_filament_mapping_config(false, etBowden));

        THEN("quality mapping is offered before manual mapping") {
            const auto modes = print.get_available_filament_map_modes();
            REQUIRE(modes == std::vector<FilamentMapMode>{ fmmAutoForFlush, fmmAutoForMatch, fmmAutoForQuality, fmmManual });
        }
    }
}

SCENARIO("Print filament mapping smoke derives volume map defaults from mapped nozzles", "[PrintFilamentMapping]") {
    GIVEN("A two-extruder print with standard and high-flow nozzle variants") {
        Print print;
        init_dual_extruder_print(print, make_filament_mapping_config(false));

        WHEN("filament maps are updated without an explicit volume map") {
            print.update_filament_maps_to_config({ 2, 1 }, {}, { 2, 1 });

            THEN("the volume map follows the mapped nozzle volume types") {
                REQUIRE(print.get_filament_maps() == std::vector<int>{ 2, 1 });
                REQUIRE(print.get_filament_volume_maps() == std::vector<int>{ nvtHighFlow, nvtStandard });
                REQUIRE(print.get_filament_nozzle_maps() == std::vector<int>{ 2, 1 });
            }
        }
    }
}
