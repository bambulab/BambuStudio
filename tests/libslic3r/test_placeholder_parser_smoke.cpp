#include <catch2/catch.hpp>

#include "libslic3r/PlaceholderParser.hpp"
#include "libslic3r/PrintConfig.hpp"

using namespace Slic3r;

SCENARIO("Placeholder parser smoke covers migrated G-code export placeholder values", "[PlaceholderParser]")
{
    PlaceholderParser parser;
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    parser.apply_config(config);

    GIVEN("the layer context values normally injected by G-code export")
    {
        DynamicConfig layer_context;
        layer_context.set_key_value("layer_num", new ConfigOptionInt(199));
        layer_context.set_key_value("layer_z", new ConfigOptionFloat(20.0));

        THEN("legacy end-gcode layer placeholders are expanded")
        {
            REQUIRE(parser.process("; Layer_num [layer_num]\n; Layer_z [layer_z]", 0, &layer_context) ==
                    "; Layer_num 199\n; Layer_z 20");
        }

        THEN("legacy layer-gcode placeholders keep layer index and Z separate")
        {
            REQUIRE(parser.process(";Layer:[layer_num] ([layer_z] mm)", 0, &layer_context) ==
                    ";Layer:199 (20 mm)");
        }
    }

    GIVEN("the current extruder value normally initialized before start gcode")
    {
        parser.set("current_extruder", 0);

        THEN("legacy start-gcode current_extruder placeholder resolves for the first extruder")
        {
            REQUIRE(parser.process("; Extruder [current_extruder]") == "; Extruder 0");
        }

        WHEN("the first printing extruder is a later filament")
        {
            parser.set("current_extruder", 1);

            THEN("legacy start-gcode current_extruder placeholder resolves to that extruder")
            {
                REQUIRE(parser.process("; Extruder [current_extruder]", 1) == "; Extruder 1");
            }
        }
    }
}
