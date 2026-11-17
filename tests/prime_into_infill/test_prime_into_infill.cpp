#include <catch2/catch.hpp>

#include "libslic3r/Print.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/GCode/IdlePrime.hpp"

#include "test_data.hpp"

#include <cstdlib>
#include <sstream>
#include <string>

using namespace Slic3r;
using namespace Slic3r::Test;

namespace {

struct Setup
{
    bool        prime_into_infill = false;
    bool        tool_changer      = true;
    bool        flush_into_infill = true;
    std::string second_type       = "PLA";
    double      idle_tau          = 0.;  // idle_prime_time_constant, s
    double      idle_min          = 15.; // idle_prime_min_volume, mm³
    double      switch_time       = 4.;  // machine_switch_extruder_time, s (the H3C's value)
    int         raft_layers       = 0;
};

// A tool changer like the H3C: one extruder whose hotends are each a tool, two filaments, prime tower.
DynamicPrintConfig tool_changer_config(const Setup &setup)
{
    const bool         tool_changer = setup.tool_changer;
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_num_filaments(2);
    config.set_key_value("handle_hotend_as_extruder", new ConfigOptionBool(tool_changer));
    // Otherwise a single-nozzle printer: every change flushes the one nozzle.
    config.set_key_value("extruder_max_nozzle_count", new ConfigOptionIntsNullable({tool_changer ? 9 : 1}));
    config.set_key_value("extruder_nozzle_stats", new ConfigOptionStrings({tool_changer ? "Standard#9" : "Standard#1"}));
    config.set_key_value("enable_prime_tower", new ConfigOptionBool(true));
    config.set_key_value("filament_diameter", new ConfigOptionFloats({1.75, 1.75}));
    config.set_key_value("filament_colour", new ConfigOptionStrings({"#FF0000", "#00FF00"}));
    config.set_key_value("filament_type", new ConfigOptionStrings({"PLA", setup.second_type}));
    config.set_key_value("filament_prime_volume", new ConfigOptionFloats({30., 30.}));
    config.set_key_value("sparse_infill_density", new ConfigOptionPercent(20));
    config.set_key_value("prime_into_infill", new ConfigOptionBool(setup.prime_into_infill));
    config.set_key_value("idle_prime_time_constant", new ConfigOptionFloat(setup.idle_tau));
    config.set_key_value("idle_prime_min_volume", new ConfigOptionFloat(setup.idle_min));
    config.set_key_value("machine_switch_extruder_time", new ConfigOptionFloat(setup.switch_time));
    config.set_key_value("raft_layers", new ConfigOptionInt(setup.raft_layers));
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
double slice_two_cubes(const Setup &setup)
{
    Model model;
    for (int i = 0; i < 2; ++i) {
        ModelObject *object = model.add_object();
        object->add_volume(mesh(TestMesh::cube_20x20x20));
        object->add_instance();
        object->instances.front()->set_offset(Vec3d(30.0 * i, 0.0, 0.0));
        object->config.set_key_value("extruder", new ConfigOptionInt(i + 1));
        object->config.set_key_value("flush_into_infill", new ConfigOptionBool(setup.flush_into_infill));
    }
    DynamicPrintConfig config = tool_changer_config(setup);
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

Setup prime_into_infill(bool on)
{
    Setup setup;
    setup.prime_into_infill = on;
    return setup;
}

Setup idle_prime(double tau, double min_volume = 15.)
{
    Setup setup;
    setup.idle_tau = tau;
    setup.idle_min = min_volume;
    return setup;
}

} // namespace

TEST_CASE("Priming into infill takes prime off the tower on a tool changer", "[PrimeIntoInfill]")
{
    const double off = slice_two_cubes(prime_into_infill(false));
    const double on  = slice_two_cubes(prime_into_infill(true));
    INFO("tower filament off " << off << " mm, on " << on << " mm");
    CHECK(off > 0.);
    CHECK(on < off);
}

TEST_CASE("Priming into infill needs objects that allow flushing into their infill", "[PrimeIntoInfill]")
{
    Setup on = prime_into_infill(true), off = prime_into_infill(false);
    on.flush_into_infill = off.flush_into_infill = false;
    CHECK(slice_two_cubes(on) == Approx(slice_two_cubes(off)));
}

TEST_CASE("Priming into infill leaves other printers unchanged", "[PrimeIntoInfill]")
{
    // A single-nozzle printer flushes on the tower and into infill as before; the option does not apply.
    Setup on = prime_into_infill(true), off = prime_into_infill(false);
    on.tool_changer = off.tool_changer = false;
    CHECK(slice_two_cubes(on) == Approx(slice_two_cubes(off)));
}

TEST_CASE("Priming into infill keeps materials apart", "[PrimeIntoInfill]")
{
    // PLA and PETG: neither filament's prime goes into the other's infill.
    Setup on = prime_into_infill(true), off = prime_into_infill(false);
    on.second_type = off.second_type = "PETG";
    CHECK(slice_two_cubes(on) == Approx(slice_two_cubes(off)));
}

TEST_CASE("Idle prime grows from the minimum to the full prime with the idle time", "[IdlePrime]")
{
    CHECK(prime_after_idle(30.f, 15.f, 0., 60.) == Approx(15.f));
    CHECK(prime_after_idle(30.f, 15.f, 60., 60.) == Approx(15.f + 15.f * (1.f - std::exp(-1.f))));
    CHECK(prime_after_idle(30.f, 15.f, 1e6, 60.) == Approx(30.f));
    // Off, a minimum at or above the full prime, and a clock running backwards.
    CHECK(prime_after_idle(30.f, 15.f, 10., 0.) == 30.f);
    CHECK(prime_after_idle(30.f, 40.f, 0., 60.) == 30.f);
    CHECK(prime_after_idle(30.f, 15.f, -5., 60.) == Approx(15.f));
}

TEST_CASE("The tool clock measures how long each nozzle was parked", "[IdlePrime]")
{
    // Layer 0 prints filament 0 (nozzle 10) for 10 s, then filament 1 (nozzle 11) for 20 s; layer 1 filament 1 for
    // 5 s, then filament 0. Swaps take 4 s.
    ToolIdleClock clock({{{0u, 10.}, {1u, 20.}}, {{1u, 5.}, {0u, 7.}}}, 4., 10);
    CHECK(clock.use(0, 0, 10) == 0.); // already active
    CHECK(clock.use(0, 1, 11) < 0.);  // first use
    CHECK(clock.now() == Approx(10. + 4. + 20.));
    CHECK(clock.use(1, 1, 11) == 0.);
    // Nozzle 10 stopped at 10 s and comes back after 20 + 5 s of printing and two swaps.
    CHECK(clock.use(1, 0, 10) == Approx(4. + 20. + 5. + 4.));
    CHECK(clock.now() == Approx(10. + 4. + 20. + 5. + 4. + 7.));
}

TEST_CASE("The tool clock follows nozzles, not filaments", "[IdlePrime]")
{
    // Filament 0 prints from nozzle 1, then from nozzle 2, then from nozzle 1 again: nozzle 1 was parked the whole
    // time in between, not just since filament 0 left nozzle 2.
    ToolIdleClock clock({{{0u, 10.}}, {{0u, 10.}}, {{0u, 10.}}}, 0., std::nullopt);
    CHECK(clock.use(0, 0, 1) < 0.); // no nozzle active at the start
    CHECK(clock.use(1, 0, 2) < 0.);
    CHECK(clock.use(2, 0, 1) == Approx(10.));
}

TEST_CASE("The tool clock lets time pass for filaments without a nozzle", "[IdlePrime]")
{
    ToolIdleClock clock({{{0u, 10.}, {1u, 10.}, {2u, 30.}}}, 4., 1);
    clock.use(0, 0, 1);
    clock.use(0, 1, 2);
    clock.pass(0, 2); // nozzle 2 stays active
    CHECK(clock.now() == Approx(10. + 4. + 10. + 30.));
    CHECK(clock.use(0, 1, 2) == 0.);
}

TEST_CASE("The tool clock counts filament reloads", "[IdlePrime]")
{
    // Nozzle 1 is parked while nozzle 2 loads a filament for 40 s and prints for 10 s.
    ToolIdleClock clock({{{0u, 10.}, {1u, 10.}}, {{0u, 10.}}}, 4., 1);
    clock.use(0, 0, 1);
    CHECK(clock.use(0, 1, 2, 40.) < 0.);
    CHECK(clock.use(1, 0, 1) == Approx(4. + 40. + 10. + 4.));
}

TEST_CASE("Layers shorter than the minimum layer time are stretched", "[IdlePrime]")
{
    std::unordered_map<unsigned int, double> short_layer{{0u, 2.}, {1u, 6.}};
    stretch_to_min_layer_time(short_layer, {}, 16.);
    CHECK(short_layer[0] == Approx(4.));
    CHECK(short_layer[1] == Approx(12.));
    std::unordered_map<unsigned int, double> long_layer{{0u, 20.}};
    stretch_to_min_layer_time(long_layer, {}, 16.);
    CHECK(long_layer[0] == 20.);
    // Filament 0 cannot slow down past 3 s; filament 1 takes the rest.
    std::unordered_map<unsigned int, double> limited{{0u, 2.}, {1u, 6.}};
    stretch_to_min_layer_time(limited, {{0u, 3.}}, 16.);
    CHECK(limited[0] == Approx(3.));
    CHECK(limited[1] == Approx(13.));
    // A filament that does not slow down for cooling (its limit is its time) keeps its time.
    std::unordered_map<unsigned int, double> no_cooling{{0u, 2.}, {1u, 6.}};
    stretch_to_min_layer_time(no_cooling, {{1u, 6.}}, 16.);
    CHECK(no_cooling[0] == Approx(10.));
    CHECK(no_cooling[1] == 6.);
    // Neither can reach the minimum layer time: both end at their limits.
    std::unordered_map<unsigned int, double> too_short{{0u, 2.}, {1u, 6.}};
    stretch_to_min_layer_time(too_short, {{0u, 3.}, {1u, 7.}}, 16.);
    CHECK(too_short[0] == Approx(3.));
    CHECK(too_short[1] == Approx(7.));
}

TEST_CASE("Idle prime takes prime off the tower on a tool changer", "[IdlePrime]")
{
    const double off     = slice_two_cubes(Setup{});
    // A huge time constant keeps every return at the minimum, a tiny one at the full prime.
    const double minimum = slice_two_cubes(idle_prime(1e9));
    const double full    = slice_two_cubes(idle_prime(1e-9));
    INFO("tower filament off " << off << " mm, minimum " << minimum << " mm, full " << full << " mm");
    CHECK(minimum < off);
    CHECK(full == Approx(off));
}

TEST_CASE("Idle prime counts the print time between the changes", "[IdlePrime]")
{
    // With free tool changes the parked time is the other tool's print time only; if it were not counted, every
    // return would prime the minimum.
    Setup at_minimum = idle_prime(1e9);
    Setup moderate   = idle_prime(60.);
    at_minimum.switch_time = moderate.switch_time = 0.;
    const double minimum = slice_two_cubes(at_minimum);
    const double some    = slice_two_cubes(moderate);
    const double off     = slice_two_cubes(Setup{});
    INFO("tower filament minimum " << minimum << " mm, time constant 60 s " << some << " mm, off " << off << " mm");
    CHECK(some > minimum);
    CHECK(some < off);
}

TEST_CASE("Idle prime never raises the prime", "[IdlePrime]")
{
    // A minimum above the filament's prime volume leaves the prime as it is.
    CHECK(slice_two_cubes(idle_prime(1e9, 40.)) == Approx(slice_two_cubes(Setup{})));
}

TEST_CASE("Idle prime leaves other printers unchanged", "[IdlePrime]")
{
    Setup single_nozzle = idle_prime(1e9);
    Setup off;
    single_nozzle.tool_changer = off.tool_changer = false;
    CHECK(slice_two_cubes(single_nozzle) == Approx(slice_two_cubes(off)));
}

// Estimated seconds of one 20 mm cube's walls and infill, and of its support (filament -1).
std::pair<double, double> estimate_cube(int raft_layers, int wall_loops)
{
    Model        model;
    ModelObject *object = model.add_object();
    object->add_volume(mesh(TestMesh::cube_20x20x20));
    object->add_instance();
    Setup setup;
    setup.raft_layers         = raft_layers;
    DynamicPrintConfig config = tool_changer_config(setup);
    config.set_key_value("wall_loops", new ConfigOptionInt(wall_loops));
    Print print;
    object->ensure_on_bed();
    print.apply(model, config);
    print.set_status_silent();
    print.set_no_check_flag(true);
    print.process();

    double support = 0., object_paths = 0.;
    print.objects().front()->estimate_layer_filament_print_time(0, {}, [&](coordf_t, int filament, const PrintObject::PathTimeSum &sum) {
        (filament < 0 ? support : object_paths) += sum.seconds;
    });
    return {object_paths, support};
}

TEST_CASE("The print time estimate counts support in seconds", "[IdlePrime]")
{
    // A three-layer raft is support without a filament of its own.
    const auto [object_paths, support] = estimate_cube(3, 2);
    INFO("estimated raft " << support << " s, walls and infill " << object_paths << " s");
    // A 20 mm cube's raft prints in well under ten minutes, and in less time than the cube itself.
    CHECK(support > 0.);
    CHECK(support < 600.);
    CHECK(support < object_paths);
}

TEST_CASE("The print time estimate counts the walls", "[IdlePrime]")
{
    // An island's walls come as one collection of inner and outer loops; more of them take longer.
    const double two_walls = estimate_cube(0, 2).first;
    const double six_walls = estimate_cube(0, 6).first;
    INFO("estimated cube with 2 walls " << two_walls << " s, with 6 walls " << six_walls << " s");
    CHECK(six_walls > two_walls * 1.1);
}
