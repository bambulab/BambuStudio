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
