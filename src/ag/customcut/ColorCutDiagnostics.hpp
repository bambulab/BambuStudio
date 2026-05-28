#ifndef slic3r_ColorCutDiagnostics_hpp_
#define slic3r_ColorCutDiagnostics_hpp_

#include "ColorCutGeometryBackend.hpp"

namespace Slic3r {
namespace ColorCut {

struct ColorCutDiagnosticStats {
    size_t inherited_triangles{0};
    size_t split_triangles{0};
    size_t cap_triangles{0};
    size_t transferred_attributes{0};
    size_t defaulted_attributes{0};
};

class ColorCutDiagnostics
{
public:
    static ColorCutDiagnosticStats summarize(const GeometryCutOutput &geometry_output);
};

} // namespace ColorCut
} // namespace Slic3r

#endif
