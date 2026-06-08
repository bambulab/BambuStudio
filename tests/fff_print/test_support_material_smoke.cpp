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
    print.set_no_check_flag(true);
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

static TriangleMesh make_rotated_cube_with_hole()
{
    // This is the legacy cube-with-hole mesh pre-rotated around X by 90 degrees.
    // Keeping the transform in test data avoids linking extra TriangleMesh transform code into this focused target.
    return TriangleMesh(
        { { 0, 0, 0 },    { 0, -10, 0 },  { 0, 0, 20 },   { 0, -10, 20 },
          { 20, 0, 0 },   { 20, -10, 0 }, { 5, 0, 5 },    { 15, 0, 5 },
          { 5, 0, 15 },   { 20, 0, 20 },  { 15, 0, 15 },  { 20, -10, 20 },
          { 5, -10, 5 },  { 5, -10, 15 }, { 15, -10, 5 }, { 15, -10, 15 } },
        { { 0, 1, 2 },    { 2, 1, 3 },    { 1, 0, 4 },    { 5, 1, 4 },
          { 6, 7, 4 },    { 8, 2, 9 },    { 0, 2, 8 },    { 10, 8, 9 },
          { 0, 8, 6 },    { 0, 6, 4 },    { 4, 7, 9 },    { 7, 10, 9 },
          { 2, 3, 9 },    { 9, 3, 11 },   { 12, 1, 5 },   { 13, 3, 12 },
          { 14, 12, 5 },  { 3, 1, 12 },   { 11, 3, 13 },  { 11, 15, 5 },
          { 11, 13, 15 }, { 15, 14, 5 },  { 5, 4, 9 },    { 11, 5, 9 },
          { 8, 13, 12 },  { 6, 8, 12 },   { 10, 15, 13 }, { 8, 10, 13 },
          { 15, 10, 14 }, { 14, 10, 7 },  { 14, 7, 12 },  { 12, 7, 6 } });
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

SCENARIO("Support material smoke covers migrated cube-with-hole support layer bounds", "[SupportMaterial]") {
    GIVEN("the legacy cube-with-hole mesh that requires generated support") {
        const TriangleMesh mesh = make_rotated_cube_with_hole();

        WHEN("support material is generated with a 0.4mm first layer") {
            Print print;
            init_and_process_mesh_print(print, mesh, {
                { "enable_support", 1 },
                { "layer_height", 0.2 },
                { "first_layer_height", 0.4 },
                { "dont_support_bridges", false }
            });

            THEN("support layers honor the first layer height and configured thickness bounds") {
                assert_support_layers_respect_height_bounds(print);
            }
        }

        WHEN("support material is generated with a 0.3mm first layer") {
            Print print;
            init_and_process_mesh_print(print, mesh, {
                { "enable_support", 1 },
                { "layer_height", 0.2 },
                { "first_layer_height", 0.3 },
                { "dont_support_bridges", false }
            });

            THEN("support layers honor the first layer height and configured thickness bounds") {
                assert_support_layers_respect_height_bounds(print);
            }
        }

        WHEN("support material is generated with zero top contact distance") {
            Print print;
            init_and_process_mesh_print(print, mesh, {
                { "enable_support", 1 },
                { "layer_height", 0.2 },
                { "first_layer_height", 0.3 },
                { "support_top_z_distance", 0.0 },
                { "dont_support_bridges", false }
            });

            THEN("support generation remains valid for the migrated contact-distance representative") {
                assert_support_layers_respect_height_bounds(print);
            }
        }
    }
}
