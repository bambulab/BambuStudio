#include "WarpCutTransferBridge.hpp"

#include "WarpCutSurfaceClassifier.hpp"

#include "ag/customcut/ColorCutAttributeTransfer.hpp"

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
    const SurfaceDefinition &surface)
{
    Vec3d instance_offset = Vec3d::Zero();
    if (instance_index >= 0 && instance_index < static_cast<int>(source_object.instances.size()))
        instance_offset = source_object.instances[static_cast<size_t>(instance_index)]->get_offset();

    ColorCut::ColorCutAttributeTransfer transfer;
    transfer.reapply_from_single_source(
        source_appearance,
        source_object,
        source_volume,
        instance_index,
        target_objects,
        uniform_cap_color,
        make_surface_classifier(surface, instance_offset));
}

} // namespace TransferBridge
} // namespace WarpCut
} // namespace Slic3r
