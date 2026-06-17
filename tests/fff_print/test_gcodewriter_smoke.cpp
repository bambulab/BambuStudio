#include <catch2/catch.hpp>

#include "libslic3r/GCodeWriter.hpp"

using namespace Slic3r;

SCENARIO("GCodeWriter emits stable fixed-point speeds", "[GCodeWriter]") {
    GIVEN("A GCodeWriter instance") {
        GCodeWriter writer;

        WHEN("set_speed is called with representative values") {
            THEN("the emitted gcode uses the expected formatting") {
                REQUIRE(writer.set_speed(99999.123) == "G1 F99999.123\n");
                REQUIRE(writer.set_speed(1.0) == "G1 F1\n");
                REQUIRE(writer.set_speed(203.200022) == "G1 F203.2\n");
                REQUIRE(writer.set_speed(203.200522) == "G1 F203.201\n");
            }
        }
    }
}

SCENARIO("GCodeWriter preamble keeps migrated G-code unit setup stable", "[GCodeWriter]") {
    GIVEN("A default GCodeWriter instance") {
        GCodeWriter writer;

        WHEN("the writer preamble is emitted") {
            const std::string preamble = writer.preamble();

            THEN("absolute positioning and millimeter units are selected") {
                REQUIRE(preamble.find("G90") != std::string::npos);
                REQUIRE(preamble.find("G21") != std::string::npos);
            }
        }
    }
}

SCENARIO("GCodeWriter tracks basic extruder and bed temperature state", "[GCodeWriter]") {
    GIVEN("A single-extruder writer") {
        GCodeWriter writer;
        writer.set_extruders({0});

        WHEN("The first extruder is initialized") {
            REQUIRE(writer.need_toolchange(0));
            writer.init_extruder(0, 0);

            THEN("toolchange is no longer needed for that filament") {
                REQUIRE(writer.get_curr_extruder_id() == 0);
                REQUIRE_FALSE(writer.need_toolchange(0));
            }
        }

        WHEN("The same bed temperature is set twice without waiting") {
            std::string first = writer.set_bed_temperature(60, false);
            std::string second = writer.set_bed_temperature(60, false);

            THEN("the second call emits no redundant gcode") {
                REQUIRE(first == "M140 S60 ; set bed temperature\n");
                REQUIRE(second.empty());
            }
        }
    }
}
