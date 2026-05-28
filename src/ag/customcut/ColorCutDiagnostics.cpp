#include "ColorCutDiagnostics.hpp"

namespace Slic3r {
namespace ColorCut {

ColorCutDiagnosticStats ColorCutDiagnostics::summarize(const GeometryCutOutput &geometry_output)
{
    ColorCutDiagnosticStats stats;
    for (const GeometryCutOutputVolume &volume : geometry_output.volumes) {
        for (const OutputTriangleProvenance &triangle : volume.provenance.triangles) {
            switch (triangle.kind) {
            case ProvenanceTriangleKind::Inherited: ++stats.inherited_triangles; break;
            case ProvenanceTriangleKind::Split:     ++stats.split_triangles; break;
            case ProvenanceTriangleKind::Cap:       ++stats.cap_triangles; break;
            default: break;
            }
        }
    }
    return stats;
}

} // namespace ColorCut
} // namespace Slic3r
