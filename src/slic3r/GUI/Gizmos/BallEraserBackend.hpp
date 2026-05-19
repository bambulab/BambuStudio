#ifndef slic3r_BallEraserBackend_hpp_
#define slic3r_BallEraserBackend_hpp_

#include "ag/ballerase/BallEraserSession.hpp"
#include "libslic3r/Model.hpp"

#include <string>

namespace Slic3r {
namespace BallEraser {

struct ApplyResult
{
    bool success{false};
    std::string error_message;
    ModelObjectPtrs new_objects;
};

ApplyResult apply_strokes_to_object(
    const ModelObject &source_object,
    int instance_index,
    OutputMode output_mode,
    const std::vector<Stroke> &strokes);

} // namespace BallEraser
} // namespace Slic3r

#endif // slic3r_BallEraserBackend_hpp_
