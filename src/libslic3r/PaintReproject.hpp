#ifndef libslic3r_PaintReproject_hpp_
#define libslic3r_PaintReproject_hpp_

#include <functional>
#include <vector>

#include "Point.hpp"

namespace Slic3r {

class ModelVolume;
class TriangleMesh;
class FacetsAnnotation;

// Progress in [0, 100]; message is a short, non-localized stage description.
using PaintReprojectProgressCallback = std::function<void(int percent, const char *message)>;
// Returns true to request early termination of the (potentially long) reprojection.
using PaintReprojectCancelCallback   = std::function<bool()>;

// ---------------------------------------------------------------------------
// Shared paint (facet-annotation) re-projection.
//
// Both cutting and mesh-repair rebuild a volume's mesh and must carry the
// painted annotations (support/seam/mmu/fuzzy skin) onto the new mesh. Because
// the new mesh may be re-triangulated far more coarsely (or more finely) than
// the paint boundaries, a plain per-face copy loses detail. This module rebuilds
// each annotation on the destination mesh with area-error driven subdivision,
// sampling the source annotation so the paint boundary is preserved regardless
// of the destination tessellation.
//
// Two provenance/sampling strategies are offered:
//   * reproject_paint            -- cutting: an explicit new-face -> source-face
//                                    map is available and the two meshes are
//                                    coplanar per face, so overlaps are measured
//                                    in-plane (exact for planar cut fragments).
//   * reproject_paint_geometric  -- repair/boolean: no face map exists; the
//                                    source and destination meshes share a
//                                    coordinate frame (up to dst_to_src), so
//                                    provenance and sampling use the nearest
//                                    source face / nearest 3D point.
//
// Boolean is not wired up yet, but reproject_paint_geometric is the intended
// extension point: pass the boolean result as the destination mesh and each
// participating operand mesh as a source (nearest-distance source selection or
// a merged source selector can be layered on later).
// ---------------------------------------------------------------------------

// Cutting: provenance from the explicit destination->source face map, measured
// by in-plane coplanar overlap. destination_to_source maps a destination-mesh
// vertex back into the source-mesh coordinate frame.
void reproject_paint(const ModelVolume &src_volume,
                     ModelVolume       &dst_volume,
                     const std::vector<int> &dst_to_src_face,
                     const Transform3d      &destination_to_source);

// Repair/boolean: no face map. Provenance is the nearest source face and paint
// is sampled at the nearest 3D point on the source mesh. dst_to_src maps a
// destination-mesh vertex into the source-mesh coordinate frame (Identity for
// in-place repair, where old/new meshes share the volume-local frame).
//
// Optional progress/cancel callbacks let a caller drive a progress bar and abort
// a long reprojection. Returns false if the operation was canceled (in which case
// the destination annotations are left in an unspecified partial state and the
// caller should discard them); true otherwise.
bool reproject_paint_geometric(const TriangleMesh     &src_mesh,
                               const FacetsAnnotation &src_supported,
                               const FacetsAnnotation &src_seam,
                               const FacetsAnnotation &src_mmu,
                               const FacetsAnnotation &src_fuzzy,
                               const TriangleMesh     &dst_mesh,
                               const Transform3d      &dst_to_src,
                               FacetsAnnotation       &dst_supported,
                               FacetsAnnotation       &dst_seam,
                               FacetsAnnotation       &dst_mmu,
                               FacetsAnnotation       &dst_fuzzy,
                               const PaintReprojectProgressCallback &progress = nullptr,
                               const PaintReprojectCancelCallback   &cancel   = nullptr);

} // namespace Slic3r

#endif // libslic3r_PaintReproject_hpp_
