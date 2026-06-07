#include <catch2/catch.hpp>

#include "libslic3r/Support/SupportLayerUtils.hpp"

#include <deque>

using namespace Slic3r;

namespace {

SlicingParameters make_slicing_params()
{
    SlicingParameters params;
    params.valid = true;
    params.object_print_z_min = 0.6;
    params.first_object_layer_height = 0.3;
    params.layer_height = 0.2;
    return params;
}

} // namespace

SCENARIO("Support material layer helpers keep support generator layer z and height stable", "[SupportMaterialLayers]") {
    GIVEN("fixed slicing parameters with a raised object start") {
        const SlicingParameters slicing_params = make_slicing_params();

        WHEN("the first generated support layer is initialized") {
            SupportGeneratorLayer layer;
            layer_initialize(layer, sltRaftBase, slicing_params, 0);

            THEN("it uses the first object layer height and starts at the object print z minimum") {
                REQUIRE(layer.layer_type == sltRaftBase);
                REQUIRE(layer.print_z == Approx(0.9));
                REQUIRE(layer.height == Approx(0.3));
                REQUIRE(layer.bottom_z == Approx(0.6));
            }
        }

        WHEN("a later generated support layer is initialized") {
            SupportGeneratorLayer layer;
            layer_initialize(layer, sltIntermediate, slicing_params, 3);

            THEN("it uses the regular layer height and the helper-derived print z") {
                REQUIRE(layer.layer_type == sltIntermediate);
                REQUIRE(layer.print_z == Approx(layer_z(slicing_params, 3)));
                REQUIRE(layer.print_z == Approx(1.5));
                REQUIRE(layer.height == Approx(0.2));
                REQUIRE(layer.bottom_z == Approx(1.3));
            }
        }

        WHEN("a support layer is allocated from storage") {
            std::deque<SupportGeneratorLayer> storage;
            SupportGeneratorLayer &layer = layer_allocate(storage, sltTopContact, slicing_params, 2);

            THEN("the allocated layer is initialized consistently and remains owned by storage") {
                REQUIRE(storage.size() == 1);
                REQUIRE(&layer == &storage.front());
                REQUIRE(layer.layer_type == sltTopContact);
                REQUIRE(layer.print_z == Approx(1.3));
                REQUIRE(layer.height == Approx(0.2));
                REQUIRE(layer.bottom_z == Approx(1.1));
            }
        }
    }
}
