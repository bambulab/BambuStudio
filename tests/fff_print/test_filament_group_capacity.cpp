#include <catch2/catch.hpp>

#include "libslic3r/Print.hpp"
#include "libslic3r/GCode/ToolOrdering.hpp"

#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

namespace {

// Dual-extruder machine (H2D-like) with one 4-slot AMS on each extruder.
DynamicPrintConfig make_dual_extruder_config(unsigned num_filaments)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_num_filaments(num_filaments);

    std::vector<std::string> colours = {"#E8DBB7", "#C8C8C8", "#3F8E43", "#6F5034", "#FFFF00", "#FFFFFF", "#000000", "#FF0000"};
    colours.resize(num_filaments, "#808080");
    config.set_key_value("filament_colour", new ConfigOptionStrings(colours));
    config.set_key_value("filament_type", new ConfigOptionStrings(std::vector<std::string>(num_filaments, "PLA")));
    config.set_key_value("filament_diameter", new ConfigOptionFloats(std::vector<double>(num_filaments, 1.75)));
    config.set_key_value("filament_map", new ConfigOptionInts(std::vector<int>(num_filaments, 1)));

    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4, 0.4}));
    config.set_key_value("extruder_type", new ConfigOptionEnumsGeneric{(int) etDirectDrive, (int) etDirectDrive});
    config.set_key_value("nozzle_volume_type", new ConfigOptionEnumsGeneric{(int) nvtStandard, (int) nvtStandard});
    config.set_key_value("extruder_max_nozzle_count", new ConfigOptionInts({1, 1}));
    config.set_key_value("extruder_nozzle_count", new ConfigOptionInts({1, 1}));
    config.set_key_value("extruder_ams_count", new ConfigOptionStrings({"1#0|4#1", "1#0|4#1"}));
    config.set_key_value("master_extruder_id", new ConfigOptionInt(1));
    config.set_key_value("filament_map_mode", new ConfigOptionEnum<FilamentMapMode>(FilamentMapMode::fmmAutoForFlush));

    // Uniform flush cost between any two different filaments, on both extruders.
    std::vector<double> flush;
    for (int ext = 0; ext < 2; ++ext)
        for (unsigned from = 0; from < num_filaments; ++from)
            for (unsigned to = 0; to < num_filaments; ++to)
                flush.push_back(from == to ? 0. : 300.);
    config.set_key_value("flush_volumes_matrix", new ConfigOptionFloats(flush));
    return config;
}

} // namespace

TEST_CASE("Auto-for-flush grouping respects per-extruder AMS capacity", "[FilamentGroupCapacity]")
{
    const unsigned num_filaments = 6;
    DynamicPrintConfig config = make_dual_extruder_config(num_filaments);

    Print print;
    Model model;
    ModelObject *object = model.add_object();
    object->add_volume(mesh(TestMesh::cube_20x20x20));
    object->add_instance();
    object->ensure_on_bed();
    print.apply(model, config);
    REQUIRE(print.config().nozzle_diameter.values.size() == 2);

    // Filament 0 on every layer; filaments 1..5 each in their own band of layers. Minimising flush alone
    // puts all five band filaments on one extruder, which exceeds its 4 AMS slots.
    std::vector<std::vector<unsigned int>> layer_filaments;
    for (unsigned band = 1; band < num_filaments; ++band)
        for (int layer = 0; layer < 10; ++layer)
            layer_filaments.push_back({0, band});

    auto result = ToolOrdering::get_recommended_filament_maps(&print, layer_filaments, FilamentMapMode::fmmAutoForFlush, {}, {}, {});
    auto extruder_map = result.get_extruder_map(true);
    REQUIRE(extruder_map.size() >= num_filaments);

    std::vector<int> per_extruder(2, 0);
    for (unsigned f = 0; f < num_filaments; ++f) {
        REQUIRE(extruder_map[f] >= 0);
        REQUIRE(extruder_map[f] < 2);
        ++per_extruder[extruder_map[f]];
    }
    INFO("left=" << per_extruder[0] << " right=" << per_extruder[1]);
    CHECK(per_extruder[0] <= 4);
    CHECK(per_extruder[1] <= 4);
}
