#include <catch2/catch.hpp>

#include <memory>
#include <vector>

#include "libslic3r/GCode.hpp"
#include "libslic3r/MultiNozzleUtils.hpp"

using namespace Slic3r;

SCENARIO("Origin manipulation", "[GCode]") {
	Slic3r::GCode gcodegen;
	WHEN("set_origin to (10,0)") {
    	gcodegen.set_origin(Vec2d(10,0));
    	REQUIRE(gcodegen.origin() == Vec2d(10, 0));
    }
	WHEN("set_origin to (10,0) and translate by (5, 5)") {
		gcodegen.set_origin(Vec2d(10,0));
		gcodegen.set_origin(gcodegen.origin() + Vec2d(5, 5));
		THEN("origin returns reference to point") {
    		REQUIRE(gcodegen.origin() == Vec2d(15,5));
    	}
    }
}

namespace {

Slic3r::MultiNozzleUtils::LayeredNozzleGroupResult dual_nozzle_mapping()
{
    using namespace Slic3r::MultiNozzleUtils;
    const std::vector<NozzleInfo> nozzles = {
        {"0.4", nvtStandard, 0, 0},
        {"0.4", nvtStandard, 1, 1},
    };
    auto result = LayeredNozzleGroupResult::create(std::vector<int>{0, 1}, nozzles, {0, 1});
    REQUIRE(result.has_value());
    return *result;
}

Slic3r::Polygon rect(double min_x, double min_y, double max_x, double max_y)
{
    return Slic3r::Polygon::new_scale({
        {min_x, min_y},
        {max_x, min_y},
        {max_x, max_y},
        {min_x, max_y},
    });
}

bool validate_single_move(EMoveType type, ExtrusionRole role, float x, unsigned char filament_id)
{
    GCodeProcessor processor;
    processor.result().filaments_count = 2;

    GCodeProcessorResult::MoveVertex move;
    move.type = type;
    move.extrusion_role = role;
    move.extruder_id = filament_id;
    move.object_label_id = 7;
    move.layer_duration = 1.0f;
    move.position = Vec3f{x, 100.0f, 0.2f};
    move.print_z = 0.2f;
    processor.result().moves.push_back(move);

    const Pointfs plate = {{0.0, 0.0}, {350.0, 0.0}, {350.0, 320.0}, {0.0, 320.0}};
    const std::vector<Polygons> unprintable = {
        {rect(325.0, 0.0, 350.0, 320.0)},
        {rect(0.0, 0.0, 25.0, 320.0)},
    };

    return processor.check_multi_extruder_gcode_valid(
        2,
        plate,
        320.0,
        {},
        unprintable,
        {320.0, 320.0},
        dual_nozzle_mapping(),
        {{}, {}});
}

} // namespace

TEST_CASE("Multi-nozzle area validation includes non-custom travel", "[GCode][multi-nozzle][travel]")
{
    SECTION("right-nozzle travel below X25 is rejected")
    {
        REQUIRE_FALSE(validate_single_move(EMoveType::Travel, erNone, 18.619f, 1));
    }

    SECTION("right-nozzle travel inside its envelope is accepted")
    {
        REQUIRE(validate_single_move(EMoveType::Travel, erNone, 25.1f, 1));
    }

    SECTION("custom machine travel remains outside the model-envelope check")
    {
        REQUIRE(validate_single_move(EMoveType::Travel, erCustom, 18.619f, 1));
    }
}
