#include <catch2/catch.hpp>

#include "libslic3r/Print.hpp"
#include "libslic3r/Model.hpp"

#include "test_data.hpp"

#include <cstdlib>
#include <sstream>
#include <string>

using namespace Slic3r;
using namespace Slic3r::Test;

namespace {

// A tool changer like the H3C: one extruder whose hotends are each a tool, two filaments, prime tower.
DynamicPrintConfig tool_changer_config(bool prime_into_infill, bool tool_changer, const std::string &second_type = "PLA")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_num_filaments(2);
    config.set_key_value("handle_hotend_as_extruder", new ConfigOptionBool(tool_changer));
    // Otherwise a single-nozzle printer: every change flushes the one nozzle.
    config.set_key_value("extruder_max_nozzle_count", new ConfigOptionIntsNullable({tool_changer ? 9 : 1}));
    config.set_key_value("extruder_nozzle_stats", new ConfigOptionStrings({tool_changer ? "Standard#9" : "Standard#1"}));
    config.set_key_value("enable_prime_tower", new ConfigOptionBool(true));
    config.set_key_value("filament_diameter", new ConfigOptionFloats({1.75, 1.75}));
    config.set_key_value("filament_colour", new ConfigOptionStrings({"#FF0000", "#00FF00"}));
    config.set_key_value("filament_type", new ConfigOptionStrings({"PLA", second_type}));
    config.set_key_value("filament_prime_volume", new ConfigOptionFloats({30., 30.}));
    config.set_key_value("sparse_infill_density", new ConfigOptionPercent(20));
    config.set_key_value("prime_into_infill", new ConfigOptionBool(prime_into_infill));
    config.set_key_value("use_relative_e_distances", new ConfigOptionBool(true));
    // No colour flush, so the infill is free to take a prime on any printer the option wrongly applied to.
    config.set_key_value("flush_volumes_matrix", new ConfigOptionFloats({0., 0., 0., 0.}));
    return config;
}

// Filament (mm) extruded by the positive E moves of relative-E G-code.
double extruded(const std::string &gcode)
{
    double             total = 0.;
    std::istringstream lines(gcode);
    for (std::string line; std::getline(lines, line);) {
        if (line.rfind("G1", 0) != 0 && line.rfind("G0", 0) != 0)
            continue;
        const size_t e = line.find(" E");
        if (e != std::string::npos) {
            const double value = std::atof(line.c_str() + e + 2);
            if (value > 0.)
                total += value;
        }
    }
    return total;
}

// Filament the prime tower's tool-change G-code extrudes.
double tower_extrusion(const Print &print)
{
    double total = 0.;
    for (const auto &layer : print.wipe_tower_data().tool_changes)
        for (const WipeTower::ToolChangeResult &change : layer)
            total += extruded(change.gcode);
    return total;
}



// Two cubes in two filaments: every layer changes tools.
double slice_two_cubes(bool prime_into_infill, bool tool_changer = true, bool flush_into_infill = true, const std::string &second_type = "PLA")
{

    Model model;
    for (int i = 0; i < 2; ++i) {
        ModelObject *object = model.add_object();
        object->add_volume(mesh(TestMesh::cube_20x20x20));
        object->add_instance();
        object->instances.front()->set_offset(Vec3d(30.0 * i, 0.0, 0.0));
        object->config.set_key_value("extruder", new ConfigOptionInt(i + 1));
        object->config.set_key_value("flush_into_infill", new ConfigOptionBool(flush_into_infill));
    }
    DynamicPrintConfig config = tool_changer_config(prime_into_infill, tool_changer, second_type);
    Print print;
    for (ModelObject *mo : model.objects)
        mo->ensure_on_bed();
    print.apply(model, config);
    print.set_status_silent();
    print.set_no_check_flag(true);
    print.process();

    REQUIRE(print.has_wipe_tower());
    REQUIRE(print.wipe_tower_data().number_of_toolchanges > 0);
    return tower_extrusion(print);
}

} // namespace

TEST_CASE("Priming into infill takes prime off the tower on a tool changer", "[PrimeIntoInfill]")
{
    const double off = slice_two_cubes(false);
    const double on  = slice_two_cubes(true);
    INFO("tower filament off " << off << " mm, on " << on << " mm");
    CHECK(off > 0.);
    CHECK(on < off);
}

TEST_CASE("Priming into infill needs objects that allow flushing into their infill", "[PrimeIntoInfill]")
{
    CHECK(slice_two_cubes(true, true, false) == Approx(slice_two_cubes(false, true, false)));
}

TEST_CASE("Priming into infill leaves other printers unchanged", "[PrimeIntoInfill]")
{
    // A single-nozzle printer flushes on the tower and into infill as before; the option does not apply.
    CHECK(slice_two_cubes(true, false) == Approx(slice_two_cubes(false, false)));
}

TEST_CASE("Priming into infill keeps materials apart", "[PrimeIntoInfill]")
{
    // PLA and PETG: neither filament's prime goes into the other's infill.
    CHECK(slice_two_cubes(true, true, true, "PETG") == Approx(slice_two_cubes(false, true, true, "PETG")));
}
