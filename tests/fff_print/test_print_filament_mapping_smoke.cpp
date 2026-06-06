#include <catch2/catch.hpp>

#include "libslic3r/Config.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/libslic3r.h"

using namespace Slic3r;

namespace {

DynamicPrintConfig make_filament_mapping_config(bool mixed_first_filament = false)
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
    config.option<ConfigOptionEnumsGeneric>("extruder_type")->values            = { etDirectDrive, etDirectDrive };
    config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values       = { nvtStandard, nvtHighFlow };
    config.option<ConfigOptionStrings>("printer_extruder_variant")->values      = { "Direct Drive Standard", "Direct Drive High Flow" };
    config.option<ConfigOptionStrings>("print_extruder_variant")->values        = { "Direct Drive Standard", "Direct Drive High Flow" };
    config.option<ConfigOptionInts>("print_extruder_id")->values                = { 1, 2 };
    config.option<ConfigOptionStrings>("filament_extruder_variant")->values     = { "Direct Drive Standard", "Direct Drive High Flow" };
    config.option<ConfigOptionInts>("filament_self_index")->values              = { 1, 2 };

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
