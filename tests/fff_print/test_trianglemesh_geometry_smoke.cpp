#include <catch2/catch.hpp>

#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"
#include "libslic3r/libslic3r.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace Slic3r;

namespace {

static TriangleMesh geometry_cube()
{
    return make_cube(20.0, 20.0, 20.0);
}

} // namespace

SCENARIO("TriangleMesh geometry smoke covers migrated transform operations", "[TriangleMeshGeometry]") {
    GIVEN("a 20mm cube with one corner on the origin") {
        WHEN("the cube is uniformly scaled") {
            TriangleMesh cube = geometry_cube();
            cube.scale(2.0f);

            THEN("volume follows the scaled dimensions") {
                REQUIRE(std::abs(cube.volume() - 40.0 * 40.0 * 40.0) < 1e-2);
            }
        }

        WHEN("the cube is scaled on the X axis") {
            TriangleMesh cube = geometry_cube();
            cube.scale(Vec3f(2.0f, 1.0f, 1.0f));

            THEN("volume and X size follow the axis scale") {
                REQUIRE(std::abs(cube.volume() - 2.0 * 20.0 * 20.0 * 20.0) < 1e-2);
                REQUIRE(cube.size().x() == Approx(40.0));
            }
        }

        WHEN("the cube is scaled down on the X axis") {
            TriangleMesh cube = geometry_cube();
            cube.scale(Vec3f(0.25f, 1.0f, 1.0f));

            THEN("volume and X size shrink by the same factor") {
                REQUIRE(std::abs(cube.volume() - 0.25 * 20.0 * 20.0 * 20.0) < 1e-2);
                REQUIRE(cube.size().x() == Approx(5.0));
            }
        }

        WHEN("the cube is rotated around Z") {
            TriangleMesh cube = geometry_cube();
            cube.rotate_z(float(PI / 4.0));

            THEN("the bounding-box X size reflects the rotation") {
                REQUIRE(cube.size().x() == Approx(std::sqrt(2.0) * 20.0).margin(1e-2));
            }
        }

        WHEN("the cube is translated with a vector") {
            TriangleMesh cube = geometry_cube();
            cube.translate(Vec3f(5.0f, 10.0f, 0.0f));

            THEN("vertices move by the vector offset") {
                REQUIRE(cube.its.vertices.at(0) == Vec3f(25.0f, 30.0f, 0.0f));
            }
        }

        WHEN("the cube is translated and aligned back to the origin") {
            TriangleMesh cube = geometry_cube();
            cube.translate(5.0f, 10.0f, 0.0f);
            REQUIRE(cube.its.vertices.at(0) == Vec3f(25.0f, 30.0f, 0.0f));

            cube.align_to_origin();

            THEN("the mesh keeps its size and returns to the origin") {
                REQUIRE(cube.bounding_box().min == Vec3d(0.0, 0.0, 0.0));
                REQUIRE(cube.size() == Vec3d(20.0, 20.0, 20.0));
            }
        }
    }
}

SCENARIO("TriangleMesh geometry smoke covers migrated slicing behavior", "[TriangleMeshGeometry]") {
    GIVEN("a 20mm cube") {
        TriangleMesh cube = geometry_cube();

        WHEN("the cube is sliced through representative Z levels") {
            const std::vector<double> z { EPSILON, 2.0, 4.0, 8.0, 6.0, 10.0, 12.0, 14.0, 16.0, 18.0, 20.0 };
            const std::vector<ExPolygons> slices = cube.slice(z);

            THEN("each layer returns one non-empty polygon") {
                REQUIRE(slices.size() == z.size());
                for (const ExPolygons &layer : slices) {
                    REQUIRE(layer.size() == 1);
                    REQUIRE(layer.front().area() > 0.0);
                }
            }
        }

        WHEN("a mirrored cube is sliced below the origin") {
            cube.mirror_z();
            const std::vector<ExPolygons> slices = cube.slice({ -5.0, -10.0 });

            THEN("the mirrored bottom plane is still included") {
                REQUIRE(slices.size() == 2);
                REQUIRE(slices.at(0).at(0).area() > 0.0);
                REQUIRE(slices.at(1).at(0).area() > 0.0);
            }
        }
    }
}

SCENARIO("TriangleMesh geometry smoke covers migrated primitive factories", "[TriangleMeshGeometry]") {
    GIVEN("cube, cylinder, and sphere factories") {
        WHEN("a cube is created") {
            TriangleMesh cube = make_cube(20.0, 20.0, 20.0);

            THEN("its topology and volume match the primitive") {
                REQUIRE(std::count_if(cube.its.vertices.begin(), cube.its.vertices.end(), [](const Vec3f &vertex) {
                    return vertex.x() == 0.0f && vertex.y() == 0.0f && vertex.z() == 0.0f;
                }) == 1);
                REQUIRE(cube.its.indices.size() == 12);
                REQUIRE(std::abs(cube.volume() - 20.0 * 20.0 * 20.0) < 1e-2);
            }
        }

        WHEN("a cylinder is created") {
            TriangleMesh cylinder = make_cylinder(10.0, 10.0, PI / 243.0);
            const double cylinder_angle = 2.0 * PI / std::floor(2.0 * PI / (PI / 243.0));
            const size_t cylinder_segments = static_cast<size_t>(std::llround(2.0 * PI / cylinder_angle));

            THEN("it has the expected axis vertices and approximate volume") {
                REQUIRE(std::count_if(cylinder.its.vertices.begin(), cylinder.its.vertices.end(), [](const Vec3f &vertex) {
                    return vertex.x() == 0.0f && vertex.y() == 0.0f && vertex.z() == 0.0f;
                }) == 1);
                REQUIRE(std::count_if(cylinder.its.vertices.begin(), cylinder.its.vertices.end(), [](const Vec3f &vertex) {
                    return vertex.x() == 0.0f && vertex.y() == 0.0f && vertex.z() == 10.0f;
                }) == 1);
                REQUIRE(cylinder.its.vertices.size() == 2 + cylinder_segments * 2);
                REQUIRE(cylinder.its.indices.size() == cylinder_segments * 4);
                REQUIRE(std::abs(cylinder.volume() - (10.0 * PI * std::pow(10.0, 2))) < 1.0);
            }
        }

        WHEN("a sphere is created") {
            TriangleMesh sphere = make_sphere(10.0, PI / 243.0);

            THEN("it has the expected poles and approximate volume") {
                REQUIRE(std::count_if(sphere.its.vertices.begin(), sphere.its.vertices.end(), [](const Vec3f &vertex) {
                    return is_approx(vertex, Vec3f(0.0f, 0.0f, 10.0f));
                }) == 1);
                REQUIRE(std::count_if(sphere.its.vertices.begin(), sphere.its.vertices.end(), [](const Vec3f &vertex) {
                    return is_approx(vertex, Vec3f(0.0f, 0.0f, -10.0f));
                }) == 1);
                REQUIRE(std::abs(sphere.volume() - (4.0 / 3.0 * PI * std::pow(10.0, 3))) < 1.0);
            }
        }
    }
}

SCENARIO("TriangleMesh geometry smoke covers migrated split, merge, and cut behavior", "[TriangleMeshGeometry]") {
    GIVEN("a cube mesh") {
        WHEN("the mesh is split") {
            TriangleMesh cube = geometry_cube();
            const std::vector<TriangleMesh> meshes = cube.split();

            THEN("one component is returned with matching bounds") {
                REQUIRE(meshes.size() == 1);
                REQUIRE(meshes.front().bounding_box().min == cube.bounding_box().min);
                REQUIRE(meshes.front().bounding_box().max == cube.bounding_box().max);
            }
        }

        WHEN("two cube meshes are merged and split") {
            TriangleMesh cube = geometry_cube();
            TriangleMesh cube2(cube);
            cube.merge(cube2);

            THEN("the merged mesh doubles the facet count and splits into two components") {
                REQUIRE(cube.facets_count() == 2 * cube2.facets_count());
                REQUIRE(cube.split().size() == 2);
            }
        }

        WHEN("the cube is cut at the bottom and center") {
            TriangleMesh cube = geometry_cube();

            indexed_triangle_set upper_bottom;
            indexed_triangle_set lower_bottom;
            cut_mesh(cube.its, 0.0, &upper_bottom, &lower_bottom);
            REQUIRE(upper_bottom.indices.size() == 12);
            REQUIRE(lower_bottom.indices.empty());

            indexed_triangle_set upper_center;
            indexed_triangle_set lower_center;
            cut_mesh(cube.its, 10.0, &upper_center, &lower_center);

            THEN("both center-cut halves contain the expected triangulated faces") {
                REQUIRE(upper_center.indices.size() == 20);
                REQUIRE(lower_center.indices.size() == 20);
            }
        }
    }
}
