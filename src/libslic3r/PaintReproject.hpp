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
//   * reproject_paint_from_volumes -- boolean/merge: the destination is rebuilt
//                                    from several operands at once, so it layers
//                                    nearest-distance source selection over
//                                    reproject_paint_geometric (see below).
// ---------------------------------------------------------------------------

// Cutting: provenance from the explicit destination->source face map, measured
// by in-plane coplanar overlap. destination_to_source maps a destination-mesh
// vertex back into the source-mesh coordinate frame.
//
// Optional progress/cancel callbacks behave as in reproject_paint_geometric below:
// returns false if the operation was canceled, in which case the destination
// annotations are left untouched for the caller to discard.
[[nodiscard]] bool reproject_paint(const ModelVolume &src_volume,
                                   ModelVolume       &dst_volume,
                                   const std::vector<int> &dst_to_src_face,
                                   const Transform3d      &destination_to_source,
                                   const PaintReprojectProgressCallback &progress = nullptr,
                                   const PaintReprojectCancelCallback   &cancel   = nullptr);

// Repair/boolean: no face map. Provenance is the nearest source face and paint
// is sampled at the nearest 3D point on the source mesh. dst_to_src maps a
// destination-mesh vertex into the source-mesh coordinate frame (Identity for
// in-place repair, where old/new meshes share the volume-local frame).
//
// Optional progress/cancel callbacks let a caller drive a progress bar and abort
// a long reprojection. Returns false if the operation was canceled (in which case
// the destination annotations are left in an unspecified partial state and the
// caller should discard them); true otherwise.
//
// dst_world_matrix maps the destination mesh into world space. It is only used to keep
// the subdivision floor a real millimeter distance on a scaled volume; pass null when the
// destination mesh is already in millimeters, or to accept mesh-local measurement.
[[nodiscard]] bool reproject_paint_geometric(const TriangleMesh     &src_mesh,
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
                                             const PaintReprojectCancelCallback   &cancel   = nullptr,
                                             const Transform3d      *dst_world_matrix = nullptr);

// One painted operand feeding a boolean result: its mesh, its four annotation
// layers, and the transform that maps this operand's mesh-local coordinates into
// the destination (boolean result) mesh-local frame. Any annotation pointer may be
// null (treated as unpainted for that layer).
struct PaintSourceVolume {
    const TriangleMesh     *mesh                  = nullptr;
    const FacetsAnnotation *supported             = nullptr;
    const FacetsAnnotation *seam                  = nullptr;
    const FacetsAnnotation *mmu                   = nullptr;
    const FacetsAnnotation *fuzzy                 = nullptr;
    Transform3d             source_to_destination = Transform3d::Identity();
    // Filament (1-based) this operand is assigned to. When > 0, the operand's bulk
    // (color-unpainted) faces are baked into the color map as this filament, so a
    // part's SOLID color survives collapse into one volume (a boolean/merge result
    // holds only one base filament). 0 = do not bake (color-paint only).
    int                     base_filament         = 0;
};

// Boolean/merge: the result is rebuilt from several operands and no per-face source map
// exists. Each operand is reprojected on its own through reproject_paint_geometric - one
// source per call, so operands cannot overwrite one another where they overlap - and every
// destination face then takes the paint of the nearest operand that covers it. An operand
// covers a face only when the closest point falls inside one of its own faces, which stops
// paint spreading past a part's outline where two parts share a plane; a tie there goes to
// the smaller part. A face no operand reached, such as surface newly cut by the boolean,
// takes the plain filament of its nearest operand.
//
// progress / cancel / dst_world_matrix behave as in reproject_paint_geometric.
// concatenated_result marks a destination that is exactly the operands concatenated (a
// merge), where the paint is an exact per-face copy rather than a resampling.
[[nodiscard]] bool reproject_paint_from_volumes(const TriangleMesh                   &dst_mesh,
                                                const std::vector<PaintSourceVolume> &sources,
                                                FacetsAnnotation                     &dst_supported,
                                                FacetsAnnotation                     &dst_seam,
                                                FacetsAnnotation                     &dst_mmu,
                                                FacetsAnnotation                     &dst_fuzzy,
                                                const PaintReprojectProgressCallback &progress = nullptr,
                                                const PaintReprojectCancelCallback   &cancel   = nullptr,
                                                const Transform3d                    *dst_world_matrix = nullptr,
                                                bool                                  concatenated_result = false);

} // namespace Slic3r

#endif // libslic3r_PaintReproject_hpp_
