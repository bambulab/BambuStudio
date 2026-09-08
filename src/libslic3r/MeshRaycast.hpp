#ifndef slic3r_MeshRaycast_hpp_
#define slic3r_MeshRaycast_hpp_

#include "Point.hpp"

#include <admesh/stl.h>

#include <memory>
#include <vector>

namespace Slic3r {

struct RayMeshHit {
    int    face = -1;
    double t = 0.;
    double u = 0.;  // P = (1-u-v)*v0 + u*v1 + v*v2
    double v = 0.;
};

class TriangleRaycaster {
    struct Impl;
    std::unique_ptr<Impl> m_impl;
public:
    explicit TriangleRaycaster(const indexed_triangle_set &its);
    ~TriangleRaycaster();
    TriangleRaycaster(const TriangleRaycaster &) = delete;
    TriangleRaycaster &operator=(const TriangleRaycaster &) = delete;
    // Hits are sorted by the ray parameter t. A ray coplanar with a triangle
    // intersects it along a segment rather than at a point, so it yields no hit;
    // pass coplanar_count to learn how often that happened.
    std::vector<RayMeshHit> all_hits(const Vec3d &origin, const Vec3d &dir, int *coplanar_count = nullptr) const;
};

} // namespace Slic3r

#endif
