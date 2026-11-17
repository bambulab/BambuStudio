#include <catch2/catch.hpp>

#include <algorithm>
#include <sstream>
#include <string>

#include "libslic3r/Calib.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"

using namespace Slic3r;

namespace {

struct ExtrusionState {
    double final_e;
    double max_e;
};

ExtrusionState simulate_absolute_e(const std::string &gcode)
{
    double final_e = 0.;
    double max_e   = 0.;

    std::istringstream lines(gcode);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::string command;
        if (!(words >> command) ||
            (command != "G0" && command != "G1" && command != "G92"))
            continue;

        std::string word;
        while (words >> word) {
            if (word.size() >= 2 && word.front() == 'E') {
                final_e = std::stod(word.substr(1));
                max_e   = std::max(max_e, final_e);
                break;
            }
        }
    }

    return {final_e, max_e};
}

} // namespace

TEST_CASE("PA pattern resets extrusion after its final layer in absolute E mode", "[Calib][Regression]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        {"use_relative_e_distances", "0"},
        {"line_width", "0.45"},
        {"initial_layer_line_width", "0.45"},
    });

    Model model;
    model.add_object("cube", "", make_cube(20., 20., 20.))->add_instance();

    Calib_Params params;
    params.mode  = CalibMode::Calib_PA_Pattern;
    params.start = 0.;
    params.end   = 0.08;
    params.step  = 0.002;

    CalibPressureAdvancePattern pattern(
        params, config, false, model, Vec3d::Zero());
    pattern.generate_custom_gcodes(
        config, false, model, Vec3d::Zero());

    std::string gcode;
    for (const CustomGCode::Item &item :
         model.plates_custom_gcodes.at(model.curr_plate_index).gcodes)
        gcode += item.extra;

    const ExtrusionState state = simulate_absolute_e(gcode);
    REQUIRE(state.max_e > 1.);
    REQUIRE_THAT(
        state.final_e,
        Catch::Matchers::WithinAbs(0., 1e-9));
}
