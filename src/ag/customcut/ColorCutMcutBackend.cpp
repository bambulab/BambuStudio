#include "ColorCutMcutBackend.hpp"

namespace Slic3r {
namespace ColorCut {

GeometryCutOutput ColorCutMcutBackend::cut(const GeometryCutInput &)
{
    GeometryCutOutput output;
    output.success = false;
    output.warnings.push_back({ColorCutWarningCode::Unhandled, "ColorCut MCUT backend bootstrap stub: implementation not yet connected."});
    return output;
}

ColorCutCapabilities ColorCutMcutBackend::capabilities() const
{
    ColorCutCapabilities capabilities;
    capabilities.supports_textured_transfer      = false;
    capabilities.supports_arbitrary_mesh_cut     = true;
    capabilities.supports_cap_surface_provenance = false;
    return capabilities;
}

const char *ColorCutMcutBackend::backend_name() const
{
    return "mcut";
}

} // namespace ColorCut
} // namespace Slic3r
