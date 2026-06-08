#include <catch2/catch.hpp>

#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"

using namespace Slic3r;

namespace {

void init_cube_print(Print &print, Model &model, const DynamicPrintConfig &config)
{
    TriangleMesh sample_mesh = make_cube(20, 20, 20);

    ModelObject *model_object = model.add_object();
    model_object->name += "object.stl";
    model_object->add_volume(sample_mesh);
    model_object->add_instance();
    model_object->ensure_on_bed();

    print.auto_assign_extruders(model_object);
    print.set_status_silent();
    print.set_no_check_flag(true);
    REQUIRE_NOTHROW(print.apply(model, config));
    REQUIRE_NOTHROW(print.validate());
}

void require_solid_fill_surfaces(const Print &print, size_t object_idx, size_t layer_idx)
{
    const PrintObject &object = *print.objects().at(object_idx);
    REQUIRE(layer_idx < object.layers().size());

    const Layer &layer = *object.get_layer((int)layer_idx);
    bool         saw_fill_surface = false;
    for (const LayerRegion *region : layer.regions()) {
        for (const Surface &surface : region->fill_surfaces.surfaces) {
            saw_fill_surface = true;
            REQUIRE(surface.is_solid());
        }
    }

    REQUIRE(saw_fill_surface);
}

} // namespace

SCENARIO("Print process core smoke path preserves solid surface classification after re-slicing", "[PrintProcessCore]") {
    GIVEN("a 20mm cube with explicit top and bottom solid layers") {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.set_deserialize_strict({
            { "top_shell_layers", 2 },
            { "bottom_shell_layers", 1 },
            { "layer_height", 0.25 },
            { "initial_layer_print_height", 0.25 }
        });

        Model model;
        Print print;
        init_cube_print(print, model, config);

        REQUIRE_NOTHROW(print.process());

        THEN("the initial slice keeps the expected bottom and top solid layers") {
            require_solid_fill_surfaces(print, 0, 0);
            require_solid_fill_surfaces(print, 0, 79);
            require_solid_fill_surfaces(print, 0, 78);
        }

        WHEN("top solid layers are increased and the model is re-applied") {
            config.set("top_shell_layers", 3);

            REQUIRE_NOTHROW(print.apply(model, config));
            REQUIRE_NOTHROW(print.process());

            THEN("the bottom solid layer stays solid while the top gains one more solid layer") {
                require_solid_fill_surfaces(print, 0, 0);
                require_solid_fill_surfaces(print, 0, 79);
                require_solid_fill_surfaces(print, 0, 78);
                require_solid_fill_surfaces(print, 0, 77);
            }
        }
    }
}
