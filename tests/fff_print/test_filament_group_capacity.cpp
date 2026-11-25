#include <catch2/catch.hpp>

#include "libslic3r/Print.hpp"
#include "libslic3r/GCode/ToolOrdering.hpp"

#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

namespace {

struct DualExtruderMachine
{
    std::vector<int>         extruder_types;     // ExtruderType per extruder
    std::vector<std::string> extruder_ams_count; // e.g. "1#0|4#1" = one 4-slot AMS, "" = no AMS
    std::vector<double>      flush_volume;       // flush between two different filaments, per extruder
};

DynamicPrintConfig make_dual_extruder_config(const DualExtruderMachine &machine, unsigned num_filaments)
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
    config.set_key_value("extruder_type", new ConfigOptionEnumsGeneric(machine.extruder_types));
    config.set_key_value("nozzle_volume_type", new ConfigOptionEnumsGeneric{(int) nvtStandard, (int) nvtStandard});
    config.set_key_value("extruder_max_nozzle_count", new ConfigOptionInts({1, 1}));
    config.set_key_value("extruder_nozzle_count", new ConfigOptionInts({1, 1}));
    config.set_key_value("extruder_ams_count", new ConfigOptionStrings(machine.extruder_ams_count));
    config.set_key_value("master_extruder_id", new ConfigOptionInt(1));
    config.set_key_value("filament_map_mode", new ConfigOptionEnum<FilamentMapMode>(FilamentMapMode::fmmAutoForFlush));

    std::vector<double> flush;
    for (double volume : machine.flush_volume)
        for (unsigned from = 0; from < num_filaments; ++from)
            for (unsigned to = 0; to < num_filaments; ++to)
                flush.push_back(from == to ? 0. : volume);
    config.set_key_value("flush_volumes_matrix", new ConfigOptionFloats(flush));
    return config;
}

// Filament 0 on every layer; every other filament in its own band of layers. Minimising flush alone
// puts all the band filaments together on one extruder.
std::vector<std::vector<unsigned int>> one_constant_plus_bands(unsigned num_filaments)
{
    std::vector<std::vector<unsigned int>> layer_filaments;
    for (unsigned band = 1; band < num_filaments; ++band)
        for (int layer = 0; layer < 10; ++layer)
            layer_filaments.push_back({0, band});
    return layer_filaments;
}

// Used filaments per extruder for the auto-for-flush grouping of a one-cube print.
std::vector<int> filaments_per_extruder(const DynamicPrintConfig &config, unsigned num_filaments)
{
    Print print;
    Model model;
    ModelObject *object = model.add_object();
    object->add_volume(mesh(TestMesh::cube_20x20x20));
    object->add_instance();
    object->ensure_on_bed();
    print.apply(model, config);
    REQUIRE(print.config().nozzle_diameter.values.size() == 2);

    auto result = ToolOrdering::get_recommended_filament_maps(&print, one_constant_plus_bands(num_filaments),
                                                              FilamentMapMode::fmmAutoForFlush, {}, {}, {});
    auto extruder_map = result.get_extruder_map(true);
    REQUIRE(extruder_map.size() >= num_filaments);

    std::vector<int> per_extruder(2, 0);
    for (unsigned f = 0; f < num_filaments; ++f) {
        REQUIRE(extruder_map[f] >= 0);
        REQUIRE(extruder_map[f] < 2);
        ++per_extruder[extruder_map[f]];
    }
    return per_extruder;
}

} // namespace

TEST_CASE("Auto-for-flush grouping respects per-extruder AMS capacity", "[FilamentGroupCapacity]")
{
    // H2D: one 4-slot AMS on each extruder, same flush cost on both.
    const unsigned num_filaments = 6;
    DualExtruderMachine h2d{{(int) etDirectDrive, (int) etDirectDrive}, {"1#0|4#1", "1#0|4#1"}, {300., 300.}};

    auto per_extruder = filaments_per_extruder(make_dual_extruder_config(h2d, num_filaments), num_filaments);
    INFO("left=" << per_extruder[0] << " right=" << per_extruder[1]);
    CHECK(per_extruder[0] <= 4);
    CHECK(per_extruder[1] <= 4);
}

TEST_CASE("Auto-for-flush grouping puts at most one filament on an auxiliary nozzle without AMS", "[FilamentGroupCapacity]")
{
    // X2D: main extruder with one 4-slot AMS, Bowden auxiliary with no AMS, so it can only hold its
    // external spool. Flushing is cheaper on the auxiliary, so minimising flush alone would send every
    // band filament there; only the one-spool capacity keeps them on the main extruder.
    const unsigned num_filaments = 5;
    DualExtruderMachine x2d{{(int) etDirectDrive, (int) etBowden}, {"1#0|4#1", ""}, {300., 30.}};

    auto per_extruder = filaments_per_extruder(make_dual_extruder_config(x2d, num_filaments), num_filaments);
    INFO("main=" << per_extruder[0] << " auxiliary=" << per_extruder[1]);
    CHECK(per_extruder[0] <= 4);
    CHECK(per_extruder[1] <= 1);
}
