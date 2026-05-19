#include "ColorCutVolumeBuilder.hpp"

namespace Slic3r {
namespace ColorCut {

ColorCutResult ColorCutVolumeBuilder::build(const GeometryCutOutput &geometry_output) const
{
    ColorCutResult result;
    result.handled = geometry_output.success;
    result.new_objects = geometry_output.new_objects;
    result.warnings = geometry_output.warnings;
    return result;
}

ColorCutResult ColorCutVolumeBuilder::build(
    const GeometryCutOutput &geometry_output,
    const ModelObject &,
    const ColorCutRequest &)
const
{
    return build(geometry_output);
}

} // namespace ColorCut
} // namespace Slic3r
