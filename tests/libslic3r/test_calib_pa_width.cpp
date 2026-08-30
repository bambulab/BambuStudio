#include <catch2/catch.hpp>

#include "libslic3r/Calib.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"

using namespace Slic3r;

TEST_CASE("PA pattern resolves automatic line widths", "[Calib][Regression]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        {"initial_layer_line_width", "0"},
        {"line_width", "0"},
    });

    Model model;
    model.add_object("cube", "", make_cube(20., 20., 20.))->add_instance();

    Calib_Params params;
    params.mode  = CalibMode::Calib_PA_Pattern;
    params.start = 0.;
    params.end   = 0.08;
    params.step  = 0.002;

    CalibPressureAdvancePattern pattern(params, config, false, model, Vec3d::Zero());
    REQUIRE_NOTHROW(pattern.generate_custom_gcodes(config, false, model, Vec3d::Zero()));

    const auto &gcodes = model.plates_custom_gcodes.at(model.curr_plate_index).gcodes;
    REQUIRE_FALSE(gcodes.empty());
    REQUIRE_FALSE(gcodes.front().extra.empty());
}
