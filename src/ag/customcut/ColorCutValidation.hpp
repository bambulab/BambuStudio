#ifndef slic3r_ColorCutValidation_hpp_
#define slic3r_ColorCutValidation_hpp_

#include "ColorCutGeometryBackend.hpp"
#include "ColorCutTypes.hpp"

#include <vector>

namespace Slic3r {
namespace ColorCut {
namespace Validation {

bool has_supported_request(const ColorCutRequest &request);
bool validate_geometry_output(const GeometryCutOutput &output, std::vector<ColorCutWarning> *warnings = nullptr);

} // namespace Validation
} // namespace ColorCut
} // namespace Slic3r

#endif
