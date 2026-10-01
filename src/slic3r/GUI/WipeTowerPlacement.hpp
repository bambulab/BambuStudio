#ifndef slic3r_GUI_WipeTowerPlacement_hpp_
#define slic3r_GUI_WipeTowerPlacement_hpp_

// Shared wipe tower placement helpers, used by both Plater.cpp (preset switch)
// and Jobs/ArrangeJob.cpp (auto-arrange).

#include <vector>

#include "libslic3r/Arrange.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"

namespace Slic3r {
class DynamicPrintConfig;
class Model;
}

namespace Slic3r { namespace GUI {

class PartPlate;
class PartPlateList;

// Preferences > General(User) > "auto_optimize_wipe_tower_placement" (default true).
bool wipe_tower_optimal_pos_enabled();

// Matches MIN_SEPARATION / exclusion_gap in Arrange.cpp.
static constexpr double WIPE_TOWER_ARRANGE_GAP = 1.0;

// Extra clearance added on top of the nest-matching hug gap for "optimal position" placement.
static constexpr double WIPE_TOWER_OPTIMAL_POS_EXTRA_CLEARANCE = 10.0;

double wipe_tower_side_gap(const arrangement::ArrangeParams &params);

// Negative prime_tower_brim_width means "auto" (WipeTower::get_auto_brim_by_height).
double wipe_tower_brim_width(const DynamicPrintConfig &config, double tower_height);

double wipe_tower_line_width(const DynamicPrintConfig &config);

// Nest obstacle footprint in the tower's own local frame (min corner near the origin).
BoundingBoxf wipe_tower_nest_footprint(double wall_w, double wall_h, double brim_width);

// line_width is unused; kept so existing call sites don't need to change.
double wipe_tower_brim_footprint_expand(double brim_width, double line_width);

double wipe_tower_hug_clearance_matching_nest(const DynamicPrintConfig &config, double tower_height,
                                              const arrangement::ArrangeParams &params);

// Slides the tower along the ray from the parts' hull centroid toward the printer's
// default tower direction, stopping where it clears the inflated hull. No forbidden-region
// check -- see avoid_forbidden_regions_delta(). On success writes the min corner to out_wt_min.
bool compute_optimal_wipe_tower_min(const Polygons &inflated_parts,
                                    const Vec2d    &plate_center,
                                    const Vec2d    &default_tower_center,
                                    const Vec2d    &tower_size,
                                    double          tower_clearance,
                                    Vec2d          &out_wt_min);

// Whole-assembly axis-aligned translate to move all part bboxes + tower bbox off the
// forbidden (exclude/wrapping) AABBs. Returns true (out_delta, possibly zero) if no overlap
// or a valid translate exists; false if nothing within tower/plate limits clears all regions.
bool avoid_forbidden_regions_delta(const std::vector<BoundingBoxf3> &part_bboxes,
                                   const BoundingBoxf3              *tower_bbox,
                                   const Vec3d                      &center_delta,
                                   const std::vector<BoundingBoxf3> &exclude_aabbs,
                                   const BoundingBoxf3              *wrapping_aabb,
                                   const BoundingBoxf3              &plate_bbox,
                                   const BoundingBoxf3              &buildvol,
                                   double                            margin,
                                   double                            brim_width,
                                   double                            parts_clearance,
                                   double                            tower_clearance,
                                   Vec3d                            &out_delta);

// Legal range for the tower's min (bottom-left) corner, matching interactive dragging's
// pre-slice range (Selection.cpp's v.is_wipe_tower branch). No brim/post-slice margin.
BoundingBoxf wipe_tower_legal_corner_range(const BoundingBoxf3 &build_volume, const Vec2d &tower_size,
                                            double brim_width = 0.0);

struct ForbiddenRect2d { double minx, miny, maxx, maxy; };

// One-shot avoidance for a single axis-aligned rectangle (the tower), ignoring parts.
// Returns whether a push was performed, NOT whether the result is legal.
bool avoid_forbidden_regions_for_wipe_tower(float &x, float &y, float w, float h,
                                            float lo_x, float hi_x, float lo_y, float hi_y,
                                            const std::vector<ForbiddenRect2d> &forbidden,
                                            double clearance = 1.0);

// Same as avoid_forbidden_regions_for_wipe_tower, but pushes using the brim footprint
// (wipe_tower_brim_footprint_expand) and converts back to the wall's own anchor (x,y/w,h;
// never prime_tower_width).
bool avoid_wipe_tower_with_brim_footprint(float &x, float &y, float w, float h,
                                          float lo_x, float hi_x, float lo_y, float hi_y,
                                          const std::vector<ForbiddenRect2d> &forbidden,
                                          double brim_width, double line_width);

// Clamps the tower's world min corner into wipe_tower_legal_corner_range(), then pushes
// it clear of any forbidden region within that range. Returns true iff x or y changed.
bool wipe_tower_pullback_then_avoid(float &x, float &y, const Vec2d &tower_size,
                                    const BoundingBoxf3 &build_volume, double brim_width,
                                    double line_width,
                                    const std::vector<ForbiddenRect2d> &forbidden);

// Only ever flips the plate's "wipe tower placed" flag false -> true; never clears it.
// Mirrors GLCanvas3D::reload_scene()'s "needs a tower" gate instead of a size estimate,
// since filament count/preset may still be mid-sync right after a 3mf import.
void init_wipe_tower_placed_flag(PartPlate &plate);

// Always-on safety net: pulls this plate's tower into the legal range and clear of any
// forbidden region. No-op if there's no tower or it's already clear. Returns true if changed.
bool ensure_wipe_tower_clears_forbidden_regions(PartPlate &plate, int plate_idx, Model &model,
                                                PartPlateList &plate_list,
                                                ConfigOptionFloats &wipe_tower_x,
                                                ConfigOptionFloats &wipe_tower_y,
                                                const DynamicPrintConfig &full_config);

// Re-seats one plate's tower at its "optimal position" using the plate's current layout.
// No-op if the plate has no tower/instances, uses By-object sequencing, or the optimal
// spot isn't immediately legal. Shared by ArrangeJob and the switch-preset path.
void try_optimal_wipe_tower_position_for_plate(PartPlate &plate, int plate_idx, Model &model,
                                               PartPlateList &plate_list,
                                               ConfigOptionFloats &wipe_tower_x,
                                               ConfigOptionFloats &wipe_tower_y,
                                               const DynamicPrintConfig &full_config,
                                               const arrangement::ArrangeParams &baseline_params);

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_WipeTowerPlacement_hpp_
