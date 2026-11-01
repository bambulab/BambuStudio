#include "FillConformalChart.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <queue>
#include <tuple>
#include <utility>
#include <vector>

#include "../BoundingBox.hpp"
#include "../ClipperUtils.hpp"
#include "../Geometry.hpp"
#include "../Point.hpp"
#include "../Polygon.hpp"
#include "../TriangleMesh.hpp"

namespace Slic3r {
namespace FillConformal {

static Point quantize_pt(const Point &p)
{
    const coord_t q = scaled<coord_t>(0.1);
    return Point(align_to_grid(p.x(), q), align_to_grid(p.y(), q));
}

static bool stitch_centerline(const Polylines &pieces, LayerGenerator &out)
{
    if (pieces.empty())
        return false;

    std::map<Point, int> node_id;
    auto id_of = [&node_id](const Point &p) {
        Point q  = quantize_pt(p);
        auto  it = node_id.find(q);
        if (it == node_id.end()) {
            int id = int(node_id.size());
            node_id.emplace(q, id);
            return id;
        }
        return it->second;
    };

    struct Edge
    {
        int    a;
        int    b;
        size_t idx;
    };
    std::vector<Edge>             edges;
    std::vector<std::vector<int>> adj;

    for (size_t i = 0; i < pieces.size(); ++i) {
        const Polyline &pl = pieces[i];
        if (pl.points.size() < 2)
            continue;
        int a = id_of(pl.front());
        int b = id_of(pl.back());
        if (int(adj.size()) <= std::max(a, b))
            adj.resize(std::max(a, b) + 1);
        int e = int(edges.size());
        edges.push_back({ a, b, i });
        adj[a].push_back(e);
        adj[b].push_back(e);
    }
    if (edges.empty())
        return false;

    const int n = int(adj.size());
    auto other = [](const Edge &e, int v) { return e.a == v ? e.b : e.a; };

    std::vector<char> used(edges.size(), 0);
    auto walk = [&](int start, bool closed_walk) {
        std::vector<int> chain;
        int              v = start;
        for (;;) {
            int next_e = -1;
            for (int ei : adj[v]) {
                if (!used[ei]) {
                    next_e = ei;
                    break;
                }
            }
            if (next_e < 0)
                break;
            used[next_e] = 1;
            chain.push_back(next_e);
            v = other(edges[next_e], v);
            if (closed_walk && v == start)
                break;
        }
        return chain;
    };

    auto concat = [&](const std::vector<int> &chain, int start, bool closed) {
        Polyline acc;
        int      v = start;
        for (int ei : chain) {
            const Edge &e  = edges[ei];
            Polyline    pl = pieces[e.idx];
            if (e.a != v)
                pl.reverse();
            if (acc.empty())
                acc.points = std::move(pl.points);
            else if (pl.points.size() > 1)
                acc.points.insert(acc.points.end(), pl.points.begin() + 1, pl.points.end());
            v = other(e, v);
        }
        if (closed && !acc.empty() && acc.front() != acc.back())
            acc.points.push_back(acc.front());
        return acc;
    };

    bool all_deg2 = true;
    for (int v = 0; v < n; ++v)
        if (adj[v].size() != 2 && !adj[v].empty())
            all_deg2 = false;

    if (all_deg2 && n > 0) {
        int start = 0;
        while (start < n && adj[start].empty())
            ++start;
        if (start < n) {
            std::vector<int> chain = walk(start, true);
            if (chain.size() >= 2) {
                out.polyline = concat(chain, start, true);
                out.closed   = true;
                return out.polyline.points.size() >= 4;
            }
        }
    }

    // Open generator: longest path in the skeleton forest (drop short branches).
    std::vector<double> elen(edges.size(), 0.);
    for (size_t i = 0; i < edges.size(); ++i)
        elen[i] = pieces[edges[i].idx].length();

    auto farthest = [&](int src) {
        std::vector<double> dist(n, -1.);
        std::vector<int>    parent(n, -1);
        std::vector<int>    parent_e(n, -1);
        std::queue<int>     q;
        dist[src] = 0.;
        q.push(src);
        int    best_v = src;
        double best_d = 0.;
        while (!q.empty()) {
            const int v = q.front();
            q.pop();
            if (dist[v] > best_d) {
                best_d = dist[v];
                best_v = v;
            }
            for (int ei : adj[v]) {
                const int u = other(edges[ei], v);
                if (dist[u] >= 0.)
                    continue;
                dist[u]     = dist[v] + elen[ei];
                parent[u]   = v;
                parent_e[u] = ei;
                q.push(u);
            }
        }
        return std::make_tuple(best_v, parent, parent_e);
    };

    int src = 0;
    for (int v = 0; v < n; ++v)
        if (adj[v].size() == 1) {
            src = v;
            break;
        }
    if (src >= n || adj[src].empty()) {
        src = 0;
        while (src < n && adj[src].empty())
            ++src;
        if (src >= n)
            return false;
    }

    int              u = 0, w = 0;
    std::vector<int> parent, parent_e;
    std::tie(u, parent, parent_e) = farthest(src);
    std::tie(w, parent, parent_e) = farthest(u);
    std::vector<int> chain;
    for (int x = w; x != u && x >= 0; x = parent[x]) {
        if (parent_e[x] < 0)
            break;
        chain.push_back(parent_e[x]);
    }
    std::reverse(chain.begin(), chain.end());
    if (chain.empty())
        return false;
    out.polyline = concat(chain, u, false);
    out.closed   = false;
    return out.polyline.points.size() >= 2;
}

static bool assign_closed_loop(const Polygon &loop, LayerGenerator &out)
{
    if (loop.points.size() < 3)
        return false;
    out.polyline.points = loop.points;
    out.closed          = true;
    if (!out.polyline.empty() && out.polyline.front() != out.polyline.back())
        out.polyline.points.push_back(out.polyline.front());
    return out.polyline.points.size() >= 4;
}

static bool generator_from_inset(const ExPolygon &expoly, LayerGenerator &out)
{
    BoundingBox ob = get_extents(expoly.contour);
    const double min_side = unscale<double>(std::min(ob.size().x(), ob.size().y()));
    double       max_inset = 0.25 * min_side;
    if (!expoly.holes.empty()) {
        BoundingBox ib = get_extents(expoly.holes.front());
        const double outer_span = unscale<double>(std::min(ob.size().x(), ob.size().y()));
        const double inner_span = unscale<double>(std::max(ib.size().x(), ib.size().y()));
        max_inset = 0.35 * std::max(0.2, 0.5 * (outer_span - inner_span));
    }
    auto try_inset = [&](double inset_mm, bool require_hole) {
        if (inset_mm < 0.35)
            return false;
        if (!expoly.holes.empty()) {
            ExPolygons mid = offset_ex(expoly, float(-scale_(inset_mm)));
            if (mid.empty())
                return false;
            const Polygon *best = nullptr;
            double         best_l = 0.;
            for (const ExPolygon &ex : mid) {
                for (const Polygon &h : ex.holes) {
                    const double L = h.length();
                    if (L > best_l) {
                        best_l = L;
                        best   = &h;
                    }
                }
            }
            if (best != nullptr)
                return assign_closed_loop(*best, out);
            if (require_hole)
                return false;
            for (const ExPolygon &ex : mid) {
                const double L = ex.contour.length();
                if (L > best_l) {
                    best_l = L;
                    best   = &ex.contour;
                }
            }
            return best != nullptr && assign_closed_loop(*best, out);
        }
        Polygons mid = offset(expoly.contour, float(-scale_(inset_mm)));
        return mid.size() == 1 && assign_closed_loop(mid.front(), out);
    };
    if (!expoly.holes.empty()) {
        for (double inset = max_inset; inset >= 0.4; inset *= 0.5)
            if (try_inset(inset, true))
                return true;
    }
    for (double inset = max_inset; inset >= 0.4; inset *= 0.5)
        if (try_inset(inset, false))
            return true;
    return try_inset(0.5, false);
}

static bool extract_medial_axis(const ExPolygon &expoly, double spacing, LayerGenerator &out)
{
    BoundingBox  bbox      = get_extents(expoly);
    const double min_width = scale_(std::max(spacing, 1e-3));
    const double max_width = double(std::max(bbox.size().x(), bbox.size().y())) + scale_(1.);

    Polylines pieces;
    expoly.medial_axis(min_width, max_width, &pieces);
    if (pieces.empty())
        expoly.medial_axis(min_width * 0.25, max_width, &pieces);
    return !pieces.empty() && stitch_centerline(pieces, out) && out.polyline.points.size() >= 2;
}

bool extract_generator(const ExPolygon &expoly, double spacing, LayerGenerator &out)
{
    out = LayerGenerator{};
    bool ok = false;
    if (!expoly.holes.empty()) {
        ok = generator_from_inset(expoly, out) || extract_medial_axis(expoly, spacing, out);
    } else {
        ok = extract_medial_axis(expoly, spacing, out) || generator_from_inset(expoly, out);
    }
    if (!ok)
        return false;
    out.length = out.polyline.length();
    return out.polyline.points.size() >= 2 && out.length > scale_(0.5);
}

static bool sample_at_s(const Polyline &pl, bool closed, double s, Point &p)
{
    const Points &pts = pl.points;
    if (pts.size() < 2)
        return false;
    const size_t nseg = pts.size() - 1;
    double       L    = 0.;
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
            return true;
        }
        acc += len;
    }
    p = pts[nseg];
    return true;
}

static Points resample(const Polyline &pl, bool closed, int n)
{
    Points out;
    if (pl.size() < 2 || n < 2)
        return out;
    const double L = pl.length();
    if (L < 1.)
        return out;
    out.reserve(size_t(n));
    for (int i = 0; i < n; ++i) {
        const double s = closed ? (double(i) / double(n)) * L
                                : (n <= 1 ? 0. : double(i) / double(n - 1) * L);
        Point p;
        if (sample_at_s(pl, closed, s, p))
            out.push_back(p);
    }
    return out;
}

static double sum_point_dist(const Points &a, const Points &b)
{
    const size_t n = std::min(a.size(), b.size());
    double       s = 0.;
    for (size_t i = 0; i < n; ++i)
        s += (a[i] - b[i]).cast<double>().norm();
    return s;
}

static void rotate_closed(Polyline &pl, size_t k)
{
    if (pl.size() < 4)
        return;
    Points pts(pl.points.begin(), pl.points.end() - (pl.front() == pl.back() ? 1 : 0));
    if (pts.empty())
        return;
    k %= pts.size();
    std::rotate(pts.begin(), pts.begin() + static_cast<std::ptrdiff_t>(k), pts.end());
    pl.points = std::move(pts);
    pl.points.push_back(pl.points.front());
}

static size_t nearest_vertex(const Polyline &pl, const Point &q)
{
    size_t n = pl.size();
    if (n >= 2 && pl.front() == pl.back())
        n -= 1;
    size_t best = 0;
    double best_d = std::numeric_limits<double>::max();
    for (size_t i = 0; i < n; ++i) {
        const double d = (pl.points[i] - q).cast<double>().norm();
        if (d < best_d) {
            best_d = d;
            best   = i;
        }
    }
    return best;
}

static void align_open(LayerGenerator &cur, const LayerGenerator &prev)
{
    if (cur.polyline.size() < 2 || prev.polyline.size() < 2)
        return;
    constexpr int N = 32;
    Points pa = resample(prev.polyline, false, N);
    auto cost = [&](bool rev) {
        Polyline b = cur.polyline;
        if (rev)
            b.reverse();
        Points pb = resample(b, false, N);
        return sum_point_dist(pa, pb);
    };
    if (cost(true) + scale_(0.2) < cost(false))
        cur.polyline.reverse();
}

static void align_closed(LayerGenerator &cur, const LayerGenerator &prev)
{
    if (cur.polyline.size() < 4 || prev.polyline.size() < 2)
        return;
    constexpr int N = 32;
    Points pa = resample(prev.polyline, prev.closed, N);
    if (pa.empty())
        return;

    auto try_orient = [&](bool rev) {
        Polyline cand = cur.polyline;
        if (rev)
            cand.reverse();
        size_t     nvert = cand.size();
        if (nvert >= 2 && cand.front() == cand.back())
            nvert -= 1;
        double best  = std::numeric_limits<double>::max();
        size_t bestk = 0;
        const size_t step = std::max<size_t>(1, nvert / 48);
        for (size_t k = 0; k < nvert; k += step) {
            Polyline t = cand;
            rotate_closed(t, k);
            Points pb = resample(t, true, N);
            const double c = sum_point_dist(pa, pb);
            if (c < best) {
                best  = c;
                bestk = k;
            }
        }
        return std::make_pair(best, bestk);
    };

    auto [c0, k0] = try_orient(false);
    auto [c1, k1] = try_orient(true);
    if (c1 < c0) {
        cur.polyline.reverse();
        rotate_closed(cur.polyline, k1);
    } else {
        rotate_closed(cur.polyline, k0);
    }
}

static void align_open_to_closed(LayerGenerator &cur, const LayerGenerator &prev)
{
    if (cur.polyline.size() < 4 || prev.polyline.size() < 2)
        return;
    rotate_closed(cur.polyline, nearest_vertex(cur.polyline, prev.polyline.front()));
    // Match walking direction to the previous open spine.
    Point p1;
    if (!sample_at_s(prev.polyline, false, std::min(prev.length, scale_(2.)), p1))
        return;
    Vec2d want = (p1 - prev.polyline.front()).cast<double>();
    Point q1;
    if (!sample_at_s(cur.polyline, true, scale_(2.), q1))
        return;
    Vec2d got = (q1 - cur.polyline.front()).cast<double>();
    if (want.dot(got) < 0.) {
        cur.polyline.reverse();
        rotate_closed(cur.polyline, nearest_vertex(cur.polyline, prev.polyline.front()));
    }
}

static void align_closed_to_open(LayerGenerator &cur, const LayerGenerator &prev)
{
    if (cur.polyline.size() < 2 || prev.polyline.size() < 2)
        return;
    const double df = (cur.polyline.front() - prev.polyline.front()).cast<double>().norm();
    const double db = (cur.polyline.back() - prev.polyline.front()).cast<double>().norm();
    if (db < df)
        cur.polyline.reverse();
}

static void align_to_prev(LayerGenerator &cur, const LayerGenerator &prev)
{
    if (prev.polyline.size() < 2 || cur.polyline.size() < 2)
        return;
    if (!cur.closed && !prev.closed)
        align_open(cur, prev);
    else if (cur.closed && prev.closed)
        align_closed(cur, prev);
    else if (cur.closed && !prev.closed)
        align_open_to_closed(cur, prev);
    else
        align_closed_to_open(cur, prev);
}

static void edt_1d(const float *f, int n, float *d, int *v, float *z)
{
    if (n <= 0)
        return;
    const float inf = 1e20f;
    int k = 0;
    v[0] = 0;
    z[0] = -inf;
    z[1] = inf;
    for (int q = 1; q < n; ++q) {
        float s;
        for (;;) {
            const int vk = v[k];
            s = ((f[q] + float(q) * float(q)) - (f[vk] + float(vk) * float(vk))) / (2.f * float(q - vk));
            if (s > z[k])
                break;
            if (--k < 0) {
                k = 0;
                s = -inf;
                break;
            }
        }
        ++k;
        v[k] = q;
        z[k] = s;
        z[k + 1] = inf;
    }
    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[k + 1] < float(q))
            ++k;
        const float diff = float(q - v[k]);
        d[q] = diff * diff + f[v[k]];
    }
}

static void rasterize_expoly(const ExPolygon &ex, int nx, int ny, double h, const Vec2d &origin, std::vector<char> &img)
{
    img.assign(size_t(nx) * size_t(ny), 0);
    if (ex.contour.size() < 3)
        return;
    auto add_edges = [&](const Polygon &poly, std::vector<std::vector<float>> &hits) {
        const Points &pts = poly.points;
        const size_t  n   = pts.size();
        for (size_t i = 0; i < n; ++i) {
            const Point &a = pts[i];
            const Point &b = pts[(i + 1) % n];
            const double y0 = unscale<double>(a.y());
            const double y1 = unscale<double>(b.y());
            if (std::abs(y1 - y0) < 1e-9)
                continue;
            const double x0 = unscale<double>(a.x());
            const double x1 = unscale<double>(b.x());
            const int    ymin = std::max(0, int(std::ceil((std::min(y0, y1) - origin.y()) / h - 1e-9)));
            const int    ymax = std::min(ny - 1, int(std::floor((std::max(y0, y1) - origin.y()) / h - 1e-9)));
            for (int y = ymin; y <= ymax; ++y) {
                const double py = origin.y() + (double(y) + 0.5) * h;
                const double t  = (py - y0) / (y1 - y0);
                if (t < 0. || t > 1.)
                    continue;
                hits[y].push_back(float(x0 + t * (x1 - x0)));
            }
        }
    };
    std::vector<std::vector<float>> hits(ny);
    add_edges(ex.contour, hits);
    for (const Polygon &hpoly : ex.holes)
        add_edges(hpoly, hits);
    for (int y = 0; y < ny; ++y) {
        auto &row = hits[y];
        if (row.size() < 2)
            continue;
        std::sort(row.begin(), row.end());
        for (size_t i = 0; i + 1 < row.size(); i += 2) {
            const int x0 = std::max(0, int(std::floor((row[i] - origin.x()) / h)));
            const int x1 = std::min(nx - 1, int(std::floor((row[i + 1] - origin.x()) / h)));
            for (int x = x0; x <= x1; ++x)
                img[size_t(x) + size_t(y) * size_t(nx)] = 1;
        }
    }
}

static void add_axis_quad(::indexed_triangle_set &its, const Vec3f &c, int axis, float h)
{
    const float s = 0.55f * h;
    Vec3f a, b, d;
    if (axis == 0) {
        a = Vec3f(0.f, -s, -s);
        b = Vec3f(0.f,  s, -s);
        d = Vec3f(0.f,  s,  s);
    } else if (axis == 1) {
        a = Vec3f(-s, 0.f, -s);
        b = Vec3f( s, 0.f, -s);
        d = Vec3f( s, 0.f,  s);
    } else {
        a = Vec3f(-s, -s, 0.f);
        b = Vec3f( s, -s, 0.f);
        d = Vec3f( s,  s, 0.f);
    }
    const Vec3f p0 = c + a;
    const Vec3f p1 = c + b;
    const Vec3f p2 = c + d;
    const Vec3f p3 = c + (a + d - b);
    const int   i  = int(its.vertices.size());
    its.vertices.push_back(p0);
    its.vertices.push_back(p1);
    its.vertices.push_back(p2);
    its.vertices.push_back(p3);
    its.indices.emplace_back(i, i + 1, i + 2);
    its.indices.emplace_back(i, i + 2, i + 3);
}

static bool polyline_from_pixels(const std::vector<Vec2i> &pix, double h, const Vec2d &origin, LayerGenerator &out)
{
    if (pix.size() < 2)
        return false;
    std::map<std::pair<int, int>, int> id_of;
    std::vector<Vec2i>                 nodes;
    auto id = [&](int x, int y) {
        auto key = std::make_pair(x, y);
        auto it  = id_of.find(key);
        if (it != id_of.end())
            return it->second;
        int n = int(nodes.size());
        id_of.emplace(key, n);
        nodes.push_back(Vec2i(x, y));
        return n;
    };
    for (const Vec2i &p : pix)
        id(p.x(), p.y());
    const int n = int(nodes.size());
    std::vector<std::vector<int>> adj(n);
    auto try_link = [&](int a, int bx, int by) {
        auto it = id_of.find({ bx, by });
        if (it == id_of.end() || it->second == a)
            return;
        adj[a].push_back(it->second);
    };
    for (int i = 0; i < n; ++i) {
        const int x = nodes[i].x();
        const int y = nodes[i].y();
        try_link(i, x + 1, y);
        try_link(i, x - 1, y);
        try_link(i, x, y + 1);
        try_link(i, x, y - 1);
    }
    auto farthest = [&](int src) {
        std::vector<int> dist(n, -1), parent(n, -1);
        std::queue<int>  q;
        dist[src] = 0;
        q.push(src);
        int best = src;
        while (!q.empty()) {
            const int v = q.front();
            q.pop();
            if (dist[v] > dist[best])
                best = v;
            for (int u : adj[v]) {
                if (dist[u] >= 0)
                    continue;
                dist[u]   = dist[v] + 1;
                parent[u] = v;
                q.push(u);
            }
        }
        return std::make_pair(best, parent);
    };
    bool all_deg2 = true;
    int  start    = 0;
    for (int i = 0; i < n; ++i) {
        if (adj[i].size() == 1)
            start = i;
        if (adj[i].size() != 2 && !adj[i].empty())
            all_deg2 = false;
    }
    std::vector<int> parent;
    int              a = 0, b = 0;
    if (all_deg2 && n >= 4) {
        std::vector<char> used(n, 0);
        std::vector<int>  chain;
        int               v = 0;
        while (v < n && adj[v].empty())
            ++v;
        int prev = -1;
        for (int step = 0; step < n + 2 && v >= 0 && v < n; ++step) {
            chain.push_back(v);
            used[v] = 1;
            int nxt = -1;
            for (int u : adj[v]) {
                if (u != prev && !used[u]) {
                    nxt = u;
                    break;
                }
            }
            if (nxt < 0) {
                for (int u : adj[v])
                    if (u != prev) {
                        nxt = u;
                        break;
                    }
            }
            prev = v;
            v    = nxt;
            if (v == chain.front())
                break;
        }
        if (chain.size() < 4)
            return false;
        out.closed = true;
        out.polyline.points.clear();
        for (int idx : chain) {
            const double px = origin.x() + (double(nodes[idx].x()) + 0.5) * h;
            const double py = origin.y() + (double(nodes[idx].y()) + 0.5) * h;
            out.polyline.points.push_back(Point::new_scale(px, py));
        }
        if (out.polyline.front() != out.polyline.back())
            out.polyline.points.push_back(out.polyline.front());
        out.length = out.polyline.length();
        return out.polyline.points.size() >= 4 && out.length > scale_(0.5);
    }
    std::tie(a, parent) = farthest(start);
    std::tie(b, parent) = farthest(a);
    std::vector<int> chain;
    for (int x = b; x >= 0; x = parent[x]) {
        chain.push_back(x);
        if (x == a)
            break;
    }
    std::reverse(chain.begin(), chain.end());
    if (chain.size() < 2)
        return false;
    out.closed = false;
    out.polyline.points.clear();
    for (int idx : chain) {
        const double px = origin.x() + (double(nodes[idx].x()) + 0.5) * h;
        const double py = origin.y() + (double(nodes[idx].y()) + 0.5) * h;
        out.polyline.points.push_back(Point::new_scale(px, py));
    }
    out.length = out.polyline.length();
    return out.polyline.points.size() >= 2 && out.length > scale_(0.5);
}

static bool build_sdf_sheet(std::vector<LayerGenerator>            &layers,
                            std::unique_ptr<::indexed_triangle_set> &sheet,
                            const std::vector<ExPolygon>           &islands,
                            const std::vector<double>              &zs,
                            double                                  spacing)
{
    const size_t nlayer = std::min(islands.size(), zs.size());
    if (nlayer < 8)
        return false;
    BoundingBox bb;
    for (const ExPolygon &ex : islands)
        if (ex.contour.size() >= 3)
            bb.merge(get_extents(ex));
    if (!bb.defined)
        return false;

    const Vec2d origin(unscale<double>(bb.min.x()) - 1., unscale<double>(bb.min.y()) - 1.);
    const Vec2d span(unscale<double>(bb.max.x()) - origin.x() + 1.,
                     unscale<double>(bb.max.y()) - origin.y() + 1.);
    const double z0 = zs.front();
    const double z1 = zs.back();
    double       h  = 0.8;
    int nx = 0, ny = 0, nz = 0;
    for (int iter = 0; iter < 6; ++iter) {
        nx = std::max(8, int(std::ceil(span.x() / h)));
        ny = std::max(8, int(std::ceil(span.y() / h)));
        nz = std::max(4, int(std::ceil(std::max(z1 - z0, h) / h)) + 1);
        if (size_t(nx) * size_t(ny) * size_t(nz) <= 9000000ull)
            break;
        h *= 1.35;
    }
    if (nz < 8 || (z1 - z0) < 4.)
        return false;
    const size_t nxyz = size_t(nx) * size_t(ny) * size_t(nz);
    if (nxyz < 64)
        return false;

    std::vector<char> occ(nxyz, 0);
    auto at = [&](int x, int y, int z) -> size_t {
        return size_t(x) + size_t(nx) * (size_t(y) + size_t(ny) * size_t(z));
    };
    std::vector<char> slice;
    for (size_t li = 0; li < nlayer; ++li) {
        if (islands[li].contour.size() < 3)
            continue;
        rasterize_expoly(islands[li], nx, ny, h, origin, slice);
        int iz0 = int(std::lround((zs[li] - z0) / h));
        int iz1 = iz0;
        if (li + 1 < nlayer)
            iz1 = int(std::lround((zs[li + 1] - z0) / h));
        iz0 = std::max(0, std::min(nz - 1, iz0));
        iz1 = std::max(0, std::min(nz - 1, iz1));
        if (iz1 < iz0)
            std::swap(iz0, iz1);
        for (int iz = iz0; iz <= iz1; ++iz)
            for (int y = 0; y < ny; ++y)
                for (int x = 0; x < nx; ++x)
                    if (slice[size_t(x) + size_t(y) * size_t(nx)])
                        occ[at(x, y, iz)] = 1;
    }

    const float inf = 1e12f;
    std::vector<float> f(nxyz), dt(nxyz), tmp(nxyz);
    for (size_t i = 0; i < nxyz; ++i)
        f[i] = occ[i] ? inf : 0.f;

    const int dim = std::max(nx, std::max(ny, nz));
    std::vector<float> line(dim), outl(dim);
    std::vector<int>   vbuf(dim);
    std::vector<float> zbuf(dim + 1);
    for (int z = 0; z < nz; ++z)
        for (int y = 0; y < ny; ++y) {
            for (int x = 0; x < nx; ++x)
                line[x] = f[at(x, y, z)];
            edt_1d(line.data(), nx, outl.data(), vbuf.data(), zbuf.data());
            for (int x = 0; x < nx; ++x)
                tmp[at(x, y, z)] = outl[x];
        }
    for (int z = 0; z < nz; ++z)
        for (int x = 0; x < nx; ++x) {
            for (int y = 0; y < ny; ++y)
                line[y] = tmp[at(x, y, z)];
            edt_1d(line.data(), ny, outl.data(), vbuf.data(), zbuf.data());
            for (int y = 0; y < ny; ++y)
                tmp[at(x, y, z)] = outl[y];
        }
    for (int y = 0; y < ny; ++y)
        for (int x = 0; x < nx; ++x) {
            for (int z = 0; z < nz; ++z)
                line[z] = tmp[at(x, y, z)];
            edt_1d(line.data(), nz, outl.data(), vbuf.data(), zbuf.data());
            for (int z = 0; z < nz; ++z)
                dt[at(x, y, z)] = std::sqrt(std::max(0.f, outl[z])) * float(h);
        }

    auto sample_dt = [&](int x, int y, int z) {
        if (x < 0 || y < 0 || z < 0 || x >= nx || y >= ny || z >= nz)
            return 0.f;
        return occ[at(x, y, z)] ? dt[at(x, y, z)] : 0.f;
    };

    std::vector<char> ridge(nxyz, 0);
    std::vector<char> axis_of(nxyz, 0);
    const float min_r = float(1.15 * h);
    size_t      n_ridge = 0;
    for (int z = 1; z < nz - 1; ++z)
        for (int y = 1; y < ny - 1; ++y)
            for (int x = 1; x < nx - 1; ++x) {
                const size_t i = at(x, y, z);
                if (!occ[i] || dt[i] < min_r)
                    continue;
                const float d  = dt[i];
                const float dx = d - 0.5f * (sample_dt(x - 1, y, z) + sample_dt(x + 1, y, z));
                const float dy = d - 0.5f * (sample_dt(x, y - 1, z) + sample_dt(x, y + 1, z));
                const float dz = d - 0.5f * (sample_dt(x, y, z - 1) + sample_dt(x, y, z + 1));
                float best = dx;
                int   ax   = 0;
                if (dy > best) {
                    best = dy;
                    ax   = 1;
                }
                if (dz > best) {
                    best = dz;
                    ax   = 2;
                }
                if (best < 0.12f * float(h))
                    continue;
                ridge[i]    = 1;
                axis_of[i]  = char(ax);
                ++n_ridge;
            }
    if (n_ridge < 16)
        return false;

    sheet = std::make_unique<::indexed_triangle_set>();
    sheet->vertices.reserve(n_ridge * 4);
    sheet->indices.reserve(n_ridge * 2);
    for (int z = 1; z < nz - 1; ++z)
        for (int y = 1; y < ny - 1; ++y)
            for (int x = 1; x < nx - 1; ++x) {
                const size_t i = at(x, y, z);
                if (!ridge[i])
                    continue;
                const Vec3f c(float(origin.x() + (double(x) + 0.5) * h),
                              float(origin.y() + (double(y) + 0.5) * h),
                              float(z0 + (double(z) + 0.5) * h));
                add_axis_quad(*sheet, c, int(axis_of[i]), float(h));
            }

    layers.resize(nlayer);
    const LayerGenerator *prev = nullptr;
    int                   ok_n = 0;
    for (size_t li = 0; li < nlayer; ++li) {
        LayerGenerator &g = layers[li];
        g.z = zs[li];
        const int iz = std::max(0, std::min(nz - 1, int(std::lround((zs[li] - z0) / h))));
        std::vector<Vec2i> pix;
        size_t             n_pix = 0;
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x)
                if (ridge[at(x, y, iz)]) {
                    pix.emplace_back(x, y);
                    ++n_pix;
                }
        const double island_a = (islands[li].contour.size() >= 3) ? unscale<double>(unscale<double>(std::abs(islands[li].area()))) : 0.;
        const double ridge_a  = double(n_pix) * h * h;
        const bool   region_like = island_a > 1. && ridge_a > 0.22 * island_a;
        bool         got         = false;
        if (!region_like && n_pix >= 4)
            got = polyline_from_pixels(pix, h, origin, g);
        if (!got && islands[li].contour.size() >= 3)
            got = extract_generator(islands[li], spacing, g);
        if (got) {
            if (prev)
                align_to_prev(g, *prev);
            g.length = g.polyline.length();
            prev     = &g;
            ++ok_n;
        } else {
            g = LayerGenerator{};
            g.z = zs[li];
        }
    }
    return ok_n >= 2 && sheet && !sheet->indices.empty();
}

MedialChart::MedialChart() = default;
MedialChart::~MedialChart() = default;
MedialChart::MedialChart(MedialChart &&) noexcept = default;
MedialChart &MedialChart::operator=(MedialChart &&) noexcept = default;

const LayerGenerator *MedialChart::layer(size_t layer_id) const
{
    if (layer_id >= m_layers.size())
        return nullptr;
    const LayerGenerator &g = m_layers[layer_id];
    if (g.polyline.size() < 2)
        return nullptr;
    return &g;
}

const ::indexed_triangle_set *MedialChart::sheet_mesh() const
{
    if (!m_sheet || m_sheet->indices.empty())
        return nullptr;
    return m_sheet.get();
}

std::unique_ptr<MedialChart> MedialChart::build(const std::vector<ExPolygon> &islands,
                                                const std::vector<double>    &zs,
                                                double                        spacing)
{
    auto chart = std::make_unique<MedialChart>();
    if (build_sdf_sheet(chart->m_layers, chart->m_sheet, islands, zs, spacing))
        return chart;

    const size_t n = std::min(islands.size(), zs.size());
    chart->m_layers.resize(n);
    const LayerGenerator *prev = nullptr;
    for (size_t i = 0; i < n; ++i) {
        LayerGenerator &g = chart->m_layers[i];
        g.z = zs[i];
        if (islands[i].contour.size() >= 3 && extract_generator(islands[i], spacing, g)) {
            if (prev)
                align_to_prev(g, *prev);
            else if (!g.closed && g.polyline.size() >= 2 && g.polyline.back().x() > g.polyline.front().x())
                g.polyline.reverse();
            g.length = g.polyline.length();
            prev     = &g;
        } else {
            g = LayerGenerator{};
            g.z = zs[i];
        }
    }
    bool any = false;
    for (const LayerGenerator &g : chart->m_layers)
        if (g.polyline.size() >= 2)
            any = true;
    if (!any)
        return nullptr;
    return chart;
}

} // namespace FillConformal
} // namespace Slic3r
