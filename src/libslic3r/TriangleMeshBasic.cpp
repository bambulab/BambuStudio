#include "TriangleMesh.hpp"

namespace Slic3r {

static void update_bounding_box_basic(const indexed_triangle_set &its, TriangleMeshStats &out)
{
    if (its.vertices.empty()) {
        out.min  = stl_vertex::Zero();
        out.max  = stl_vertex::Zero();
        out.size = stl_vertex::Zero();
        return;
    }

    Vec3f bmin = its.vertices.front();
    Vec3f bmax = its.vertices.front();
    for (const Vec3f &vertex : its.vertices) {
        bmin = bmin.cwiseMin(vertex);
        bmax = bmax.cwiseMax(vertex);
    }

    out.min  = bmin;
    out.max  = bmax;
    out.size = bmax - bmin;
}

static float its_volume_basic(const indexed_triangle_set &its)
{
    if (its.empty())
        return 0.f;

    const Vec3f p0 = its.vertices.front();
    float       volume = 0.f;
    for (size_t i = 0; i < its.indices.size(); ++i) {
        const its_triangle triangle = its_triangle_vertices(its, i);
        const Vec3f        u        = triangle[1] - triangle[0];
        const Vec3f        v        = triangle[2] - triangle[0];
        const Vec3f        cross    = u.cross(v);
        const float        area     = 0.5f * cross.norm();
        if (area == 0.f)
            continue;
        const Vec3f normal = cross.normalized();
        const float height = normal.dot(triangle[0] - p0);
        volume += (area * height) / 3.f;
    }
    return volume;
}

static void fill_initial_stats_basic(const indexed_triangle_set &its, TriangleMeshStats &out)
{
    out.clear();
    out.number_of_facets = static_cast<uint32_t>(its.indices.size());
    out.number_of_parts  = its.indices.empty() ? 0 : 1;
    out.volume           = its_volume_basic(its);
    update_bounding_box_basic(its, out);
}

TriangleMesh::TriangleMesh(const std::vector<Vec3f> &vertices, const std::vector<Vec3i> &faces) : its{faces, vertices}
{
    fill_initial_stats_basic(this->its, m_stats);
}

TriangleMesh::TriangleMesh(std::vector<Vec3f> &&vertices, const std::vector<Vec3i> &&faces) : its{std::move(faces), std::move(vertices)}
{
    fill_initial_stats_basic(this->its, m_stats);
}

TriangleMesh::TriangleMesh(const indexed_triangle_set &its) : its(its)
{
    fill_initial_stats_basic(this->its, m_stats);
}

TriangleMesh::TriangleMesh(indexed_triangle_set &&its, const RepairedMeshErrors &errors) : its(std::move(its))
{
    m_stats.repaired_errors = errors;
    fill_initial_stats_basic(this->its, m_stats);
}

float TriangleMesh::volume()
{
    return m_stats.volume;
}

void TriangleMesh::translate(const Vec3f &displacement)
{
    if (displacement.isZero())
        return;

    for (stl_vertex &vertex : this->its.vertices)
        vertex += displacement;

    m_stats.min += displacement;
    m_stats.max += displacement;
}

void TriangleMesh::translate(float x, float y, float z)
{
    this->translate(Vec3f(x, y, z));
}

BoundingBoxf3 TriangleMesh::bounding_box() const
{
    if (this->its.vertices.empty())
        return {};

    BoundingBoxf3 bb;
    bb.defined = true;
    bb.min     = m_stats.min.cast<double>();
    bb.max     = m_stats.max.cast<double>();
    return bb;
}

bool TriangleMesh::repaired() const
{
    return m_stats.repaired();
}

bool TriangleMesh::is_splittable() const
{
    return false;
}

size_t TriangleMesh::memsize() const
{
    return sizeof(TriangleMesh) + this->its.vertices.capacity() * sizeof(Vec3f) + this->its.indices.capacity() * sizeof(Vec3i);
}

indexed_triangle_set its_make_cube(double xd, double yd, double zd)
{
    const auto x = float(xd);
    const auto y = float(yd);
    const auto z = float(zd);
    return {
        {{0, 1, 2}, {0, 2, 3}, {4, 5, 6}, {4, 6, 7},
         {0, 4, 7}, {0, 7, 1}, {1, 7, 6}, {1, 6, 2},
         {2, 6, 5}, {2, 5, 3}, {4, 0, 3}, {4, 3, 5}},
        {{x, y, 0}, {x, 0, 0}, {0, 0, 0}, {0, y, 0},
         {x, y, z}, {0, y, z}, {0, 0, z}, {x, 0, z}}
    };
}

} // namespace Slic3r
