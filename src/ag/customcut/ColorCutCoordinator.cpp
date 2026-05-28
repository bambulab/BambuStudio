#include "ColorCutCoordinator.hpp"

#include "ColorCutAppearanceSnapshot.hpp"
#include "ColorCutAttributeRepository.hpp"
#include "ColorCutAttributeTransfer.hpp"
#include "ColorCutLegacyPlaneBackend.hpp"
#include "ColorCutMcutBackend.hpp"
#include "ColorCutValidation.hpp"
#include "ColorCutVolumeBuilder.hpp"

#include <algorithm>

namespace Slic3r {
namespace ColorCut {

static const ModelVolume *find_single_source_model_part(const ModelObject &object)
{
    const ModelVolume *source_volume = nullptr;
    for (const ModelVolume *volume : object.volumes) {
        if (!volume->is_model_part() || volume->is_cut_connector())
            continue;

        if (source_volume != nullptr)
            return nullptr;

        source_volume = volume;
    }

    return source_volume;
}

ColorCutCoordinator::ColorCutCoordinator(ColorCutAttributeRepository *repository)
    : m_repository(repository != nullptr ? repository : &global_color_cut_attribute_repository())
{
}

std::optional<ColorCutResult> ColorCutCoordinator::execute(const ColorCutRequest &request) const
{
    if (!Validation::has_supported_request(request))
        return std::nullopt;

    const ModelVolume *source_volume = find_single_source_model_part(*request.object);
    if (source_volume == nullptr)
        return ColorCutResult{false, {}, {{ColorCutWarningCode::UnsupportedAppearanceData, "ColorCut currently handles only single model-part plane cuts; falling back to the existing cut path."}}};

    ColorCutAppearanceSnapshotBuilder snapshot_builder;
    ObjectAppearanceSnapshot          snapshot = snapshot_builder.build(*request.object, m_repository);

    GeometryCutInput input;
    input.source_object  = request.object;
    input.source_volume  = source_volume;
    input.object_index   = request.object_index;
    input.instance_index = request.instance_index;
    input.cut_matrix     = request.cut_matrix;
    input.attributes     = request.attributes;
    input.progress_cb    = request.progress_cb;
    input.cancel_cb      = request.cancel_cb;

    ColorCutLegacyPlaneBackend legacy_backend;
    ColorCutMcutBackend        mcut_backend;
    IColorCutGeometryBackend * backend = static_cast<IColorCutGeometryBackend *>(&legacy_backend);
    if (request.mode == ColorCutMode::Mcut)
        backend = static_cast<IColorCutGeometryBackend *>(&mcut_backend);

    GeometryCutOutput geometry_output = backend->cut(input);

    ColorCutAttributeTransfer transfer;
    transfer.apply(snapshot, geometry_output, request);

    if (!Validation::validate_geometry_output(geometry_output, &geometry_output.warnings))
        return ColorCutResult{false, {}, geometry_output.warnings};

    ColorCutVolumeBuilder volume_builder;
    return volume_builder.build(geometry_output, *request.object, request);
}

} // namespace ColorCut
} // namespace Slic3r
