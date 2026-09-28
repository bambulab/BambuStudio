#include <catch2/catch.hpp>

#include "libslic3r/GCodeWriter.hpp"

using namespace Slic3r;

TEST_CASE("Travel before initial tool selection uses the default process configuration", "[GCodeWriter]")
{
    GCodeWriter writer;
    writer.config.travel_speed.values   = {150.0};
    writer.config.travel_speed_z.values = {0.0};
    writer.set_extruders({0, 1});

    REQUIRE(writer.filament() == nullptr);
    REQUIRE_THAT(writer.travel_to_xy(Vec2d(10.0, 20.0)), Catch::Contains("F9000"));
    REQUIRE_THAT(writer.travel_to_z(0.2), Catch::Contains("F9000"));
}

TEST_CASE("Travel speed override replaces the configured travel speed", "[GCodeWriter]")
{
    // The override lets a caller print a travel at a speed the profile does not
    // describe. 0 means "no override", so every existing call site keeps emitting
    // the configured travel speed.
    GCodeWriter writer;
    writer.config.travel_speed.values   = {150.0};
    writer.config.travel_speed_z.values = {0.0};
    writer.set_extruders({0, 1});

    SECTION("an override of 0 leaves the configured speed alone") {
        REQUIRE_THAT(writer.travel_to_xy(Vec2d(10.0, 20.0), 0.0), Catch::Contains("F9000"));
    }

    SECTION("a positive override replaces it") {
        // 50 mm/s -> F3000, and the configured 150 mm/s must not appear.
        const std::string gcode = writer.travel_to_xy(Vec2d(10.0, 20.0), 50.0);
        REQUIRE_THAT(gcode, Catch::Contains("F3000"));
        REQUIRE_THAT(gcode, !Catch::Contains("F9000"));
    }

    SECTION("travel_to_xyz honours the override too") {
        REQUIRE_THAT(writer.travel_to_xyz(Vec3d(10.0, 20.0, 0.2), 50.0), Catch::Contains("F3000"));
    }
}
