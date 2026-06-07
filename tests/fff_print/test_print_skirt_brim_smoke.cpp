#include <catch2/catch.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/TriangleMesh.hpp"

using namespace Slic3r;

static size_t brim_items_count(Slic3r::Print &print)
{
    size_t count = 0;
    for (auto &[object_id, brim] : print.get_brimMap())
        count += brim.items_count();
    return count;
}

static void init_and_process_cube_print(Slic3r::Print &print, std::initializer_list<Slic3r::ConfigBase::SetDeserializeItem> config_items)
{
    Slic3r::DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict(config_items);

    Slic3r::Model        model;
    Slic3r::TriangleMesh sample_mesh = Slic3r::make_cube(20, 20, 20);

    Slic3r::ModelObject *model_object = model.add_object();
    model_object->name += "object.stl";
    model_object->add_volume(sample_mesh);
    model_object->add_instance();
    model_object->ensure_on_bed();

    print.auto_assign_extruders(model_object);
    print.set_status_silent();
    REQUIRE_NOTHROW(print.apply(model, config));
    REQUIRE_NOTHROW(print.validate());
    REQUIRE_NOTHROW(print.process());
}

SCENARIO("Print skirt and brim smoke path covers migrated legacy adhesion scenarios", "[PrintSkirtBrim]") {
    GIVEN("a 20mm cube and the legacy skirt configuration") {
        Slic3r::Print print;
        init_and_process_cube_print(print, {
            { "skirt_height", 1 },
            { "skirt_distance", 1 },
            { "skirt_loops", 2 }
        });

        THEN("the skirt extrusion collection has two loops") {
            REQUIRE(print.skirt().items_count() == 2);
            REQUIRE(print.skirt().flatten().entities.size() == 2);
        }
    }

    GIVEN("a 20mm cube and legacy skirt/brim edge configurations") {
        WHEN("the brim width is larger than the skirt area") {
            Slic3r::Print print;
            init_and_process_cube_print(print, {
                { "skirt_height", 1 },
                { "skirt_loops", 1 },
                { "brim_type", "outer_only" },
                { "brim_width", 10 }
            });

            THEN("the print process keeps both adhesion features executable") {
                REQUIRE(print.skirt().items_count() == 1);
                REQUIRE(brim_items_count(print) > 0);
            }
        }

        WHEN("skirt height is disabled while skirt loops remain configured") {
            Slic3r::Print print;
            init_and_process_cube_print(print, {
                { "skirt_height", 0 },
                { "skirt_loops", 2 }
            });

            THEN("the print process completes without generating skirt loops") {
                REQUIRE(print.skirt().empty());
            }
        }
    }

    GIVEN("a 20mm cube and the legacy brim configuration") {
        WHEN("brim width is 3mm with 1mm initial-layer line width") {
            Slic3r::Print print;
            init_and_process_cube_print(print, {
                { "initial_layer_line_width", 1 },
                { "brim_type", "outer_only" },
                { "brim_width", 3 }
            });

            THEN("the brim extrusion collection has one current outer-only path") {
                REQUIRE(brim_items_count(print) == 1);
            }
        }

        WHEN("brim width is 6mm with 1mm initial-layer line width") {
            Slic3r::Print print;
            init_and_process_cube_print(print, {
                { "initial_layer_line_width", 1 },
                { "brim_type", "outer_only" },
                { "brim_width", 6 }
            });

            THEN("the brim extrusion collection grows with configured brim width") {
                REQUIRE(brim_items_count(print) == 3);
            }
        }

        WHEN("brim width is 6mm with 0.5mm initial-layer line width") {
            Slic3r::Print print;
            init_and_process_cube_print(print, {
                { "initial_layer_line_width", 0.5 },
                { "brim_type", "outer_only" },
                { "brim_width", 6 }
            });

            THEN("the brim extrusion collection grows again when the initial line width narrows") {
                REQUIRE(brim_items_count(print) == 6);
            }
        }
    }
}
