#include "RasterArrange.hpp"

#include "BoundingBox.hpp"
#include "ClipperUtils.hpp"
#include "Geometry/ConvexHull.hpp"
#include "Print.hpp"

#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <map>
#include <memory>
#include <set>

#ifdef _MSC_VER
#include <intrin.h>
#endif
#include <cmath>
#include <numeric>
#include <random>

namespace Slic3r { namespace arrangement {

namespace {

using Word = uint64_t;
constexpr Word ALL_ONES = ~Word(0);

// Minimal clearance kept around every object (same value as libnest2d's MIN_SEPARATION).
const double MIN_SEPARATION = scale_(1.0);

struct Span { int a; int len; };              // [a, a + len) in cells
using Rows = std::vector<std::vector<Span>>;  // spans per raster row

// ----------------------------------------------------------------------------------------------
// Bit helpers. Bit x of a row lives in word x / 64, bit x % 64. Reading past the end of a row
// yields ones (= blocked), so the outside of the bed is always an obstacle.
// ----------------------------------------------------------------------------------------------

// dst[x] |= src[x + k] for all x.
inline void shr_or(Word *dst, const Word *src, int nw, int k)
{
    const int q = k >> 6, r = k & 63;
    if (r == 0) {
        for (int w = 0; w < nw; ++w)
            dst[w] |= (w + q < nw) ? src[w + q] : ALL_ONES;
    } else {
        for (int w = 0; w < nw; ++w) {
            const Word lo = (w + q < nw) ? src[w + q] : ALL_ONES;
            const Word hi = (w + q + 1 < nw) ? src[w + q + 1] : ALL_ONES;
            dst[w] |= (lo >> r) | (hi << (64 - r));
        }
    }
}

inline void set_bits(Word *row, int x0, int len)
{
    int x1 = x0 + len; // exclusive
    while (x0 < x1) {
        const int w = x0 >> 6, b = x0 & 63;
        const int n = std::min(64 - b, x1 - x0);
        const Word m = (n == 64) ? ALL_ONES : (((Word(1) << n) - 1) << b);
        row[w] |= m;
        x0 += n;
    }
}

inline void clear_bits(Word *row, int x0, int len)
{
    int x1 = x0 + len;
    while (x0 < x1) {
        const int w = x0 >> 6, b = x0 & 63;
        const int n = std::min(64 - b, x1 - x0);
        const Word m = (n == 64) ? ALL_ONES : (((Word(1) << n) - 1) << b);
        row[w] &= ~m;
        x0 += n;
    }
}

// Index of the lowest / highest set bit of a non-zero word.
inline int ctz64(Word v)
{
#ifdef _MSC_VER
    unsigned long i;
    _BitScanForward64(&i, v);
    return int(i);
#else
    return __builtin_ctzll(v);
#endif
}
inline int msb64(Word v)
{
#ifdef _MSC_VER
    unsigned long i;
    _BitScanReverse64(&i, v);
    return int(i);
#else
    return 63 - __builtin_clzll(v);
#endif
}

inline int floor_log2(int v)
{
    int k = 0;
    while ((2 << k) <= v) ++k;
    return k;
}

// Lowest zero bit in [0, xmax], -1 if none.
inline int lowest_zero(const Word *row, int xmax)
{
    for (int w = 0; (w << 6) <= xmax; ++w) {
        Word z = ~row[w];
        if (z) {
            const int x = (w << 6) + ctz64(z);
            return x <= xmax ? x : -1;
        }
    }
    return -1;
}

// Highest zero bit in [0, xmax], -1 if none.
inline int highest_zero(const Word *row, int xmax)
{
    for (int w = xmax >> 6; w >= 0; --w) {
        Word z = ~row[w];
        if (w == (xmax >> 6)) {
            const int b = xmax & 63;
            if (b < 63) z &= (Word(1) << (b + 1)) - 1;
        }
        if (z) return (w << 6) + msb64(z);
    }
    return -1;
}

// ----------------------------------------------------------------------------------------------
// Scanline rasterization: a cell is set when its center lies inside the shape (even-odd rule over
// all contours and holes). Shapes are pre-inflated by half of the cell diagonal, therefore every
// cell touched by the original shape gets set (conservative rasterization).
// ----------------------------------------------------------------------------------------------
struct Edge { double x0, y0, x1, y1; };

static void collect_edges(const ExPolygons &shape, std::vector<Edge> &edges)
{
    auto add = [&edges](const Polygon &p) {
        const size_t n = p.points.size();
        for (size_t i = 0; i < n; ++i) {
            const Point &a = p.points[i];
            const Point &b = p.points[(i + 1) % n];
            if (a.y() != b.y()) edges.push_back({double(a.x()), double(a.y()), double(b.x()), double(b.y())});
        }
    };
    for (const ExPolygon &ex : shape) {
        add(ex.contour);
        for (const Polygon &h : ex.holes) add(h);
    }
}

static Rows scanline(const ExPolygons &shape, double minx, double miny, double s, int w, int h)
{
    std::vector<Edge> edges;
    collect_edges(shape, edges);
    // Bucket edges by the rows they span so every row only looks at relevant edges.
    std::vector<std::vector<int>> bucket(std::max(h, 0));
    for (int e = 0; e < int(edges.size()); ++e) {
        const double ylo = std::min(edges[e].y0, edges[e].y1), yhi = std::max(edges[e].y0, edges[e].y1);
        int i0 = std::max(0, int(std::floor((ylo - miny) / s - 0.5)));
        int i1 = std::min(h - 1, int(std::ceil((yhi - miny) / s - 0.5)));
        for (int i = i0; i <= i1; ++i) bucket[i].push_back(e);
    }
    Rows rows(std::max(h, 0));
    std::vector<double> xs;
    for (int i = 0; i < h; ++i) {
        const double yc = miny + (i + 0.5) * s;
        xs.clear();
        for (int e : bucket[i]) {
            const Edge &E = edges[e];
            if ((E.y0 <= yc) != (E.y1 <= yc))
                xs.push_back(E.x0 + (yc - E.y0) * (E.x1 - E.x0) / (E.y1 - E.y0));
        }
        std::sort(xs.begin(), xs.end());
        auto &row = rows[i];
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
            int j0 = int(std::ceil((xs[k] - minx) / s - 0.5));
            int j1 = int(std::floor((xs[k + 1] - minx) / s - 0.5));
            j0 = std::max(j0, 0);
            j1 = std::min(j1, w - 1);
            if (j0 > j1) continue;
            if (!row.empty() && row.back().a + row.back().len >= j0)
                row.back().len = std::max(row.back().len, j1 + 1 - row.back().a);
            else
                row.push_back({j0, j1 - j0 + 1});
        }
    }
    return rows;
}

// ----------------------------------------------------------------------------------------------
// Occupancy grid with dilation levels: level k row y has bit x set when any of the cells
// x .. x + 2^k - 1 of row y is occupied. With these, "is the span [x + a, x + a + L) free"
// for all x at once costs two shifted ORs per word.
// ----------------------------------------------------------------------------------------------
struct Occupancy {
    int H = 0, nw = 0, K = 0;
    std::vector<Word> lv; // (K + 1) * H * nw

    Word       *row(int k, int y)       { return &lv[(size_t(k) * H + y) * nw]; }
    const Word *row(int k, int y) const { return &lv[(size_t(k) * H + y) * nw]; }

    void init(int H_, int nw_, int K_)
    {
        H = H_; nw = nw_; K = K_;
        lv.assign(size_t(K + 1) * H * nw, ALL_ONES);
    }
    void rebuild_row(int y)
    {
        for (int k = 1; k <= K; ++k) {
            Word *dst = row(k, y);
            const Word *src = row(k - 1, y);
            std::copy(src, src + nw, dst);
            shr_or(dst, src, nw, 1 << (k - 1));
        }
    }
    void rebuild() { for (int y = 0; y < H; ++y) rebuild_row(y); }

    // OR spans placed at (X, Y) into the grid (clipped) and refresh the dilation levels.
    void stamp(const Rows &rows, int X, int Y, int W)
    {
        for (int i = 0; i < int(rows.size()); ++i) {
            const int y = Y + i;
            if (y < 0 || y >= H || rows[i].empty()) continue;
            Word *r = row(0, y);
            for (const Span &sp : rows[i]) {
                int x0 = std::max(0, X + sp.a), x1 = std::min(W, X + sp.a + sp.len);
                if (x0 < x1) set_bits(r, x0, x1 - x0);
            }
            rebuild_row(y);
        }
    }
};

// ----------------------------------------------------------------------------------------------
// Item rasters
// ----------------------------------------------------------------------------------------------
struct Raster {
    double angle = 0.;   // absolute rotation applied to ArrangePolygon::poly
    int    tier  = 0;    // lower tiers are preferred, higher ones only used if nothing else fits
    int    w = 0, h = 0;
    double minx = 0., miny = 0.; // item-local (rotated) coordinate of the raster origin
    Rows   rows;
    size_t cells = 0;
};

struct ItemData {
    int                 idx = -1;      // index into the input vector
    ExPolygons          shape;         // footprint used for nesting (unrotated, local coordinates)
    double              area = 0.;     // real area (not inflated), scaled^2
    double              inflation = 0.;
    std::vector<Raster> rasters;
    std::vector<Raster> boxes;         // filled bounding rectangles of `rasters` (same origin)
    bool                fits_empty = false;
    int                 fit_count  = 0;   // number of rasters fitting an empty bed
    int                 max_dim    = 0;   // cells
    int                 bbox_cells = 0;
};

static bool same_rows(const Rows &a, const Rows &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].size() != b[i].size()) return false;
        for (size_t j = 0; j < a[i].size(); ++j)
            if (a[i][j].a != b[i][j].a || a[i][j].len != b[i][j].len) return false;
    }
    return true;
}

static Raster make_raster(const ExPolygons &shape, double angle, double grow, double s, int tier)
{
    Raster r;
    r.angle = angle;
    r.tier  = tier;
    ExPolygons rot = shape;
    for (ExPolygon &e : rot) e.rotate(angle);
    ExPolygons infl = grow > 0 ? offset_ex(rot, float(grow)) : rot;
    if (infl.empty()) return r;
    BoundingBox bb = get_extents(infl);
    const int w = int(std::ceil((bb.max.x() - bb.min.x()) / s)) + 1;
    const int h = int(std::ceil((bb.max.y() - bb.min.y()) / s)) + 1;
    Rows rows = scanline(infl, double(bb.min.x()), double(bb.min.y()), s, w, h);
    // trim empty borders
    int i0 = 0, i1 = h - 1, j0 = w, j1 = -1;
    while (i0 <= i1 && rows[i0].empty()) ++i0;
    while (i1 >= i0 && rows[i1].empty()) --i1;
    if (i0 > i1) return r;
    for (int i = i0; i <= i1; ++i)
        for (const Span &sp : rows[i]) { j0 = std::min(j0, sp.a); j1 = std::max(j1, sp.a + sp.len - 1); }
    r.rows.assign(rows.begin() + i0, rows.begin() + i1 + 1);
    for (auto &row : r.rows)
        for (Span &sp : row) { sp.a -= j0; r.cells += sp.len; }
    r.w    = j1 - j0 + 1;
    r.h    = i1 - i0 + 1;
    r.minx = double(bb.min.x()) + j0 * s;
    r.miny = double(bb.min.y()) + i0 * s;
    return r;
}

// Rotation which turns the minimum area bounding box of the convex hull axis aligned.
// Also reports the box size after that rotation.
static double min_area_box_angle(const ExPolygons &shape, double &bw, double &bh)
{
    Points pts;
    for (const ExPolygon &e : shape) append(pts, e.contour.points);
    Polygon hull = Geometry::convex_hull(pts);
    double best = 0., best_area = std::numeric_limits<double>::max();
    bw = bh = 0.;
    const size_t n = hull.points.size();
    for (size_t i = 0; i < n; ++i) {
        const Point &a = hull.points[i], &b = hull.points[(i + 1) % n];
        const double ang = std::atan2(double(b.y() - a.y()), double(b.x() - a.x()));
        const double c = std::cos(-ang), sn = std::sin(-ang);
        double x0 = 1e300, x1 = -1e300, y0 = 1e300, y1 = -1e300;
        for (const Point &p : hull.points) {
            const double x = c * p.x() - sn * p.y(), y = sn * p.x() + c * p.y();
            x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
        }
        const double area = (x1 - x0) * (y1 - y0);
        if (area < best_area) { best_area = area; best = -ang; bw = x1 - x0; bh = y1 - y0; }
    }
    return best;
}

// ----------------------------------------------------------------------------------------------
// The nester
// ----------------------------------------------------------------------------------------------
enum Gravity : int { G_CENTER = 0, G_CORNER_FIRST = 1, G_COUNT = 9 };
// corner gravities 1..8: c = g - 1; bit 0: right side, bit 1: top side, bit 2: X is the primary axis

struct Placement { int item; int raster; int X; int Y; };

struct Strategy {
    std::vector<int> order; // indices into items
    int              gravity = G_CENTER;
    // Place the bounding rectangles instead of the real shapes. Greedy placement of round or
    // irregular parts tends to drop parts into the valleys between their neighbours, which can
    // leave less room than a plain grid; packing the boxes yields such grids.
    bool             boxes = false;
};

struct BinResult {
    std::vector<Placement> placed;
    double                 area = 0.;
    long long              pile_area = 0; // bbox of the pile in cells^2
    int                    strategy = -1;
};

class Nester {
public:
    Nester(ArrangePolygons &items, const ArrangePolygons &excludes, const Polygon &bed, const ArrangeParams &params)
        : m_items(items), m_excludes(excludes), m_bed(bed), m_params(params)
    {}

    void run();

private:
    ArrangePolygons       &m_items;
    const ArrangePolygons &m_excludes;
    Polygon                m_bed;
    const ArrangeParams   &m_params;

    double m_s = 0., m_ox = 0., m_oy = 0., m_halfdiag = 0.;
    int    m_W = 0, m_H = 0, m_nw = 0, m_K = 0;
    Occupancy m_bed_occ;        // bed outline + global excluded regions
    std::vector<ItemData> m_data;

    bool stopped() const { return m_params.stopcondition && m_params.stopcondition(); }

    void   setup_grid();
    void   stamp_shape(Occupancy &occ, const ExPolygons &shape, double grow) const;
    ExPolygons world_shape(const ArrangePolygon &ap) const;
    void   prepare_items();
    bool   fits(const Occupancy &occ, const Raster &r) const;

    void   bad_row(const Occupancy &occ, const Raster &r, int y, Word *out, Word *tmp) const;
    bool   find(const Occupancy &occ, const Raster &r, int gravity, const int pile[4], double tx, double ty,
                int &X, int &Y, double &score) const;

    bool   compatible(const ArrangePolygon &ap, const std::vector<const ArrangePolygon *> &members) const;

    BinResult pack_bin(const Strategy &st, const Occupancy &hard, const Occupancy *soft,
                       const std::vector<const ArrangePolygon *> &fixed_members, int sidx) const;
    void   align_pile(BinResult &res, const Occupancy &hard, const Occupancy *soft) const;
    std::vector<Strategy> make_strategies(const std::vector<int> &remaining) const;

    double target_x() const { return m_params.align_center.x() * m_W; }
    double target_y() const { return m_params.align_center.y() * m_H; }
};

void Nester::setup_grid()
{
    BoundingBox bb = m_bed.bounding_box();
    const double maxdim = double(std::max(bb.max.x() - bb.min.x(), bb.max.y() - bb.min.y()));
    // ~640 cells along the longer bed side, but never finer than 0.25 mm nor coarser than 2 mm.
    m_s  = std::clamp(maxdim / 640., scale_(0.25), scale_(2.0));
    // Many items: coarser grid keeps the search fast.
    if (m_items.size() > 60) m_s = std::max(m_s, std::min(scale_(2.0), maxdim / 400.));
    m_halfdiag = m_s * 0.5 * std::sqrt(2.);
    m_ox = double(bb.min.x());
    m_oy = double(bb.min.y());
    m_W  = std::max(1, int(std::ceil(double(bb.max.x() - bb.min.x()) / m_s)));
    m_H  = std::max(1, int(std::ceil(double(bb.max.y() - bb.min.y()) / m_s)));
    m_nw = (m_W + 63) / 64 + 1;
    m_K  = floor_log2(m_W);

    // Free cells are those lying completely inside the bed.
    m_bed_occ.init(m_H, m_nw, m_K);
    ExPolygons inner = offset_ex(ExPolygon(m_bed), float(-m_halfdiag));
    Rows free_rows = scanline(inner, m_ox, m_oy, m_s, m_W, m_H);
    for (int y = 0; y < m_H; ++y)
        for (const Span &sp : free_rows[y]) clear_bits(m_bed_occ.row(0, y), sp.a, sp.len);

    // Globally excluded regions (same on every plate).
    for (const ArrangePolygon &ex : m_params.excluded_regions)
        stamp_shape(m_bed_occ, world_shape(ex), std::max(double(ex.inflation), 0.));
    m_bed_occ.rebuild();
}

ExPolygons Nester::world_shape(const ArrangePolygon &ap) const
{
    ExPolygons shape = ap.footprint.empty() ? ExPolygons{ap.poly} : ap.footprint;
    for (ExPolygon &e : shape) {
        e.rotate(ap.rotation);
        e.translate(ap.translation.x(), ap.translation.y());
    }
    return shape;
}

void Nester::stamp_shape(Occupancy &occ, const ExPolygons &shape, double grow) const
{
    ExPolygons infl = offset_ex(shape, float(grow + m_halfdiag));
    if (infl.empty()) return;
    BoundingBox bb = get_extents(infl);
    const int j0 = std::max(0, int(std::floor((bb.min.x() - m_ox) / m_s)) - 1);
    const int i0 = std::max(0, int(std::floor((bb.min.y() - m_oy) / m_s)) - 1);
    const int j1 = std::min(m_W - 1, int(std::ceil((bb.max.x() - m_ox) / m_s)) + 1);
    const int i1 = std::min(m_H - 1, int(std::ceil((bb.max.y() - m_oy) / m_s)) + 1);
    if (j0 > j1 || i0 > i1) return;
    Rows rows = scanline(infl, m_ox + j0 * m_s, m_oy + i0 * m_s, m_s, j1 - j0 + 1, i1 - i0 + 1);
    for (int i = 0; i < int(rows.size()); ++i)
        for (const Span &sp : rows[i]) set_bits(occ.row(0, i0 + i), j0 + sp.a, sp.len);
}

void Nester::prepare_items()
{
    const bool user_distance = m_params.min_obj_distance > 0;
    m_data.resize(m_items.size());
    for (size_t i = 0; i < m_items.size(); ++i) {
        const ArrangePolygon &ap = m_items[i];
        ItemData &d = m_data[i];
        d.idx = int(i);
        ExPolygons shape = ap.footprint.empty() ? ExPolygons{ap.poly} : ap.footprint;
        if (!m_params.allow_nesting_in_holes || ap.is_virt_object)
            for (ExPolygon &e : shape) e.holes.clear();
        d.shape = union_ex(shape);
        for (const ExPolygon &e : d.shape) d.area += e.area();
        double infl = std::max(double(ap.inflation), MIN_SEPARATION);
        // Rasterization adds up to half a cell diagonal of extra clearance around every object. With
        // automatic spacing and no support/brim margin, let that absorb part of the default 1 mm
        // separation so tight fits are not lost to quantization (the gap between two such objects is
        // then at least 2 * (1 mm - half cell diagonal), ~1.4 mm on a 256 mm bed). Support margins
        // and user defined spacing are always kept in full.
        if (!user_distance && !ap.is_virt_object && double(ap.inflation) <= MIN_SEPARATION)
            infl = std::max(infl - m_halfdiag, 0.5 * infl);
        d.inflation = infl;
    }

    // Candidate angles per item, by tier.
    struct Job { int item; double angle; int tier; };
    std::vector<std::vector<Job>> jobs(m_items.size());
    const double deg = M_PI / 180.;
    for (size_t i = 0; i < m_items.size(); ++i) {
        const ArrangePolygon &ap = m_items[i];
        const ItemData &d = m_data[i];
        auto &J = jobs[i];
        const double r0 = ap.rotation;
        if (d.shape.empty()) continue;
        const bool fixed_rot = ap.is_virt_object || ap.is_wipe_tower;
        double bw, bh;
        const double mab = fixed_rot ? 0. : min_area_box_angle(d.shape, bw, bh);
        if (fixed_rot) {
            J.push_back({int(i), r0, 0});
        } else if (m_params.allow_rotations) {
            for (int k = 0; k < 12; ++k) J.push_back({int(i), r0 + k * 30. * deg, 0});
            for (int k = 0; k < 4; ++k) J.push_back({int(i), mab + k * 90. * deg, 0});
        } else if (m_params.align_to_y_axis && (bw > bh * 1.1 || bh > bw * 1.1)) {
            // long side along Y, original orientation as fallback
            const double ay = mab + (bw > bh ? 90. * deg : 0.);
            J.push_back({int(i), ay, 0});
            J.push_back({int(i), ay + 180. * deg, 0});
            J.push_back({int(i), r0, 1});
        } else {
            J.push_back({int(i), r0, 0});
        }
    }

    auto normalize = [](double a) { a = std::fmod(a, 2. * M_PI); return a < 0 ? a + 2. * M_PI : a; };
    auto build = [&](std::vector<std::vector<Job>> &all) {
        std::vector<Job> flat;
        for (auto &J : all) {
            // drop duplicate angles
            std::vector<Job> uniq;
            for (const Job &j : J) {
                bool dup = false;
                for (const Job &u : uniq)
                    if (std::abs(normalize(u.angle - j.angle)) < 1e-4 || std::abs(normalize(u.angle - j.angle) - 2. * M_PI) < 1e-4) { dup = true; break; }
                if (!dup) uniq.push_back(j);
            }
            J = uniq;
            append(flat, J);
        }
        std::vector<Raster> out(flat.size());
        tbb::parallel_for(tbb::blocked_range<size_t>(0, flat.size()), [&](const tbb::blocked_range<size_t> &range) {
            for (size_t k = range.begin(); k < range.end(); ++k) {
                const Job &j = flat[k];
                out[k] = make_raster(m_data[j.item].shape, j.angle, m_data[j.item].inflation + m_halfdiag, m_s, j.tier);
            }
        });
        for (size_t k = 0; k < flat.size(); ++k) {
            ItemData &d = m_data[flat[k].item];
            if (out[k].rows.empty()) continue;
            bool dup = false;
            for (const Raster &r : d.rasters)
                if (r.tier == out[k].tier && same_rows(r.rows, out[k].rows)) { dup = true; break; }
            if (!dup) d.rasters.push_back(std::move(out[k]));
        }
    };
    build(jobs);

    // Rescue rotations for objects which do not fit the empty bed at any of their candidate angles,
    // e.g. long parts which only fit along the diagonal.
    std::vector<std::vector<Job>> rescue(m_items.size());
    bool any_rescue = false;
    for (size_t i = 0; i < m_items.size(); ++i) {
        ItemData &d = m_data[i];
        d.fit_count = 0;
        for (const Raster &r : d.rasters)
            if (fits(m_bed_occ, r)) ++d.fit_count;
        d.fits_empty = d.fit_count > 0;
        const ArrangePolygon &ap = m_items[i];
        if (!d.fits_empty && !d.shape.empty() && !ap.is_virt_object && !ap.is_wipe_tower) {
            double bw, bh;
            const double mab = min_area_box_angle(d.shape, bw, bh);
            for (int k = 0; k < 4; ++k) rescue[i].push_back({int(i), mab + k * 90. * deg, 2});
            for (int k = 1; k < 72; ++k) rescue[i].push_back({int(i), ap.rotation + k * 5. * deg, 2});
            any_rescue = true;
        }
    }
    if (any_rescue) {
        build(rescue);
        for (size_t i = 0; i < m_items.size(); ++i) {
            ItemData &d = m_data[i];
            if (d.fits_empty) continue;
            std::vector<Raster> keep;
            for (Raster &r : d.rasters)
                if (r.tier == 2 && fits(m_bed_occ, r)) keep.push_back(std::move(r));
            d.rasters = std::move(keep);
            d.fit_count = int(d.rasters.size());
            d.fits_empty = d.fit_count > 0;
            if (d.fits_empty)
                BOOST_LOG_TRIVIAL(info) << "raster arrange: " << m_items[i].name << " only fits rotated, " << d.fit_count << " angles";
        }
    }

    for (ItemData &d : m_data) {
        d.boxes.clear();
        for (const Raster &r : d.rasters) {
            Raster b = r;
            b.rows.assign(r.h, std::vector<Span>{Span{0, r.w}});
            b.cells = size_t(r.w) * r.h;
            d.boxes.push_back(std::move(b));
        }
        for (const Raster &r : d.rasters) {
            d.max_dim    = std::max(d.max_dim, std::max(r.w, r.h));
            d.bbox_cells = std::max(d.bbox_cells, r.w * r.h);
        }
    }
}

// Bit x of `out` set <=> the raster placed with its origin at (x, y) collides.
void Nester::bad_row(const Occupancy &occ, const Raster &r, int y, Word *out, Word *tmp) const
{
    std::fill(out, out + m_nw, Word(0));
    for (int i = 0; i < r.h; ++i) {
        const auto &spans = r.rows[i];
        if (spans.empty()) continue;
        for (const Span &sp : spans) {
            const int k = std::min(floor_log2(sp.len), occ.K);
            const Word *P = occ.row(k, y + i);
            std::copy(P, P + m_nw, tmp);
            int rest = sp.len - (1 << k);
            // spans longer than the top level (never for a bed sized raster) are covered in chunks
            int off = 0;
            while (rest > 0) {
                const int step = std::min(rest, 1 << k);
                off += step;
                shr_or(tmp, P, m_nw, off);
                rest -= step;
            }
            shr_or(out, tmp, m_nw, sp.a);
        }
        if ((i & 15) == 15) {
            bool all = true;
            for (int w = 0; w < m_nw && all; ++w) all = out[w] == ALL_ONES;
            if (all) return;
        }
    }
}

bool Nester::fits(const Occupancy &occ, const Raster &r) const
{
    if (r.w > m_W || r.h > m_H) return false;
    std::vector<Word> bad(m_nw), tmp(m_nw);
    for (int y = 0; y + r.h <= m_H; ++y) {
        bad_row(occ, r, y, bad.data(), tmp.data());
        if (lowest_zero(bad.data(), m_W - r.w) >= 0) return true;
    }
    return false;
}

// pile: x0, y0, x1, y1 (exclusive) of already placed items, x0 > x1 means empty
bool Nester::find(const Occupancy &occ, const Raster &r, int gravity, const int pile[4], double tx, double ty,
                  int &X, int &Y, double &score) const
{
    if (r.w > m_W || r.h > m_H) return false;
    const int ymax = m_H - r.h, xmax = m_W - r.w;
    std::vector<Word> bad(m_nw), tmp(m_nw);

    if (gravity != G_CENTER && ((gravity - 1) & 4) == 0) {
        // Corner gravity, Y primary: scan rows from the gravity side and stop at the first hit.
        const bool right = (gravity - 1) & 1, top = (gravity - 1) & 2;
        for (int t = 0; t <= ymax; ++t) {
            const int y = top ? ymax - t : t;
            bad_row(occ, r, y, bad.data(), tmp.data());
            const int x = right ? highest_zero(bad.data(), xmax) : lowest_zero(bad.data(), xmax);
            if (x >= 0) {
                X = x; Y = y;
                score = double(t) * (m_W + 1) + (right ? xmax - x : x);
                return true;
            }
        }
        return false;
    }

    bool   found = false;
    double best  = std::numeric_limits<double>::max();
    const bool pile_empty = pile[0] > pile[2];
    const double cx_t = pile_empty ? tx : 0.5 * (pile[0] + pile[2]);
    const double cy_t = pile_empty ? ty : 0.5 * (pile[1] + pile[3]);
    const bool right = gravity != G_CENTER && ((gravity - 1) & 1);
    const bool top   = gravity != G_CENTER && ((gravity - 1) & 2);
    for (int y = 0; y <= ymax; ++y) {
        bad_row(occ, r, y, bad.data(), tmp.data());
        for (int w = 0; (w << 6) <= xmax; ++w) {
            Word z = ~bad[w];
            while (z) {
                const int x = (w << 6) + ctz64(z);
                z &= z - 1;
                if (x > xmax) break;
                double sc;
                if (gravity == G_CENTER) {
                    int ux0 = x, uy0 = y, ux1 = x + r.w, uy1 = y + r.h;
                    if (!pile_empty) {
                        ux0 = std::min(ux0, pile[0]); uy0 = std::min(uy0, pile[1]);
                        ux1 = std::max(ux1, pile[2]); uy1 = std::max(uy1, pile[3]);
                    }
                    const double Wu = ux1 - ux0, Hu = uy1 - uy0;
                    const double dx = x + 0.5 * r.w - cx_t, dy = y + 0.5 * r.h - cy_t;
                    sc = Wu + Hu + std::max(Wu, Hu) + 0.5 * std::sqrt(dx * dx + dy * dy);
                } else {
                    // X primary corner gravity
                    const double px = right ? xmax - x : x;
                    const double py = top ? ymax - y : y;
                    sc = px * (m_H + 1) + py;
                }
                if (sc < best) { best = sc; X = x; Y = y; found = true; }
            }
        }
    }
    score = best;
    return found;
}

bool Nester::compatible(const ArrangePolygon &ap, const std::vector<const ArrangePolygon *> &members) const
{
    if (ap.is_virt_object) return true;
    std::map<int, std::string> ids;
    std::set<int> tpu;
    for (const ArrangePolygon *p : members) {
        if (p->is_virt_object) continue;
        if (!Print::is_filaments_compatible({ap.filament_temp_type, p->filament_temp_type})) return false;
        ids.insert(p->extrude_id_filament_types.begin(), p->extrude_id_filament_types.end());
        for (auto &it : p->extrude_id_filament_types) if (it.second == "TPU") tpu.insert(it.first);
    }
    if (ids.empty()) return true; // first object on the plate
    for (auto &it : ap.extrude_id_filament_types) if (it.second == "TPU") tpu.insert(it.first);
    if (tpu.size() > 1) return false;
    if (!m_params.allow_multi_materials_on_same_plate && !ids.empty() &&
        !std::includes(ids.begin(), ids.end(), ap.extrude_id_filament_types.begin(), ap.extrude_id_filament_types.end()))
        return false;
    return true;
}

BinResult Nester::pack_bin(const Strategy &st, const Occupancy &hard_in, const Occupancy *soft_in,
                           const std::vector<const ArrangePolygon *> &fixed_members, int sidx) const
{
    BinResult res;
    res.strategy = sidx;
    Occupancy hard = hard_in;
    std::unique_ptr<Occupancy> soft;
    if (soft_in) soft = std::make_unique<Occupancy>(*soft_in);
    std::vector<const ArrangePolygon *> members = fixed_members;
    int pile[4] = {m_W, m_H, -1, -1};
    for (int it : st.order) {
        if (stopped()) break;
        const ItemData &d = m_data[it];
        const ArrangePolygon &ap = m_items[d.idx];
        if (!compatible(ap, members)) continue;
        int best_r = -1, bX = 0, bY = 0;
        // Prefer placements outside of the non-preferred regions, then the lowest rotation tier.
        const std::vector<Raster> &rasters = st.boxes ? d.boxes : d.rasters;
        for (int pass = soft ? 0 : 1; pass < 2 && best_r < 0; ++pass) {
            const Occupancy &occ = pass == 0 ? *soft : hard;
            int tier_found = INT_MAX;
            double best = std::numeric_limits<double>::max();
            for (int ri = 0; ri < int(rasters.size()); ++ri) {
                const Raster &r = rasters[ri];
                if (r.tier > tier_found) continue;
                int X, Y; double sc;
                if (find(occ, r, st.gravity, pile, target_x(), target_y(), X, Y, sc)) {
                    if (r.tier < tier_found || sc < best) {
                        tier_found = r.tier; best = sc; best_r = ri; bX = X; bY = Y;
                    }
                }
            }
        }
        if (best_r < 0) continue;
        const Raster &r = rasters[best_r];
        hard.stamp(r.rows, bX, bY, m_W);
        if (soft) soft->stamp(r.rows, bX, bY, m_W);
        members.push_back(&ap);
        res.placed.push_back({it, best_r, bX, bY});
        res.area += d.area;
        pile[0] = std::min(pile[0], bX); pile[1] = std::min(pile[1], bY);
        pile[2] = std::max(pile[2], bX + r.w); pile[3] = std::max(pile[3], bY + r.h);
    }
    if (!res.placed.empty())
        res.pile_area = (long long)(pile[2] - pile[0]) * (pile[3] - pile[1]);
    return res;
}

// Move the whole pile as close as possible to the preferred object position.
void Nester::align_pile(BinResult &res, const Occupancy &hard, const Occupancy *soft) const
{
    if (res.placed.empty()) return;
    int x0 = m_W, y0 = m_H, x1 = -1, y1 = -1;
    for (const Placement &p : res.placed) {
        const Raster &r = m_data[p.item].rasters[p.raster];
        x0 = std::min(x0, p.X); y0 = std::min(y0, p.Y);
        x1 = std::max(x1, p.X + r.w); y1 = std::max(y1, p.Y + r.h);
    }
    // Build the union of the pile as one raster.
    Raster pile;
    pile.w = x1 - x0; pile.h = y1 - y0;
    const int pnw = (pile.w + 63) / 64 + 1;
    std::vector<Word> bits(size_t(pile.h) * pnw, 0);
    for (const Placement &p : res.placed) {
        const Raster &r = m_data[p.item].rasters[p.raster];
        for (int i = 0; i < r.h; ++i)
            for (const Span &sp : r.rows[i]) set_bits(&bits[size_t(p.Y - y0 + i) * pnw], p.X - x0 + sp.a, sp.len);
    }
    pile.rows.resize(pile.h);
    for (int i = 0; i < pile.h; ++i) {
        const Word *row = &bits[size_t(i) * pnw];
        int x = 0;
        while (x < pile.w) {
            if (!((row[x >> 6] >> (x & 63)) & 1)) { ++x; continue; }
            int a = x;
            while (x < pile.w && ((row[x >> 6] >> (x & 63)) & 1)) ++x;
            pile.rows[i].push_back({a, x - a});
        }
    }
    // desired pile origin
    const double tx = target_x() - 0.5 * pile.w, ty = target_y() - 0.5 * pile.h;
    std::vector<Word> bad(m_nw), tmp(m_nw);
    for (const Occupancy *occ : {soft, &hard}) {
        if (!occ) continue;
        int bestX = -1, bestY = -1;
        double best = std::numeric_limits<double>::max();
        for (int y = 0; y + pile.h <= m_H; ++y) {
            const double dy = y - ty;
            if (dy * dy >= best) continue;
            bad_row(*occ, pile, y, bad.data(), tmp.data());
            for (int w = 0; (w << 6) <= m_W - pile.w; ++w) {
                Word z = ~bad[w];
                while (z) {
                    const int x = (w << 6) + ctz64(z);
                    z &= z - 1;
                    if (x > m_W - pile.w) break;
                    const double dx = x - tx, dd = dx * dx + dy * dy;
                    if (dd < best) { best = dd; bestX = x; bestY = y; }
                }
            }
        }
        if (bestX >= 0) {
            for (Placement &p : res.placed) { p.X += bestX - x0; p.Y += bestY - y0; }
            return;
        }
    }
}

std::vector<Strategy> Nester::make_strategies(const std::vector<int> &remaining) const
{
    auto prio_then = [this](auto key) {
        return [this, key](int a, int b) {
            const int pa = m_items[m_data[a].idx].priority, pb = m_items[m_data[b].idx].priority;
            if (pa != pb) return pa > pb;
            return key(a, b);
        };
    };
    std::vector<std::vector<int>> orders;
    auto add_order = [&](auto key) {
        std::vector<int> o = remaining;
        std::stable_sort(o.begin(), o.end(), prio_then(key));
        for (const auto &e : orders) if (e == o) return;
        orders.push_back(std::move(o));
    };
    add_order([this](int a, int b) { return m_data[a].area > m_data[b].area; });
    add_order([this](int a, int b) {
        // hardest first: objects which fit the bed at few angles, then big ones
        if ((m_data[a].fit_count <= 2) != (m_data[b].fit_count <= 2)) return m_data[a].fit_count <= 2;
        return m_data[a].area > m_data[b].area;
    });
    add_order([this](int a, int b) { return m_data[a].max_dim > m_data[b].max_dim; });
    add_order([this](int a, int b) { return m_data[a].bbox_cells > m_data[b].bbox_cells; });

    size_t rasters = 0;
    for (int i : remaining) rasters += m_data[i].rasters.size();
    // Spend less effort on big jobs.
    const bool big   = remaining.size() > 40 || rasters > 400;
    const bool large = remaining.size() > 150 || rasters > 1500;
    if (!big && remaining.size() >= 3) {
        std::mt19937 rng(12345);
        for (int k = 0; k < 6; ++k) {
            std::vector<int> o = orders.front();
            for (int s = 0; s < int(o.size()) / 3 + 1; ++s) {
                const int i = int(rng() % o.size()), j = int(rng() % o.size());
                if (m_items[m_data[o[i]].idx].priority == m_items[m_data[o[j]].idx].priority) std::swap(o[i], o[j]);
            }
            bool dup = false;
            for (const auto &e : orders) if (e == o) { dup = true; break; }
            if (!dup) orders.push_back(std::move(o));
        }
    }

    std::vector<Strategy> out;
    if (large) {
        out.push_back({orders[0], G_CENTER});
        out.push_back({orders[0], G_CORNER_FIRST});
        out.push_back({orders[0], G_CORNER_FIRST, true});
    } else if (big) {
        for (size_t o = 0; o < std::min<size_t>(2, orders.size()); ++o) {
            for (int g : {int(G_CENTER), 1, 2, 3, 4})
                out.push_back({orders[o], g});
            for (int g : {int(G_CENTER), 1, 5})
                out.push_back({orders[o], g, true});
        }
    } else {
        for (size_t o = 0; o < orders.size(); ++o) {
            for (int g = 0; g < G_COUNT; ++g)
                out.push_back({orders[o], g});
            // grid-like packings of the bounding boxes (only the deterministic orders)
            if (o < 4)
                for (int g : {int(G_CENTER), 1, 2, 5, 6})
                    out.push_back({orders[o], g, true});
        }
    }
    return out;
}

void Nester::run()
{
    auto t0 = std::chrono::steady_clock::now();
    setup_grid();
    prepare_items();

    for (ArrangePolygon &ap : m_items) ap.bed_idx = UNARRANGED;

    std::vector<int> remaining;
    for (size_t i = 0; i < m_data.size(); ++i) {
        if (m_data[i].fits_empty) remaining.push_back(int(i));
        else BOOST_LOG_TRIVIAL(warning) << "raster arrange: " << m_items[i].name << " does not fit the bed";
    }

    int max_fixed_bed = -1;
    for (const ArrangePolygon &ex : m_excludes) max_fixed_bed = std::max(max_fixed_bed, ex.bed_idx);

    const size_t total = remaining.size();
    for (int bin = 0; !remaining.empty() && bin < 1000; ++bin) {
        if (stopped()) break;
        // Obstacles of this bin
        Occupancy hard = m_bed_occ;
        std::unique_ptr<Occupancy> soft;
        std::vector<const ArrangePolygon *> fixed_members;
        for (const ArrangePolygon &ex : m_excludes) {
            if (ex.bed_idx != bin) continue;
            double infl = std::max(double(ex.inflation), MIN_SEPARATION);
            if (!ex.is_virt_object && m_params.min_obj_distance <= 0 && double(ex.inflation) <= MIN_SEPARATION)
                infl = std::max(infl - m_halfdiag, 0.5 * infl);
            if (ex.is_virt_object) infl = std::max(double(ex.inflation), 0.);
            ExPolygons shape = world_shape(ex);
            if (!m_params.allow_nesting_in_holes || ex.is_virt_object)
                for (ExPolygon &e : shape) e.holes.clear();
            stamp_shape(hard, shape, infl);
            fixed_members.push_back(&ex);
        }
        hard.rebuild();
        if (!m_params.nonprefered_regions.empty()) {
            soft = std::make_unique<Occupancy>(hard);
            for (const ArrangePolygon &np : m_params.nonprefered_regions)
                stamp_shape(*soft, world_shape(np), std::max(double(np.inflation), 0.));
            soft->rebuild();
        }

        std::vector<Strategy> strategies = make_strategies(remaining);
        std::vector<BinResult> results(strategies.size());
        tbb::parallel_for(tbb::blocked_range<size_t>(0, strategies.size(), 1), [&](const tbb::blocked_range<size_t> &range) {
            for (size_t k = range.begin(); k < range.end(); ++k)
                results[k] = pack_bin(strategies[k], hard, soft.get(), fixed_members, int(k));
        });

        // Most placed area wins; among equal fills the most compact pile.
        int best = -1;
        for (int k = 0; k < int(results.size()); ++k) {
            if (results[k].placed.empty()) continue;
            if (best < 0) { best = k; continue; }
            const BinResult &a = results[k], &b = results[best];
            const double eps = 1e-6 * std::max(a.area, b.area);
            if (a.area > b.area + eps || (std::abs(a.area - b.area) <= eps && a.pile_area < b.pile_area))
                best = k;
        }
        if (best < 0) {
            // Nothing fits here (plate taken by fixed objects). Further plates are empty once we are past
            // the last plate with fixed objects, and every remaining item fits an empty plate.
            if (bin > max_fixed_bed) break;
            continue;
        }
        BinResult &res = results[best];
        if (m_params.do_final_align) align_pile(res, hard, soft.get());

        std::vector<char> done(m_data.size(), 0);
        for (const Placement &p : res.placed) {
            const ItemData &d = m_data[p.item];
            const Raster &r = d.rasters[p.raster];
            ArrangePolygon &ap = m_items[d.idx];
            ap.rotation = r.angle;
            // The raster was built from the shape rotated by `angle` around the item origin, exactly like
            // transformed_poly() rotates the original poly by `rotation` before translating it.
            ap.translation = Vec2crd(coord_t(std::llround(m_ox + p.X * m_s - r.minx)),
                                     coord_t(std::llround(m_oy + p.Y * m_s - r.miny)));
            ap.bed_idx = bin;
            done[p.item] = 1;
            if (m_params.on_packed) m_params.on_packed(ap);
        }
        remaining.erase(std::remove_if(remaining.begin(), remaining.end(), [&done](int i) { return done[i] != 0; }), remaining.end());

        BOOST_LOG_TRIVIAL(info) << "raster arrange: plate " << bin << " got " << res.placed.size() << " items with strategy " << best
                                << " (gravity " << strategies[best].gravity << "), " << remaining.size() << " left";
        if (m_params.progressind) m_params.progressind(unsigned(total - remaining.size()), res.placed.empty() ? std::string() : m_items[m_data[res.placed.back().item].idx].name);
    }

    BOOST_LOG_TRIVIAL(info) << "raster arrange: " << m_items.size() << " items, cell " << unscale_(m_s) << "mm, took "
                            << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() << "s";
}

} // namespace

void raster_arrange(ArrangePolygons &items, const ArrangePolygons &excludes, const Polygon &bed, const ArrangeParams &params)
{
    if (items.empty() || bed.points.size() < 3) return;
    Polygon b = bed;
    if (b.is_clockwise()) b.reverse();
    Nester nester(items, excludes, b, params);
    nester.run();
}

}} // namespace Slic3r::arrangement
