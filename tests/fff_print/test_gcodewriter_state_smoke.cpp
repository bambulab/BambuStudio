#include <catch2/catch.hpp>

#include "libslic3r/GCodeWriter.hpp"

using namespace Slic3r;

namespace {

void require_lift_cycle_after_unlift(double travel_z)
{
    GCodeWriter writer;
    GCodeConfig &config = writer.config;
    config.set_deserialize_strict({
        { "gcode_flavor", "reprap" },
        { "travel_speed", 130 },
        { "retraction_length", 2 },
        { "retract_length_toolchange", 10 },
        { "z_hop", 1.5 },
        { "retract_lift_above", 0 },
        { "retract_lift_below", 1000000 }
    });
    writer.set_extruders({0});
    writer.set_extruder(0, 0);
    writer.travel_to_z(travel_z);
    REQUIRE_FALSE(writer.eager_lift(LiftType::NormalLift).empty());
    REQUIRE(writer.travel_to_z(travel_z + writer.filament()->retract_lift()).empty());
    REQUIRE(writer.unlift().empty());
    REQUIRE_FALSE(writer.eager_lift(LiftType::NormalLift).empty());
}

}

SCENARIO("GCodeWriter preserves lift state after unlift across representative travel heights", "[GCodeWriter]") {
    GIVEN("A configured single-extruder writer") {
        for (double travel_z : {203.0, 500003.0, 10.3}) {
            CAPTURE(travel_z);
            require_lift_cycle_after_unlift(travel_z);
        }
    }
}
