#include <catch2/catch.hpp>

#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"

#include <algorithm>
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

SCENARIO("Model assembly smoke path covers TriangleMesh basic translation state", "[ModelAssembly]") {
    GIVEN("a 20mm cube with one corner on the origin") {
        TriangleMesh cube = make_cube(20.0, 20.0, 20.0);

        WHEN("the cube is translated") {
            cube.translate(5.0, 10.0, 0.0);

            THEN("vertices and bounding box move without changing volume or size") {
                REQUIRE(cube.its.vertices.at(0) == Vec3f(25.0f, 30.0f, 0.0f));
                REQUIRE(cube.bounding_box().min == Vec3d(5.0, 10.0, 0.0));
                REQUIRE(cube.bounding_box().max == Vec3d(25.0, 30.0, 20.0));
                REQUIRE(cube.size() == Vec3d(20.0, 20.0, 20.0));
                REQUIRE(std::abs(cube.volume() - 20.0 * 20.0 * 20.0) < 1e-2);
            }
        }
    }
}

SCENARIO("Model assembly smoke path covers TriangleMesh cube factory basics", "[ModelAssembly]") {
    GIVEN("primitive meshes created by the basic cube factory") {
        WHEN("a cube is created") {
            TriangleMesh cube = make_cube(20.0, 20.0, 20.0);

            THEN("its topology and volume match the expected primitive") {
                REQUIRE(cube.its.vertices.size() == 8);
                REQUIRE(cube.its.indices.size() == 12);
                REQUIRE(std::count_if(cube.its.vertices.begin(), cube.its.vertices.end(), [](const Vec3f &vertex) {
                    return vertex == Vec3f(0.0f, 0.0f, 0.0f);
                }) == 1);
                REQUIRE(cube.bounding_box().min == Vec3d(0.0, 0.0, 0.0));
                REQUIRE(cube.bounding_box().max == Vec3d(20.0, 20.0, 20.0));
                REQUIRE(std::abs(cube.volume() - 20.0 * 20.0 * 20.0) < 1e-2);
            }
        }
    }
}

SCENARIO("Model assembly smoke path documents TriangleMeshBasic split boundary", "[ModelAssembly]") {
    GIVEN("a basic cube mesh in the model basic core target") {
        TriangleMesh cube = make_cube(20.0, 20.0, 20.0);

        THEN("splitting is intentionally outside the basic target boundary") {
            REQUIRE_FALSE(cube.is_splittable());
        }
    }
}
