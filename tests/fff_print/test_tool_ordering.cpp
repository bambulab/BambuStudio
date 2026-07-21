#include <catch2/catch.hpp>

#include "libslic3r/Print.hpp"

#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

TEST_CASE("Tool ordering accepts fewer maximum layer heights than nozzles", "[ToolOrdering]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_num_filaments(2);
    // Keep max_layer_height deliberately shorter than the nozzle list.
    config.set_deserialize_strict({
        { "nozzle_diameter", "0.4,0.4" },
        { "max_layer_height", "0.3" }
    });

    REQUIRE(config.option<ConfigOptionFloatsNullable>("nozzle_diameter")->values.size() == 2);
    REQUIRE(config.option<ConfigOptionFloatsNullable>("max_layer_height")->values.size() == 1);

    Print print;
    init_and_process_print({TestMesh::cube_20x20x20}, print, config);

    REQUIRE_FALSE(print.objects().front()->layers().empty());
}
