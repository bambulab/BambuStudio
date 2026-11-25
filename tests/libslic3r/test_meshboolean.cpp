#include <catch2/catch.hpp>

#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/MeshBoolean.hpp>

using namespace Slic3r;

TEST_CASE("CGAL and TriangleMesh conversions", "[MeshBoolean]") {
    TriangleMesh sphere = make_sphere(1.);
    
    auto cgalmesh_ptr = MeshBoolean::cgal::triangle_mesh_to_cgal(sphere);
    
    REQUIRE(cgalmesh_ptr);
    REQUIRE(! MeshBoolean::cgal::does_self_intersect(*cgalmesh_ptr));
    
    TriangleMesh M = MeshBoolean::cgal::cgal_to_triangle_mesh(*cgalmesh_ptr);
    
    REQUIRE(M.its.vertices.size() == sphere.its.vertices.size());
    REQUIRE(M.its.indices.size() == sphere.its.indices.size());
    
    REQUIRE(M.volume() == Approx(sphere.volume()));
    
    REQUIRE(! MeshBoolean::cgal::does_self_intersect(M));
}

TEST_CASE("Boolean preflight checks only open and non-manifold edges", "[MeshBoolean]") {
    // Construct the triangle sets directly, bypassing any repair at import.
    indexed_triangle_set tetrahedron;
    tetrahedron.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    tetrahedron.indices = {{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {2, 0, 3}};

    SECTION("A closed tetrahedron passes") {
        const TriangleMesh mesh(tetrahedron);
        CHECK_FALSE(MeshBoolean::has_operand_edge_defects(mesh.stats()));
    }
    SECTION("Removing a face leaves open edges") {
        tetrahedron.indices.pop_back();
        const TriangleMesh mesh(tetrahedron);
        REQUIRE(mesh.stats().open_edges == 3);
        REQUIRE(mesh.stats().non_manifold_edges == 0);
        CHECK(MeshBoolean::has_operand_edge_defects(mesh.stats()));
    }
    SECTION("Two closed tetrahedra sharing an edge are non-manifold") {
        tetrahedron.vertices.insert(tetrahedron.vertices.end(), {{0, -1, 0}, {0, 0, -1}});
        tetrahedron.indices.insert(tetrahedron.indices.end(), {{0, 4, 1}, {0, 1, 5}, {1, 4, 5}, {4, 0, 5}});
        const TriangleMesh mesh(tetrahedron);
        REQUIRE(mesh.stats().open_edges == 0);
        REQUIRE(mesh.stats().non_manifold_edges == 1);
        CHECK(MeshBoolean::has_operand_edge_defects(mesh.stats()));
    }
    SECTION("Other mesh diagnostics do not trigger the repair prompt") {
        TriangleMeshStats stats;
        stats.non_manifold_vertices = 1;
        stats.has_reversed_faces = true;
        CHECK_FALSE(MeshBoolean::has_operand_edge_defects(stats));
    }
}
