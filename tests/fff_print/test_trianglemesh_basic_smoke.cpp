#include <catch2/catch.hpp>

#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"

#include <cmath>
#include <vector>

using namespace Slic3r;

SCENARIO("Model assembly smoke path covers migrated TriangleMesh basic statistics", "[ModelAssembly]") {
    GIVEN("a 20mm cube built from explicit vertices and facets") {
        const std::vector<Vec3f> vertices {
            { 20, 20, 0 }, { 20, 0, 0 }, { 0, 0, 0 }, { 0, 20, 0 },
            { 20, 20, 20 }, { 0, 20, 20 }, { 0, 0, 20 }, { 20, 0, 20 }
        };
        const std::vector<Vec3i> facets {
            { 0, 1, 2 }, { 0, 2, 3 }, { 4, 5, 6 }, { 4, 6, 7 },
            { 0, 4, 7 }, { 0, 7, 1 }, { 1, 7, 6 }, { 1, 6, 2 },
            { 2, 6, 5 }, { 2, 5, 3 }, { 4, 0, 3 }, { 4, 3, 5 }
        };

        TriangleMesh cube(vertices, facets);

        THEN("the mesh keeps the expected cube statistics") {
            REQUIRE(std::abs(cube.volume() - 20.0 * 20.0 * 20.0) < 1e-2);
            REQUIRE(cube.facets_count() == facets.size());
            REQUIRE(cube.size() == Vec3d(20.0, 20.0, 20.0));
            REQUIRE(cube.bounding_box().min == Vec3d(0.0, 0.0, 0.0));
            REQUIRE(cube.bounding_box().max == Vec3d(20.0, 20.0, 20.0));
        }

        THEN("the mesh preserves the input vertex and facet arrays") {
            REQUIRE(cube.its.vertices.size() == vertices.size());
            REQUIRE(cube.its.indices.size() == facets.size());

            for (size_t i = 0; i < vertices.size(); ++i)
                REQUIRE(cube.its.vertices.at(i) == vertices.at(i));

            for (size_t i = 0; i < facets.size(); ++i)
                REQUIRE(cube.its.indices.at(i) == facets.at(i));
        }
    }
}
