#ifndef slic3r_FillRadialZigZag_hpp_
#define slic3r_FillRadialZigZag_hpp_

#include <string>
#include <vector>

#include "../ExPolygon.hpp"
#include "../Point.hpp"
#include "../Polyline.hpp"
#include "FillBase.hpp"

namespace Slic3r {

namespace FillRadialZigZag {

struct RadialRay
{
    double theta { 0. };
    Point  inner;
    Point  outer;
    Point  mid;
    double inner_radius { 0. };
    double outer_radius { 0. };
    bool   valid { false };
    bool   lip { false }; // one-interval graze too thin to be a scan; keep outer as the hit point
};

struct RadialInfillResult
{
    bool                 success { false };
    Point                pole;
    double               valid_ratio { 0. };
    int                  num_final_rays { 0 };
    int                  n_scan { 0 };
    int                  n_lock { 0 };
    size_t               lock_rows { 0 };
    std::vector<RadialRay> rays;
    Polylines            extrusion_paths;
    std::string          fallback_reason;
};

struct LayerDiag
{
    size_t      layer_id { 0 };
    bool        success { false };
    double      pole_x { 0. };
    double      pole_y { 0. };
    double      lock_x { 0. };
    double      lock_y { 0. };
    double      layer_x { 0. };
    double      layer_y { 0. };
    bool        has_lock { false };
    bool        axis_applied { false };
    bool        pole_inside { false };
    bool        drifted { false };
    int         pole_mode { 0 };
    double      inner_mean { 0. };
    double      outer_mean { 0. };
    double      island_mm { 0. };
    double      valid_ratio { 0. };
    int         num_final_rays { 0 };
    int         n_scan { 0 };
    int         n_lock { 0 };
    int         n_holes { 0 };
    size_t      lock_rows { 0 };
    std::string fallback_reason;
    std::vector<RadialRay> rays;
};

struct PoleVis
{
    size_t             region_id { 0 };
    bool               axis_ok { false };
    bool               bezier_ok { false };
    std::vector<Vec3d> layer_poles;
    std::vector<Vec3d> axis_polyline;
    std::vector<Vec3d> bezier_polyline;
};

void debug_enable(bool on);
void debug_clear();
std::vector<LayerDiag> debug_snapshot();
// Copy fitted poles/axis/Bezier out of the per-region lock table (call before reset_n_lock).
std::vector<PoleVis> snapshot_pole_vis();

// Pin ray count, pole, and zigzag start phase before parallel make_fills.
// n is the even scan count of the largest island in this region; every layer
// of the same region uses that n. n_override > 0 forces that even count
// (at least 4) instead of the density-based estimate.
// Invalid rays keep their slot (no reindex).
// Zig-Zag walk starts at world +X (k=0). Pole holds for ≤1 mm jitter, else
// slews ≤0.5 mm/layer. If print_z is provided (one value per layer), also fit
// a 3D axis (hole/bbox poles) and a quadratic Bezier (bbox centers) for
// ConformalPole::Axis / Bezier. region_id keeps modifier locks separate from
// the parent remainder (default 0 for unit tests).
void reset_n_lock();
void pin_scan_counts(const std::vector<std::vector<ExPolygon>> &islands_by_layer, double spacing_mm,
                     const std::vector<double> &print_z = {}, size_t region_id = 0, int n_override = 0);

// Fill a single already-inset infill island. Returns false to let the caller fall back.
bool fill_region(const ExPolygon &region, const Fill &fill, const FillParams &params, Polylines &out);

// Circle B around the layer pole. conformal_hub_radius: 0 = auto n*1.2*w/(2π),
// >0 = mm override, <0 = off. Returns false if hub is disabled or radius is tiny.
bool make_hub_disk(const ExPolygon &region, const Fill &fill, const FillParams &params,
                   Point &pole, ExPolygon &disk);

} // namespace FillRadialZigZag
} // namespace Slic3r

#endif // slic3r_FillRadialZigZag_hpp_
