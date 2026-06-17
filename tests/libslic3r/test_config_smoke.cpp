#include <catch2/catch.hpp>

#include "libslic3r/PrintConfig.hpp"

using namespace Slic3r;

namespace {

DynamicPrintConfig make_full_print_config()
{
    FullPrintConfig defaults;
    DynamicPrintConfig config;
    config.apply(defaults, false);
    return config;
}

} // namespace

SCENARIO("Current full print config validates expected wall options", "[Config]") {
    GIVEN("A dynamic config expanded from FullPrintConfig defaults") {
        DynamicPrintConfig config = make_full_print_config();

        WHEN("wall_loops is set to a valid positive value") {
            config.set("wall_loops", 3);
            THEN("validation succeeds") {
                REQUIRE(config.validate().empty());
            }
        }

        WHEN("wall_loops is set to a negative value") {
            config.set("wall_loops", -1);
            THEN("validation reports an error") {
                REQUIRE_FALSE(config.validate().empty());
                REQUIRE(config.validate().count("wall_loops") == 1);
            }
        }
    }
}

SCENARIO("Current full print config supports stable typed accessors", "[Config]") {
    GIVEN("A dynamic config expanded from FullPrintConfig defaults") {
        DynamicPrintConfig config = make_full_print_config();

        WHEN("A boolean option is set through the bool interface") {
            REQUIRE_NOTHROW(config.set("spiral_mode", true));
            THEN("the underlying value is updated") {
                REQUIRE(config.opt<ConfigOptionBool>("spiral_mode")->getBool());
            }
        }

        WHEN("An integer option is deserialized from string") {
            REQUIRE_NOTHROW(config.set_deserialize_strict("support_filament", "2"));
            THEN("the underlying value is updated") {
                REQUIRE(config.opt<ConfigOptionInt>("support_filament")->value == 2);
            }
        }

        WHEN("A float option is set through the double interface") {
            REQUIRE_NOTHROW(config.set("outer_wall_line_width", 0.42));
            THEN("the underlying value is updated") {
                REQUIRE(config.opt<ConfigOptionFloat>("outer_wall_line_width")->getFloat() == Approx(0.42));
            }
        }

        WHEN("A string option is set through the string interface") {
            REQUIRE_NOTHROW(config.set("machine_end_gcode", "M104 S0"));
            THEN("the underlying value is updated") {
                REQUIRE(config.opt<ConfigOptionString>("machine_end_gcode")->value == "M104 S0");
            }
        }
    }
}
