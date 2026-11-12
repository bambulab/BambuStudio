#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <catch2/catch.hpp>

#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"

using namespace Slic3r;

namespace {

struct CutTopologyStats {
    size_t open_edges { 0 };
    size_t non_manifold_edges { 0 };
    size_t same_direction_edges { 0 };
};

CutTopologyStats topology_stats(const indexed_triangle_set &mesh)
{
    struct Edge {
        int low;
        int high;
        int orientation;

        bool operator<(const Edge &rhs) const
        {
            return low < rhs.low || (low == rhs.low && high < rhs.high);
        }
    };

    std::vector<Edge> edges;
    edges.reserve(mesh.indices.size() * 3);
    for (const Vec3i &face : mesh.indices)
        for (int edge_idx = 0; edge_idx < 3; ++edge_idx) {
            const int from = face(edge_idx);
            const int to   = face((edge_idx + 1) % 3);
            edges.push_back({std::min(from, to), std::max(from, to), from < to ? 1 : -1});
        }
    std::sort(edges.begin(), edges.end());

    CutTopologyStats stats;
    for (size_t first = 0; first < edges.size();) {
        size_t last = first + 1;
        int orientation = edges[first].orientation;
        while (last < edges.size() &&
               edges[last].low == edges[first].low && edges[last].high == edges[first].high) {
            orientation += edges[last].orientation;
            ++last;
        }
        const size_t count = last - first;
        if (count == 1)
            ++stats.open_edges;
        else if (count > 2)
            ++stats.non_manifold_edges;
        else if (orientation != 0)
            ++stats.same_direction_edges;
        first = last;
    }
    return stats;
}

CutTopologyStats operator+(const CutTopologyStats &lhs, const CutTopologyStats &rhs)
{
    return {
        lhs.open_edges + rhs.open_edges,
        lhs.non_manifold_edges + rhs.non_manifold_edges,
        lhs.same_direction_edges + rhs.same_direction_edges
    };
}

void require_not_worse(const CutTopologyStats &result, const CutTopologyStats &baseline)
{
    REQUIRE(result.open_edges <= baseline.open_edges);
    REQUIRE(result.non_manifold_edges <= baseline.non_manifold_edges);
    REQUIRE(result.same_direction_edges <= baseline.same_direction_edges);
}

bool vertex_on_cut_plane(const Vec3f &v, float cut_z)
{
    return std::abs(v.z() - cut_z) <= 1e-4f;
}

// Two cut-plane vertex indices at the same XY (within snap) should not exist after edge_id reuse + welding.
size_t count_unmerged_cut_plane_xy_duplicates(const indexed_triangle_set &mesh, float cut_z)
{
    const float snap2 = 1e-8f;
    size_t      pairs = 0;
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        if (!vertex_on_cut_plane(mesh.vertices[i], cut_z))
            continue;
        const Vec2f pi = to_2d(mesh.vertices[i]);
        for (size_t j = i + 1; j < mesh.vertices.size(); ++j) {
            if (!vertex_on_cut_plane(mesh.vertices[j], cut_z))
                continue;
            if ((pi - to_2d(mesh.vertices[j])).squaredNorm() <= snap2)
                ++pairs;
        }
    }
    return pairs;
}

} // namespace

TEST_CASE("Cut mesh closes intersections just outside the planar epsilon", "[TriangleMeshSlicer][cut]")
{
    indexed_triangle_set mesh = its_make_cube(20., 20., 20.);
    for (Vec3f &vertex : mesh.vertices)
        if (vertex.z() > 0.f)
            vertex.x() += 20.f;

    indexed_triangle_set upper;
    indexed_triangle_set lower;
    cut_mesh(mesh, std::nextafter(0.001f, std::numeric_limits<float>::infinity()), &upper, &lower);

    require_not_worse(topology_stats(upper) + topology_stats(lower), {});
}

TEST_CASE("Cut mesh does not amplify invalid source topology", "[TriangleMeshSlicer][cut]")
{
    indexed_triangle_set mesh = its_make_cube(20., 20., 20.);
    mesh.indices.emplace_back(mesh.indices[2]);
    const CutTopologyStats input_stats = topology_stats(mesh);

    indexed_triangle_set upper;
    indexed_triangle_set lower;
    cut_mesh(mesh, 10.f, &upper, &lower);

    require_not_worse(topology_stats(upper) + topology_stats(lower), input_stats);
}

TEST_CASE("Cut mesh conserves volume and closes both halves", "[TriangleMeshSlicer][cut]")
{
    const indexed_triangle_set mesh = its_make_cube(20., 20., 20.);
    indexed_triangle_set upper;
    indexed_triangle_set lower;
    cut_mesh(mesh, 7.f, &upper, &lower);

    CHECK(its_volume(upper) == Approx(13.f * 400.f).margin(1e-2));
    CHECK(its_volume(lower) == Approx(7.f * 400.f).margin(1e-2));
    require_not_worse(topology_stats(upper) + topology_stats(lower), {});
}

TEST_CASE("Cut mesh keeps source face indices aligned with the output faces", "[TriangleMeshSlicer][cut]")
{
    const indexed_triangle_set mesh = its_make_cube(20., 20., 20.);
    indexed_triangle_set upper;
    indexed_triangle_set lower;
    std::vector<int>     upper_src;
    std::vector<int>     lower_src;
    cut_mesh(mesh, 7.f, &upper, &lower, true, &upper_src, &lower_src);

    REQUIRE(upper_src.size() == upper.indices.size());
    REQUIRE(lower_src.size() == lower.indices.size());
    for (int src : upper_src)
        CHECK(src < int(mesh.indices.size()));
    for (int src : lower_src)
        CHECK(src < int(mesh.indices.size()));
}

TEST_CASE("Cut mesh without caps leaves the section open", "[TriangleMeshSlicer][cut]")
{
    const indexed_triangle_set mesh = its_make_cube(20., 20., 20.);
    indexed_triangle_set upper_open;
    indexed_triangle_set upper_closed;
    cut_mesh(mesh, 7.f, &upper_open, nullptr, false);
    cut_mesh(mesh, 7.f, &upper_closed, nullptr, true);

    CHECK(topology_stats(upper_open).open_edges > 0);
    CHECK(topology_stats(upper_closed).open_edges == 0);
    CHECK(upper_closed.indices.size() > upper_open.indices.size());
}

TEST_CASE("Cut mesh shares cut vertices on short coplanar edges", "[TriangleMeshSlicer][cut]")
{
    // Closed slender prism: ~1 mm along X, tall along Z ([-1, +1]). Cut at z=0 splits side faces so
    // cut_mesh hits new_vertex(), edge_id caching, and near-endpoint shell reuse (TriangleMeshSlicer ~3654).
    constexpr float short_axis = 0.001f;
    constexpr float cut_z      = 0.f;
    indexed_triangle_set mesh = its_make_cube(short_axis, 20.f, 2.f);
    for (Vec3f &vertex : mesh.vertices)
        vertex.z() -= 1.f;
    REQUIRE(topology_stats(mesh).open_edges == 0);

    indexed_triangle_set upper;
    indexed_triangle_set lower;
    cut_mesh(mesh, cut_z, &upper, &lower);

    REQUIRE(! upper.indices.empty());
    REQUIRE(! lower.indices.empty());
    REQUIRE(upper.indices.size() + lower.indices.size() > mesh.indices.size());

    const float expected_volume = short_axis * 20.f * 2.f;
    CHECK(its_volume(upper) + its_volume(lower) == Approx(expected_volume).margin(1e-4f));

    require_not_worse(topology_stats(upper) + topology_stats(lower), {});
    REQUIRE(topology_stats(upper).open_edges == 0);
    REQUIRE(topology_stats(lower).open_edges == 0);
    CHECK(count_unmerged_cut_plane_xy_duplicates(upper, cut_z) == 0);
    CHECK(count_unmerged_cut_plane_xy_duplicates(lower, cut_z) == 0);
}

TEST_CASE("Cut mesh returns promptly when cancelled", "[TriangleMeshSlicer][cut]")
{
    const indexed_triangle_set mesh = its_make_cube(20., 20., 20.);
    indexed_triangle_set upper;
    indexed_triangle_set lower;
    // Only checks that a cancelled cut terminates cleanly; the result is to be discarded.
    cut_mesh(mesh, 7.f, &upper, &lower, true, nullptr, nullptr, nullptr, [] { return true; });
    SUCCEED();
}
