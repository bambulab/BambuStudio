#ifndef slic3r_FillConformal_hpp_
#define slic3r_FillConformal_hpp_

#include "../ExPolygon.hpp"
#include "../Polyline.hpp"
#include "FillBase.hpp"
#include "FillConformalChart.hpp"

namespace Slic3r {

class Surface;

namespace FillConformal {

// Fill via equal-angle radial ZigZag around a locked pole.
// Older polar / medial-axis / chart paths are kept in this file but not called.
// Returns false if the island cannot be filled conformally; caller falls back to world-axis scan.
bool fill_surface(const Fill &fill, const Surface *surface, const FillParams &params, Polylines &out);

} // namespace FillConformal
} // namespace Slic3r

#endif // slic3r_FillConformal_hpp_
