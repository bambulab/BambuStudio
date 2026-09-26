#include "MeshRaycast.hpp"
#include "AABBTreeIndirect.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

namespace Slic3r {

struct TriangleRaycaster::Impl {
    const indexed_triangle_set *its = nullptr;
    AABBTreeIndirect::Tree3f    tree;
    double                      eps = 0.000001;
};

TriangleRaycaster::TriangleRaycaster(const indexed_triangle_set &its)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->its = &its;
    if (its.indices.empty() || its.vertices.empty())
        return;
    // Float AABBs with eps=0 drop leaves whose box face/edge the ray grazes
    // (axis rays on a silhouette shared edge). Expansion only affects
    // traversal; the triangle test is unchanged. Scale with the mesh AABB
    // so a 2 mm part and a 300 mm part share one policy.
    stl_vertex bmin = its.vertices.front();
    stl_vertex bmax = bmin;
    for (const auto &v : its.vertices) {
        bmin = bmin.cwiseMin(v);
        bmax = bmax.cwiseMax(v);
    }
    const float max_ext   = (bmax - bmin).maxCoeff();
    const float build_eps = std::max(1e-4f * max_ext, 1e-5f);
    m_impl->tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(its.vertices, its.indices, build_eps);
}

TriangleRaycaster::~TriangleRaycaster() = default;

std::vector<RayMeshHit> TriangleRaycaster::all_hits(const Vec3d &origin, const Vec3d &dir, int *coplanar_count) const
{
    if (coplanar_count)
        *coplanar_count = 0;

    std::vector<RayMeshHit> hits;
    if (!m_impl || !m_impl->its || m_impl->tree.empty() || dir.squaredNorm() == 0.)
        return hits;

    // AABBTreeIndirect reports a miss when the ray is coplanar with a triangle
    // (Möller–Trumbore det ~ 0), so coplanar_count stays 0. Odd-hit discard in
    // the watertight test already treats those leaks as invalid rays.
    std::vector<igl::Hit> igl_hits;
    AABBTreeIndirect::intersect_ray_all_hits(m_impl->its->vertices, m_impl->its->indices, m_impl->tree, origin, dir,
                                             igl_hits, m_impl->eps);

    hits.reserve(igl_hits.size());
    for (const igl::Hit &h : igl_hits) {
        RayMeshHit hit;
        hit.face = h.id;
        hit.t    = h.t;
        hit.u    = h.u;
        hit.v    = h.v;
        hits.push_back(hit);
    }
    return hits;
}

} // namespace Slic3r
