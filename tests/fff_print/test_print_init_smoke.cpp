#include <catch2/catch.hpp>

#include "libslic3r/Config.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/TriangleMesh.hpp"

using namespace Slic3r;

SCENARIO("Print process core smoke path covers legacy single-mesh print initialization", "[PrintProcessCore]") {
    GIVEN("a default print config and a 20mm cube mesh") {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        Model              model;
        Print              print;
        TriangleMesh       sample_mesh = make_cube(20, 20, 20);

        WHEN("the mesh is added as one model object and applied to a print") {
            ModelObject *model_object = model.add_object();
            model_object->name += "object.stl";
            model_object->add_volume(sample_mesh);
            model_object->add_instance();
            model_object->ensure_on_bed();

            print.auto_assign_extruders(model_object);
            print.set_status_silent();

            THEN("the print owns one valid print object without needing full slicing or G-code export") {
                REQUIRE_NOTHROW(print.apply(model, config));
                REQUIRE_NOTHROW(print.validate());
                REQUIRE(print.objects().size() == 1);
            }
        }
    }
}
