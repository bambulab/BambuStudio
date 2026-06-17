#include <catch2/catch.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"

using namespace Slic3r;

SCENARIO("FFF print smoke path keeps the minimal print apply pipeline executable", "[ModelPipeline]") {
    GIVEN("A cube mesh with one prepared model object and instance") {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        Model              model;
        Print              print;
        TriangleMesh       sample_mesh = make_cube(20, 20, 20);

        ModelObject *model_object = model.add_object();
        model_object->name += "object.stl";
        model_object->add_volume(sample_mesh);
        model_object->add_instance();
        model_object->ensure_on_bed();
        print.auto_assign_extruders(model_object);
        print.set_status_silent();

        WHEN("the prepared model is applied to the print backend") {
            REQUIRE_NOTHROW(print.apply(model, config));

            THEN("the print backend validates the minimal pipeline state") {
                REQUIRE_NOTHROW(print.validate());
                REQUIRE(print.objects().size() == 1);
            }
        }
    }
}
