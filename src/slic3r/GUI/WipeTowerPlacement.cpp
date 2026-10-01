#include "WipeTowerPlacement.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "libslic3r/libslic3r.h"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"
#include "libslic3r/GCode/WipeTower.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/ModelArrange.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "GUI_App.hpp"
#include "PartPlate.hpp"
#include "Plater.hpp"

namespace Slic3r { namespace GUI {

bool wipe_tower_optimal_pos_enabled()
{
    const AppConfig *app_config = wxGetApp().app_config;
    if (!app_config)
        return false;
    return app_config->get("auto_optimize_wipe_tower_placement") != "false";
}

double wipe_tower_side_gap(const arrangement::ArrangeParams &params)
{
    // Mirrors update_unselected_items_inflation()'s wipe-tower branch.
    if (params.plate_has_tree_support)
        return std::max(WIPE_TOWER_ARRANGE_GAP, (double) params.brim_max / 2.0);
    return WIPE_TOWER_ARRANGE_GAP;
}

namespace {
// Negative prime_tower_brim_width means "auto", resolved via get_auto_brim_by_height().
double resolve_wipe_tower_brim_width(const DynamicPrintConfig &config, double tower_height)
{
    double brim_width = 0.0;
    const ConfigOption *opt = config.option("prime_tower_brim_width");
    if (opt) {
        brim_width = opt->getFloat();
        if (brim_width < 0)
            brim_width = WipeTower::get_auto_brim_by_height((float) tower_height);
    }
    return brim_width;
}
} // namespace

double wipe_tower_brim_width(const DynamicPrintConfig &config, double tower_height)
{
    return resolve_wipe_tower_brim_width(config, tower_height);
}

double wipe_tower_line_width(const DynamicPrintConfig &config)
{
    const ConfigOption *opt = config.option("line_width");
    const double lw = opt ? opt->getFloat() : 0.0;
    return lw > 0.0 ? lw : 0.4;
}

BoundingBoxf wipe_tower_nest_footprint(double wall_w, double wall_h, double brim_width)
{
    BoundingBoxf bb(Vec2d(0.0, 0.0), Vec2d(wall_w, wall_h));
    // Matches GLCanvas3D::get_wipe_tower_info()'s doubled offset on an existing tower's bbox.
    bb.offset(brim_width);
    bb.offset(brim_width);
    return bb;
}

double wipe_tower_brim_footprint_expand(double brim_width, double /*line_width*/)
{
    return 2.0 * std::max(0.0, brim_width);
}

double wipe_tower_hug_clearance_matching_nest(const DynamicPrintConfig &config, double tower_height,
                                              const arrangement::ArrangeParams &params)
{
    return wipe_tower_brim_footprint_expand(resolve_wipe_tower_brim_width(config, tower_height), 0.0)
         + wipe_tower_side_gap(params) + WIPE_TOWER_OPTIMAL_POS_EXTRA_CLEARANCE;
}

BoundingBoxf wipe_tower_legal_corner_range(const BoundingBoxf3 &build_volume, const Vec2d &tower_size,
                                            double brim_width)
{
    // brim_width unused here -- kept as a parameter so callers/signatures don't need to change.
    const double m = WIPE_TOWER_MARGIN;
    double lo_x = build_volume.min.x() + m, hi_x = build_volume.max.x() - tower_size.x() - m;
    double lo_y = build_volume.min.y() + m, hi_y = build_volume.max.y() - tower_size.y() - m;
    // Tower wider/deeper than the usable area: pin to the low edge.
    if (hi_x < lo_x) hi_x = lo_x;
    if (hi_y < lo_y) hi_y = lo_y;
    return BoundingBoxf(Vec2d(lo_x, lo_y), Vec2d(hi_x, hi_y));
}

namespace {
// A push lands exactly on a forbidden edge; storing the result as float can fall a few
// ulps back inside, so sub-micron overlap must not count. Shared by both overlap checks below.
static constexpr double WIPE_TOWER_OVERLAP_EPS = 1e-3;
struct AvoidAABB2d { double minx, miny, maxx, maxy; };

static inline bool avoid_overlap(const AvoidAABB2d &a, const AvoidAABB2d &b) {
    // Overlap requires more than WIPE_TOWER_OVERLAP_EPS of penetration on every axis.
    return a.minx < b.maxx - WIPE_TOWER_OVERLAP_EPS && a.maxx > b.minx + WIPE_TOWER_OVERLAP_EPS &&
           a.miny < b.maxy - WIPE_TOWER_OVERLAP_EPS && a.maxy > b.miny + WIPE_TOWER_OVERLAP_EPS;
}

} // namespace


// See declaration in WipeTowerPlacement.hpp. Bboxes are given at their original positions
// plus center_delta; exclude_aabbs are tried as anchors before wrapping_aabb, smallest-push-first.
bool avoid_forbidden_regions_delta(const std::vector<BoundingBoxf3> &part_bboxes_old,
                                           const BoundingBoxf3              *tower_bbox_old,
                                           const Vec3d                     &center_delta,
                                           const std::vector<BoundingBoxf3> &exclude_aabbs,
                                           const BoundingBoxf3             *wrapping_aabb,
                                           const BoundingBoxf3             &plate_bbox,
                                           const BoundingBoxf3             &buildvol,
                                           double                           margin,
                                           double                           brim_width,
                                           double                           parts_clearance,
                                           double                           tower_clearance,
                                           Vec3d                           &out_delta)
{
    out_delta = Vec3d::Zero();
    const double cdx = center_delta.x(), cdy = center_delta.y();

    std::vector<AvoidAABB2d> items;
    items.reserve(part_bboxes_old.size() + (tower_bbox_old ? 1 : 0));
    auto add = [&](const BoundingBoxf3 &b) {
        items.push_back({b.min.x() + cdx, b.min.y() + cdy, b.max.x() + cdx, b.max.y() + cdy});
    };
    for (const BoundingBoxf3 &b : part_bboxes_old) add(b);
    AvoidAABB2d tower2d{0, 0, 0, 0};
    bool        has_tower = false;
    if (tower_bbox_old) { add(*tower_bbox_old); tower2d = items.back(); has_tower = true; }
    if (items.empty()) return true;
    const int tower_item_index = has_tower ? (int) items.size() - 1 : -1;
    auto item_clearance = [&](int i) { return i == tower_item_index ? tower_clearance : parts_clearance; };

    // Merged envelope (for plate-boundary d_max).
    double eminx = items[0].minx, emaxx = items[0].maxx, eminy = items[0].miny, emaxy = items[0].maxy;
    for (const auto &it : items) {
        if (it.minx < eminx) eminx = it.minx;
        if (it.maxx > emaxx) emaxx = it.maxx;
        if (it.miny < eminy) eminy = it.miny;
        if (it.maxy > emaxy) emaxy = it.maxy;
    }

    // Forbidden regions stored raw; each item inflates them by its own role's clearance below.
    std::vector<AvoidAABB2d> raw_forbidden;
    auto add_forbidden = [&](double minx, double miny, double maxx, double maxy) {
        raw_forbidden.push_back({minx, miny, maxx, maxy});
    };
    for (const BoundingBoxf3 &r : exclude_aabbs)
        add_forbidden(r.min.x(), r.min.y(), r.max.x(), r.max.y());
    if (wrapping_aabb)
        add_forbidden(wrapping_aabb->min.x(), wrapping_aabb->min.y(), wrapping_aabb->max.x(), wrapping_aabb->max.y());
    if (raw_forbidden.empty()) return true;
    auto inflate = [&](const AvoidAABB2d &raw, double c) -> AvoidAABB2d {
        return {raw.minx - c, raw.miny - c, raw.maxx + c, raw.maxy + c};
    };

    // 1. Detect overlap.
    bool any_overlap = false;
    for (int ii = 0; ii < (int) items.size(); ++ii)
        for (const auto &raw : raw_forbidden)
            if (avoid_overlap(items[ii], inflate(raw, item_clearance(ii)))) { any_overlap = true; break; }
    if (!any_overlap) return true; // out_delta stays Zero

    // 2. d_max per direction (plate boundary AND tower build_volume; tower tighter).
    double twminx = 0, twminy = 0, twszx = 0, twszy = 0;
    if (has_tower) { twminx = tower2d.minx; twminy = tower2d.miny; twszx = tower2d.maxx - tower2d.minx; twszy = tower2d.maxy - tower2d.miny; }
    const double plx0 = plate_bbox.min.x(), ply0 = plate_bbox.min.y(), plx1 = plate_bbox.max.x(), ply1 = plate_bbox.max.y();
    const double bvx0 = buildvol.min.x(),  bvy0 = buildvol.min.y(),  bvx1 = buildvol.max.x(),  bvy1 = buildvol.max.y();
    const double tower_margin = margin;
    double lox = bvx0 + tower_margin, hix = bvx1 - twszx - tower_margin; if (hix < lox) hix = lox;
    double loy = bvy0 + tower_margin, hiy = bvy1 - twszy - tower_margin; if (hiy < loy) hiy = loy;
    const double dmax_px = std::min(plx1 - emaxx, hix - twminx); // +x
    const double dmax_nx = std::min(eminx - plx0, twminx - lox); // -x
    const double dmax_py = std::min(ply1 - emaxy, hiy - twminy); // +y
    const double dmax_ny = std::min(eminy - ply0, twminy - loy); // -y

    // 3. Anchors: forbidden regions some item overlaps (exclude first, wrapping after — already ordered).
    std::vector<int> anchors;
    for (int ri = 0; ri < (int) raw_forbidden.size(); ++ri) {
        for (int ii = 0; ii < (int) items.size(); ++ii)
            if (avoid_overlap(items[ii], inflate(raw_forbidden[ri], item_clearance(ii)))) { anchors.push_back(ri); break; }
    }
    if (anchors.empty()) return true;

    // 4. Per anchor, try 4 dirs; per dir minimal clear dist; verify vs all forbidden; first clean wins.
    const double dmaxs[4]   = {dmax_px, dmax_nx, dmax_py, dmax_ny}; // 0=+x 1=-x 2=+y 3=-y
    for (int ri : anchors) {
        double cand[4] = {0, 0, 0, 0};
        bool   ok[4]   = {false, false, false, false};
        for (int d = 0; d < 4; ++d) {
            double dc = 0; bool need = false;
            for (int ii = 0; ii < (int) items.size(); ++ii) {
                const auto &it = items[ii];
                const AvoidAABB2d R = inflate(raw_forbidden[ri], item_clearance(ii));
                if (d < 2) { // x-direction clear: require y-overlap with R
                    if (!(it.miny < R.maxy && it.maxy > R.miny)) continue;
                    if (d == 0) { if (it.minx < R.maxx) { dc = std::max(dc, R.maxx - it.minx); need = true; } }
                    else        { if (it.maxx > R.minx) { dc = std::max(dc, it.maxx - R.minx); need = true; } }
                } else { // y-direction clear: require x-overlap with R
                    if (!(it.minx < R.maxx && it.maxx > R.minx)) continue;
                    if (d == 2) { if (it.miny < R.maxy) { dc = std::max(dc, R.maxy - it.miny); need = true; } }
                    else        { if (it.maxy > R.miny) { dc = std::max(dc, it.maxy - R.miny); need = true; } }
                }
            }
            if (!need) { ok[d] = true; cand[d] = 0; }
            else if (dc <= 1e-9 || dc > dmaxs[d] + 1e-9) { ok[d] = false; }
            else { ok[d] = true; cand[d] = dc; }
        }
        int order[4] = {0, 1, 2, 3};
        std::sort(order, order + 4, [&](int a, int b) { return cand[a] < cand[b]; });
        for (int oi = 0; oi < 4; ++oi) {
            const int d = order[oi];
            if (!ok[d]) continue;
            double dx = 0, dy = 0;
            if (d == 0) dx = cand[d]; else if (d == 1) dx = -cand[d]; else if (d == 2) dy = cand[d]; else dy = -cand[d];
            bool clean = true;
            for (int ii = 0; ii < (int) items.size(); ++ii) {
                const AvoidAABB2d t{items[ii].minx + dx, items[ii].miny + dy, items[ii].maxx + dx, items[ii].maxy + dy};
                for (const auto &raw : raw_forbidden)
                    if (avoid_overlap(t, inflate(raw, item_clearance(ii)))) { clean = false; break; }
                if (!clean) break;
            }
            if (clean) { out_delta = Vec3d(dx, dy, 0.0); return true; }
        }
    }
    return false; // bail
}


// Slides the tower along the ray from the parts' hull centroid to the default tower
// direction, stopping where it clears the hull. Geometry: placing tower rect R's centre
// outside hull H (+) (-R) (Minkowski sum) means R and H are disjoint; for convex H that
// reduces to a single O(n) ray-exit query, no NFP/SAT needed.
bool compute_optimal_wipe_tower_min(const Polygons &inflated_parts,
                                            const Vec2d    &plate_center,
                                            const Vec2d    &default_tower_center,
                                            const Vec2d    &tower_size,
                                            double          tower_clearance,
                                            Vec2d          &out_wt_min)
{
    if (inflated_parts.empty()) return false;
    if (tower_size.x() <= EPSILON || tower_size.y() <= EPSILON) return false;

    // 1. Convex hull of all inflated part contours. Hulling the merged point set is
    //    equivalent to hulling their union, so no Clipper boolean is needed here.
    Points pts;
    for (const Polygon &p : inflated_parts)
        pts.insert(pts.end(), p.points.begin(), p.points.end());
    const Polygon hull = Geometry::convex_hull(std::move(pts));
    if (hull.points.size() < 3) return false; // degenerate (collinear / single part sliver)

    // 2. Ray origin: the hull's area centroid. Guaranteed inside the Minkowski sum below.
    const Vec2d C = unscale(hull.centroid());

    // 3. Ray direction: plate centre -> printer's native default tower position. The angle
    //    itself is never needed explicitly; the normalised direction vector is enough.
    Vec2d d = default_tower_center - plate_center;
    if (d.norm() <= EPSILON)
        return false; // default tower sits on the plate centre: no usable direction
    d.normalize();

    // 4. M = hull (+) (-R): hull vertices offset by the tower rect's four corners.
    //    The rect is grown by tower_clearance so the tower keeps its own share of the gap;
    //    the real half-extents are kept separately to recover the true corner in step 6.
    const double gap = std::max(0.0, tower_clearance);
    const double hw_real = tower_size.x() / 2.0, hh_real = tower_size.y() / 2.0;
    const double hw = hw_real + gap, hh = hh_real + gap;
    Points msum_pts;
    msum_pts.reserve(hull.points.size() * 4);
    for (const Point &v : hull.points) {
        const Vec2d vd = unscale(v);
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2)
                msum_pts.emplace_back(Point::new_scale(Vec2d(vd.x() + sx * hw, vd.y() + sy * hh)));
    }
    Polygon msum = Geometry::convex_hull(std::move(msum_pts));
    if (msum.points.size() < 3) return false;
    // Don't depend on convex_hull()'s winding: normalize it so the edge normals below
    // really are outward-facing.
    if (!msum.is_counter_clockwise())
        msum.make_counter_clockwise();

    // 5. Ray exit: C is inside the convex, CCW-oriented msum, so the exit parameter is
    //    the smallest positive hit among the edges the ray actually faces.
    double t_exit = std::numeric_limits<double>::infinity();
    const size_t n = msum.points.size();
    for (size_t i = 0; i < n; ++i) {
        const Vec2d a = unscale(msum.points[i]);
        const Vec2d b = unscale(msum.points[(i + 1) % n]);
        const Vec2d e = b - a;
        Vec2d nrm(e.y(), -e.x()); // outward normal for a CCW contour
        if (nrm.norm() <= EPSILON) continue; // degenerate edge
        nrm.normalize();
        const double denom = nrm.dot(d);
        if (denom <= EPSILON) continue;  // edge faces away from / parallel to the ray
        const double t = nrm.dot(a - C) / denom;
        if (t < t_exit) t_exit = t;
    }
    if (!std::isfinite(t_exit) || t_exit < 0.0)
        return false;

    // 6. Back to the tower's real bottom-left corner: the ray solved for the CENTRE of the
    //    clearance-grown rect, and that centre is also the real rect's centre.
    const Vec2d tower_center = C + t_exit * d;
    out_wt_min = Vec2d(tower_center.x() - hw_real, tower_center.y() - hh_real);
    return true;
}


namespace {
static inline bool wipe_tower_rect_overlap(double aminx, double aminy, double amaxx, double amaxy,
                                            double bminx, double bminy, double bmaxx, double bmaxy)
{
    // Overlap requires more than WIPE_TOWER_OVERLAP_EPS of penetration on every axis.
    return aminx < bmaxx - WIPE_TOWER_OVERLAP_EPS && amaxx > bminx + WIPE_TOWER_OVERLAP_EPS &&
           aminy < bmaxy - WIPE_TOWER_OVERLAP_EPS && amaxy > bminy + WIPE_TOWER_OVERLAP_EPS;
}
} // namespace

// Doc comment on the declaration in WipeTowerPlacement.hpp.
bool avoid_forbidden_regions_for_wipe_tower(float &x, float &y, float w, float h,
                                                    float lo_x, float hi_x, float lo_y, float hi_y,
                                                    const std::vector<ForbiddenRect2d> &forbidden,
                                                    double clearance)
{
    if (forbidden.empty()) return true;
    if (hi_x < lo_x) hi_x = lo_x;
    if (hi_y < lo_y) hi_y = lo_y;

    // Anchors: forbidden rects the tower currently overlaps, inflated by `clearance` so the
    // result keeps at least that much gap (matches avoid_forbidden_regions_delta's clearance).
    for (const ForbiddenRect2d &R0 : forbidden) {
        const ForbiddenRect2d R{R0.minx - clearance, R0.miny - clearance, R0.maxx + clearance, R0.maxy + clearance};
        if (!wipe_tower_rect_overlap(x, y, x + w, y + h, R.minx, R.miny, R.maxx, R.maxy))
            continue;

        // dmax per direction, from the caller's already-established legal clamp range.
        const double dmaxs[4] = {hi_x - x, x - lo_x, hi_y - y, y - lo_y}; // 0=+x 1=-x 2=+y 3=-y
        // dc: exact distance needed to clear THIS anchor (with clearance) in each direction
        // (overlap guarantees all 4 are actually needed, since the tower already overlaps R).
        const double dc[4] = {R.maxx - x, (x + w) - R.minx, R.maxy - y, (y + h) - R.miny};

        bool ok[4];
        for (int d = 0; d < 4; ++d) ok[d] = dc[d] > 1e-9 && dc[d] <= dmaxs[d] + 1e-9;

        int order[4] = {0, 1, 2, 3};
        std::sort(order, order + 4, [&](int a, int b) { return dc[a] < dc[b]; });
        for (int oi = 0; oi < 4; ++oi) {
            const int d = order[oi];
            if (!ok[d]) continue;
            float nx = x, ny = y;
            if (d == 0) nx = x + (float) dc[d];
            else if (d == 1) nx = x - (float) dc[d];
            else if (d == 2) ny = y + (float) dc[d];
            else ny = y - (float) dc[d];

            bool clean = true;
            for (const ForbiddenRect2d &r0 : forbidden) {
                const ForbiddenRect2d r{r0.minx - clearance, r0.miny - clearance, r0.maxx + clearance, r0.maxy + clearance};
                if (wipe_tower_rect_overlap(nx, ny, nx + w, ny + h, r.minx, r.miny, r.maxx, r.maxy)) { clean = false; break; }
            }
            if (clean) {
                x = nx; y = ny;
                return true;
            }
        }
    }
    // No one-shot push clears all forbidden regions: leave x/y as they were.
    return false;
}

// Doc comment on the declaration in WipeTowerPlacement.hpp.
bool avoid_wipe_tower_with_brim_footprint(float &x, float &y, float w, float h,
                                          float lo_x, float hi_x, float lo_y, float hi_y,
                                          const std::vector<ForbiddenRect2d> &forbidden,
                                          double brim_width, double line_width)
{
    const float expand = (float) wipe_tower_brim_footprint_expand(brim_width, line_width);
    float ax = x - expand, ay = y - expand;
    const float aw = w + 2.f * expand, ah = h + 2.f * expand;
    const bool pushed = avoid_forbidden_regions_for_wipe_tower(
        ax, ay, aw, ah,
        lo_x - expand, hi_x - expand, lo_y - expand, hi_y - expand,
        forbidden, WIPE_TOWER_ARRANGE_GAP);
    x = ax + expand;
    y = ay + expand;
    return pushed;
}

// Doc comment on the declaration in WipeTowerPlacement.hpp.
bool wipe_tower_pullback_then_avoid(float &x, float &y, const Vec2d &tower_size,
                                    const BoundingBoxf3 &build_volume, double brim_width,
                                    double line_width,
                                    const std::vector<ForbiddenRect2d> &forbidden)
{
    const float x0 = x, y0 = y;
    const BoundingBoxf legal = wipe_tower_legal_corner_range(build_volume, tower_size, brim_width);
    x = std::clamp(x, (float) legal.min.x(), (float) std::max(legal.min.x(), legal.max.x()));
    y = std::clamp(y, (float) legal.min.y(), (float) std::max(legal.min.y(), legal.max.y()));
    if (!forbidden.empty())
        avoid_wipe_tower_with_brim_footprint(x, y, (float) tower_size.x(), (float) tower_size.y(),
                                             (float) legal.min.x(), (float) legal.max.x(),
                                             (float) legal.min.y(), (float) legal.max.y(),
                                             forbidden, brim_width, line_width);
    return x != x0 || y != y0;
}

void try_optimal_wipe_tower_position_for_plate(PartPlate &plate, int plate_idx, Model &model,
                                               PartPlateList &plate_list,
                                               ConfigOptionFloats &wipe_tower_x,
                                               ConfigOptionFloats &wipe_tower_y,
                                               const DynamicPrintConfig &full_config,
                                               const arrangement::ArrangeParams &baseline_params)
{
    if (plate_idx >= static_cast<int>(wipe_tower_x.values.size()) ||
        plate_idx >= static_cast<int>(wipe_tower_y.values.size()))
        return;
    if (plate.is_locked() || plate.empty())
        return;
    // Mirrors GLCanvas3D::reload_scene()'s / init_wipe_tower_placed_flag()'s own gate:
    // ByObject only has a wipe tower when the plate holds exactly one printable instance.
    if (plate.get_real_print_seq() == PrintSequence::ByObject && plate.printable_instance_size() != 1)
        return;

    const int  nozzle_nums     = wxGetApp().preset_bundle->get_printer_extruder_count();
    const bool enable_wrapping = full_config.opt_bool("enable_wrapping_detection");

    // Decided from config/plate state, not the tower GLVolume: this may run before the
    // next reload_scene() has (re)created the tower volume.
    const DynamicPrintConfig &print_cfg = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    if (!print_cfg.opt_bool("enable_prime_tower"))
        return;
    const float  prime_tower_width = print_cfg.opt_float("prime_tower_width");
    std::vector<double> prime_volumes = full_config.option<ConfigOptionFloats>("filament_prime_volume")->values;
    if (full_config.option<ConfigOptionEnum<PrimeVolumeMode>>("prime_volume_mode")->value == pvmSaving)
        for (auto &pv : prime_volumes) pv = 15.f;
    const Vec3d wt_size_3d = plate.estimate_wipe_tower_size(print_cfg, prime_tower_width,
                                                             get_max_element(prime_volumes),
                                                             nozzle_nums, 0, false, enable_wrapping);
    const Vec2d tower_size(wt_size_3d.x(), wt_size_3d.y());
    if (tower_size.x() <= EPSILON || tower_size.y() <= EPSILON)
        return;

    const Vec3d plate_origin = plate.get_origin();
    // wipe_tower_x/y are plate-local; work in world coords like the switch-preset path.
    const Vec3d cur_wt_min(wipe_tower_x.values[plate_idx] + plate_origin.x(),
                           wipe_tower_y.values[plate_idx] + plate_origin.y(), 0.0);

    // Collect this plate's instances: inflated contours for the hull the tower will hug.
    arrangement::ArrangePolygons aps;
    for (const auto &oi : plate.get_obj_and_inst_set()) {
        const int obj_idx = oi.first, inst_idx = oi.second;
        if (obj_idx < 0 || obj_idx >= static_cast<int>(model.objects.size()))
            continue;
        ModelObject *mo = model.objects[obj_idx];
        if (!mo || inst_idx < 0 || inst_idx >= static_cast<int>(mo->instances.size()))
            continue;
        aps.emplace_back(get_instance_arrange_poly(mo->instances[inst_idx], full_config));
    }
    if (aps.empty())
        return;

    // Declared out here because update_selected_items_inflation() fills in brim_max /
    // plate_has_tree_support as a side effect, and the tower's own gap depends on them.
    arrangement::ArrangeParams p = baseline_params;
    Polygons inflated_parts;
    {
        arrangement::update_selected_items_inflation(aps, full_config, p);
        for (const arrangement::ArrangePolygon &ap : aps) {
            Polygon c = ap.poly.contour;
            c.translate(ap.translation.x(), ap.translation.y());
            // Match arrange's effective inflation: process_arrangeable() floors every item at
            // MIN_SEPARATION after update_selected_items_inflation has run.
            const coord_t infl = std::max(ap.inflation, scaled(WIPE_TOWER_ARRANGE_GAP));
            if (infl > 0) {
                Polygons grown = offset(c, (float) infl);
                for (Polygon &g : grown) inflated_parts.emplace_back(std::move(g));
            } else {
                inflated_parts.emplace_back(std::move(c));
            }
        }
    }

    // Ray direction anchor: the printer's native default tower position, plate-local.
    const Vec2d def_local = plate_list.get_machine_default_wipe_tower_pos();
    const Vec2d default_tower_center(plate_origin.x() + def_local.x() + tower_size.x() / 2.0,
                                      plate_origin.y() + def_local.y() + tower_size.y() / 2.0);
    const Vec3d plate_center3 = plate.get_center_origin();

    // Hug gap matches nest (2x brim + wipe_tower_side_gap) plus extra optimal-position clearance.
    const double hug_clearance = wipe_tower_hug_clearance_matching_nest(print_cfg, wt_size_3d.z(), p);
    Vec2d optimal_min;
    if (!compute_optimal_wipe_tower_min(inflated_parts,
                                        Vec2d(plate_center3.x(), plate_center3.y()),
                                        default_tower_center, tower_size,
                                        hug_clearance, optimal_min))
        return; // keep the plain arrange result

    // Forbidden regions, world coords (same derivation as the switch-preset path).
    const std::vector<BoundingBoxf3> &exclude_aabbs = plate.get_exclude_areas();
    BoundingBoxf3 wrapping_bb;
    bool          has_wrapping = false;
    if (enable_wrapping) {
        // m_wrapping_exclude_areas is stored plate-local, so add the plate origin.
        const Pointfs wpts = plate_list.get_wrapping_exclude_area();
        if (!wpts.empty()) {
            for (const Vec2d &pt : wpts)
                wrapping_bb.merge(Vec3d(pt.x() + plate_origin.x(), pt.y() + plate_origin.y(), 0.0));
            has_wrapping = wrapping_bb.defined;
        }
    }

    // Accept the optimal position only if directly usable (reachable + clear of forbidden
    // regions); parts keep their layout, the tower either moves there or stays put.
    const BoundingBoxf legal = wipe_tower_legal_corner_range(plate.get_build_volume(true), tower_size,
                                                              wipe_tower_brim_width(print_cfg, wt_size_3d.z()));
    const double tx0 = optimal_min.x(), ty0 = optimal_min.y();
    const double tx1 = tx0 + tower_size.x(), ty1 = ty0 + tower_size.y();

    if (tx0 < legal.min.x() - EPSILON || tx0 > legal.max.x() + EPSILON ||
        ty0 < legal.min.y() - EPSILON || ty0 > legal.max.y() + EPSILON)
        return; // outside the reachable area -> keep the current position

    {
        // Test against the tower's brim footprint, matching the doubled-brim convention
        // used elsewhere for an existing tower's bbox.
        const double brim = wipe_tower_brim_width(print_cfg, wt_size_3d.z());
        const double expand = wipe_tower_brim_footprint_expand(brim, wipe_tower_line_width(print_cfg));
        const double c = WIPE_TOWER_ARRANGE_GAP;
        auto overlaps = [&](const BoundingBoxf3 &r) {
            return tx0 - expand < r.max.x() + c && tx1 + expand > r.min.x() - c &&
                   ty0 - expand < r.max.y() + c && ty1 + expand > r.min.y() - c;
        };
        bool bad = false;
        for (const BoundingBoxf3 &r : exclude_aabbs)
            if (overlaps(r)) { bad = true; break; }
        if (!bad && has_wrapping && overlaps(wrapping_bb))
            bad = true;
        if (bad)
            return; // hits a forbidden region -> keep the current position
    }

    const Vec3d new_wt_min(optimal_min.x(), optimal_min.y(), 0.0);
    if ((new_wt_min - cur_wt_min).cwiseAbs().maxCoeff() <= EPSILON)
        return; // already there

    ConfigOptionFloat wt_x_opt(new_wt_min.x() - plate_origin.x());
    ConfigOptionFloat wt_y_opt(new_wt_min.y() - plate_origin.y());
    wipe_tower_x.set_at(&wt_x_opt, plate_idx, 0);
    wipe_tower_y.set_at(&wt_y_opt, plate_idx, 0);
    if (plate_idx < static_cast<int>(model.wipe_tower.positions.size()))
        model.wipe_tower.positions[plate_idx] = Vec2d(wt_x_opt.value, wt_y_opt.value);
    // Already vetted against the legal range and every forbidden region, so reload_scene()
    // must not treat this plate as a first materialization and redo this placement.
    plate.set_wipe_tower_placed(true);
}

void init_wipe_tower_placed_flag(PartPlate &plate)
{
    if (plate.is_wipe_tower_placed())
        return;

    // Mirrors GLCanvas3D::reload_scene()'s own "does this plate need a tower" gate so
    // the two never disagree about which plates need first-time placement.
    if (wxGetApp().plater()->only_gcode_mode() || wxGetApp().plater()->is_gcode_3mf())
        return;

    const DynamicPrintConfig &print_cfg = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    auto enable_op = print_cfg.option("enable_prime_tower");
    if (!enable_op || !enable_op->getBool())
        return;

    auto timelapse_type = print_cfg.option<ConfigOptionEnum<TimelapseType>>("timelapse_type");
    bool need_wipe_tower = timelapse_type ? (timelapse_type->value == TimelapseType::tlSmooth) : false;
    if (print_cfg.has("enable_wrapping_detection"))
        need_wipe_tower |= print_cfg.opt_bool("enable_wrapping_detection");

    // Same rule as plate_allows_wipe_tower()/try_optimal_wipe_tower_position_for_plate():
    // ByObject only has a tower when the plate holds exactly one printable instance.
    if (plate.get_real_print_seq() == PrintSequence::ByObject && plate.printable_instance_size() != 1)
        return;

    if (plate.get_objects_on_this_plate().empty())
        return;

    if (!need_wipe_tower && plate.get_extruders(true).size() < 2)
        return;

    plate.set_wipe_tower_placed(true);
}

bool ensure_wipe_tower_clears_forbidden_regions(PartPlate &plate, int plate_idx, Model &model,
                                                PartPlateList &plate_list,
                                                ConfigOptionFloats &wipe_tower_x,
                                                ConfigOptionFloats &wipe_tower_y,
                                                const DynamicPrintConfig &full_config)
{
    if (plate_idx >= static_cast<int>(wipe_tower_x.values.size()) ||
        plate_idx >= static_cast<int>(wipe_tower_y.values.size()))
        return false;

    const int  nozzle_nums     = wxGetApp().preset_bundle->get_printer_extruder_count();
    const bool enable_wrapping = full_config.opt_bool("enable_wrapping_detection");

    const DynamicPrintConfig &print_cfg = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    if (!print_cfg.opt_bool("enable_prime_tower"))
        return false;
    const float  prime_tower_width = print_cfg.opt_float("prime_tower_width");
    std::vector<double> prime_volumes = full_config.option<ConfigOptionFloats>("filament_prime_volume")->values;
    if (full_config.option<ConfigOptionEnum<PrimeVolumeMode>>("prime_volume_mode")->value == pvmSaving)
        for (auto &pv : prime_volumes) pv = 15.f;
    const Vec3d wt_size_3d = plate.estimate_wipe_tower_size(print_cfg, prime_tower_width,
                                                             get_max_element(prime_volumes),
                                                             nozzle_nums, 0, false, enable_wrapping);
    const Vec2d tower_size(wt_size_3d.x(), wt_size_3d.y());
    if (tower_size.x() <= EPSILON || tower_size.y() <= EPSILON)
        return false; // no tower on this plate -- nothing to guard

    const Vec3d plate_origin = plate.get_origin();
    // Config position, not the tower GLVolume, may not exist yet at this point.
    float x = wipe_tower_x.values[plate_idx] + (float) plate_origin.x();
    float y = wipe_tower_y.values[plate_idx] + (float) plate_origin.y();
    const float x0 = x, y0 = y;

    const std::vector<BoundingBoxf3> &exclude_aabbs = plate.get_exclude_areas();
    std::vector<ForbiddenRect2d> forbidden;
    forbidden.reserve(exclude_aabbs.size() + 1);
    for (const BoundingBoxf3 &b : exclude_aabbs)
        forbidden.push_back({b.min.x(), b.min.y(), b.max.x(), b.max.y()});
    if (enable_wrapping) {
        // Converted to world coords below since this function works in world coords.
        const Pointfs wpts = plate_list.get_wrapping_exclude_area();
        if (!wpts.empty()) {
            BoundingBoxf wrap_bb(wpts);
            forbidden.push_back({wrap_bb.min.x() + plate_origin.x(), wrap_bb.min.y() + plate_origin.y(),
                                 wrap_bb.max.x() + plate_origin.x(), wrap_bb.max.y() + plate_origin.y()});
        }
    }

    // Pull-back always runs regardless of forbidden-region overlap, so any plate
    // gets pulled back into the reachable area even with nothing to avoid.
    const double brim = wipe_tower_brim_width(print_cfg, wt_size_3d.z());
    wipe_tower_pullback_then_avoid(x, y, tower_size, plate.get_build_volume(true), brim,
                                   wipe_tower_line_width(print_cfg), forbidden);

    if (std::abs(x - x0) <= EPSILON && std::abs(y - y0) <= EPSILON)
        return false; // already pulled back and clear -- nothing changed

    ConfigOptionFloat wt_x_opt(x - (float) plate_origin.x());
    ConfigOptionFloat wt_y_opt(y - (float) plate_origin.y());
    wipe_tower_x.set_at(&wt_x_opt, plate_idx, 0);
    wipe_tower_y.set_at(&wt_y_opt, plate_idx, 0);
    if (plate_idx < static_cast<int>(model.wipe_tower.positions.size()))
        model.wipe_tower.positions[plate_idx] = Vec2d(wt_x_opt.value, wt_y_opt.value);
    plate.set_wipe_tower_placed(true);
    return true;
}

}} // namespace Slic3r::GUI
