#include <catch2/catch.hpp>

#include "libslic3r/PrintConfig.hpp"

using namespace Slic3r;

SCENARIO("get_real_skirt_dist calculates the correct boundary including loop width", "[Config][ConfigRuntime]") {
    GIVEN("A DynamicPrintConfig with skirt loops and spacing") {
        Slic3r::DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();

        config.set("skirt_distance", 2.0);
        config.set("skirt_loops", 3);
        config.set("initial_layer_line_width", 0.4);
        config.set("draft_shield", "disabled");
        config.set("skirt_per_object", true);

        WHEN("get_real_skirt_dist is called") {
            float dist = Slic3r::get_real_skirt_dist(config);

            THEN("The distance includes the width of the skirt loops") {
                REQUIRE(dist == Approx(3.2));
            }
        }

        WHEN("skirt_per_object is disabled") {
            config.set("skirt_per_object", false);
            float dist = Slic3r::get_real_skirt_dist(config);
            THEN("The distance is not applied") {
                REQUIRE(dist == 0.0f);
            }
        }

        WHEN("skirt_loops is 0") {
            config.set("skirt_loops", 0);
            float dist = Slic3r::get_real_skirt_dist(config);
            THEN("The distance is exactly 0 because has_skirt() should return false") {
                REQUIRE(dist == 0.0f);
            }
        }
    }
}
