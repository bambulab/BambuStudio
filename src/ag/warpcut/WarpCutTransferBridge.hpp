#ifndef slic3r_WarpCutTransferBridge_hpp_
#define slic3r_WarpCutTransferBridge_hpp_

#include "WarpCutTypes.hpp"

#include "ag/customcut/ColorCutAppearanceTypes.hpp"
#include "libslic3r/Model.hpp"

namespace Slic3r {
namespace WarpCut {
namespace TransferBridge {

void reapply_from_single_source(
    const ColorCut::ObjectAppearanceSnapshot &source_appearance,
    const ModelObject &source_object,
    const ModelVolume &source_volume,
    int instance_index,
    const ModelObjectPtrs &target_objects,
    bool uniform_cap_color,
    const SurfaceDefinition &surface);

} // namespace TransferBridge
} // namespace WarpCut
} // namespace Slic3r

#endif // slic3r_WarpCutTransferBridge_hpp_
