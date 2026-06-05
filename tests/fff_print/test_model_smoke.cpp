#include <catch2/catch.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Model.hpp"

using namespace Slic3r;

SCENARIO("Model placement smoke path keeps basic instance placement valid", "[ModelPlacement]") {
    GIVEN("A cube mesh with one model object, volume, and instance") {
        Model model;
        TriangleMesh sample_mesh = make_cube(20, 20, 20);
        ModelObject *model_object = model.add_object();
        model_object->add_volume(sample_mesh);
        model_object->add_instance();

        WHEN("ensure_on_bed is applied") {
            THEN("the model instance can still be placed on bed coordinates") {
                REQUIRE_NOTHROW(model_object->ensure_on_bed());
            }
        }
    }
}
