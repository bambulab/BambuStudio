#include "FillRadialZigZag.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <mutex>
#include <utility>

#include "../BoundingBox.hpp"
#include "../ClipperUtils.hpp"
#include "../ExPolygon.hpp"
#include "../Line.hpp"
#include "../Point.hpp"
#include "../Polygon.hpp"

namespace Slic3r {
namespace FillRadialZigZag {

static constexpr int    kTestRays       = 360;
static constexpr int    kDenseRays      = 720;
// Among rays that actually hit material, how many have exactly one interval.
// Misses (open side / empty sector) are ignored — they are not a failure.
static constexpr double kValidThreshold = 0.90;
static constexpr int    kMinHitRays     = 8;
static constexpr double kMinSegMm       = 0.2;

namespace {

std::mutex               g_dbg_mutex;
bool                     g_dbg_on { false };
std::vector<LayerDiag>   g_dbg_log;

struct LockedIslandN
{
    Point  key;
    double size { 0. };
    int    n { 0 };
    double theta0 { 0. };
    Point  pole;
};
struct AxisLine
{
    bool   ok { false };
    double x0 { 0. };
    double ax { 0. };
    double y0 { 0. };
    double ay { 0. };
};

struct QuadBezier
{
    bool   ok { false };
    double z0 { 0. };
    double z1 { 0. };
    double p0x { 0. }, p1x { 0. }, p2x { 0. };
    double p0y { 0. }, p1y { 0. }, p2y { 0. };
};

std::mutex g_n_mutex;

struct LipMem
{
    Point pole;
    Point left;
    Point right;
    bool  ok { false };
};

struct RegionLock
{
    std::vector<std::vector<LockedIslandN>> n_by_layer;
    int                                     n_global { 0 };
    AxisLine                                axis;
    QuadBezier                              bezier;
    std::vector<double>                     zs;
    std::vector<Point>                      poles;
    std::map<size_t, std::vector<LipMem>>   lips_by_layer;
    // Modifier island before later volumes clip it. Rays are cast here.
    // Pairing and hops follow the final fill region (already inside walls).
    std::vector<std::vector<ExPolygon>>     cast_by_layer;
};

std::map<size_t, RegionLock> g_locks;

struct LockedScan
{
    int    n { 0 };
    double theta0 { 0. };
    Point  pole;
    bool   ok { false };
    bool   has_pole { false };
    size_t table_rows { 0 };
    size_t row_islands { 0 };
};

static constexpr double kNMatchMinMm  = 12.;
static constexpr double kPoleHoldMm   = 1.0;
static constexpr double kPoleSlewMm   = 0.5;

static double island_size(const ExPolygon &ex)
{
    const BoundingBox bb = get_extents(ex);
    return double(std::max(bb.size().x(), bb.size().y()));
}

static double n_match_limit(double size_a, double size_b)
{
    return std::max(scale_(kNMatchMinMm), 0.45 * std::max(size_a, size_b));
}

static double ang_diff(double a, double b)
{
    double d = a - b;
    while (d > PI)
        d -= 2. * PI;
    while (d < -PI)
        d += 2. * PI;
    return std::abs(d);
}

static int even_scan_count(int n)
{
    n = std::max(4, n);
    if (n % 2)
        ++n;
    return n;
}

static double estimate_mid_len(const ExPolygon &region)
{
    const double outer = region.contour.length();
    if (region.holes.empty())
        return 0.5 * outer;
    return 0.5 * (outer + region.holes.front().length());
}

static size_t layer_index(const Fill &fill)
{
    return fill.layer_id == size_t(-1) ? 0 : fill.layer_id;
}

static double line_spacing_mm(const Fill &fill, const FillParams &params)
{
    return fill.spacing / std::max(0.05, double(params.density));
}

static Point lerp_pt(const Point &a, const Point &b, double t)
{
    const double x = double(a.x()) + t * (double(b.x()) - double(a.x()));
    const double y = double(a.y()) + t * (double(b.y()) - double(a.y()));
    return Point(coord_t(std::lround(x)), coord_t(std::lround(y)));
}

static Point avg_pt(const Point &a, const Point &b)
{
    return lerp_pt(a, b, 0.5);
}

static double dist_pts(const Point &a, const Point &b)
{
    return (b - a).cast<double>().norm();
}

static Point raw_pole(const ExPolygon &ex)
{
    // Hole centroid is the radial center of a collar. Bbox center jumps when a
    // thin arm appears or the AABB resizes, even if the hole barely moved.
    if (!ex.holes.empty() && ex.holes.front().size() >= 3)
        return ex.holes.front().centroid();
    return get_extents(ex).center();
}

static Point hold_or_slew_pole(const Point &prev, const Point &raw)
{
    const double d = dist_pts(prev, raw);
    if (d <= scale_(kPoleHoldMm))
        return prev;
    const double step = scale_(kPoleSlewMm);
    if (d <= step)
        return raw;
    return lerp_pt(prev, raw, step / d);
}

static bool is_supported_topology(const ExPolygon &region)
{
    if (region.contour.size() < 3)
        return false;
    if (region.holes.size() > 1)
        return false;
    return region.holes.empty() || region.holes.front().size() >= 3;
}

static double size_mismatch(double a, double b)
{
    const double lo = std::max(1., std::min(a, b));
    return std::max(a, b) / lo;
}

static const RegionLock *find_lock(size_t region_id)
{
    auto it = g_locks.find(region_id);
    if (it != g_locks.end())
        return &it->second;
    if (region_id != 0) {
        it = g_locks.find(0);
        if (it != g_locks.end())
            return &it->second;
    }
    return nullptr;
}

static LipMem recall_lips(size_t region_id, size_t layer_id, const Point &pole)
{
    LipMem out;
    if (layer_id == 0)
        return out;
    std::lock_guard<std::mutex> lock(g_n_mutex);
    const RegionLock *rl = find_lock(region_id);
    if (rl == nullptr)
        return out;
    auto it = rl->lips_by_layer.find(layer_id - 1);
    if (it == rl->lips_by_layer.end())
        return out;
    const LipMem *best = nullptr;
    double        best_d = std::numeric_limits<double>::max();
    for (const LipMem &m : it->second) {
        if (!m.ok)
            continue;
        const double d = dist_pts(m.pole, pole);
        if (d < best_d) {
            best_d = d;
            best   = &m;
        }
    }
    if (best != nullptr && best_d < scale_(8.))
        return *best;
    return out;
}

static void store_lips(size_t region_id, size_t layer_id, LipMem mem)
{
    if (!mem.ok)
        return;
    std::lock_guard<std::mutex> lock(g_n_mutex);
    auto it = g_locks.find(region_id);
    if (it == g_locks.end() && region_id != 0)
        it = g_locks.find(0);
    if (it == g_locks.end())
        return;
    it->second.lips_by_layer[layer_id].push_back(std::move(mem));
}

static const ExPolygon *lookup_cast_island(size_t region_id, size_t layer_id, const ExPolygon &clip)
{
    std::lock_guard<std::mutex> lock(g_n_mutex);
    const RegionLock *rl = find_lock(region_id);
    if (rl == nullptr || layer_id >= rl->cast_by_layer.size())
        return nullptr;
    const std::vector<ExPolygon> &row = rl->cast_by_layer[layer_id];
    if (row.empty())
        return nullptr;
    if (row.size() == 1)
        return &row.front();
    const ExPolygon *best = nullptr;
    double           best_a = -1.;
    for (const ExPolygon &ex : row) {
        const ExPolygons hit = intersection_ex(ex, clip);
        double a = 0.;
        for (const ExPolygon &h : hit)
            a += std::abs(h.area());
        if (a > best_a) {
            best_a = a;
            best   = &ex;
        }
    }
    return best_a > 1. ? best : &row.front();
}

static LockedScan lookup_locked(size_t layer_id, const Point &key, double query_size, size_t region_id)
{
    std::lock_guard<std::mutex> lock(g_n_mutex);
    LockedScan out;
    const RegionLock *rl = find_lock(region_id);
    if (rl == nullptr)
        return out;
    out.table_rows = rl->n_by_layer.size();
    if (layer_id >= rl->n_by_layer.size())
        return out;
    const auto &row = rl->n_by_layer[layer_id];
    out.row_islands = row.size();
    if (row.empty())
        return out;
    const LockedIslandN *pick = nullptr;
    if (row.size() == 1) {
        pick = &row.front();
    } else {
        double best_score = std::numeric_limits<double>::max();
        for (const LockedIslandN &is : row) {
            if (size_mismatch(query_size, is.size) > 3.)
                continue;
            const double d   = dist_pts(key, is.key);
            const double lim = n_match_limit(query_size, is.size);
            if (d > lim)
                continue;
            const double score = size_mismatch(query_size, is.size) * scale_(100.) + d;
            if (score < best_score) {
                best_score = score;
                pick       = &is;
            }
        }
        if (pick == nullptr) {
            pick = &row.front();
            for (const LockedIslandN &is : row)
                if (is.size > pick->size)
                    pick = &is;
        }
    }
    out.n        = pick->n;
    out.theta0   = pick->theta0;
    out.pole     = pick->pole;
    out.ok       = true;
    out.has_pole = true;
    return out;
}

static AxisLine fit_axis_line(const std::vector<double> &zs, const std::vector<Point> &poles)
{
    AxisLine line;
    if (zs.size() < 2 || zs.size() != poles.size())
        return line;
    const double n = double(zs.size());
    double sum_z = 0., sum_z2 = 0., sum_x = 0., sum_y = 0., sum_zx = 0., sum_zy = 0.;
    for (size_t i = 0; i < zs.size(); ++i) {
        const double z = zs[i];
        const double x = unscale<double>(poles[i].x());
        const double y = unscale<double>(poles[i].y());
        sum_z  += z;
        sum_z2 += z * z;
        sum_x  += x;
        sum_y  += y;
        sum_zx += z * x;
        sum_zy += z * y;
    }
    const double denom = n * sum_z2 - sum_z * sum_z;
    if (std::abs(denom) < 1e-12) {
        line.x0 = sum_x / n;
        line.y0 = sum_y / n;
        line.ok = true;
        return line;
    }
    line.ax = (n * sum_zx - sum_z * sum_x) / denom;
    line.ay = (n * sum_zy - sum_z * sum_y) / denom;
    line.x0 = (sum_x - line.ax * sum_z) / n;
    line.y0 = (sum_y - line.ay * sum_z) / n;
    line.ok = true;
    return line;
}

static bool lookup_axis(double z_mm, Point &out, size_t region_id)
{
    std::lock_guard<std::mutex> lock(g_n_mutex);
    const RegionLock *rl = find_lock(region_id);
    if (rl == nullptr || !rl->axis.ok)
        return false;
    const AxisLine &axis = rl->axis;
    out = Point::new_scale(axis.x0 + axis.ax * z_mm, axis.y0 + axis.ay * z_mm);
    return true;
}

static bool solve3(const double A[3][3], const double b[3], double x[3])
{
    double M[3][4] = {
        { A[0][0], A[0][1], A[0][2], b[0] },
        { A[1][0], A[1][1], A[1][2], b[1] },
        { A[2][0], A[2][1], A[2][2], b[2] },
    };
    for (int col = 0; col < 3; ++col) {
        int piv = col;
        for (int r = col + 1; r < 3; ++r)
            if (std::abs(M[r][col]) > std::abs(M[piv][col]))
                piv = r;
        if (std::abs(M[piv][col]) < 1e-12)
            return false;
        if (piv != col)
            for (int c = col; c < 4; ++c)
                std::swap(M[col][c], M[piv][c]);
        const double d = M[col][col];
        for (int c = col; c < 4; ++c)
            M[col][c] /= d;
        for (int r = 0; r < 3; ++r) {
            if (r == col)
                continue;
            const double f = M[r][col];
            for (int c = col; c < 4; ++c)
                M[r][c] -= f * M[col][c];
        }
    }
    x[0] = M[0][3];
    x[1] = M[1][3];
    x[2] = M[2][3];
    return true;
}

static QuadBezier bezier_from_line(const AxisLine &line, double z0, double z1)
{
    QuadBezier q;
    if (!line.ok)
        return q;
    q.z0  = z0;
    q.z1  = z1;
    q.p0x = line.x0 + line.ax * z0;
    q.p0y = line.y0 + line.ay * z0;
    q.p2x = line.x0 + line.ax * z1;
    q.p2y = line.y0 + line.ay * z1;
    q.p1x = 0.5 * (q.p0x + q.p2x);
    q.p1y = 0.5 * (q.p0y + q.p2y);
    q.ok  = true;
    return q;
}

// Spatial quadratic Bezier through bounding-box centers, parameterized by z.
// B(t) = (1-t)^2 P0 + 2(1-t)t P1 + t^2 P2, t = (z-zmin)/(zmax-zmin).
static QuadBezier fit_bbox_bezier(const std::vector<double> &zs, const std::vector<Point> &bbs)
{
    QuadBezier q;
    if (zs.size() < 2 || zs.size() != bbs.size())
        return q;
    double zmin = zs.front();
    double zmax = zs.front();
    for (double z : zs) {
        zmin = std::min(zmin, z);
        zmax = std::max(zmax, z);
    }
    if (zmax - zmin < 1e-9)
        return bezier_from_line(fit_axis_line(zs, bbs), zmin, zmax);
    if (zs.size() < 3)
        return bezier_from_line(fit_axis_line(zs, bbs), zmin, zmax);

    double A[3][3] = {};
    double bx[3]   = {};
    double by[3]   = {};
    for (size_t i = 0; i < zs.size(); ++i) {
        const double t    = (zs[i] - zmin) / (zmax - zmin);
        const double u    = 1. - t;
        const double w[3] = { u * u, 2. * u * t, t * t };
        const double x    = unscale<double>(bbs[i].x());
        const double y    = unscale<double>(bbs[i].y());
        for (int r = 0; r < 3; ++r) {
            bx[r] += w[r] * x;
            by[r] += w[r] * y;
            for (int c = 0; c < 3; ++c)
                A[r][c] += w[r] * w[c];
        }
    }
    double px[3] = {};
    double py[3] = {};
    if (!solve3(A, bx, px) || !solve3(A, by, py))
        return bezier_from_line(fit_axis_line(zs, bbs), zmin, zmax);
    q.ok  = true;
    q.z0  = zmin;
    q.z1  = zmax;
    q.p0x = px[0];
    q.p1x = px[1];
    q.p2x = px[2];
    q.p0y = py[0];
    q.p1y = py[1];
    q.p2y = py[2];
    return q;
}

static bool lookup_bezier(double z_mm, Point &out, size_t region_id)
{
    std::lock_guard<std::mutex> lock(g_n_mutex);
    const RegionLock *rl = find_lock(region_id);
    if (rl == nullptr || !rl->bezier.ok)
        return false;
    const QuadBezier &bz = rl->bezier;
    double t = 0.;
    if (bz.z1 - bz.z0 > 1e-12)
        t = (z_mm - bz.z0) / (bz.z1 - bz.z0);
    t = std::max(0., std::min(1., t));
    const double u = 1. - t;
    const double x = u * u * bz.p0x + 2. * u * t * bz.p1x + t * t * bz.p2x;
    const double y = u * u * bz.p0y + 2. * u * t * bz.p1y + t * t * bz.p2y;
    out = Point::new_scale(x, y);
    return true;
}

static double ray_length_scaled(const ExPolygon &region, const Point &pole)
{
    const BoundingBox bb = get_extents(region);
    const double      span = double(std::max(bb.size().x(), bb.size().y()));
    return span + dist_pts(pole, bb.center()) + double(scaled<coord_t>(4.));
}

static Point along_dir(const Point &pole, const Vec2d &dir, double dist)
{
    return Point(coord_t(std::lround(double(pole.x()) + dir.x() * dist)),
                 coord_t(std::lround(double(pole.y()) + dir.y() * dist)));
}

// One-way ray t>=0 clipped to Omega. Valid iff exactly one thick material interval.
// A unique graze too thin/short to be a scan is kept as lip (the far hit point)
// so the zigzag can connect to that one point instead of inventing a spoke.
// Pole in a hole: interval is [inner wall, outer wall] (two boundary hits).
// Pole inside solid: interval is the half-chord [near pole, exit] (one boundary hit).
static bool cast_ray(const ExPolygon &region, const Point &pole, double theta, double hub_scaled, RadialRay &ray)
{
    ray        = RadialRay{};
    ray.theta  = theta;
    const Vec2d dir(std::cos(theta), std::sin(theta));
    const double len = ray_length_scaled(region, pole);
    Point        tip = along_dir(pole, dir, len);
    if (tip == pole)
        return false;

    Polyline probe;
    probe.points = { pole, tip };
    Polylines hits = intersection_pl(probe, region);
    hits.erase(std::remove_if(hits.begin(), hits.end(),
                              [](const Polyline &pl) { return pl.size() < 2; }),
               hits.end());
    const double min_len = scale_(kMinSegMm);
    Polylines    long_hits = hits;
    long_hits.erase(std::remove_if(long_hits.begin(), long_hits.end(),
                                   [min_len](const Polyline &pl) { return pl.length() < min_len; }),
                    long_hits.end());
    if (long_hits.size() != 1 && hits.size() != 1)
        return false;
    Polyline hit = (long_hits.size() == 1) ? long_hits.front() : hits.front();
    if (dist_pts(hit.front(), pole) > dist_pts(hit.back(), pole))
        hit.reverse();

    ray.inner        = hit.front();
    ray.outer        = hit.back();
    const bool pole_inside = region.contains(pole);
    if (pole_inside && hub_scaled > 1.) {
        if (dist_pts(ray.outer, pole) <= hub_scaled + min_len)
            return false;
        Point hub = along_dir(pole, dir, hub_scaled);
        if (region.contains(hub) && dist_pts(hub, pole) + min_len * 0.25 < dist_pts(ray.outer, pole))
            ray.inner = hub;
    }
    ray.mid          = avg_pt(ray.inner, ray.outer);
    ray.inner_radius = dist_pts(ray.inner, pole);
    ray.outer_radius = dist_pts(ray.outer, pole);
    ray.valid        = long_hits.size() == 1 &&
                ray.outer_radius > ray.inner_radius + min_len * 0.25;
    if (!ray.valid) {
        ray.lip          = true;
        ray.inner        = ray.outer;
        ray.mid          = ray.outer;
        ray.inner_radius = ray.outer_radius;
    }
    return ray.valid;
}

// Fill region is inset from the slice. A graze on the cut can miss Omega but
// still nick the outset. Keep that as a single lip point on Omega's contour —
// never promote it to a scan.
static void maybe_outset_lip(const ExPolygon &region, const ExPolygons &fat, const Point &pole,
                             double hub_scaled, RadialRay &ray)
{
    if (ray.valid || ray.lip || fat.empty())
        return;
    RadialRay fr;
    if (!cast_ray(fat.front(), pole, ray.theta, hub_scaled, fr) && !fr.lip)
        return;
    const Point on_fill = region.point_projection(fr.outer);
    if (dist_pts(on_fill, fr.outer) > scale_(2.0))
        return;
    ray.lip          = true;
    ray.valid        = false;
    ray.outer        = on_fill;
    ray.inner        = on_fill;
    ray.mid          = on_fill;
    ray.outer_radius = dist_pts(on_fill, pole);
    ray.inner_radius = ray.outer_radius;
}

enum class RayClass { Miss, One, Multi };

static RayClass classify_ray(const ExPolygon &region, const Point &pole, double theta)
{
    const Vec2d dir(std::cos(theta), std::sin(theta));
    const double len = ray_length_scaled(region, pole);
    Point        tip = along_dir(pole, dir, len);
    if (tip == pole)
        return RayClass::Miss;

    Polyline probe;
    probe.points = { pole, tip };
    Polylines hits = intersection_pl(probe, region);
    const double min_len = scale_(kMinSegMm);
    hits.erase(std::remove_if(hits.begin(), hits.end(),
                              [min_len](const Polyline &pl) { return pl.size() < 2 || pl.length() < min_len; }),
               hits.end());
    if (hits.empty())
        return RayClass::Miss;
    if (hits.size() == 1)
        return RayClass::One;
    return RayClass::Multi;
}

// R = (# one-interval hits) / (# rays that hit any material).
// A miss means "no material in this direction" (open side of a U), not a bad pole.
static double evaluate_pole(const ExPolygon &region, const Point &pole, int n_test)
{
    int n_one = 0, n_multi = 0;
    for (int i = 0; i < n_test; ++i) {
        switch (classify_ray(region, pole, 2. * PI * double(i) / double(n_test))) {
        case RayClass::One:   ++n_one; break;
        case RayClass::Multi: ++n_multi; break;
        case RayClass::Miss:  break;
        }
    }
    const int n_hit = n_one + n_multi;
    if (n_one < kMinHitRays || n_hit <= 0)
        return 0.;
    return double(n_one) / double(n_hit);
}

static std::vector<Point> pole_candidates(const ExPolygon &region)
{
    std::vector<Point> out;
    const Point bb_c    = get_extents(region).center();
    const Point outer_c = region.contour.centroid();
    auto push_unique = [&](const Point &p) {
        for (const Point &q : out)
            if (dist_pts(p, q) < scale_(0.05))
                return;
        out.push_back(p);
    };
    push_unique(bb_c);
    push_unique(outer_c);
    if (!region.holes.empty()) {
        const Point inner_c = region.holes.front().centroid();
        push_unique(inner_c);
        push_unique(avg_pt(inner_c, outer_c));
    }
    return out;
}

static bool pick_pole(const ExPolygon &region, bool has_lock, const Point &pole_lock,
                      Point &out_pole, double &out_r)
{
    if (has_lock) {
        out_r = evaluate_pole(region, pole_lock, kTestRays);
        if (out_r >= kValidThreshold) {
            out_pole = pole_lock;
            return true;
        }
        const Point raw = raw_pole(region);
        for (double t : { 0.25, 0.5, 0.75, 1.0 }) {
            const Point p = lerp_pt(pole_lock, raw, t);
            const double r = evaluate_pole(region, p, kTestRays);
            if (r >= kValidThreshold) {
                out_pole = p;
                out_r    = r;
                return true;
            }
        }
    }
    const auto candidates = pole_candidates(region);
    out_pole = candidates.empty() ? get_extents(region).center() : candidates.front();
    out_r    = evaluate_pole(region, out_pole, kTestRays);
    if (out_r >= kValidThreshold)
        return true;
    for (size_t i = 1; i < candidates.size(); ++i) {
        const double r = evaluate_pole(region, candidates[i], kTestRays);
        if (r > out_r) {
            out_r    = r;
            out_pole = candidates[i];
        }
    }
    return out_r >= kValidThreshold;
}

static std::vector<RadialRay> dense_rays(const ExPolygon &region, const Point &pole, double hub_scaled, int n)
{
    std::vector<RadialRay> rays;
    rays.reserve(size_t(n));
    for (int i = 0; i < n; ++i) {
        RadialRay ray;
        if (cast_ray(region, pole, 2. * PI * double(i) / double(n), hub_scaled, ray))
            rays.push_back(ray);
    }
    return rays;
}

struct MidSample
{
    double s { 0. };
    double theta { 0. };
    Point  mid;
};

static bool build_mid_samples(const std::vector<RadialRay> &rays, std::vector<MidSample> &samples,
                              double &length, bool &closed)
{
    samples.clear();
    length = 0.;
    closed = false;
    if (rays.size() < 8)
        return false;

    samples.reserve(rays.size());
    for (size_t i = 0; i < rays.size(); ++i)
        samples.push_back({ 0., rays[i].theta, rays[i].mid });

    double max_internal = 0.;
    for (size_t i = 0; i + 1 < rays.size(); ++i) {
        const double d = dist_pts(rays[i].mid, rays[i + 1].mid);
        samples[i + 1].s = samples[i].s + d;
        max_internal = std::max(max_internal, d);
    }
    const double wrap_d = dist_pts(rays.back().mid, rays.front().mid);
    // Full ring: wrap is comparable to other steps. Open U/C: wrap jumps the empty side.
    closed = wrap_d <= std::max(scale_(kMinSegMm), 2.5 * max_internal);
    length = samples.back().s;
    if (closed)
        length += wrap_d;
    return length > scale_(kMinSegMm);
}

static double wrap_theta(double th)
{
    while (th < 0.)
        th += 2. * PI;
    while (th >= 2. * PI)
        th -= 2. * PI;
    return th;
}

static double ccw_from(double from, double to)
{
    double d = wrap_theta(to) - wrap_theta(from);
    if (d < 0.)
        d += 2. * PI;
    return d;
}

static double sample_theta_at_s(const std::vector<MidSample> &samples, double length, double s, bool closed)
{
    if (samples.empty() || length <= 1.)
        return 0.;
    if (closed) {
        s = std::fmod(s, length);
        if (s < 0.)
            s += length;
    } else {
        s = std::max(0., std::min(s, length));
    }

    const size_t nseg = closed ? samples.size() : (samples.size() > 0 ? samples.size() - 1 : 0);
    for (size_t i = 0; i < nseg; ++i) {
        const MidSample &a = samples[i];
        const MidSample &b = samples[(i + 1) % samples.size()];
        const double     s0 = a.s;
        const double     s1 = (closed && i + 1 == samples.size()) ? length : b.s;
        if (s > s1 + 1e-9)
            continue;
        const double span = std::max(1., s1 - s0);
        const double t    = std::max(0., std::min(1., (s - s0) / span));
        double       th0  = a.theta;
        double       th1  = b.theta;
        double dth = th1 - th0;
        if (dth > PI)
            dth -= 2. * PI;
        if (dth < -PI)
            dth += 2. * PI;
        return wrap_theta(th0 + t * dth);
    }
    return wrap_theta(samples.back().theta);
}

static double s_for_theta(const std::vector<MidSample> &samples, double length, bool closed, double target)
{
    if (samples.empty())
        return 0.;
    const size_t n    = samples.size();
    const size_t nseg = closed ? n : (n > 0 ? n - 1 : 0);
    double       best_s = samples.front().s;
    double       best_d = ang_diff(samples.front().theta, target);
    for (size_t i = 0; i < nseg; ++i) {
        const MidSample &a  = samples[i];
        const MidSample &b  = samples[(i + 1) % n];
        const double     s0 = a.s;
        const double     s1 = (closed && i + 1 == n) ? length : b.s;
        const double     d_ab = ccw_from(a.theta, b.theta);
        const double     d_at = ccw_from(a.theta, target);
        if (d_ab > 1e-12 && d_at <= d_ab + 1e-12)
            return s0 + (d_at / d_ab) * (s1 - s0);
        const double d = ang_diff(a.theta, target);
        if (d < best_d) {
            best_d = d;
            best_s = a.s;
        }
    }
    return best_s;
}

static bool locate_on_polygon(const Polygon &poly, const Point &p, size_t &seg, double &t)
{
    const Points &pts = poly.points;
    if (pts.size() < 2)
        return false;
    double best = std::numeric_limits<double>::max();
    seg = 0;
    t   = 0.;
    const size_t n = pts.size();
    for (size_t i = 0; i < n; ++i) {
        const Point &a = pts[i];
        const Point &b = pts[(i + 1) % n];
        Vec2d        ab = (b - a).cast<double>();
        const double len2 = ab.squaredNorm();
        double       u = 0.;
        if (len2 > 1.)
            u = std::max(0., std::min(1., (p - a).cast<double>().dot(ab) / len2));
        const Point  q = lerp_pt(a, b, u);
        const double d = dist_pts(p, q);
        if (d < best) {
            best = d;
            seg  = i;
            t    = u;
        }
    }
    return best < scale_(3.);
}

static double dist_to_polygon(const Polygon &poly, const Point &p)
{
    size_t s = 0;
    double t = 0.;
    if (!locate_on_polygon(poly, p, s, t))
        return std::numeric_limits<double>::max();
    const Points &pts = poly.points;
    const Point   q   = lerp_pt(pts[s], pts[(s + 1) % pts.size()], t);
    return dist_pts(p, q);
}

static Polyline walk_forward(const Points &pts, size_t sa, double ta, size_t sb, double tb)
{
    const size_t n = pts.size();
    auto at = [&](size_t s, double u) {
        return lerp_pt(pts[s], pts[(s + 1) % n], u);
    };
    Polyline pl;
    pl.append(at(sa, ta));
    if (sa == sb && tb + 1e-9 >= ta) {
        pl.append(at(sb, tb));
        return pl;
    }
    size_t i = (sa + 1) % n;
    pl.append(pts[i]);
    while (i != sb) {
        i = (i + 1) % n;
        pl.append(pts[i]);
    }
    pl.append(at(sb, tb));
    return pl;
}

static Polyline walk_backward(const Points &pts, size_t sa, double ta, size_t sb, double tb)
{
    const size_t n = pts.size();
    auto at = [&](size_t s, double u) {
        return lerp_pt(pts[s], pts[(s + 1) % n], u);
    };
    Polyline pl;
    pl.append(at(sa, ta));
    if (sa == sb && ta + 1e-9 >= tb) {
        pl.append(at(sb, tb));
        return pl;
    }
    pl.append(pts[sa]);
    size_t i = sa;
    while (i != (sb + 1) % n) {
        i = (i + n - 1) % n;
        pl.append(pts[i]);
    }
    pl.append(at(sb, tb));
    return pl;
}

static bool locate_or_project(const Polygon &poly, Point p, size_t &seg, double &t)
{
    if (locate_on_polygon(poly, p, seg, t))
        return true;
    p = poly.point_projection(p);
    return locate_on_polygon(poly, p, seg, t);
}

static Polyline shorter_boundary_arc(const Polygon &poly, const Point &a, const Point &b)
{
    size_t sa = 0, sb = 0;
    double ta = 0., tb = 0.;
    if (!locate_or_project(poly, a, sa, ta) || !locate_or_project(poly, b, sb, tb))
        return Polyline{};
    Polyline fwd = walk_forward(poly.points, sa, ta, sb, tb);
    Polyline bak = walk_backward(poly.points, sa, ta, sb, tb);
    if (fwd.size() < 2)
        return bak;
    if (bak.size() < 2)
        return fwd;
    return (fwd.length() <= bak.length()) ? fwd : bak;
}

// Length-weighted |R - r0|. Vertex-mean radius is biased by tessellation: a
// concave outer dent can have many low-R verts, so the short same-rim walk
// looks "inner" and the long way around the C wins — then arc_lim rejects it
// and the adjacent 铆线 disappears (layers 1–154 on an open vamp).
static double radius_mad_len(const Polyline &pl, const Point &pole, double r0)
{
    if (pl.size() < 2)
        return std::numeric_limits<double>::max();
    double acc = 0., L = 0.;
    for (size_t i = 1; i < pl.points.size(); ++i) {
        const double len = dist_pts(pl.points[i - 1], pl.points[i]);
        if (len < 1.)
            continue;
        const double r = 0.5 * (dist_pts(pl.points[i - 1], pole) + dist_pts(pl.points[i], pole));
        acc += std::abs(r - r0) * len;
        L   += len;
    }
    return L > 1. ? acc / L : std::numeric_limits<double>::max();
}

static bool polyline_has_radial_cut(const Polyline &pl, const Point &pole)
{
    if (pl.size() < 2)
        return false;
    double rmin = dist_pts(pl.front(), pole);
    double rmax = rmin;
    bool   radial_seg = false;
    for (size_t i = 0; i < pl.points.size(); ++i) {
        const double r = dist_pts(pl.points[i], pole);
        rmin = std::min(rmin, r);
        rmax = std::max(rmax, r);
        if (i == 0)
            continue;
        const double len = dist_pts(pl.points[i - 1], pl.points[i]);
        if (len < scale_(1.2))
            continue;
        Vec2d v = (pl.points[i] - pl.points[i - 1]).cast<double>();
        Vec2d rad = (pl.points[i - 1] - pole).cast<double>();
        const double rn = rad.norm();
        if (rn < 1.)
            continue;
        if (std::abs(v.dot(rad) / (len * rn)) > 0.88)
            radial_seg = true;
    }
    // A real cut walks inner↔outer (several mm of radius). A dented O-ring hop
    // can have one locally-radial edge without leaving its rim.
    return radial_seg && (rmax - rmin) > scale_(2.0);
}

// On a C (one contour, no hole) the inner and outer rims share the same polygon.
// Stay on the rim the endpoints already sit on (radius of a,b). Never walk the
// radial cut (inner↔outer) — that is the opening 回环.
static Polyline same_side_boundary_arc(const Polygon &poly, const Point &a, const Point &b,
                                       const Point &pole, bool /*want_outer*/)
{
    size_t sa = 0, sb = 0;
    double ta = 0., tb = 0.;
    if (!locate_or_project(poly, a, sa, ta) || !locate_or_project(poly, b, sb, tb))
        return Polyline{};
    Polyline fwd = walk_forward(poly.points, sa, ta, sb, tb);
    Polyline bak = walk_backward(poly.points, sa, ta, sb, tb);
    const bool fwd_cut = polyline_has_radial_cut(fwd, pole);
    const bool bak_cut = polyline_has_radial_cut(bak, pole);
    if (fwd_cut && !bak_cut)
        return bak.size() >= 2 ? bak : Polyline{};
    if (bak_cut && !fwd_cut)
        return fwd.size() >= 2 ? fwd : Polyline{};
    if (fwd_cut && bak_cut)
        return Polyline{};
    if (fwd.size() < 2)
        return bak;
    if (bak.size() < 2)
        return fwd;
    const double r0 = 0.5 * (dist_pts(a, pole) + dist_pts(b, pole));
    const double df = radius_mad_len(fwd, pole, r0);
    const double db = radius_mad_len(bak, pole, r0);
    if (std::abs(df - db) < scale_(0.35))
        return (fwd.length() <= bak.length()) ? fwd : bak;
    return df <= db ? fwd : bak;
}

static bool step_is_radial_cut(const Point &a, const Point &b, const Point &pole)
{
    const double len = dist_pts(a, b);
    if (len < scale_(0.4))
        return false;
    const double dr = std::abs(dist_pts(b, pole) - dist_pts(a, pole));
    // Stay on the rim: a slightly diagonal contour edge is not the opening cut.
    if (dr < scale_(0.8))
        return false;
    Vec2d v = (b - a).cast<double>();
    Vec2d r = (a - pole).cast<double>();
    const double rn = r.norm();
    if (rn < 1.)
        return false;
    const double al = std::abs(v.dot(r) / (len * rn));
    return (al > 0.85 && len >= scale_(1.2)) || (dr / len > 0.65 && dr > scale_(1.2));
}

// Walk the rim from `from` until the opening cut. Keep the corner vertex;
// do not step onto the radial cut (inner↔outer).
static Polyline rim_extend(const Polygon &poly, const Point &from, bool forward, const Point &pole,
                           double r0, double band, double max_len)
{
    size_t s = 0;
    double t = 0.;
    Point  on = from;
    if (!locate_on_polygon(poly, from, s, t)) {
        on = poly.point_projection(from);
        if (!locate_on_polygon(poly, on, s, t))
            return {};
    }
    const Points &pts = poly.points;
    const size_t  n   = pts.size();
    if (n < 3)
        return {};
    Polyline pl;
    pl.append(from);
    double len  = 0.;
    Point  prev = from;
    if (dist_pts(from, on) >= scale_(0.05)) {
        pl.append(on);
        prev = on;
    }
    size_t i     = forward ? (s + 1) % n : s;
    int    guard = int(n) + 2;
    while (guard-- > 0) {
        const Point  q    = pts[i];
        const double step = dist_pts(prev, q);
        if (step < 1.) {
            i = forward ? (i + 1) % n : (i + n - 1) % n;
            continue;
        }
        if (step_is_radial_cut(prev, q, pole))
            break;
        if (std::abs(dist_pts(q, pole) - r0) > band)
            break;
        if (len + step > max_len) {
            const double u = (max_len - len) / step;
            const Point  w = lerp_pt(prev, q, u);
            if (!step_is_radial_cut(prev, w, pole) && std::abs(dist_pts(w, pole) - r0) <= band)
                pl.append(w);
            break;
        }
        pl.append(q);
        len += step;
        prev = q;
        i = forward ? (i + 1) % n : (i + n - 1) % n;
    }
    return pl.size() >= 2 ? pl : Polyline{};
}

static double point_theta(const Point &p, const Point &pole)
{
    return std::atan2(double(p.y() - pole.y()), double(p.x() - pole.x()));
}

static bool connector_inside(const Point &a, const Point &b, const ExPolygon &region, const ExPolygons &fat, double max_len)
{
    const double len = dist_pts(a, b);
    if (len < scale_(0.05))
        return true;
    if (len > max_len)
        return false;
    if (region.contains(Line(a, b)))
        return true;
    for (const ExPolygon &ex : fat)
        if (ex.contains(Line(a, b)))
            return true;
    return false;
}

static bool on_hub_radius(const Point &p, const Point &pole, double hub_cut_scaled, double tol_mm = 4.5)
{
    if (hub_cut_scaled <= 1.)
        return false;
    return std::abs(dist_pts(p, pole) - hub_cut_scaled) < scale_(tol_mm);
}

// Multi-vertex walk that stays on circle B. A 2-point 铆线 is not this.
static bool polyline_follows_hub(const Polyline &pl, const Point &pole, double hub_cut_scaled)
{
    if (hub_cut_scaled <= 1. || pl.size() < 3)
        return false;
    double on = 0., tot = 0.;
    int    n_hub = 0;
    for (const Point &p : pl.points)
        if (on_hub_radius(p, pole, hub_cut_scaled, 3.5))
            ++n_hub;
    for (size_t i = 1; i < pl.points.size(); ++i) {
        const double len = dist_pts(pl.points[i - 1], pl.points[i]);
        if (len < 1.)
            continue;
        tot += len;
        const double r = 0.5 * (dist_pts(pl.points[i - 1], pole) + dist_pts(pl.points[i], pole));
        if (std::abs(r - hub_cut_scaled) < scale_(3.5))
            on += len;
    }
    if (n_hub >= 3)
        return true;
    return on > scale_(6.) || (tot > scale_(4.) && on > 0.55 * tot);
}

static bool make_connector(const Point &a, const Point &b, bool toward_outer,
                           const ExPolygon &region, const ExPolygons &fat, double max_len,
                           const Point &pole, Polyline &out, double hub_cut_scaled = 0.)
{
    out = Polyline{};
    const double chord = dist_pts(a, b);
    if (chord < scale_(0.05))
        return true;

    auto try_straight = [&]() {
        // Adjacent rims at ~45 mm / 90 rays are ~3 mm apart; 4.5×spacing can be
        // shorter than that when infill is dense, so always allow this chord.
        const double lim = std::max(max_len, std::min(chord * 1.1, scale_(8.)));
        if (!connector_inside(a, b, region, fat, lim))
            return false;
        out.points = { a, b };
        return true;
    };

    // Inner 铆线 at circle B: short a–b chord. Do not walk the punched hole
    // or the C-shaped circular bite (that extra stroke = 大圆边).
    if (on_hub_radius(a, pole, hub_cut_scaled) || on_hub_radius(b, pole, hub_cut_scaled)) {
        if (try_straight())
            return true;
        return false;
    }

    if (try_straight())
        return true;

    auto finish_arc = [&](Polyline arc, double arc_lim) {
        if (arc.size() < 2 || arc.length() > arc_lim)
            return false;
        if (arc.front() != a)
            arc.append_before(a);
        if (arc.back() != b)
            arc.append(b);
        if (polyline_follows_hub(arc, pole, hub_cut_scaled))
            return false;
        out = std::move(arc);
        return out.size() >= 2;
    };

    // Chord left the island (C opening, real inner hole). Walk the real contour,
    // not an inset copy — stitching onto a 0.08 mm inset made a dogleg at each end.
    if (!region.holes.empty()) {
        if (hub_cut_scaled > 1. && !toward_outer)
            return false;
        const Polygon &poly = toward_outer ? region.contour : region.holes.front();
        const double arc_lim = std::max(max_len * 3., 0.35 * poly.length());
        if (finish_arc(shorter_boundary_arc(poly, a, b), arc_lim))
            return true;
        return false;
    }

    // C / U: both endpoints sit on the one contour (inner and outer rims share
    // it). Do not walk a split inset fragment: that snaps hops onto the other rim.
    const Polygon &cpoly = region.contour;
    const bool both_on_rim = dist_to_polygon(cpoly, a) < scale_(1.2) &&
                             dist_to_polygon(cpoly, b) < scale_(1.2);
    if (both_on_rim) {
        const double arc_lim = std::max({ max_len * 3., 12. * chord, 0.42 * cpoly.length() });
        if (finish_arc(same_side_boundary_arc(cpoly, a, b, pole, toward_outer), arc_lim) &&
            !polyline_has_radial_cut(out, pole))
            return true;
        Polyline sh = shorter_boundary_arc(cpoly, a, b);
        if (!polyline_has_radial_cut(sh, pole) && finish_arc(std::move(sh), arc_lim) &&
            !polyline_has_radial_cut(out, pole))
            return true;
    }
    return false;
}

static bool path_inside_region(const Polylines &paths, const ExPolygon &region)
{
    const ExPolygons fat = offset_ex(region, float(scale_(0.45)));
    for (const Polyline &pl : paths) {
        for (size_t i = 1; i < pl.points.size(); ++i) {
            const Line ln(pl.points[i - 1], pl.points[i]);
            if (dist_pts(ln.a, ln.b) < scale_(0.05))
                continue;
            bool ok = region.contains(ln);
            if (!ok) {
                for (const ExPolygon &ex : fat) {
                    if (ex.contains(ln)) {
                        ok = true;
                        break;
                    }
                }
            }
            if (!ok)
                return false;
        }
    }
    return !paths.empty();
}

static int wrap_idx(int i, int n, bool closed)
{
    if (n <= 0)
        return -1;
    if (closed)
        return (i % n + n) % n;
    if (i < 0 || i >= n)
        return -1;
    return i;
}

static bool clip_scan_chord(const Point &from, const Point &to, const ExPolygon &region, Point &a, Point &b)
{
    if (from == to)
        return false;
    Polyline probe;
    probe.points = { from, to };
    Polylines hits = intersection_pl(probe, region);
    const double min_len = scale_(kMinSegMm);
    hits.erase(std::remove_if(hits.begin(), hits.end(),
                              [min_len](const Polyline &pl) { return pl.size() < 2 || pl.length() < min_len; }),
               hits.end());
    if (hits.empty())
        return false;
    // Prefer a 2-point radial clip. A long hole-walk (64-gon around circle B)
    // is longer than the real scan and used to become the infill path.
    const Polyline *best = &hits.front();
    for (const Polyline &pl : hits) {
        const bool simple      = pl.size() <= 3;
        const bool best_simple = best->size() <= 3;
        if (simple != best_simple) {
            if (simple)
                best = &pl;
            continue;
        }
        if (pl.length() > best->length())
            best = &pl;
    }
    a = best->front();
    b = best->back();
    if (dist_pts(a, from) > dist_pts(b, from))
        std::swap(a, b);
    return dist_pts(a, b) >= min_len;
}

static bool in_circular(int i, int a, int b, int n)
{
    if (a <= b)
        return i >= a && i <= b;
    return i >= a || i <= b;
}

static bool longest_invalid_run(const std::vector<char> &valid, int &gap_a, int &gap_b)
{
    const int n = int(valid.size());
    gap_a = gap_b = -1;
    if (n < 3)
        return false;
    int best = 0;
    for (int i = 0; i < n; ++i) {
        if (valid[size_t(i)])
            continue;
        if (valid[size_t((i + n - 1) % n)]) {
            int len = 0, j = i;
            while (len < n && !valid[size_t(j)]) {
                j = (j + 1) % n;
                ++len;
            }
            if (len > best) {
                best  = len;
                gap_a = i;
                gap_b = (i + len - 1 + n) % n;
            }
        }
    }
    return best >= 1;
}

static Polyline trim_to_len(Polyline pl, double max_len)
{
    if (pl.size() < 2)
        return {};
    const double L = pl.length();
    if (L <= max_len + 1.)
        return pl;
    pl.clip_end(L - max_len);
    return pl.size() >= 2 ? pl : Polyline{};
}

static const Polygon *poly_for_point(const ExPolygon &region, const Point &p)
{
    size_t s = 0;
    double t = 0.;
    if (locate_on_polygon(region.contour, p, s, t))
        return &region.contour;
    for (const Polygon &h : region.holes)
        if (locate_on_polygon(h, p, s, t))
            return &h;
    return nullptr;
}

// Walk the island contour from `from` toward `toward`. Used to reach a single
// lip hit, or to nudge the last endpoint a short way when this layer missed.
static Polyline cap_along_edge(const Point &from, const Point &toward, const ExPolygon &region,
                               double max_len, bool reach_target)
{
    const Polygon *poly = poly_for_point(region, from);
    Point          dest = toward;
    if (poly == nullptr)
        poly = poly_for_point(region, toward);
    if (poly == nullptr)
        return {};
    size_t ds = 0;
    double dt = 0.;
    if (!locate_on_polygon(*poly, toward, ds, dt))
        dest = poly->point_projection(toward);
    Polyline arc = shorter_boundary_arc(*poly, from, dest);
    if (arc.size() < 2)
        return {};
    if (dist_pts(arc.front(), from) > scale_(0.05))
        arc.append_before(from);
    Polyline cap = trim_to_len(std::move(arc), max_len);
    if (!reach_target && cap.size() >= 2 && dist_pts(cap.back(), dest) < scale_(0.4))
        cap.clip_end(scale_(0.4));
    return cap.size() >= 2 ? cap : Polyline{};
}

static void prepend_cap(Polyline &dst, Polyline cap)
{
    if (cap.size() < 2 || dst.size() < 2)
        return;
    if (dist_pts(cap.back(), dst.front()) > dist_pts(cap.front(), dst.front()))
        cap.reverse();
    if (cap.size() >= 2 && dist_pts(cap.back(), dst.front()) < scale_(0.15))
        cap.points.pop_back();
    cap.points.insert(cap.points.end(), dst.points.begin(), dst.points.end());
    dst.points.swap(cap.points);
}

static void append_cap(Polyline &dst, Polyline cap)
{
    if (cap.size() < 2 || dst.size() < 2)
        return;
    if (dist_pts(cap.front(), dst.back()) > dist_pts(cap.back(), dst.back()))
        cap.reverse();
    size_t i0 = 0;
    if (dist_pts(cap.front(), dst.back()) < scale_(0.15))
        i0 = 1;
    for (size_t i = i0; i < cap.points.size(); ++i)
        dst.append(cap.points[i]);
}

static void stitch_to_point(Polyline &dst, bool at_start, const Point &target, const ExPolygon &region,
                            double max_len, bool reach_target)
{
    if (dst.size() < 2)
        return;
    const Point from = at_start ? dst.front() : dst.back();
    if (dist_pts(from, target) < scale_(0.08))
        return;
    Polyline cap = cap_along_edge(from, target, region, max_len, reach_target);
    if (cap.size() < 2 && dist_pts(from, target) <= max_len) {
        cap.points = { from, target };
    }
    if (at_start)
        prepend_cap(dst, std::move(cap));
    else
        append_cap(dst, std::move(cap));
}

static Point nearest_poly_end(const Polylines &paths, const Point &ref)
{
    Point  best = ref;
    double best_d = std::numeric_limits<double>::max();
    for (const Polyline &pl : paths) {
        if (pl.size() < 2)
            continue;
        for (const Point &p : { pl.front(), pl.back() }) {
            const double d = dist_pts(p, ref);
            if (d < best_d) {
                best_d = d;
                best   = p;
            }
        }
    }
    return best;
}

static Polylines connect_rays(const std::vector<RadialRay> &rays, const ExPolygon &region,
                              double spacing_mm, bool connect, int skew, bool closed, bool reverse_walk,
                              const LipMem *prev_lips, LipMem &this_lips, bool stitch_ends, const Point &pole,
                              double hub_cut_scaled = 0.)
{
    const int n = int(rays.size());
    struct Scan {
        Point inner_pt;
        Point outer_pt;
        bool  valid { false };
    };
    std::vector<Scan> scans;
    scans.reserve(size_t(n));
    for (int i = 0; i < n; ++i) {
        Scan s;
        if (!rays[size_t(i)].valid) {
            scans.push_back(s);
            continue;
        }
        const int j = wrap_idx(i + skew, n, closed);
        if (j < 0 || !rays[size_t(j)].valid) {
            scans.push_back(s);
            continue;
        }
        if (clip_scan_chord(rays[size_t(i)].inner, rays[size_t(j)].outer, region, s.inner_pt, s.outer_pt))
            s.valid = true;
        scans.push_back(s);
    }

    std::vector<char> slot_ok(size_t(n), 0);
    for (int i = 0; i < n; ++i)
        slot_ok[size_t(i)] = scans[size_t(i)].valid ? 1 : 0;
    int gap_a = -1, gap_b = -1;
    int  gap_len = 0;
    const bool found_gap = longest_invalid_run(slot_ok, gap_a, gap_b);
    if (found_gap && gap_a >= 0 && gap_b >= 0)
        gap_len = (gap_b - gap_a + n) % n + 1;
    // A hole means this fill is an O-ring, not a C. One missed clip is not an
    // opening; treating it as one skips the last→first 铆线 (收尾未链接).
    const bool open_gap = region.holes.empty() && found_gap && gap_len >= 3;
    const int  lip_l    = open_gap ? (gap_a + n - 1) % n : -1;
    const int  lip_r    = open_gap ? (gap_b + 1) % n : -1;

    if (!connect || scans.size() <= 1) {
        Polylines out;
        out.reserve(scans.size());
        for (const Scan &s : scans) {
            if (!s.valid)
                continue;
            Polyline pl;
            pl.points = { s.inner_pt, s.outer_pt };
            out.emplace_back(std::move(pl));
        }
        return out;
    }

    const ExPolygons fat     = offset_ex(region, float(scale_(0.4)));
    const double     max_len = scale_(8. * spacing_mm);

    Polylines out;
    Polyline  current;
    bool      ended_outer = false;
    int       prev_i      = -1;
    // Open C/U: start at the lip just after the opening so the +X seam
    // (k=n-1 → k=0) is hopped. Do not start at world +X — that splits the
    // body into two paths and drops the one 铆线 that should close the C.
    const int i0 = (open_gap && lip_r >= 0 && scans[size_t(lip_r)].valid) ? lip_r : 0;
    const bool start_at_lip = open_gap && lip_r >= 0 && i0 == lip_r;
    for (int step = 0; step < n; ++step) {
        const int i = (i0 + step) % n;
        if (!scans[size_t(i)].valid)
            continue;
        const bool cross_gap = open_gap && prev_i >= 0 &&
            in_circular((prev_i + 1) % n, gap_a, gap_b, n);
        if (cross_gap && start_at_lip)
            break;
        const bool  going_out = reverse_walk ? (i % 2) == 1 : (i % 2) == 0;
        const Point a = going_out ? scans[size_t(i)].inner_pt : scans[size_t(i)].outer_pt;
        const Point b = going_out ? scans[size_t(i)].outer_pt : scans[size_t(i)].inner_pt;
        if (current.empty()) {
            current.append(a);
            current.append(b);
        } else if (cross_gap) {
            if (current.size() >= 2)
                out.emplace_back(std::move(current));
            current = Polyline{};
            current.append(a);
            current.append(b);
        } else {
            Polyline hop;
            const bool hop_outer =
                0.5 * (dist_pts(current.back(), pole) + dist_pts(a, pole)) >=
                0.5 * (dist_pts(scans[size_t(i)].inner_pt, pole) + dist_pts(scans[size_t(i)].outer_pt, pole));
            if (!make_connector(current.back(), a, hop_outer, region, fat, max_len, pole, hop,
                                hub_cut_scaled) ||
                polyline_has_radial_cut(hop, pole)) {
                if (current.size() >= 2)
                    out.emplace_back(std::move(current));
                current = Polyline{};
                current.append(a);
                current.append(b);
            } else {
                for (size_t pi = 0; pi < hop.points.size(); ++pi)
                    current.append(hop.points[pi]);
                current.append(b);
            }
        }
        ended_outer = going_out;
        prev_i      = i;
    }
    if (current.size() >= 2)
        out.emplace_back(std::move(current));

    // Close the ring seam (last scan → first). Previously this ran only when
    // every hop already succeeded (`out.empty()`). One failed mid-hop skipped
    // the last→first 铆线, so an O-ring looked unfinished at the join.
    if (closed && !open_gap && !out.empty() && out.front().size() >= 2 && out.back().size() >= 2) {
        const Point a = out.back().back();
        const Point b = out.front().front();
        const bool loop_one = out.size() == 1;
        if (dist_pts(a, b) < scale_(0.08)) {
            if (loop_one && out.front().front() != out.front().back())
                out.front().append(out.front().front());
        } else {
            bool toward_outer = true;
            if (!region.holes.empty()) {
                const double da = dist_to_polygon(region.holes.front(), a);
                const double db = dist_to_polygon(region.holes.front(), b);
                const double oa = dist_to_polygon(region.contour, a);
                const double ob = dist_to_polygon(region.contour, b);
                if (da + db + 1. < oa + ob)
                    toward_outer = false;
            } else {
                toward_outer = ended_outer;
            }
            Polyline hop;
            if (make_connector(a, b, toward_outer, region, fat, max_len, pole, hop, hub_cut_scaled) &&
                !polyline_has_radial_cut(hop, pole)) {
                if (loop_one) {
                    Polyline &pl = out.front();
                    for (size_t k = 0; k < hop.points.size(); ++k)
                        pl.append(hop.points[k]);
                    pl.append(pl.front());
                } else {
                    Polyline &last = out.back();
                    for (size_t k = 0; k < hop.points.size(); ++k)
                        last.append(hop.points[k]);
                    for (const Point &p : out.front().points)
                        last.append(p);
                    out.erase(out.begin());
                }
            }
        }
    }

    // Open C/U: extend each path end along the SAME rim toward the opening,
    // and stop at the cut. Do not walk inner↔outer, and do not join the two lips.
    if (stitch_ends && open_gap && lip_l >= 0 && lip_r >= 0) {
        double sx = 0., sy = 0.;
        int    gi = gap_a;
        for (int k = 0; k < n; ++k) {
            const double th = rays[size_t(gi)].theta;
            sx += std::cos(th);
            sy += std::sin(th);
            if (gi == gap_b)
                break;
            gi = (gi + 1) % n;
        }
        const double gap_th = std::atan2(sy, sx);
        const double near_mm = scale_(1.5);
        auto cap_same_side = [&](Polyline &pl, bool at_start, const Scan &S) {
            if (!S.valid)
                return;
            const Point p = at_start ? pl.front() : pl.back();
            bool        want_outer = false;
            if (dist_pts(p, S.outer_pt) < near_mm)
                want_outer = true;
            else if (dist_pts(p, S.inner_pt) < near_mm)
                want_outer = false;
            else
                return;
            const Polygon *poly = &region.contour;
            if (!region.holes.empty() && !want_outer) {
                if (hub_cut_scaled > 1.)
                    return;
                poly = &region.holes.front();
            }
            const double r0    = dist_pts(p, pole);
            // Circle B bite on a C contour: do not rim-walk it (one-shot 大圆边).
            if (on_hub_radius(p, pole, hub_cut_scaled) ||
                (hub_cut_scaled > 1. && std::abs(r0 - hub_cut_scaled) < scale_(4.5)))
                return;
            const double r_in  = dist_pts(S.inner_pt, pole);
            const double r_out = dist_pts(S.outer_pt, pole);
            const double band  = std::max(scale_(0.5), 0.18 * std::abs(r_out - r_in));
            const double cap_max = std::max(scale_(12. * spacing_mm), 0.18 * poly->length());
            Polyline fwd = rim_extend(*poly, p, true,  pole, r0, band, cap_max);
            Polyline bak = rim_extend(*poly, p, false, pole, r0, band, cap_max);
            const double d0 = ang_diff(point_theta(p, pole), gap_th);
            auto toward_gap = [&](const Polyline &c) {
                if (c.size() < 2)
                    return false;
                return ang_diff(point_theta(c.back(), pole), gap_th) + 1e-6 < d0;
            };
            Polyline cap;
            const bool fu = toward_gap(fwd);
            const bool bu = toward_gap(bak);
            if (fu && !bu)
                cap = std::move(fwd);
            else if (bu && !fu)
                cap = std::move(bak);
            else if (fu && bu) {
                const double df = ang_diff(point_theta(fwd.back(), pole), gap_th);
                const double db = ang_diff(point_theta(bak.back(), pole), gap_th);
                cap = df <= db ? std::move(fwd) : std::move(bak);
            }
            if (cap.size() < 2)
                return;
            if (polyline_follows_hub(cap, pole, hub_cut_scaled))
                return;
            if (at_start)
                prepend_cap(pl, std::move(cap));
            else
                append_cap(pl, std::move(cap));
        };
        const Scan *L = (lip_l >= 0) ? &scans[size_t(lip_l)] : nullptr;
        const Scan *R = (lip_r >= 0) ? &scans[size_t(lip_r)] : nullptr;
        for (Polyline &pl : out) {
            if (L) {
                cap_same_side(pl, true,  *L);
                cap_same_side(pl, false, *L);
            }
            if (R) {
                cap_same_side(pl, true,  *R);
                cap_same_side(pl, false, *R);
            }
        }
        this_lips.ok = (L && L->valid) || (R && R->valid);
        if (L && L->valid)
            this_lips.left = nearest_poly_end(out, L->outer_pt);
        if (R && R->valid)
            this_lips.right = nearest_poly_end(out, R->outer_pt);
    }
    return out;
}

static RadialInfillResult generate(const ExPolygon &region, double spacing_mm, bool connect,
                                   double s0_mm, double hub_mm, int n_lock, double theta_lock,
                                   int skew, bool reverse_walk, bool has_pole_lock, const Point &pole_lock,
                                   bool equal_angle, double horiz_move, size_t region_id, size_t layer_id,
                                   const ExPolygon *clip_region, double hub_cut_mm = 0.)
{
    RadialInfillResult result;
    if (!is_supported_topology(region)) {
        result.fallback_reason = "Unsupported topology";
        return result;
    }
    if (spacing_mm < 1e-6) {
        result.fallback_reason = "Invalid spacing";
        return result;
    }

    Point  best_pole;
    double best_r = 0.;
    if (hub_cut_mm > 0.05) {
        // Keep the hub center even when it sits in the punched hole; pick_pole
        // would otherwise drift into A−B and rays would cross circle B.
        best_pole = pole_lock;
        best_r    = 1.;
    } else if (!pick_pole(region, has_pole_lock, pole_lock, best_pole, best_r)) {
        result.pole        = best_pole;
        result.valid_ratio = best_r;
        result.fallback_reason = "No valid radial pole";
        return result;
    }
    result.pole        = best_pole;
    result.valid_ratio = best_r;

    const double hub_scaled = region.contains(best_pole) ? scale_(std::max(0.4, hub_mm)) : 0.;
    const double h          = scale_(spacing_mm);
    const ExPolygons fat_lip = offset_ex(region, float(scale_(1.0)));

    int                    n_scan     = 0;
    bool                   mid_closed = true;
    std::vector<RadialRay> final_rays;
    int                    n_valid = 0;

    if (equal_angle) {
        n_scan = n_lock >= 3 ? n_lock
                             : even_scan_count(int(std::lround(estimate_mid_len(region) / std::max(h, 1.))));
        n_scan = even_scan_count(n_scan);
        const double dtheta = 2. * PI / double(std::max(1, n_scan));
        // CrossZag: δθ = horiz_move / r_mid. Use the pinned fan (n_scan · spacing),
        // not this slice's mid-curve. A C-shaped layer has no hole, so
        // estimate_mid_len returns ~½ of an annulus and used to jump δθ by ~2×.
        double dtheta0 = 0.;
        if (std::abs(horiz_move) > 1.) {
            const double mid_lock = double(std::max(1, n_scan)) * h;
            dtheta0 = horiz_move * 2. * PI / mid_lock;
        }
        const double theta0 = theta_lock + (s0_mm > 1e-9 ? 0.5 * dtheta : 0.) + dtheta0;
        final_rays.reserve(size_t(n_scan));
        for (int k = 0; k < n_scan; ++k) {
            const double theta = theta0 + double(k) * dtheta;
            RadialRay    ray;
            if (!cast_ray(region, best_pole, theta, hub_scaled, ray) && !ray.lip) {
                ray       = RadialRay{};
                ray.theta = theta;
                ray.valid = false;
                maybe_outset_lip(region, fat_lip, best_pole, hub_scaled, ray);
            }
            if (ray.valid)
                ++n_valid;
            final_rays.push_back(ray);
        }
    } else {
        auto dense = dense_rays(region, best_pole, hub_scaled, kDenseRays);
        std::vector<MidSample> samples;
        double mid_len = 0.;
        if (!build_mid_samples(dense, samples, mid_len, mid_closed)) {
            result.fallback_reason = "Mid-curve failed";
            return result;
        }

        n_scan = int(std::lround(mid_len / std::max(h, 1.)));
        if (n_lock >= 3)
            n_scan = n_lock;
        n_scan = mid_closed ? even_scan_count(n_scan) : std::max(3, n_scan);
        const double step    = mid_len / double(std::max(1, mid_closed ? n_scan : std::max(1, n_scan - 1)));
        // World +X: k=0 is the scan that actually sits on the lock angle. Do not
        // rotate afterwards — nearest/CCW pick flickers by one slot when a sample
        // crosses the lock.
        const double s_align = mid_closed ? s_for_theta(samples, mid_len, true, theta_lock) : 0.;
        const double s0      = s_align + scale_(s0_mm) + horiz_move;

        final_rays.reserve(size_t(n_scan));
        for (int k = 0; k < n_scan; ++k) {
            const double theta = sample_theta_at_s(samples, mid_len, s0 + double(k) * step, mid_closed);
            RadialRay    ray;
            if (!cast_ray(region, best_pole, theta, hub_scaled, ray) && !ray.lip) {
                ray       = RadialRay{};
                ray.theta = theta;
                ray.valid = false;
                maybe_outset_lip(region, fat_lip, best_pole, hub_scaled, ray);
            }
            if (ray.valid)
                ++n_valid;
            final_rays.push_back(ray);
        }
    }

    result.n_scan = n_scan;
    result.n_lock = n_lock;
    if (n_valid < 3) {
        result.fallback_reason = "Too few final rays";
        return result;
    }

    const LipMem prev_lips = recall_lips(region_id, layer_id, best_pole);
    LipMem       this_lips;
    this_lips.pole = best_pole;
    // Rays were cast on `region` (modifier island). Pairing and 铆线 follow
    // the final fill (`clip_region`), which already sits inside walls — do not
    // guess a wall inset or fatten the clip back into other parts.
    const ExPolygon &link = clip_region != nullptr ? *clip_region : region;
    Polylines paths = connect_rays(final_rays, link, spacing_mm, connect, skew, mid_closed, reverse_walk,
                                  prev_lips.ok ? &prev_lips : nullptr, this_lips, true, best_pole,
                                  scale_(hub_cut_mm));
    if (paths.empty()) {
        result.fallback_reason = "Path generation failed";
        return result;
    }
    // Do not retry with stitch_ends off: C/U lip caps sit on the fill contour
    // and Clipper contains() often rejects the boundary, which used to strip
    // the 孤点 extensions.
    this_lips.pole = best_pole;
    store_lips(region_id, layer_id, this_lips);

    result.success         = true;
    result.num_final_rays  = n_valid;
    result.rays            = std::move(final_rays);
    result.extrusion_paths = std::move(paths);
    return result;
}

static void record_diag(const Fill &fill, const RadialInfillResult &r, bool has_lock, const Point &pole_lock,
                        const Point &layer_pole, bool axis_applied, int pole_mode, const ExPolygon &region)
{
    if (!g_dbg_on)
        return;
    LayerDiag d;
    d.layer_id       = layer_index(fill);
    d.success        = r.success;
    d.pole_x         = unscale<double>(r.pole.x());
    d.pole_y         = unscale<double>(r.pole.y());
    d.has_lock       = has_lock;
    if (has_lock) {
        d.lock_x = unscale<double>(pole_lock.x());
        d.lock_y = unscale<double>(pole_lock.y());
    }
    d.layer_x        = unscale<double>(layer_pole.x());
    d.layer_y        = unscale<double>(layer_pole.y());
    d.axis_applied   = axis_applied;
    d.pole_inside    = region.contains(r.pole);
    d.drifted        = has_lock && dist_pts(r.pole, pole_lock) > scale_(0.4);
    d.pole_mode      = pole_mode;
    d.n_holes        = int(region.holes.size());
    d.island_mm      = unscale<double>(island_size(region));
    int n_in = 0;
    for (const RadialRay &ray : r.rays) {
        if (!ray.valid)
            continue;
        d.inner_mean += unscale<double>(ray.inner_radius);
        d.outer_mean += unscale<double>(ray.outer_radius);
        ++n_in;
    }
    if (n_in > 0) {
        d.inner_mean /= double(n_in);
        d.outer_mean /= double(n_in);
    }
    d.valid_ratio    = r.valid_ratio;
    d.num_final_rays = r.num_final_rays;
    d.n_scan         = r.n_scan;
    d.n_lock         = r.n_lock;
    d.lock_rows      = r.lock_rows;
    d.fallback_reason = r.fallback_reason;
    d.rays           = r.rays;
    std::lock_guard<std::mutex> lock(g_dbg_mutex);
    g_dbg_log.push_back(std::move(d));
}

} // namespace

void debug_enable(bool on)
{
    std::lock_guard<std::mutex> lock(g_dbg_mutex);
    g_dbg_on = on;
}

void debug_clear()
{
    std::lock_guard<std::mutex> lock(g_dbg_mutex);
    g_dbg_log.clear();
}

void reset_n_lock()
{
    std::lock_guard<std::mutex> lock(g_n_mutex);
    g_locks.clear();
}

void pin_scan_counts(const std::vector<std::vector<ExPolygon>> &islands_by_layer, double spacing_mm,
                     const std::vector<double> &print_z, size_t region_id, int n_override)
{
    const double h = scale_(std::max(spacing_mm, 1e-6));
    struct Cand {
        Point  key;
        double size { 0. };
        int    n_raw { 0 };
        Point  pole_raw;
    };
    std::vector<std::vector<Cand>> cands_by_layer(islands_by_layer.size());
    int n_global = 4;
    const bool force_n = n_override > 0;
    if (force_n)
        n_global = even_scan_count(n_override);
    for (size_t i = 0; i < islands_by_layer.size(); ++i) {
        const ExPolygons merged = union_ex(islands_by_layer[i]);
        for (const ExPolygon &ex : merged) {
            if (ex.contour.size() < 3)
                continue;
            const double L = estimate_mid_len(ex);
            const int n_raw = even_scan_count(int(std::lround(L / std::max(h, 1.))));
            if (!force_n)
                n_global = std::max(n_global, n_raw);
            cands_by_layer[i].push_back({ get_extents(ex).center(), island_size(ex), n_raw,
                                          raw_pole(ex) });
        }
        std::sort(cands_by_layer[i].begin(), cands_by_layer[i].end(),
                  [](const Cand &a, const Cand &b) { return a.size > b.size; });
    }

    std::vector<std::vector<LockedIslandN>> table(islands_by_layer.size());
    std::vector<LockedIslandN> prev;
    std::vector<double> fit_z;
    std::vector<Point>  fit_p;
    std::vector<Point>  fit_bb;
    for (size_t i = 0; i < islands_by_layer.size(); ++i) {
        const auto &cands = cands_by_layer[i];
        std::vector<char> used(prev.size(), 0);
        std::vector<LockedIslandN> cur;
        cur.reserve(cands.size());
        for (size_t ci = 0; ci < cands.size(); ++ci) {
            const Cand &c     = cands[ci];
            int         best  = -1;
            double      best_score = std::numeric_limits<double>::max();
            // Primary island continues even if the AABB jumped or a satellite appeared.
            if (ci == 0 && !prev.empty())
                best = 0;
            else {
                for (size_t j = 0; j < prev.size(); ++j) {
                    if (used[j])
                        continue;
                    if (size_mismatch(c.size, prev[j].size) > 3.)
                        continue;
                    const double d   = dist_pts(c.key, prev[j].key);
                    const double lim = n_match_limit(c.size, prev[j].size);
                    if (d > lim)
                        continue;
                    const double score = size_mismatch(c.size, prev[j].size) * scale_(100.) + d;
                    if (score < best_score) {
                        best_score = score;
                        best       = int(j);
                    }
                }
            }
            Point pole = c.pole_raw;
            if (best >= 0) {
                used[size_t(best)] = 1;
                pole = hold_or_slew_pole(prev[size_t(best)].pole, c.pole_raw);
            }
            cur.push_back({ c.key, c.size, n_global, 0., pole });
        }
        if (!cands.empty() && i < print_z.size()) {
            fit_z.push_back(print_z[i]);
            fit_p.push_back(cands.front().pole_raw);
            fit_bb.push_back(cands.front().key);
        }
        table[i] = cur;
        prev     = std::move(cur);
    }
    AxisLine   axis;
    QuadBezier bezier;
    if (print_z.size() == islands_by_layer.size()) {
        axis   = fit_axis_line(fit_z, fit_p);
        bezier = fit_bbox_bezier(fit_z, fit_bb);
    }
    RegionLock rl;
    rl.n_by_layer = std::move(table);
    rl.n_global   = n_global;
    rl.axis       = axis;
    rl.bezier     = bezier;
    rl.zs         = std::move(fit_z);
    rl.poles      = std::move(fit_p);
    rl.cast_by_layer = islands_by_layer;
    std::lock_guard<std::mutex> lock(g_n_mutex);
    g_locks[region_id] = std::move(rl);
}

std::vector<PoleVis> snapshot_pole_vis()
{
    std::lock_guard<std::mutex> lock(g_n_mutex);
    std::vector<PoleVis>        out;
    out.reserve(g_locks.size());
    for (const auto &kv : g_locks) {
        const RegionLock &rl = kv.second;
        PoleVis           vis;
        vis.region_id = kv.first;
        vis.axis_ok   = rl.axis.ok;
        vis.bezier_ok = rl.bezier.ok;
        vis.layer_poles.reserve(rl.zs.size());
        for (size_t i = 0; i < rl.zs.size() && i < rl.poles.size(); ++i)
            vis.layer_poles.emplace_back(unscale<double>(rl.poles[i].x()), unscale<double>(rl.poles[i].y()), rl.zs[i]);
        if (rl.axis.ok) {
            double z0 = rl.zs.empty() ? (rl.bezier.ok ? rl.bezier.z0 : 0.) : rl.zs.front();
            double z1 = rl.zs.empty() ? (rl.bezier.ok ? rl.bezier.z1 : 1.) : rl.zs.back();
            if (z1 - z0 < 1e-6) {
                z0 -= 2.;
                z1 += 2.;
            }
            const int n = std::max(2, int(rl.zs.size()));
            vis.axis_polyline.reserve(size_t(n));
            for (int i = 0; i < n; ++i) {
                const double t = (n == 1) ? 0. : double(i) / double(n - 1);
                const double z = z0 + t * (z1 - z0);
                vis.axis_polyline.emplace_back(rl.axis.x0 + rl.axis.ax * z, rl.axis.y0 + rl.axis.ay * z, z);
            }
        }
        if (rl.bezier.ok) {
            const int ns = 48;
            vis.bezier_polyline.reserve(size_t(ns + 1));
            for (int i = 0; i <= ns; ++i) {
                const double t  = double(i) / double(ns);
                const double u  = 1. - t;
                const double x  = u * u * rl.bezier.p0x + 2. * u * t * rl.bezier.p1x + t * t * rl.bezier.p2x;
                const double y  = u * u * rl.bezier.p0y + 2. * u * t * rl.bezier.p1y + t * t * rl.bezier.p2y;
                const double z  = rl.bezier.z0 + t * (rl.bezier.z1 - rl.bezier.z0);
                vis.bezier_polyline.emplace_back(x, y, z);
            }
        }
        out.push_back(std::move(vis));
    }
    return out;
}

std::vector<LayerDiag> debug_snapshot()
{
    std::lock_guard<std::mutex> lock(g_dbg_mutex);
    return g_dbg_log;
}

static bool link_flip_layer(size_t layer_id, int keep, int flip)
{
    if (flip <= 0)
        return false;
    keep = std::max(0, keep);
    const int period = keep + flip;
    return int(layer_id % size_t(period)) >= keep;
}

static constexpr double kHubK = 1.2;
// Full line width on each side of the hub so extrusion beads do not sit on the A−B / A∩B cut.
static constexpr double kHubClearance = 1.0;

struct RadialBind
{
    const ExPolygon *gen { nullptr };
    const ExPolygon *hop { nullptr };
    LockedScan       locked;
    int              n_lock { 0 };
    Point            pole;
    Point            layer_pole;
    bool             has_pole { false };
    bool             axis_applied { false };
};

static RadialBind bind_radial(const ExPolygon &region, const Fill &fill, const FillParams &params)
{
    RadialBind       b;
    const size_t     region_id   = fill.lock_region_id;
    const ExPolygon *cast_island = lookup_cast_island(region_id, layer_index(fill), region);
    if (cast_island != nullptr) {
        b.gen = cast_island;
        b.hop = &region;
    } else {
        b.gen = &region;
    }
    b.locked = lookup_locked(layer_index(fill), get_extents(*b.gen).center(), island_size(*b.gen), region_id);
    b.n_lock = b.locked.ok ? b.locked.n : 0;
    {
        std::lock_guard<std::mutex> lock(g_n_mutex);
        if (b.n_lock < 3) {
            const RegionLock *rl = find_lock(region_id);
            if (rl != nullptr)
                b.n_lock = rl->n_global;
        }
    }
    b.layer_pole = b.locked.pole;
    b.pole       = b.locked.pole;
    b.has_pole   = b.locked.has_pole;
    if (params.conformal_pole == ConformalPole::Axis) {
        Point ap;
        if (lookup_axis(fill.z, ap, region_id)) {
            b.pole         = ap;
            b.has_pole     = true;
            b.axis_applied = true;
        }
    } else if (params.conformal_pole == ConformalPole::Bezier) {
        Point ap;
        if (lookup_bezier(fill.z, ap, region_id)) {
            b.pole     = ap;
            b.has_pole = true;
        }
    }
    return b;
}

static int hub_n_scan(const ExPolygon &region, const Fill &fill, const FillParams &params, int n_lock)
{
    if (params.conformal_ray_count > 0)
        return even_scan_count(params.conformal_ray_count);
    if (n_lock >= 3)
        return even_scan_count(n_lock);
    const double h = scale_(line_spacing_mm(fill, params));
    return even_scan_count(int(std::lround(estimate_mid_len(region) / std::max(h, 1.))));
}

static double hub_line_width_mm(const Fill &fill, const FillParams &params)
{
    const double w = params.flow.width();
    if (w > 1e-6)
        return w;
    if (fill.spacing > 1e-6)
        return fill.spacing;
    return 0.4;
}

static ExPolygon max_overlap_piece(const ExPolygons &parts, const ExPolygon &key)
{
    if (parts.size() == 1)
        return parts.front();
    const ExPolygon *best   = &parts.front();
    double           best_a = -1.;
    for (const ExPolygon &p : parts) {
        const ExPolygons hit = intersection_ex(p, key);
        double           a   = 0.;
        for (const ExPolygon &h : hit)
            a += std::abs(h.area());
        if (a > best_a) {
            best_a = a;
            best   = &p;
        }
    }
    return *best;
}

bool make_hub_disk(const ExPolygon &region, const Fill &fill, const FillParams &params,
                   Point &pole, ExPolygon &disk)
{
    if (params.conformal_hub_radius < 0.f)
        return false;
    const RadialBind b = bind_radial(region, fill, params);
    if (b.gen == nullptr)
        return false;
    Point pole_use = b.pole;
    if (!b.has_pole) {
        double br = 0.;
        if (!pick_pole(*b.gen, false, Point(), pole_use, br))
            return false;
    }
    const int    n = hub_n_scan(*b.gen, fill, params, b.n_lock);
    const double w = hub_line_width_mm(fill, params);
    const double r = params.conformal_hub_radius > 0.f
                         ? double(params.conformal_hub_radius)
                         : double(n) * kHubK * w / (2. * PI);
    if (r < 0.05)
        return false;
    Polygon c = make_circle_num_segments(scale_(r), 64);
    c.translate(pole_use);
    c.make_counter_clockwise();
    disk          = ExPolygon();
    disk.contour  = std::move(c);
    pole          = pole_use;
    return true;
}

bool fill_region(const ExPolygon &region, const Fill &fill, const FillParams &params, Polylines &out)
{
    const double spacing_mm = line_spacing_mm(fill, params);
    const bool   odd        = (layer_index(fill) % 2) == 1;
    const bool   alternate  = params.conformal_stagger == ConformalStagger::Alternate
                           || params.conformal_stagger == ConformalStagger::HalfStep;
    const bool   reverse_walk = link_flip_layer(layer_index(fill), params.conformal_link_keep_layers,
                                               params.conformal_link_flip_layers);
    const double s0_mm      = 0.;
    const bool   connect    = !params.dont_connect();
    const int    skew       = alternate ? (odd ? -1 : 1) : 0;
    const RadialBind b      = bind_radial(region, fill, params);
    const double theta_lock = 0.;
    const bool   equal_angle = true;
    const int    n_lock     = params.conformal_ray_count > 0 ? even_scan_count(params.conformal_ray_count)
                                                             : b.n_lock;

    ExPolygons gen_parts{ *b.gen };
    ExPolygons hop_parts;
    const bool has_hop = b.hop != nullptr;
    if (has_hop)
        hop_parts.push_back(*b.hop);

    Point     hub_pole;
    ExPolygon disk;
    double    hub_cut_mm = 0.;
    if (make_hub_disk(*b.gen, fill, params, hub_pole, disk)) {
        const double gap = kHubClearance * hub_line_width_mm(fill, params);
        const double r_mm = 0.5 * unscale<double>(get_extents(disk).size().x());
        hub_cut_mm = r_mm + gap;
        ExPolygons   cut = offset_ex(disk, float(scale_(gap)));
        if (cut.empty())
            cut.push_back(disk);
        gen_parts = diff_ex(*b.gen, cut);
        if (gen_parts.empty())
            return false;
        if (has_hop)
            hop_parts = diff_ex(*b.hop, cut);
    }

    bool any = false;
    for (const ExPolygon &gp : gen_parts) {
        const ExPolygon *hp = nullptr;
        ExPolygon        hp_store;
        if (has_hop && !hop_parts.empty()) {
            hp_store = max_overlap_piece(hop_parts, gp);
            hp       = &hp_store;
        }
        const bool pin_pole = hub_cut_mm > 0.05;
        RadialInfillResult r = generate(gp, spacing_mm, connect, s0_mm, fill.spacing, n_lock, theta_lock, skew,
                                        reverse_walk, pin_pole || b.has_pole, pin_pole ? hub_pole : b.pole, equal_angle,
                                        params.pattern == ipCrossZag ? double(params.horiz_move) : 0.,
                                        fill.lock_region_id, layer_index(fill), hp, hub_cut_mm);
        r.n_lock    = n_lock;
        r.lock_rows = b.locked.table_rows;
        record_diag(fill, r, b.has_pole, b.pole, b.layer_pole, b.axis_applied, int(params.conformal_pole), gp);
        if (r.success && !r.extrusion_paths.empty()) {
            append(out, std::move(r.extrusion_paths));
            any = true;
        }
    }
    return any;
}

} // namespace FillRadialZigZag
} // namespace Slic3r
