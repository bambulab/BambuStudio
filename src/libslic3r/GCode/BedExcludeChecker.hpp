#ifndef slic3r_BedExcludeChecker_hpp_
#define slic3r_BedExcludeChecker_hpp_

#include <vector>

namespace Slic3r {

class Polygon;
struct GCodeProcessorResult;

// Runtime-combined exclude areas used only for toolpath collision checks.
// Callers are responsible for collecting the business-specific areas to inspect
// (for example: bed exclude area + optional clumping detection area).
bool toolpath_intersects_bed_exclude_area_2d(const GCodeProcessorResult& gcode_result, const std::vector<Polygon>& combined_exclude_area_for_toolpath_check);

} // namespace Slic3r

#endif // slic3r_BedExcludeChecker_hpp_
