#include <catch2/catch.hpp>

#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"

using namespace Slic3r;

namespace {

static void init_and_process_mesh_print(Print &print, const TriangleMesh &mesh, std::initializer_list<ConfigBase::SetDeserializeItem> config_items)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict(config_items);

    Model model;
    ModelObject *model_object = model.add_object();
    model_object->name += "object.stl";
    model_object->add_volume(mesh);
    model_object->add_instance();
    model_object->ensure_on_bed();

    print.auto_assign_extruders(model_object);
    print.set_status_silent();
    REQUIRE_NOTHROW(print.apply(model, config));
    REQUIRE_NOTHROW(print.validate());
    REQUIRE_NOTHROW(print.process());
}

static void assert_support_layers_respect_height_bounds(const Print &print)
{
    const ConstSupportLayerPtrsAdaptor support_layers = print.objects().front()->support_layers();
    REQUIRE_FALSE(support_layers.empty());

    REQUIRE(support_layers.front()->print_z == Approx(print.config().initial_layer_print_height.value));

    const double min_layer_height = print.config().min_layer_height.values.front();
    double       max_layer_height = print.config().nozzle_diameter.values.front();
    if (print.config().max_layer_height.values.front() > EPSILON)
        max_layer_height = std::min(max_layer_height, print.config().max_layer_height.values.front());

    for (size_t i = 1; i < support_layers.size(); ++i) {
        const double layer_delta = support_layers[i]->print_z - support_layers[i - 1]->print_z;
        REQUIRE(layer_delta >= min_layer_height - EPSILON);
        REQUIRE(layer_delta <= max_layer_height + EPSILON);
    }
}

} // namespace

SCENARIO("Support material smoke covers migrated raft layer count", "[SupportMaterial]") {
    GIVEN("a 20mm cube with support material and three raft layers") {
        Print print;
        init_and_process_mesh_print(print, make_cube(20, 20, 20), {
            { "support_material", 1 },
            { "raft_layers", 3 },
            { "layer_height", 0.2 },
            { "first_layer_height", 0.4 }
        });

        THEN("three support layers are created for the raft") {
            REQUIRE(print.objects().front()->support_layers().size() == 3);
        }

        THEN("support layers honor the first layer height and configured thickness bounds") {
            assert_support_layers_respect_height_bounds(print);
        }
    }
}

SCENARIO("Support material smoke covers migrated support layer height variants", "[SupportMaterial]") {
    GIVEN("a 20mm cube with raft-backed support layers") {
        WHEN("the first layer height is 0.3mm") {
            Print print;
            init_and_process_mesh_print(print, make_cube(20, 20, 20), {
                { "support_material", 1 },
                { "raft_layers", 3 },
                { "layer_height", 0.2 },
                { "first_layer_height", 0.3 }
            });

            THEN("support layers honor the configured first layer height and bounds") {
                REQUIRE(print.objects().front()->support_layers().size() == 3);
                assert_support_layers_respect_height_bounds(print);
            }
        }

        WHEN("the layer height matches the nozzle diameter") {
            Print print;
            init_and_process_mesh_print(print, make_cube(20, 20, 20), {
                { "support_material", 1 },
                { "raft_layers", 3 },
                { "layer_height", 0.4 },
                { "first_layer_height", 0.3 },
                { "nozzle_diameter", 0.4 }
            });

            THEN("support layer deltas stay within the nozzle-derived maximum") {
                REQUIRE(print.objects().front()->support_layers().size() == 3);
                assert_support_layers_respect_height_bounds(print);
            }
        }
    }
}
