#include "WarpCutCoordinator.hpp"

#include "WarpCutGeometryBackend.hpp"

#include "ag/customcut/ColorCutValidation.hpp"
#include "ag/customcut/ColorCutVolumeBuilder.hpp"

namespace Slic3r {
namespace WarpCut {

namespace {

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

} // namespace

std::optional<ColorCut::ColorCutResult> WarpCutCoordinator::execute(const Request &request) const
{
    if (request.object == nullptr)
        return std::nullopt;

    const ModelVolume *source_volume = find_single_source_model_part(*request.object);
    if (source_volume == nullptr) {
        return ColorCut::ColorCutResult{
            false,
            {},
            {{ColorCut::ColorCutWarningCode::UnsupportedAppearanceData, "WarpCut currently handles only single model-part cuts."}}
        };
    }

    GeometryInput input;
    input.source_object = request.object;
    input.source_volume = source_volume;
    input.object_index = request.object_index;
    input.instance_index = request.instance_index;
    input.cut_matrix = request.cut_matrix;
    input.attributes = request.attributes;
    input.surface = request.surface;
    input.progress_cb = request.progress_cb;
    input.cancel_cb = request.cancel_cb;

    WarpCutGeometryBackend backend;
    ColorCut::GeometryCutOutput geometry_output = backend.cut(input);

    if (!ColorCut::Validation::validate_geometry_output(geometry_output, &geometry_output.warnings))
        return ColorCut::ColorCutResult{false, {}, geometry_output.warnings};

    ColorCut::ColorCutVolumeBuilder volume_builder;
    return volume_builder.build(geometry_output);
}

} // namespace WarpCut
} // namespace Slic3r
