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

} // namespace

SCENARIO("Support material smoke covers migrated raft layer count", "[SupportMaterial]") {
    GIVEN("a 20mm cube with support material and three raft layers") {
        Print print;
        init_and_process_mesh_print(print, make_cube(20, 20, 20), {
            { "support_material", 1 },
            { "raft_layers", 3 }
        });

        THEN("three support layers are created for the raft") {
            REQUIRE(print.objects().front()->support_layers().size() == 3);
        }
    }
}
