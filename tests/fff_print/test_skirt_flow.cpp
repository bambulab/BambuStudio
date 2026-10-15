#include <catch2/catch.hpp>

#include "libslic3r/Print.hpp"

using namespace Slic3r;

TEST_CASE("Skirt flow is available for an empty plate", "[Print][Skirt][Regression]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        {"skirt_per_object", "1"},
        {"skirt_loops", "1"},
        {"initial_layer_line_width", "0"},
    });

    Print print;
    Model model;
    print.apply(model, config);

    REQUIRE(print.empty());

    Flow flow;
    REQUIRE_NOTHROW(flow = print.skirt_flow());
    REQUIRE(flow.width() > 0.);
}
