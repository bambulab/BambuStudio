#include "MeshRaycast.hpp"

// CGAL's Plane_3_Triangle_3_intersection.h calls boost::prior without including
// the header that declares it, so it has to come in before any CGAL header.
#include <boost/next_prior.hpp>

#include <CGAL/AABB_tree.h>
#include <CGAL/AABB_traits.h>
#include <CGAL/AABB_triangle_primitive.h>
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/intersections.h>

#include <algorithm>
#include <boost/variant.hpp>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

namespace Slic3r {

namespace {

using Kernel       = CGAL::Exact_predicates_inexact_constructions_kernel;
using CgalPoint    = Kernel::Point_3;
using CgalSegment  = Kernel::Segment_3;
using CgalTriangle = Kernel::Triangle_3;
using CgalRay      = Kernel::Ray_3;
using Iterator     = std::vector<CgalTriangle>::iterator;
using Primitive    = CGAL::AABB_triangle_primitive<Kernel, Iterator>;
using Traits       = CGAL::AABB_traits<Kernel, Primitive>;
using Tree         = CGAL::AABB_tree<Traits>;
using Intersection = Tree::Intersection_and_primitive_id<CgalRay>::Type;

CgalPoint to_cgal(const Vec3d &p)
{
    return CgalPoint(p.x(), p.y(), p.z());
}

Vec3d from_cgal(const CgalPoint &p)
{
    return Vec3d(CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z()));
}

// igl / Embree convention: P = (1-u-v)*v0 + u*v1 + v*v2
void barycentric_igl(const Vec3d &p, const Vec3d &v0, const Vec3d &v1, const Vec3d &v2, double &u, double &v)
{
    const Vec3d  e0    = v1 - v0;
    const Vec3d  e1    = v2 - v0;
    const Vec3d  e2    = p - v0;
    const double d00   = e0.dot(e0);
    const double d01   = e0.dot(e1);
    const double d11   = e1.dot(e1);
    const double d20   = e2.dot(e0);
    const double d21   = e2.dot(e1);
    const double denom = d00 * d11 - d01 * d01;
    if (std::abs(denom) < 1e-30) {
        u = 0.;
        v = 0.;
        return;
    }
    u = (d11 * d20 - d01 * d21) / denom;
    v = (d00 * d21 - d01 * d20) / denom;
}

enum class HitKind {
    // The variant held neither a point nor a segment.
    None,
    Point,
    // Ray coplanar with the triangle. CGAL yields a segment, which is not a
    // single crossing, so it cannot be turned into a RayMeshHit.
    Coplanar,
};

// Intersect_3 answers a Ray_3 / Triangle_3 query with an optional variant, but
// AABB_traits::Intersection_and_primitive_id strips the optional, so on CGAL 5.4
// this is a bare boost::variant<Point_3, Segment_3>. Handling exactly those two
// alternatives means a CGAL upgrade that changes the shape breaks the build,
// instead of silently degrading into HitKind::None -- a normal branch for the
// caller, so such a regression would go unnoticed.
HitKind classify_intersection(const Intersection::first_type &result, CgalPoint &out)
{
    if (const CgalPoint *p = boost::get<CgalPoint>(&result)) {
        out = *p;
        return HitKind::Point;
    }
    if (boost::get<CgalSegment>(&result) != nullptr)
        return HitKind::Coplanar;
    return HitKind::None;
}

} // namespace

struct TriangleRaycaster::Impl {
    std::vector<CgalTriangle> triangles;
    std::vector<int>          face_ids;
    Tree                      tree;
};

TriangleRaycaster::TriangleRaycaster(const indexed_triangle_set &its)
    : m_impl(std::make_unique<Impl>())
{
    const size_t nfaces = its.indices.size();
    m_impl->triangles.reserve(nfaces);
    m_impl->face_ids.reserve(nfaces);

    for (size_t fi = 0; fi < nfaces; ++fi) {
        const auto &idx = its.indices[fi];
        const auto  v0  = static_cast<size_t>(idx[0]);
        const auto  v1  = static_cast<size_t>(idx[1]);
        const auto  v2  = static_cast<size_t>(idx[2]);
        if (v0 >= its.vertices.size() || v1 >= its.vertices.size() || v2 >= its.vertices.size())
            continue;
        const Vec3d a = its.vertices[v0].cast<double>();
        const Vec3d b = its.vertices[v1].cast<double>();
        const Vec3d c = its.vertices[v2].cast<double>();
        CgalTriangle tri(to_cgal(a), to_cgal(b), to_cgal(c));
        if (tri.is_degenerate())
            continue;
        m_impl->triangles.push_back(tri);
        m_impl->face_ids.push_back(static_cast<int>(fi));
    }

    if (!m_impl->triangles.empty())
        m_impl->tree.rebuild(m_impl->triangles.begin(), m_impl->triangles.end());
}

TriangleRaycaster::~TriangleRaycaster() = default;

std::vector<RayMeshHit> TriangleRaycaster::all_hits(const Vec3d &origin, const Vec3d &dir, int *coplanar_count) const
{
    if (coplanar_count)
        *coplanar_count = 0;

    std::vector<RayMeshHit> hits;
    if (!m_impl || m_impl->triangles.empty() || dir.squaredNorm() == 0.)
        return hits;

    const CgalRay ray(to_cgal(origin), to_cgal(origin + dir));
    std::vector<Intersection> intersections;
    m_impl->tree.all_intersections(ray, std::back_inserter(intersections));

    const double        dir2 = dir.squaredNorm();
    const CgalTriangle *base = m_impl->triangles.data();
    const std::size_t   ntri = m_impl->triangles.size();
    hits.reserve(intersections.size());
    for (const Intersection &item : intersections) {
        CgalPoint     hit_pt;
        const HitKind kind = classify_intersection(item.first, hit_pt);
        if (kind == HitKind::Coplanar && coplanar_count)
            ++*coplanar_count;
        if (kind != HitKind::Point)
            continue;

        // Primitive::Id is an iterator into triangles. Pointer arithmetic
        // avoids mixing iterator / const_iterator (MSVC std::distance).
        const CgalTriangle *ptr = std::addressof(*item.second);
        if (ptr < base || ptr >= base + ntri)
            continue;
        const std::size_t tri_idx = static_cast<std::size_t>(ptr - base);

        const CgalTriangle &tri = m_impl->triangles[tri_idx];
        const Vec3d         p   = from_cgal(hit_pt);
        RayMeshHit          hit;
        hit.face = m_impl->face_ids[tri_idx];
        hit.t    = (p - origin).dot(dir) / dir2;
        barycentric_igl(p, from_cgal(tri.vertex(0)), from_cgal(tri.vertex(1)), from_cgal(tri.vertex(2)), hit.u, hit.v);
        hits.push_back(hit);
    }

    std::sort(hits.begin(), hits.end(), [](const RayMeshHit &a, const RayMeshHit &b) { return a.t < b.t; });
    return hits;
}

} // namespace Slic3r
