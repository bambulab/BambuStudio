#include <catch2/catch.hpp>

#include "libslic3r/PresetBundle.hpp"

using namespace Slic3r;

TEST_CASE("Project vectors and flush matrices resize across the 32 boundary", "[filament_capacity_integration]")
{
    PresetBundle bundle;
    bundle.filament_presets = {bundle.filaments.get_selected_preset_name()};
    for (unsigned int count : {32u, 33u, 64u, 65u}) {
        CAPTURE(count);
        bundle.set_num_filaments(count, "#FFFFFF");
        const size_t expected = std::min<size_t>(count, MAXIMUM_EXTRUDER_NUMBER);
        REQUIRE(bundle.filament_presets.size() == expected);
        REQUIRE(bundle.project_config.option<ConfigOptionStrings>("filament_colour")->values.size() == expected);
        REQUIRE(bundle.project_config.option<ConfigOptionInts>("filament_map")->values.size() == expected);
        REQUIRE(bundle.project_config.option<ConfigOptionBools>("filament_is_mixed")->values.size() == expected);
        REQUIRE(bundle.project_config.option<ConfigOptionStrings>("filament_mixed_components")->values.size() == expected);
        REQUIRE(bundle.project_config.option<ConfigOptionStrings>("filament_mixed_sublayer_ratios")->values.size() == expected);
        const size_t nozzles = bundle.project_config.option<ConfigOptionFloats>("flush_multiplier")->values.size();
        REQUIRE(bundle.project_config.option<ConfigOptionFloats>("flush_volumes_matrix")->values.size() == expected * expected * nozzles);
    }
}

TEST_CASE("Hardware color counts exclude mixed recipes in a 64-slot project", "[filament_capacity_integration]")
{
    PresetBundle bundle;
    bundle.filament_presets = {bundle.filaments.get_selected_preset_name()};
    bundle.set_num_filaments(64, "#FFFFFF");
    auto* mixed = bundle.project_config.option<ConfigOptionBools>("filament_is_mixed");
    REQUIRE(mixed != nullptr);
    std::fill(mixed->values.begin() + 4, mixed->values.end(), true);
    REQUIRE((bundle.physical_filament_config_indices() == std::vector<size_t>{0, 1, 2, 3}));
    REQUIRE(bundle.filament_presets.size() == 64);
}
