#include <catch2/catch.hpp>

#include "libslic3r/Config.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"

using namespace Slic3r;

namespace {

std::vector<coordf_t> generate_cube_print_zs(std::initializer_list<ConfigBase::SetDeserializeItem> config_items)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict(config_items);

    Model        model;
    TriangleMesh sample_mesh = make_cube(20, 20, 20);

    ModelObject *model_object = model.add_object();
    model_object->name += "object.stl";
    model_object->add_volume(sample_mesh);
    model_object->add_instance();
    model_object->ensure_on_bed();

    const SlicingParameters slicing_params = PrintObject::slicing_parameters(config, *model_object, float(model_object->bounding_box().max(2)));

    std::vector<coordf_t> layer_height_profile;
    bool                  nozzle_range_reset = false;
    REQUIRE(PrintObject::update_layer_height_profile(*model_object, slicing_params, layer_height_profile, nozzle_range_reset));
    REQUIRE_FALSE(nozzle_range_reset);

    const std::vector<coordf_t> object_layers = generate_object_layers(
        slicing_params,
        layer_height_profile,
        config.option<ConfigOptionBool>("precise_z_height")->value);

    std::vector<coordf_t> print_zs;
    print_zs.reserve(object_layers.size() / 2);
    for (size_t i = 1; i < object_layers.size(); i += 2)
        print_zs.push_back(object_layers[i]);

    return print_zs;
}

} // namespace

SCENARIO("Print process core smoke path covers migrated PrintObject layer height generation", "[PrintProcessCore]") {
    GIVEN("a 20mm cube with 2mm first layer height") {
        WHEN("layer height is 2mm and nozzle diameter is 3mm") {
            const std::vector<coordf_t> print_zs = generate_cube_print_zs({
                { "initial_layer_print_height", 2 },
                { "layer_height", 2 },
                { "nozzle_diameter", 3 }
            });

            THEN("the object contains ten 2mm layers") {
                REQUIRE(print_zs.size() == 10);
                coordf_t last_print_z = 0.0;
                for (const coordf_t print_z : print_zs) {
                    REQUIRE((print_z - last_print_z) == Approx(2.0));
                    last_print_z = print_z;
                }
            }
        }

        WHEN("layer height is 10mm and nozzle diameter is 11mm") {
            const std::vector<coordf_t> print_zs = generate_cube_print_zs({
                { "initial_layer_print_height", 2 },
                { "layer_height", 10 },
                { "nozzle_diameter", 11 }
            });

            THEN("the object keeps the expected coarse layer z positions") {
                REQUIRE(print_zs.size() == 3);
                REQUIRE(print_zs.front() == Approx(2.0));
                REQUIRE(print_zs[1] == Approx(12.0));
            }
        }

        WHEN("layer height is 15mm and nozzle diameter is 16mm") {
            const std::vector<coordf_t> print_zs = generate_cube_print_zs({
                { "initial_layer_print_height", 2 },
                { "layer_height", 15 },
                { "nozzle_diameter", 16 }
            });

            THEN("the object keeps the expected two-layer output") {
                REQUIRE(print_zs.size() == 2);
                REQUIRE(print_zs[0] == Approx(2.0));
                REQUIRE(print_zs[1] == Approx(17.0));
            }
        }
    }
}
