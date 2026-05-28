#include "ColorCutValidation.hpp"

namespace Slic3r {
namespace ColorCut {
namespace Validation {

bool has_supported_request(const ColorCutRequest &request)
{
    return request.object != nullptr;
}

bool validate_geometry_output(const GeometryCutOutput &output, std::vector<ColorCutWarning> *warnings)
{
    const bool valid = output.success || !output.new_objects.empty() || !output.volumes.empty();
    if (!valid && warnings != nullptr) {
        warnings->push_back({
            ColorCutWarningCode::Unhandled,
            "ColorCut validation bootstrap stub: geometry output is incomplete."
        });
    }
    return valid;
}

} // namespace Validation
} // namespace ColorCut
} // namespace Slic3r
