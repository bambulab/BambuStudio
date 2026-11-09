#ifndef slic3r_Utils_OBB_hpp_
#define slic3r_Utils_OBB_hpp_

#include <optional>
#include <vector>

#include "libslic3r/Point.hpp" // Vec3d

struct indexed_triangle_set;

namespace Slic3r {
namespace Utils {

/// <summary>
/// Minimum area rectangle enclosing a set of coplanar points, expressed in the same space as
/// those points. Both axes are unit vectors lying in the plane; major_axis is the longer side and
/// minor_axis is plane_normal x major_axis. major_axis carries a canonical sign (first non zero
/// of z, y, x is positive), so the same patch always yields the same direction rather than one of
/// the two the rectangle allows.
/// </summary>
struct PlanarOBB
{
    Vec3d  major_axis{Vec3d::UnitX()};
    Vec3d  minor_axis{Vec3d::UnitY()};
    double major_extent{0.0};
    double minor_extent{0.0};
};

/// <summary>
/// Minimum area rectangle enclosing coplanar points.
/// Points off the plane are projected onto it rather than rejected.
/// </summary>
/// <param name="points">Coplanar points.</param>
/// <param name="plane_normal">Normal of their common plane, need not be normalized.</param>
/// <returns>The rectangle, or nothing when the input degenerates to a single point.</returns>
std::optional<PlanarOBB> min_area_obb(const std::vector<Vec3d> &points, const Vec3d &plane_normal);

/// <summary>
/// Minimum area rectangle enclosing the corners of the given triangles, in mesh coordinates.
/// The plane normal is taken from the triangles themselves, area weighted.
///
/// The result belongs to the mesh, not to any placement of it: a rigid transform maps this
/// rectangle onto the minimum area rectangle of the transformed patch. Callers that need world
/// space should rotate the axes rather than pass transformed points in, which keeps the axis
/// turning with the object instead of being re-derived, and re-signed, at every pose.
/// </summary>
/// <param name="its">Mesh owning the triangles.</param>
/// <param name="triangle_indices">Indices into its.indices; out of range entries are skipped.</param>
std::optional<PlanarOBB> min_area_obb(const indexed_triangle_set &its, const std::vector<int> &triangle_indices);

/// <summary>
/// Memo for the mesh flavour of min_area_obb(). That call walks every vertex of the patch, which
/// is too much for callers that run per frame, while its result only changes when the patch does.
/// Holds a single entry, so it fits a caller tracking one patch.
/// </summary>
class PlanarOBBCache
{
public:
    /// Returns the memoized rectangle, recomputing it only when the patch differs from the
    /// previous one. Arguments match min_area_obb(); both references must outlive the call.
    std::optional<PlanarOBB> get(const indexed_triangle_set &its, const std::vector<int> &triangle_indices);

    /// Forget the entry. Call it once the referenced mesh or index vector may be freed, the memo
    /// keys on their addresses.
    void invalidate() { m_valid = false; }

private:
    bool                     m_valid{false};
    const void              *m_its{nullptr};
    const void              *m_triangle_indices{nullptr};
    size_t                   m_index_count{0};
    std::optional<PlanarOBB> m_result;
};

} // namespace Utils
} // namespace Slic3r

#endif // slic3r_Utils_OBB_hpp_
