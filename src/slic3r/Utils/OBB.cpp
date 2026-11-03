#include "OBB.hpp"

#include <algorithm>
#include <limits>

#include "admesh/stl.h" // indexed_triangle_set
#include "libslic3r/Geometry/ConvexHull.hpp"

namespace Slic3r {
namespace Utils {

std::optional<PlanarOBB> min_area_obb(const std::vector<Vec3d> &points, const Vec3d &plane_normal)
{
    if (points.size() < 2)
        return std::nullopt;

    const double normal_len = plane_normal.norm();
    if (normal_len < EPSILON)
        return std::nullopt;
    const Vec3d normal = plane_normal / normal_len;

    // Orthonormal basis of the plane, so the search can run in 2d. Seeding from the world axis
    // least aligned with the normal keeps the Gram-Schmidt step well conditioned.
    const Vec3d seed = std::abs(normal.x()) < std::abs(normal.y()) ?
        (std::abs(normal.x()) < std::abs(normal.z()) ? Vec3d::UnitX() : Vec3d::UnitZ()) :
        (std::abs(normal.y()) < std::abs(normal.z()) ? Vec3d::UnitY() : Vec3d::UnitZ());
    const Vec3d e0 = (seed - normal * seed.dot(normal)).normalized();
    const Vec3d e1 = normal.cross(e0);

    std::vector<Vec2d> projected;
    projected.reserve(points.size());
    for (const Vec3d &p : points)
        projected.emplace_back(p.dot(e0), p.dot(e1));

    // A minimum area enclosing rectangle always has one side flush with a convex hull edge, and
    // the hull spans the same extents as the full point set. Restricting both the candidate
    // directions and the extent scan to the hull is what keeps this off the O(n^2) path over
    // every input direction.
    const std::vector<Vec2d> hull = Geometry::convex_hull(projected);
    if (hull.size() < 2)
        return std::nullopt;

    double best_area = 0.0;
    Vec2d  best_u{1.0, 0.0};
    double best_u_extent = 0.0;
    double best_v_extent = 0.0;
    bool   found = false;

    for (size_t i = 0; i < hull.size(); ++i) {
        Vec2d u = hull[(i + 1) % hull.size()] - hull[i];
        const double edge_len = u.norm();
        if (edge_len < EPSILON)
            continue;
        u /= edge_len;
        // In this basis normal.cross(u) is exactly the left perpendicular of u.
        const Vec2d v(-u.y(), u.x());

        double min_u = std::numeric_limits<double>::max();
        double max_u = -std::numeric_limits<double>::max();
        double min_v = std::numeric_limits<double>::max();
        double max_v = -std::numeric_limits<double>::max();
        for (const Vec2d &p : hull) {
            const double pu = p.dot(u);
            const double pv = p.dot(v);
            min_u = std::min(min_u, pu);
            max_u = std::max(max_u, pu);
            min_v = std::min(min_v, pv);
            max_v = std::max(max_v, pv);
        }

        const double u_extent = max_u - min_u;
        const double v_extent = max_v - min_v;
        const double area     = u_extent * v_extent;
        if (!found || area + EPSILON < best_area) {
            best_area     = area;
            best_u        = u;
            best_u_extent = u_extent;
            best_v_extent = v_extent;
            found         = true;
        }
    }
    if (!found)
        return std::nullopt;

    const Vec3d u3 = e0 * best_u.x() + e1 * best_u.y();
    const Vec3d v3 = normal.cross(u3);

    PlanarOBB obb;
    if (best_u_extent >= best_v_extent) {
        obb.major_axis   = u3;
        obb.major_extent = best_u_extent;
        obb.minor_extent = best_v_extent;
    } else {
        obb.major_axis   = v3;
        obb.major_extent = best_v_extent;
        obb.minor_extent = best_u_extent;
    }

    // The rectangle is bounded by a pair of parallel line pairs, so +axis and -axis describe the
    // same box and the search above may return either. Pinning the sign here means callers that
    // rotate the result into another space keep a direction that turns with the box instead of
    // snapping by 180 degrees.
    const Vec3d &a = obb.major_axis;
    if (a.z() < -EPSILON ||
        (std::abs(a.z()) <= EPSILON && (a.y() < -EPSILON || (std::abs(a.y()) <= EPSILON && a.x() < 0.0))))
        obb.major_axis = -a;
    obb.minor_axis = normal.cross(obb.major_axis);
    return obb;
}

std::optional<PlanarOBB> min_area_obb(const indexed_triangle_set &its, const std::vector<int> &triangle_indices)
{
    // Adjacent triangles share most of their corners, so gather them by vertex id and read each
    // one once instead of three times per triangle. Summing the raw cross products on the way
    // gives an area weighted plane normal, so a patch that is not perfectly flat is dominated by
    // its largest triangles.
    std::vector<int> vertex_ids;
    vertex_ids.reserve(triangle_indices.size() * 3);
    Vec3d normal = Vec3d::Zero();
    for (int tri_idx : triangle_indices) {
        if (tri_idx < 0 || tri_idx >= (int) its.indices.size())
            continue;
        const auto &tri = its.indices[tri_idx];
        bool usable = true;
        for (int i = 0; i < 3; ++i) {
            const int vi = tri[i];
            if (vi >= 0 && vi < (int) its.vertices.size())
                vertex_ids.emplace_back(vi);
            else
                usable = false;
        }
        if (usable) {
            const Vec3d v0 = its.vertices[tri[0]].cast<double>();
            const Vec3d v1 = its.vertices[tri[1]].cast<double>();
            const Vec3d v2 = its.vertices[tri[2]].cast<double>();
            normal += (v1 - v0).cross(v2 - v0);
        }
    }
    std::sort(vertex_ids.begin(), vertex_ids.end());
    vertex_ids.erase(std::unique(vertex_ids.begin(), vertex_ids.end()), vertex_ids.end());

    std::vector<Vec3d> points;
    points.reserve(vertex_ids.size());
    for (int vi : vertex_ids)
        points.emplace_back(its.vertices[vi].cast<double>());

    return min_area_obb(points, normal);
}

std::optional<PlanarOBB> PlanarOBBCache::get(const indexed_triangle_set &its, const std::vector<int> &triangle_indices)
{
    if (m_valid &&
        m_its == static_cast<const void *>(&its) &&
        m_triangle_indices == static_cast<const void *>(&triangle_indices) &&
        m_index_count == triangle_indices.size())
        return m_result;

    m_result           = min_area_obb(its, triangle_indices);
    m_its              = &its;
    m_triangle_indices = &triangle_indices;
    m_index_count      = triangle_indices.size();
    m_valid            = true;
    return m_result;
}

} // namespace Utils
} // namespace Slic3r
