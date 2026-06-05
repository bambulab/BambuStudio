#include <catch2/catch.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Model.hpp"

using namespace Slic3r;

SCENARIO("Model assembly smoke path covers object assembly and mesh preservation", "[ModelAssembly]") {
    GIVEN("A cube mesh") {
        Model model;
        TriangleMesh sample_mesh = make_cube(20, 20, 20);

        WHEN("A model object, volume, and instance are added") {
            ModelObject *model_object = model.add_object();
            model_object->add_volume(sample_mesh);
            model_object->add_instance();

            THEN("the basic model structure is populated") {
                REQUIRE(model.objects.size() == 1);
                REQUIRE(model_object->volumes.size() == 1);
                REQUIRE(model_object->instances.size() == 1);
                REQUIRE(model_object->volumes.front()->is_model_part());
            }

            AND_THEN("the stored mesh remains equivalent to the input mesh") {
                REQUIRE_FALSE(sample_mesh.its.vertices.empty());
                const std::vector<Vec3f> &mesh_vertices = model_object->volumes.front()->mesh().its.vertices;
                Vec3f mesh_offset = model_object->volumes.front()->source.mesh_offset.cast<float>();

                REQUIRE(mesh_vertices.size() == sample_mesh.its.vertices.size());
                for (size_t i = 0; i < sample_mesh.its.vertices.size(); ++i) {
                    const Vec3f &expected = sample_mesh.its.vertices[i];
                    const Vec3f  actual   = mesh_vertices[i] + mesh_offset;
                    REQUIRE((actual - expected).norm() < EPSILON);
                }
            }
        }
    }
}
