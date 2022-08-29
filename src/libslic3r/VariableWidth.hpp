#ifndef slic3r_VariableWidth_hpp_
#define slic3r_VariableWidth_hpp_

#include "Polygon.hpp"
#include "ExtrusionEntity.hpp"
#include "Flow.hpp"

namespace Slic3r {
    ExtrusionPaths thick_polyline_to_extrusion_paths(const ThickPolyline& thick_polyline, ExtrusionRole role, const Flow& flow, const float tolerance, const float merge_tolerance);
    void variable_width(const ThickPolylines& polylines, ExtrusionRole role, const Flow& flow, std::vector<ExtrusionEntity*>& out);
}

#endif
