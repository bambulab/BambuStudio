#include <catch2/catch.hpp>

#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/libslic3r.h"

using namespace Slic3r;

SCENARIO("Print perimeter stage smoke path keeps wall generation executable", "[PrintPerimeters]") {
    GIVEN("A cube mesh prepared through the minimal print pipeline") {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.set_deserialize_strict({
            { "fill_density", 0 },
            { "skirts", 0 },
            { "brim_width", 0 }
        });

        Model        model;
        Print        print;
        TriangleMesh sample_mesh = make_cube(20, 20, 20);

        ModelObject *model_object = model.add_object();
        model_object->name += "object.stl";
        model_object->add_volume(sample_mesh);
        model_object->add_instance();
        model_object->ensure_on_bed();
        print.auto_assign_extruders(model_object);
        print.set_status_silent();
        REQUIRE_NOTHROW(print.apply(model, config));

        WHEN("the print backend runs the perimeter stage") {
            REQUIRE_NOTHROW(print.process_perimeters());

            THEN("the processed object exposes generated perimeter layers") {
                REQUIRE(print.objects().size() == 1);
                const PrintObject &object = *print.objects().front();
                REQUIRE_FALSE(object.layers().empty());
                REQUIRE(object.layers().front()->regions().front()->perimeters.items_count() > 0);
            }
        }
    }
}
