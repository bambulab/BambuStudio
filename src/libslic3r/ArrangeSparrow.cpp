#include "ArrangeSparrow.hpp"

#include "BoundingBox.hpp"
#include "ClipperUtils.hpp"
#include "I18N.hpp"

#include <libnest2d/common.hpp> // MAX_NUM_PLATES

#include <sparrow_arrange.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

//! macro used to mark string used at localization, return same string
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r { namespace arrangement {

namespace {

// process_arrangeable() clamps inflation up to this; match it so spacing is identical.
const coord_t SPARROW_MIN_SEPARATION = scale_(1.0);

// Key used to recognize the same exclusion polygon replicated across beds.
std::string poly_key(const Polygon &p)
{
    std::string k;
    k.reserve(p.points.size() * 24);
    for (const Point &pt : p.points) {
        k += std::to_string(pt.x());
        k.push_back(',');
        k += std::to_string(pt.y());
        k.push_back(';');
    }
    return k;
}

// Same jtSquare offset libnest2d's Item::inflate applies; keeps the largest contour.
bool inflate_contour(const Polygon &src, coord_t infl, Polygon &out)
{
    if (src.points.size() < 3)
        return false;
    if (infl <= 0) {
        out = src;
        return true;
    }

    Polygons res       = offset(src, float(infl), jtSquare);
    size_t   best      = res.size();
    double   best_area = 0.;
    for (size_t i = 0; i < res.size(); ++i) {
        double a = std::abs(res[i].area());
        if (a > best_area) { best_area = a; best = i; }
    }
    if (best == res.size() || res[best].points.size() < 3) {
        // Offset collapsed the contour; keep the raw one.
        out = src;
        return true;
    }
    out = std::move(res[best]);
    return true;
}

std::vector<sp_point> to_sp_points(const Polygon &p, const Point &origin)
{
    std::vector<sp_point> pts;
    pts.reserve(p.points.size());
    for (const Point &q : p.points)
        pts.push_back(sp_point{unscaled<double>(q.x() - origin.x()),
                               unscaled<double>(q.y() - origin.y())});
    return pts;
}

// Same tiny shrink arrange() applies to fixed items.
coord_t fixed_inflation(const ArrangePolygon &ap)
{
    coord_t infl = std::max(ap.inflation, SPARROW_MIN_SEPARATION) - scaled<coord_t>(2. * EPSILON);
    return std::max(infl, coord_t(0));
}

Polygon world_contour(const ArrangePolygon &ap)
{
    Polygon w = ap.poly.contour;
    w.rotate(ap.rotation);
    w.translate(ap.translation.x(), ap.translation.y());
    return w;
}

// Shared closure for both FFI callbacks: sp_input has one `user` pointer.
struct SparrowCallbackCtx
{
    const std::function<bool(void)> *              stopcondition = nullptr;
    const std::function<void(unsigned, std::string)> *progressind = nullptr;
    int                                            last_bed      = -1;
    int                                            last_placed   = -1;
};

int sparrow_should_stop_cb(void *user)
{
    const auto *ctx = static_cast<const SparrowCallbackCtx *>(user);
    if (ctx == nullptr || ctx->stopcondition == nullptr || !*ctx->stopcondition)
        return 0;
    return (*ctx->stopcondition)() ? 1 : 0;
}

// Runs on the worker thread, at bed start and after each committed item.
void sparrow_on_progress_cb(void *user, int bed_idx, int placed, int total)
{
    auto *ctx = static_cast<SparrowCallbackCtx *>(user);
    if (ctx == nullptr || ctx->progressind == nullptr || !*ctx->progressind)
        return;
    // A bed start repeats the previous placed count; only report real changes.
    if (bed_idx == ctx->last_bed && placed == ctx->last_placed)
        return;
    ctx->last_bed    = bed_idx;
    ctx->last_placed = placed;

    // The GUI shows _L("Arranging") + " " + str, so emit only the suffix.
    std::string msg = (boost::format(L("plate %1%, %2%/%3% objects placed"))
                       % (bed_idx + 1) % placed % total).str();
    BOOST_LOG_TRIVIAL(debug) << "sparrow: progress st=" << placed << " " << msg;
    // progressind takes the count done, so pass placed.
    (*ctx->progressind)(unsigned(placed), msg);
}

} // namespace

bool arrange_sparrow(ArrangePolygons &      arrangables,
                     const ArrangePolygons &excludes,
                     const BoundingBox &    bed,
                     const ArrangeParams &  params)
{
    if (arrangables.empty())
        return true; // nothing to place; both backends agree on the empty result
    if (!bed.defined)
        return false;

    // `bed` is already shrunk by get_shrink_bedpts(); do not shrink again. The edge
    // margin comes from item inflation, as in _arrange(), which packs inflated
    // outlines into the uncorrected bin.
    const Point  bed_min = bed.min;
    const double bed_w   = unscaled<double>(bed.max.x() - bed.min.x());
    const double bed_h   = unscaled<double>(bed.max.y() - bed.min.y());
    if (bed_w <= 0. || bed_h <= 0.)
        return false;

    // Point buffers must outlive the FFI call; reserve so nothing is reallocated.
    std::vector<std::vector<sp_point>> pt_store;
    pt_store.reserve(arrangables.size() + excludes.size() + params.excluded_regions.size());

    std::vector<sp_item>    items;
    std::vector<sp_polygon> holes;
    std::vector<size_t>     movable_src; // items[k] -> arrangables[movable_src[k]]

    // ---- movable items -----------------------------------------------------
    for (size_t i = 0; i < arrangables.size(); ++i) {
        const ArrangePolygon &ap = arrangables[i];
        Polygon               infl_poly;
        if (!inflate_contour(ap.poly.contour, std::max(ap.inflation, SPARROW_MIN_SEPARATION), infl_poly))
            continue; // degenerate outline; process_arrangeable() drops these too

        pt_store.emplace_back(to_sp_points(infl_poly, Point(0, 0)));
        sp_item it{};
        it.outline.pts    = pt_store.back().data();
        it.outline.n      = pt_store.back().size();
        it.fixed          = 0;
        it.bed_idx        = -1;
        it.x              = unscaled<double>(ap.translation.x() - bed_min.x());
        it.y              = unscaled<double>(ap.translation.y() - bed_min.y());
        it.rotation       = ap.rotation;
        // allowed_rotations is never populated and libnest2d ignores it too.
        it.allow_rotation = params.allow_rotations ? 1 : 0;
        items.push_back(it);
        movable_src.push_back(i);
    }
    if (items.empty())
        return false;
    const size_t n_movable = items.size();

    // ---- fixed items and exclusion holes ----------------------------------
    // Zones replicated on every bed become one hole; anything else is a fixed
    // item pinned to its bed.
    std::vector<Polygon>                 virt_world(excludes.size());
    std::vector<std::string>             virt_keys(excludes.size());
    std::map<std::string, std::set<int>> virt_beds;
    for (size_t i = 0; i < excludes.size(); ++i) {
        const ArrangePolygon &ap = excludes[i];
        if (!ap.is_virt_object || ap.bed_idx < 0)
            continue;
        virt_world[i] = world_contour(ap);
        virt_keys[i]  = poly_key(virt_world[i]);
        virt_beds[virt_keys[i]].insert(ap.bed_idx);
    }

    // A key present on beds {0..k-1}, k >= 2, is a per-bed replica: collapse it to
    // one hole. Check each key against its own k: callers clone zones over different
    // bed counts (the CLI uses 16 for exclusion zones, 36 for wipe towers).
    std::set<std::string> hole_keys;
    for (const auto &kv : virt_beds) {
        const std::set<int> &beds_seen = kv.second;
        if (beds_seen.size() >= 2 && *beds_seen.begin() == 0 &&
            *beds_seen.rbegin() == int(beds_seen.size()) - 1)
            hole_keys.insert(kv.first);
    }

    // Beds beyond the zone coverage get no holes; libnest2d has the same gap.
    std::set<std::string> emitted_holes;
    auto add_hole = [&](const Polygon &world, const std::string &key, coord_t infl) {
        if (!emitted_holes.insert(key).second)
            return;
        Polygon infl_poly;
        if (!inflate_contour(world, infl, infl_poly))
            return;
        pt_store.emplace_back(to_sp_points(infl_poly, bed_min));
        holes.push_back(sp_polygon{pt_store.back().data(), pt_store.back().size()});
    };

    for (size_t i = 0; i < excludes.size(); ++i) {
        const ArrangePolygon &ap = excludes[i];
        if (ap.bed_idx < 0)
            continue;
        const coord_t infl = fixed_inflation(ap);
        if (ap.is_virt_object && hole_keys.count(virt_keys[i])) {
            add_hole(virt_world[i], virt_keys[i], infl);
            continue;
        }
        Polygon infl_poly;
        if (!inflate_contour(ap.poly.contour, infl, infl_poly))
            continue;
        pt_store.emplace_back(to_sp_points(infl_poly, Point(0, 0)));
        sp_item it{};
        it.outline.pts    = pt_store.back().data();
        it.outline.n      = pt_store.back().size();
        it.fixed          = 1;
        it.bed_idx        = ap.bed_idx;
        it.x              = unscaled<double>(ap.translation.x() - bed_min.x());
        it.y              = unscaled<double>(ap.translation.y() - bed_min.y());
        it.rotation       = ap.rotation;
        it.allow_rotation = 0;
        items.push_back(it);
    }

    // excluded_regions apply to every bed, matching libnest2d's m_excluded_items.
    for (const ArrangePolygon &ap : params.excluded_regions) {
        Polygon w = world_contour(ap);
        add_hole(w, poly_key(w), fixed_inflation(ap));
    }

    // libnest2d only penalizes the calibration strip; the ABI has no soft regions,
    // so it becomes a hard hole here.
    if (params.avoid_extrusion_cali_region) {
        for (const ArrangePolygon &ap : params.nonprefered_regions) {
            Polygon w = world_contour(ap);
            add_hole(w, poly_key(w), fixed_inflation(ap));
        }
    }

    BOOST_LOG_TRIVIAL(info) << "sparrow: bed " << bed_w << "x" << bed_h
                            << " mm, movable=" << n_movable
                            << ", fixed=" << (items.size() - n_movable)
                            << ", holes=" << holes.size()
                            << ", excludes_in=" << excludes.size()
                            << ", excluded_regions=" << params.excluded_regions.size()
                            << ", nonprefered_regions=" << params.nonprefered_regions.size();
    // ---- call the backend --------------------------------------------------
    sp_input in{};
    in.bed_w        = bed_w;
    in.bed_h        = bed_h;
    in.holes        = holes.empty() ? nullptr : holes.data();
    in.n_holes      = holes.size();
    in.items        = items.data();
    in.n_items      = items.size();
    in.max_beds     = MAX_NUM_PLATES; // same cap FirstFitSelection enforces
    in.time_limit_s = params.sparrow_time_limit_s;
    in.seed         = 0;

    SparrowCallbackCtx cb_ctx;
    if (params.stopcondition) cb_ctx.stopcondition = &params.stopcondition;
    if (params.progressind)   cb_ctx.progressind   = &params.progressind;
    in.user        = &cb_ctx;
    in.should_stop = params.stopcondition ? &sparrow_should_stop_cb : nullptr;
    in.on_progress = params.progressind ? &sparrow_on_progress_cb : nullptr;

    std::vector<sp_placement> out(items.size());

    // Empty name: the GUI prefixes it with "Arranging". Report 0 before and all
    // placed after, since ArrangeJob feeds the value to update_status(num_finished).
    if (params.progressind) params.progressind(0, "");
    const int rc = ::sparrow_arrange(&in, out.data());
    if (params.progressind) params.progressind(unsigned(n_movable), "");

    if (rc != 0) {
        BOOST_LOG_TRIVIAL(error) << "sparrow_arrange failed with rc=" << rc
                                 << ", falling back to the libnest2d arranger";
        return false;
    }

    // ---- write the result back --------------------------------------------
    // Same as arrange()'s tail, minus itemid: sparrow keeps caller ids.
    for (size_t k = 0; k < n_movable; ++k) {
        ArrangePolygon &ap = arrangables[movable_src[k]];
        ap.bed_idx         = out[k].bed_idx;
        if (ap.bed_idx == UNARRANGED)
            continue; // leave translation/rotation untouched, like libnest2d does
        ap.translation = {scaled<coord_t>(out[k].x) + bed_min.x(),
                          scaled<coord_t>(out[k].y) + bed_min.y()};
        ap.rotation    = out[k].rotation;

        // Raw footprint in bed coordinates for the bed and hole checks.
        Polygon w = ap.poly.contour;
        w.rotate(ap.rotation);
        w.translate(ap.translation.x() - bed_min.x(), ap.translation.y() - bed_min.y());
        const BoundingBox wb = w.bounding_box();
        BOOST_LOG_TRIVIAL(debug) << "sparrow: placed \"" << ap.name << "\" bed=" << ap.bed_idx
                                 << " bed-bbox (" << unscaled<double>(wb.min.x()) << ","
                                 << unscaled<double>(wb.min.y()) << ")-("
                                 << unscaled<double>(wb.max.x()) << ","
                                 << unscaled<double>(wb.max.y()) << ")";
    }

    return true;
}

}} // namespace Slic3r::arrangement
