#include "TriangleMesh.hpp"

namespace Slic3r {

static void update_bounding_box_transform_only(const indexed_triangle_set &its, TriangleMeshStats &out)
{
    if (its.vertices.empty()) {
        out.min = out.max = Vec3f::Zero();
        return;
    }

    out.min = out.max = its.vertices.front();
    for (const stl_vertex &v : its.vertices) {
        out.min = out.min.cwiseMin(v);
        out.max = out.max.cwiseMax(v);
    }
}

static inline void flip_its_triangles(indexed_triangle_set &its)
{
    for (stl_triangle_vertex_indices &idx : its.indices)
        std::swap(idx[0], idx[1]);
}

void TriangleMesh::transform(const Transform3d &t, bool fix_left_handed)
{
    for (stl_vertex &v : this->its.vertices)
        v = (t * v.cast<double>()).cast<float>().eval();
    double det = t.matrix().block(0, 0, 3, 3).determinant();
    if (fix_left_handed && det < 0.) {
        flip_its_triangles(this->its);
        det = -det;
    }
    m_stats.volume *= det;
    update_bounding_box_transform_only(this->its, this->m_stats);
}

void TriangleMesh::transform(const Matrix3d &m, bool fix_left_handed)
{
    for (stl_vertex &v : this->its.vertices)
        v = (m * v.cast<double>()).cast<float>().eval();
    double det = m.block(0, 0, 3, 3).determinant();
    if (fix_left_handed && det < 0.) {
        flip_its_triangles(this->its);
        det = -det;
    }
    m_stats.volume *= det;
    update_bounding_box_transform_only(this->its, this->m_stats);
}

BoundingBoxf3 TriangleMesh::transformed_bounding_box(const Transform3d &trafo) const
{
    BoundingBoxf3 bbox;
    for (const stl_vertex &v : this->its.vertices)
        bbox.merge(trafo * v.cast<double>());
    return bbox;
}

BoundingBoxf3 TriangleMesh::transformed_bounding_box(const Transform3d &trafod, double world_min_z) const
{
    std::vector<char>           sides;
    size_t                      num_above = 0;
    Eigen::AlignedBox<float, 3> bbox;
    Transform3f                 trafo = trafod.cast<float>();
    sides.reserve(its.vertices.size());
    for (const stl_vertex &v : this->its.vertices) {
        const stl_vertex pt   = trafo * v;
        const int        sign = pt.z() > world_min_z ? 1 : pt.z() < world_min_z ? -1 : 0;
        sides.emplace_back(sign);
        if (sign >= 0) {
            ++num_above;
            bbox.extend(pt);
        }
    }

    if (num_above < its.vertices.size()) {
        for (const stl_triangle_vertex_indices &tri : its.indices) {
            const int s[3] = {sides[tri(0)], sides[tri(1)], sides[tri(2)]};
            if (std::min(s[0], std::min(s[1], s[2])) < 0 && std::max(s[0], std::max(s[1], s[2])) > 0) {
                int iprev = 2;
                for (int iedge = 0; iedge < 3; ++iedge) {
                    if (s[iprev] * s[iedge] == -1) {
                        const stl_vertex p1 = trafo * its.vertices[tri(iprev)];
                        const stl_vertex p2 = trafo * its.vertices[tri(iedge)];
                        const float      t  = (world_min_z - p1.z()) / (p2.z() - p1.z());
                        bbox.extend(Vec3f(p1.x() + (p2.x() - p1.x()) * t, p1.y() + (p2.y() - p1.y()) * t, world_min_z));
                    }
                    iprev = iedge;
                }
            }
        }
    }

    BoundingBoxf3 out;
    if (!bbox.isEmpty()) {
        out.defined = true;
        out.min     = bbox.min().cast<double>();
        out.max     = bbox.max().cast<double>();
    }
    return out;
}

} // namespace Slic3r
