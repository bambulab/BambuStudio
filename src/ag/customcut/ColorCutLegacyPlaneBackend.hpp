#ifndef slic3r_ColorCutLegacyPlaneBackend_hpp_
#define slic3r_ColorCutLegacyPlaneBackend_hpp_

#include "ColorCutGeometryBackend.hpp"

namespace Slic3r {
namespace ColorCut {

class ColorCutLegacyPlaneBackend : public IColorCutGeometryBackend
{
public:
    GeometryCutOutput cut(const GeometryCutInput &input) override;
    ColorCutCapabilities capabilities() const override;
    const char *backend_name() const override;
};

} // namespace ColorCut
} // namespace Slic3r

#endif
