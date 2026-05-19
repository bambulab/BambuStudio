#ifndef slic3r_WarpCutSurfaceClassifier_hpp_
#define slic3r_WarpCutSurfaceClassifier_hpp_

#include "WarpCutTypes.hpp"

#include "ag/customcut/ColorCutTypes.hpp"

namespace Slic3r {
namespace WarpCut {

bool triangle_on_cut_surface(
    const SurfaceDefinition &surface,
    const Vec3d &instance_offset,
    const Vec3d &centroid,
    const Vec3d &normal);

ColorCut::CutSurfaceClassifier make_surface_classifier(
    const SurfaceDefinition &surface,
    const Vec3d &instance_offset);

} // namespace WarpCut
} // namespace Slic3r

#endif // slic3r_WarpCutSurfaceClassifier_hpp_
