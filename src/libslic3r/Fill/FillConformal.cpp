#include "FillConformal.hpp"
#include "FillConformalChart.hpp"
#include "FillRadialZigZag.hpp"
#include "FillRectilinear.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "../ClipperUtils.hpp"
#include "../ExPolygon.hpp"
#include "../BoundingBox.hpp"
#include "../Geometry.hpp"
#include "../Line.hpp"
#include "../Point.hpp"
#include "../Polygon.hpp"
#include "../Surface.hpp"

namespace Slic3r {
namespace FillConformal {

struct Centerline
{
    Polyline polyline;
    bool     closed { false };
};

static Centerline to_centerline(const LayerGenerator &lg)
{
    Centerline g;
    g.polyline = lg.polyline;
    g.closed   = lg.closed;
    return g;
}

static size_t layer_index(const Fill &fill)
{
    return fill.layer_id == size_t(-1) ? 0 : fill.layer_id;
}

static BoundingBox object_or_region_bb(const Fill &fill, const ExPolygon &region)
{
    BoundingBox bb = fill.bounding_box;
    if (bb.size().x() <= 0 || bb.size().y() <= 0)
        bb = get_extents(region);
    return bb;
}

static double line_spacing_mm(const Fill &fill, const FillParams &params)
{
    return fill.spacing / std::max(0.05, double(params.density));
}

// Polar diameters: N from the object AABB long side so disk/cylinder tests keep a stable family.
static int stable_polar_count(const Fill &fill, const FillParams &params, const ExPolygon &region)
{
    BoundingBox bb = object_or_region_bb(fill, region);
    const double char_len = unscale<double>(std::max(bb.size().x(), bb.size().y()));
    const double delta    = line_spacing_mm(fill, params);
    return std::max(2, int(std::lround(char_len / std::max(delta, 1e-6))));
}

// Space samples by target line spacing along a generator span.
static int chart_line_count(double L_scaled, double delta_mm)
{
    const double Lmm = unscale<double>(L_scaled);
    return std::max(2, int(std::lround(Lmm / std::max(delta_mm, 1e-6))));
}

static double theta0_for_layer(const Fill &fill, const FillParams &params, int N)
{
    double theta0 = 0.;
    const bool odd = (layer_index(fill) % 2) == 1;
    if (odd && params.conformal_stagger == ConformalStagger::HalfStep)
        theta0 += PI / (2. * double(N));
    return theta0;
}

static Vec2d unit_from_angle(double theta)
{
    return Vec2d(std::cos(theta), std::sin(theta));
}

static void append_closed_polygon(const Polygon &poly, Polylines &out)
{
    if (poly.points.size() < 3)
        return;
    Polyline pl;
    pl.points = poly.points;
    if (pl.front() != pl.back())
        pl.points.push_back(pl.front());
    if (pl.length() > scale_(0.2))
        out.emplace_back(std::move(pl));
}

static bool fill_offset_loops(const ExPolygon &region, const Fill &fill, const FillParams &params, Polylines &out)
{
    const double step = scale_(line_spacing_mm(fill, params));
    if (step <= 0)
        return false;
    BoundingBox bb     = get_extents(region);
    const double max_in = 0.5 * double(std::min(bb.size().x(), bb.size().y()));
    bool         any    = false;
    for (double o = step; o < max_in - 0.5 * step; o += step) {
        ExPolygons inner = offset_ex(region, float(-o));
        for (const ExPolygon &ex : inner) {
            append_closed_polygon(ex.contour, out);
            for (const Polygon &hole : ex.holes)
                append_closed_polygon(hole, out);
            any = true;
        }
    }
    return any && !out.empty();
}

static void clip_ray_to_region(const Point &origin, const Vec2d &dir, const ExPolygon &region, Polylines &scans)
{
    const BoundingBox bb      = get_extents(region);
    const double      ray_len = double(std::max(bb.size().x(), bb.size().y())) + double(scaled<coord_t>(2.));
    Vec2d             n       = dir;
    const double      nn      = n.norm();
    if (nn < 1e-9)
        return;
    n /= nn;
    Polyline ray;
    ray.points = { origin - Point(coord_t(n.x() * ray_len), coord_t(n.y() * ray_len)),
                   origin + Point(coord_t(n.x() * ray_len), coord_t(n.y() * ray_len)) };
    Polylines hits = intersection_pl(ray, region);
    for (Polyline &hit : hits)
        if (hit.length() > scale_(0.2))
            scans.emplace_back(std::move(hit));
}

static double polyline_dist_to_point(const Polyline &pl, const Point &p)
{
    double best = std::numeric_limits<double>::max();
    for (size_t i = 1; i < pl.points.size(); ++i)
        best = std::min(best, Line::distance_to(p, pl.points[i - 1], pl.points[i]));
    return best;
}

// Keep the fragment that belongs to this sample (local wall thickness / local width),
// not the far-side hit across a hole.
static void clip_local_chord(const Point &origin, const Vec2d &dir, const ExPolygon &region, Polylines &scans)
{
    Polylines hits;
    clip_ray_to_region(origin, dir, region, hits);
    if (hits.empty())
        return;
    size_t best   = 0;
    double best_d = polyline_dist_to_point(hits[0], origin);
    for (size_t i = 1; i < hits.size(); ++i) {
        const double d = polyline_dist_to_point(hits[i], origin);
        if (d < best_d) {
            best_d = d;
            best   = i;
        }
    }
    scans.emplace_back(std::move(hits[best]));
}

static bool fill_polar_rays(const ExPolygon &region, const Fill &fill, const FillParams &params, Polylines &scans)
{
    const Point c = region.contour.centroid();
    const int   N = stable_polar_count(fill, params, region);
    const double theta0 = theta0_for_layer(fill, params, N);
    const double dtheta = PI / double(N);
    for (int k = 0; k < N; ++k)
        clip_ray_to_region(c, unit_from_angle(theta0 + k * dtheta), region, scans);
    return scans.size() >= 2;
}

static double generator_length(const Centerline &g)
{
    return g.polyline.length();
}

static bool sample_generator(const Centerline &g, double s, Point &p, Vec2d &tangent)
{
    const Points &pts = g.polyline.points;
    if (pts.size() < 2)
        return false;
    const bool   closed = g.closed && pts.size() >= 3 && pts.front() == pts.back();
    const size_t nseg   = pts.size() - 1;
    double       L      = 0.;
    for (size_t i = 0; i < nseg; ++i)
        L += (pts[i + 1] - pts[i]).cast<double>().norm();
    if (L < 1.)
        return false;
    if (closed) {
        s = std::fmod(s, L);
        if (s < 0.)
            s += L;
    } else {
        s = std::max(0., std::min(s, L));
    }
    double acc = 0.;
    for (size_t i = 0; i < nseg; ++i) {
        Vec2d        d   = (pts[i + 1] - pts[i]).cast<double>();
        const double len = d.norm();
        if (len < 1.)
            continue;
        if (acc + len >= s - 1e-3) {
            const double t = std::max(0., std::min(1., (s - acc) / len));
            p = pts[i] + Point(coord_t(std::lround(d.x() * t)), coord_t(std::lround(d.y() * t)));
            tangent = d / len;
            return true;
        }
        acc += len;
    }
    p = pts[nseg];
    Vec2d d = (pts[nseg] - pts[nseg - 1]).cast<double>();
    const double ln = d.norm();
    tangent = (ln > 1.) ? d / ln : Vec2d(1., 0.);
    return true;
}

// s=0: nearest generator point to the ray from centroid along world +X.
static double s0_world_plus_x(const Centerline &g, const Point &c)
{
    const Points &pts = g.polyline.points;
    if (pts.size() < 2)
        return 0.;
    const bool   closed = g.closed && pts.size() >= 3 && pts.front() == pts.back();
    const size_t nseg   = pts.size() - 1;
    auto dist_to_ray = [&](const Point &p) {
        Vec2d v = (p - c).cast<double>();
        const double t = std::max(0., v.x());
        return (v - Vec2d(t, 0.)).norm();
    };
    double best_d = std::numeric_limits<double>::max();
    double best_s = 0.;
    double acc    = 0.;
    for (size_t i = 0; i < nseg; ++i) {
        const Point &a = pts[i];
        const double d = dist_to_ray(a);
        if (d < best_d) {
            best_d = d;
            best_s = acc;
        }
        acc += (pts[i + 1] - a).cast<double>().norm();
    }
    if (!closed) {
        const double d = dist_to_ray(pts[nseg]);
        if (d < best_d)
            best_s = acc;
    }
    return best_s;
}

static void orient_open_plus_x(Centerline &g)
{
    if (g.closed || g.polyline.size() < 2)
        return;
    if (g.polyline.back().x() > g.polyline.front().x())
        g.polyline.reverse();
}

struct Chord
{
    double   s { 0. };
    Polyline pl;
};

static bool polylines_interior_cross(const Polyline &a, const Polyline &b, double end_eps)
{
    for (size_t ia = 1; ia < a.points.size(); ++ia) {
        Vec2d p1 = a.points[ia - 1].cast<double>();
        Vec2d v1 = a.points[ia].cast<double>() - p1;
        for (size_t ib = 1; ib < b.points.size(); ++ib) {
            Vec2d p2 = b.points[ib - 1].cast<double>();
            Vec2d v2 = b.points[ib].cast<double>() - p2;
            Vec2d hit;
            if (!Geometry::segment_segment_intersection(p1, v1, p2, v2, hit))
                continue;
            const double da = std::min((hit - p1).norm(), (hit - (p1 + v1)).norm());
            const double db = std::min((hit - p2).norm(), (hit - (p2 + v2)).norm());
            if (da > end_eps && db > end_eps)
                return true;
        }
    }
    return false;
}

static void drop_crossing_chords(std::vector<Chord> &chords)
{
    std::sort(chords.begin(), chords.end(), [](const Chord &a, const Chord &b) {
        return a.pl.length() > b.pl.length();
    });
    const double end_eps = scale_(0.4);
    std::vector<Chord> kept;
    kept.reserve(chords.size());
    for (Chord &c : chords) {
        bool hit = false;
        for (const Chord &k : kept) {
            if (polylines_interior_cross(c.pl, k.pl, end_eps)) {
                hit = true;
                break;
            }
        }
        if (!hit)
            kept.push_back(std::move(c));
    }
    chords.swap(kept);
}

static bool emit_chords(Centerline g, const ExPolygon &region, const Fill &fill, const FillParams &params,
                        bool world_anchor, Polylines &scans)
{
    const double L = generator_length(g);
    if (L < scale_(0.5))
        return false;

    const double delta       = line_spacing_mm(fill, params);
    const bool   half_closed = g.closed && region.holes.empty();
    const double span        = half_closed ? 0.5 * L : L;
    const int    N           = chart_line_count(span, delta);
    const Point  centroid    = region.contour.centroid();
    double       s0          = (world_anchor && g.closed) ? s0_world_plus_x(g, centroid) : 0.;
    const bool   odd         = (layer_index(fill) % 2) == 1;
    const bool   half_step   = odd && params.conformal_stagger == ConformalStagger::HalfStep;
    if (g.closed && half_step)
        s0 += span / (2. * double(N));

    std::vector<Chord> chords;
    chords.reserve(size_t(N));
    for (int k = 0; k < N; ++k) {
        double s;
        if (g.closed) {
            s = s0 + double(k) * span / double(N);
        } else {
            s = (double(k) + 0.5) * L / double(N);
            if (half_step)
                s += L / (2. * double(N));
        }
        Point p;
        Vec2d tangent;
        if (!sample_generator(g, s, p, tangent))
            continue;
        Vec2d n(-tangent.y(), tangent.x());
        Polylines hits;
        clip_local_chord(p, n, region, hits);
        for (Polyline &hit : hits)
            chords.push_back({ s, std::move(hit) });
    }

    drop_crossing_chords(chords);
    std::sort(chords.begin(), chords.end(), [](const Chord &a, const Chord &b) { return a.s < b.s; });
    for (Chord &ch : chords)
        scans.emplace_back(std::move(ch.pl));
    return scans.size() >= 2;
}

static bool generator_near_region(const LayerGenerator &g, const ExPolygon &region)
{
    BoundingBox bb = get_extents(region);
    bb.offset(scaled<coord_t>(3.));
    for (const Point &p : g.polyline.points)
        if (bb.contains(p))
            return true;
    return false;
}

static bool fill_chart_rays(const ExPolygon &region, const Fill &fill, const FillParams &params, Polylines &scans)
{
    LayerGenerator lg;
    if (!extract_generator(region, fill.spacing, lg))
        return false;
    Centerline g = to_centerline(lg);
    orient_open_plus_x(g);
    return emit_chords(std::move(g), region, fill, params, true, scans);
}

static bool connection_inside(const Point &a, const Point &b, const ExPolygon &region,
                              const ExPolygons &fat, double max_len)
{
    const double len = (b - a).cast<double>().norm();
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

// Join consecutive s-ordered chords if the straight link stays inside the island.
// Unlike Fill::connect_infill this does not assume parallel scanlines, so holes
// get a zigzag around the opening instead of hundreds of loose segments.
static void connect_chart_polylines(Polylines &&scans, const ExPolygon &region, double max_len, Polylines &out)
{
    if (scans.size() <= 1) {
        append(out, std::move(scans));
        return;
    }

    const ExPolygons fat = offset_ex(region, float(scale_(0.4)));
    const double     end_eps = scale_(0.45);
    auto join_clean = [&](const Point &a, const Point &b, const Polyline &cur, const Polyline &nxt) {
        if (!connection_inside(a, b, region, fat, max_len))
            return false;
        if ((b - a).cast<double>().norm() < scale_(0.08))
            return true;
        Polyline hop;
        hop.points = { a, b };
        return !polylines_interior_cross(hop, cur, end_eps) &&
               !polylines_interior_cross(hop, nxt, end_eps) &&
               [&] {
                   for (const Polyline &o : out)
                       if (polylines_interior_cross(hop, o, end_eps))
                           return false;
                   return true;
               }();
    };
    auto try_append = [&](Polyline &current, Polyline &nxt) -> bool {
        if (current.size() < 2 || nxt.size() < 2)
            return false;
        const Point tip  = current.back();
        const bool  ok_f = join_clean(tip, nxt.front(), current, nxt);
        const bool  ok_b = join_clean(tip, nxt.back(), current, nxt);
        if (!ok_f && !ok_b)
            return false;
        bool rev = false;
        if (ok_f && ok_b) {
            const double df = (nxt.front() - tip).cast<double>().norm();
            const double db = (nxt.back() - tip).cast<double>().norm();
            rev = db < df;
        } else {
            rev = ok_b;
        }
        if (rev)
            nxt.reverse();
        current.append(nxt);
        return true;
    };

    Polyline current = std::move(scans.front());
    for (size_t i = 1; i < scans.size(); ++i) {
        if (!try_append(current, scans[i])) {
            if (current.size() >= 2)
                out.emplace_back(std::move(current));
            current = std::move(scans[i]);
        }
    }
    if (current.size() >= 2)
        out.emplace_back(std::move(current));

    // Close a single zigzag into a loop when the wrap-around hop is short.
    if (out.size() == 1) {
        Polyline &pl = out.front();
        if (pl.size() >= 2 && connection_inside(pl.back(), pl.front(), region, fat, max_len))
            pl.append(pl.front());
    }
}

static bool is_blob_island(const ExPolygon &region)
{
    if (!region.holes.empty())
        return false;
    BoundingBox bb = get_extents(region);
    const double w = double(std::max(coord_t(1), bb.size().x()));
    const double h = double(std::max(coord_t(1), bb.size().y()));
    const double aspect = std::max(w, h) / std::min(w, h);
    if (aspect >= 2.5)
        return false;
    // L-shapes and other concave islands share a square AABB with a disk;
    // require the island to fill most of its box before using polar diameters.
    return std::abs(region.area()) > 0.65 * w * h;
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

static Polylines fill_hub_rectilinear(const Fill &src, const Surface *surface, const FillParams &params,
                                      const ExPolygon &hub)
{
    FillRectilinear rect;
    rect.layer_id        = src.layer_id;
    rect.lock_region_id  = src.lock_region_id;
    rect.z               = src.z;
    rect.spacing         = src.spacing;
    rect.overlap         = src.overlap;
    rect.angle           = src.angle;
    rect.link_max_length = src.link_max_length;
    rect.loop_clipping   = src.loop_clipping;
    rect.bounding_box    = src.bounding_box;
    Surface s            = *surface;
    s.expolygon          = hub;
    FillParams p         = params;
    p.conformal          = false;
    p.pattern            = ipRectilinear;
    // Do not walk the hub circle: that traces the A−B / A∩B boundary.
    p.anchor_length      = 0.f;
    p.anchor_length_max  = 0.f;
    Polylines raw = rect.fill_surface(&s, p);
    // dont_connect still chain_polylines, which strings endpoints around circle B.
    Polylines out;
    out.reserve(raw.size());
    for (const Polyline &pl : raw) {
        if (pl.size() < 2)
            continue;
        if (pl.size() == 2) {
            out.push_back(pl);
            continue;
        }
        for (size_t i = 1; i < pl.points.size(); ++i) {
            Polyline seg;
            seg.points = { pl.points[i - 1], pl.points[i] };
            if (seg.length() > scale_(0.05))
                out.emplace_back(std::move(seg));
        }
    }
    return out;
}

bool fill_surface(const Fill &fill, const Surface *surface, const FillParams &params, Polylines &out)
{
    if (surface == nullptr || params.density < 0.0001f)
        return false;

    const float        inset   = float(scale_(fill.overlap - 0.5 * fill.spacing));
    Slic3r::ExPolygons regions = offset_ex(surface->expolygon, inset);
    if (regions.empty())
        regions.push_back(surface->expolygon);

    bool any_ok = false;
    for (const ExPolygon &region : regions) {
        Polylines radial;
        if (FillRadialZigZag::fill_region(region, fill, params, radial) && !radial.empty()) {
            append(out, std::move(radial));
            any_ok = true;
        }
        Point     pole;
        ExPolygon disk;
        if (FillRadialZigZag::make_hub_disk(region, fill, params, pole, disk)) {
            // Inset from circle B so rectilinear hops stay inside A∩B.
            const double     gap  = 1.0 * hub_line_width_mm(fill, params);
            const ExPolygons slim = offset_ex(disk, -float(scale_(gap)));
            if (!slim.empty()) {
                const ExPolygons hub = intersection_ex(region, slim);
                for (const ExPolygon &h : hub) {
                    Polylines rl = fill_hub_rectilinear(fill, surface, params, h);
                    if (!rl.empty()) {
                        append(out, std::move(rl));
                        any_ok = true;
                    }
                }
            }
        }
    }

    if (!any_ok || out.empty()) {
        out.clear();
        return false;
    }
    return true;
}

} // namespace FillConformal
} // namespace Slic3r
