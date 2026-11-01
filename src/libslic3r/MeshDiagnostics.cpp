#include "MeshDiagnostics.hpp"
#include "MeshRaycast.hpp"
#include "Point.hpp"

#include <Eigen/Core>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

#include <boost/log/trivial.hpp>
#include <tbb/parallel_sort.h>

namespace Slic3r {

namespace {

struct VertexFan {
    std::vector<size_t> faces;
    std::vector<size_t> parent;
};

struct EdgeRef {
    size_t v0;
    size_t v1;
    size_t v0_fan_idx;
    size_t v1_fan_idx;
    bool   forward = true; // true if the face winding is v0 -> v1
};

static size_t fan_root(std::vector<size_t> &parent, size_t idx)
{
    size_t root = idx;
    while (parent[root] != root)
        root = parent[root];

    while (parent[idx] != idx) {
        size_t next = parent[idx];
        parent[idx] = root;
        idx = next;
    }

    return root;
}

static void fan_union(VertexFan &fan, size_t a, size_t b)
{
    size_t root_a = fan_root(fan.parent, a);
    size_t root_b = fan_root(fan.parent, b);
    if (root_a != root_b)
        fan.parent[root_b] = root_a;
}

static bool face_vertices_valid(const indexed_triangle_set &its, const stl_triangle_vertex_indices &idx)
{
    const size_t n = its.vertices.size();
    return idx[0] >= 0 && idx[1] >= 0 && idx[2] >= 0
        && static_cast<size_t>(idx[0]) < n
        && static_cast<size_t>(idx[1]) < n
        && static_cast<size_t>(idx[2]) < n;
}

// A hit whose smallest barycentric coordinate is this close to 0 sits on a
// triangle edge or vertex, where neighbouring faces answer the same query
// differently. AABBTreeIndirect may also place a near-edge crossing slightly
// inside one triangle; CGAL often collapsed those to coincident hits and
// discarded the ray. 0.02 stays below the interior barycentric of known
// reversed-face samples (~0.06) while covering those near-edge false hits.
constexpr double k_bary_eps = 0.02;

// True if the hit lies on a triangle edge or vertex (barycentric coord ~ 0).
// RayMeshHit uses P = (1-u-v)*v0 + u*v1 + v*v2.
static bool hit_on_edge_or_vertex(const RayMeshHit &hit, double bary_eps)
{
    const double w = 1.0 - hit.u - hit.v;
    return std::min(hit.u, std::min(hit.v, w)) <= bary_eps;
}

// Visible backfaces from outside the AABB. The caller must pass a watertight
// mesh (no open edges, no non-manifold edges). Per ray: all hits. A closed
// mesh must produce an even hit count (enter/leave pairs); an odd count is
// discarded as a leak. The whole ray is then discarded if the two smallest t
// differ by less than k_coincident_t, or if its outermost hit is a sliver,
// lands on an edge / vertex, or is grazing. Only the outermost surviving hit
// is tested; one back-facing hit sets the flag.
static bool detect_visible_backfaces(const indexed_triangle_set &its)
{
    if (its.indices.empty() || its.vertices.empty())
        return false;

    // Skip on malformed indices; later face lookups index vertices without a
    // bounds check. Face count is not a reason to skip.
    for (const auto &idx : its.indices) {
        if (!face_vertices_valid(its, idx)) {
            BOOST_LOG_TRIVIAL(info) << "reversed-faces: layer3 skip invalid_index";
            return false;
        }
    }

    Eigen::Vector3f bmin_f = its.vertices.front();
    Eigen::Vector3f bmax_f = bmin_f;
    for (const auto &v : its.vertices) {
        bmin_f = bmin_f.cwiseMin(v);
        bmax_f = bmax_f.cwiseMax(v);
    }

    const Vec3d  bmin    = bmin_f.cast<double>();
    const Vec3d  bmax    = bmax_f.cast<double>();
    const Vec3d  center  = 0.5 * (bmin + bmax);
    const Vec3d  extent  = bmax - bmin;
    const double radius  = 0.5 * extent.norm();
    if (radius <= 0.) {
        BOOST_LOG_TRIVIAL(info) << "reversed-faces: layer3 skip zero_radius";
        return false;
    }
    const double max_ext  = extent.maxCoeff();
    const double standoff = radius + std::max(max_ext * 0.05, 1.0);

    BOOST_LOG_TRIVIAL(info)
        << "reversed-faces: layer3 aabb extent=["
        << extent.x() << "," << extent.y() << "," << extent.z() << "]"
        << " faces=" << its.indices.size()
        << " verts=" << its.vertices.size();

    const TriangleRaycaster caster(its);

    std::vector<Vec3d> dirs;
    dirs.reserve(46);
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            for (int z = -1; z <= 1; ++z) {
                if (x == 0 && y == 0 && z == 0)
                    continue;
                dirs.emplace_back(double(x), double(y), double(z));
            }
    constexpr int    extra  = 20;
    constexpr double golden = 2.399963229728653;
    for (int i = 0; i < extra; ++i) {
        const double y   = 1.0 - 2.0 * (i + 0.5) / extra;
        const double r   = std::sqrt(std::max(0.0, 1.0 - y * y));
        const double phi = i * golden;
        dirs.emplace_back(r * std::cos(phi), y, r * std::sin(phi));
    }

    constexpr double graze           = 0.08;
    constexpr double backface        = 1e-4;
    constexpr int    k_log_back_hits = 8;
    // Two crossings closer than this are treated as coincident faces. Relative
    // to the AABB so a 2 mm part and a 300 mm part do not share one absolute
    // threshold; the factor keeps a typical 100 mm part near the former 0.01 mm.
    const double k_coincident_t = radius * 1e-4;
    // nlen = ||(b-a)×(c-a)|| = 2 * area. Drop slivers whose area is tiny
    // relative to the AABB; their normals are not reliable.
    const double min_nlen = 2.0 * radius * radius * 1e-12;

    int n_ray = 0, n_miss = 0, n_odd = 0, n_coincident = 0, n_boundary = 0;
    int n_graze = 0, n_sliver = 0, n_front = 0, n_back = 0, n_coplanar = 0;

    for (Vec3d dir : dirs) {
        const double len = dir.norm();
        if (len == 0.)
            continue;
        dir /= len;
        ++n_ray;
        const Vec3d origin  = center + dir * standoff;
        const Vec3d ray_dir = -dir;

        // Coplanar crossings yield no hit, so they can break the parity check
        // below; count them to explain an elevated odd count in the log.
        int                     ray_coplanar = 0;
        std::vector<RayMeshHit> hits         = caster.all_hits(origin, ray_dir, &ray_coplanar);
        n_coplanar += ray_coplanar;
        hits.erase(std::remove_if(hits.begin(), hits.end(),
                                  [](const RayMeshHit &h) { return h.t < 1e-6; }),
                   hits.end());
        if (hits.empty()) {
            ++n_miss;
            continue;
        }
        // Closed meshes must enter and leave in pairs; an odd hit count is a leak.
        if (hits.size() % 2 == 1) {
            ++n_odd;
            continue;
        }

        if (hits[1].t - hits[0].t < k_coincident_t) {
            ++n_coincident;
            continue;
        }

        const RayMeshHit &hit = hits.front();
        if (hit.face < 0 || static_cast<size_t>(hit.face) >= its.indices.size()) {
            ++n_miss;
            continue;
        }

        const auto &idx = its.indices[hit.face];
        if (!face_vertices_valid(its, idx)) {
            ++n_miss;
            continue;
        }

        const Vec3d a = its.vertices[idx[0]].cast<double>();
        const Vec3d b = its.vertices[idx[1]].cast<double>();
        const Vec3d c = its.vertices[idx[2]].cast<double>();
        Vec3d       N = (b - a).cross(c - a);
        const double nlen = N.norm();
        if (nlen < min_nlen) {
            ++n_sliver;
            continue;
        }
        N /= nlen;

        if (hit_on_edge_or_vertex(hit, k_bary_eps)) {
            ++n_boundary;
            continue;
        }

        const double n_ray_dot = N.dot(ray_dir);
        if (std::abs(n_ray_dot) < graze) {
            ++n_graze;
            continue;
        }

        const Vec3d  hit_pt = origin + ray_dir * hit.t;
        const Vec3d  V      = origin - hit_pt;
        const double ndot   = N.dot(V);

        if (ndot < -backface) {
            ++n_back;
            if (n_back <= k_log_back_hits) {
                const double w = 1.0 - hit.u - hit.v;
                BOOST_LOG_TRIVIAL(info)
                    << "reversed-faces: layer3 backface#" << n_back
                    << " face=" << hit.face
                    << " t=" << hit.t
                    << " uvw=[" << hit.u << "," << hit.v << "," << w << "]"
                    << " ndot=" << ndot
                    << " n_dot_ray=" << n_ray_dot
                    << " dir=[" << dir.x() << "," << dir.y() << "," << dir.z() << "]"
                    << " N=[" << N.x() << "," << N.y() << "," << N.z() << "]"
                    << " hit=[" << hit_pt.x() << "," << hit_pt.y() << "," << hit_pt.z() << "]";
            }
        } else {
            ++n_front;
        }
    }

    BOOST_LOG_TRIVIAL(info)
        << "reversed-faces: layer3 summary rays=" << n_ray
        << " miss=" << n_miss
        << " odd=" << n_odd
        << " coplanar=" << n_coplanar
        << " coincident=" << n_coincident
        << " sliver=" << n_sliver
        << " boundary=" << n_boundary
        << " graze=" << n_graze
        << " front=" << n_front
        << " back=" << n_back;

    const bool flagged = n_back > 0;
    if (!flagged)
        BOOST_LOG_TRIVIAL(info) << "reversed-faces: layer3 no visible backface";
    return flagged;
}

static void log_reversed_face_decision(const char                 *src,
                                       const indexed_triangle_set &its,
                                       const MeshDiagnosticStats  &stats)
{
    BOOST_LOG_TRIVIAL(info)
        << "reversed-faces: " << src
        << " has_reversed_faces=" << (stats.has_reversed_faces ? 1 : 0)
        << " open_edges=" << stats.open_edges
        << " nm_edges=" << stats.non_manifold_edges
        << " same_dir=" << stats.same_direction_edges
        << " faces=" << its.indices.size()
        << " verts=" << its.vertices.size();
    if (stats.has_reversed_faces) {
        BOOST_LOG_TRIVIAL(info)
            << "reversed-faces: FLAG src=" << src
            << " faces=" << its.indices.size()
            << " verts=" << its.vertices.size()
            << " open_edges=" << stats.open_edges
            << " nm_edges=" << stats.non_manifold_edges
            << " same_dir=" << stats.same_direction_edges;
    }
}

} // namespace

static void detect_reversed_faces_impl(const indexed_triangle_set &its,
                                       MeshDiagnosticStats        &stats)
{
    if (stats.non_manifold_edges != 0 || stats.same_direction_edges != 0) {
        stats.has_reversed_faces = true;
        BOOST_LOG_TRIVIAL(info) << "reversed-faces: edge shortcut"
                               << " nm_edges=" << stats.non_manifold_edges
                               << " same_dir=" << stats.same_direction_edges
                               << " open_edges=" << stats.open_edges;
        log_reversed_face_decision("detect", its, stats);
        return;
    }
    if (stats.open_edges != 0) {
        BOOST_LOG_TRIVIAL(info) << "reversed-faces: skip not_watertight"
                               << " open_edges=" << stats.open_edges;
        log_reversed_face_decision("detect", its, stats);
        return;
    }
    stats.has_reversed_faces = detect_visible_backfaces(its);
    log_reversed_face_decision("detect", its, stats);
}

void its_detect_reversed_faces(const indexed_triangle_set &its, MeshDiagnosticStats &stats)
{
    detect_reversed_faces_impl(its, stats);
}

MeshDiagnosticStats its_quick_diagnostics(const indexed_triangle_set &its, std::vector<Vec3i> *neighbors)
{
    MeshDiagnosticStats stats = its_edge_diagnostics(its, neighbors);
    detect_reversed_faces_impl(its, stats);
    return stats;
}

MeshDiagnosticStats its_mesh_diagnostics(const indexed_triangle_set &its)
{
    MeshDiagnosticStats result;
    const size_t     num_vertices = its.vertices.size();
    const size_t     num_faces    = its.indices.size();

    if (num_faces == 0)
        return result;

    // --- Pass 1: build per-vertex face fans and flat edge refs --------------
    std::vector<VertexFan> vertex_fans(num_vertices);
    std::vector<EdgeRef>   edge_refs;
    edge_refs.reserve(num_faces * 3);

    for (size_t fid = 0; fid < num_faces; ++fid) {
        const auto &face = its.indices[fid];

        // Skip degenerate faces (two or more identical vertex indices).
        if (face[0] == face[1] || face[1] == face[2] || face[2] == face[0])
            continue;

        size_t vertices[3] = {
            static_cast<size_t>(face[0]),
            static_cast<size_t>(face[1]),
            static_cast<size_t>(face[2])
        };
        size_t fan_indices[3] = { size_t(-1), size_t(-1), size_t(-1) };

        for (int i = 0; i < 3; ++i) {
            const size_t vid = vertices[i];
            if (vid >= num_vertices)
                continue;

            fan_indices[i] = vertex_fans[vid].faces.size();
            vertex_fans[vid].faces.push_back(fid);
        }

        for (int i = 0; i < 3; ++i) {
            const int    j  = (i + 1) % 3;
            const size_t va = vertices[i];
            const size_t vb = vertices[j];

            if (va >= num_vertices || vb >= num_vertices)
                continue;

            if (va < vb)
                edge_refs.push_back({ va, vb, fan_indices[i], fan_indices[j], true });
            else
                edge_refs.push_back({ vb, va, fan_indices[j], fan_indices[i], false });
        }
    }

    // Initialize per-vertex union-find storage. Non-degenerate faces insert
    // each incident face only once per vertex, so no sort/unique pass is needed.
    for (auto &fan : vertex_fans) {
        fan.parent.resize(fan.faces.size());
        std::iota(fan.parent.begin(), fan.parent.end(), 0);
    }

    // --- Edge classification (each undirected edge counted at most once) -----
    // Also mark vertices incident on non-manifold edges so that the vertex
    // fan-connectivity test below can skip them (same strategy as VCGlib).
    // Two-face edge groups connect those two face fans at both edge endpoints.
    std::vector<bool> on_nm_edge(num_vertices, false);

    std::sort(edge_refs.begin(), edge_refs.end(), [](const EdgeRef &a, const EdgeRef &b) {
        return a.v0 < b.v0 || (a.v0 == b.v0 && a.v1 < b.v1);
    });

    for (size_t i = 0; i < edge_refs.size();) {
        size_t j = i + 1;
        while (j < edge_refs.size() && edge_refs[j].v0 == edge_refs[i].v0 && edge_refs[j].v1 == edge_refs[i].v1)
            ++j;

        const size_t edge_face_count = j - i;
        size_t plus = 0, minus = 0;
        for (size_t k = i; k < j; ++k) {
            if (edge_refs[k].forward)
                ++plus;
            else
                ++minus;
        }
        if (plus >= 2 || minus >= 2)
            ++result.same_direction_edges;

        if (edge_face_count == 1) {
            ++result.open_edges;
        } else if (edge_face_count == 2) {
            fan_union(vertex_fans[edge_refs[i].v0], edge_refs[i].v0_fan_idx, edge_refs[i + 1].v0_fan_idx);
            fan_union(vertex_fans[edge_refs[i].v1], edge_refs[i].v1_fan_idx, edge_refs[i + 1].v1_fan_idx);
        } else {
            ++result.non_manifold_edges;
            on_nm_edge[edge_refs[i].v0] = true;
            on_nm_edge[edge_refs[i].v1] = true;
        }

        i = j;
    }

    // --- Pass 2: non-manifold vertex detection ------------------------------
    // A vertex is non-manifold if its incident faces form more than one
    // component when connected through regular two-face edges.
    // Vertices on non-manifold edges are excluded: an edge with >2 faces
    // cannot define a reliable two-face fan traversal, and these vertices are
    // already accounted for by non_manifold_edges.
    std::vector<size_t> roots;

    for (size_t vid = 0; vid < num_vertices; ++vid) {
        if (on_nm_edge[vid])
            continue;

        auto &fan = vertex_fans[vid];
        if (fan.faces.size() <= 1)
            continue;

        roots.clear();
        roots.reserve(fan.parent.size());
        for (size_t i = 0; i < fan.parent.size(); ++i)
            roots.push_back(fan_root(fan.parent, i));

        std::sort(roots.begin(), roots.end());
        if (std::unique(roots.begin(), roots.end()) != roots.begin() + 1)
            ++result.non_manifold_vertices;
    }

    its_detect_reversed_faces(its, result);
    return result;
}

MeshDiagnosticStats its_edge_diagnostics(const indexed_triangle_set &its, std::vector<Vec3i> *neighbors)
{
    MeshDiagnosticStats result;
    const size_t num_vertices = its.vertices.size();
    const size_t num_faces    = its.indices.size();

    if (num_faces == 0)
        return result;

    if (neighbors) {
        neighbors->assign(num_faces, Vec3i(-1, -1, -1));
        if (num_faces > UINT32_MAX)
            neighbors = nullptr;
    }

    struct HalfEdge {
        uint64_t key     = 0;
        uint32_t face    = 0;
        uint8_t  slot    = 0;
        uint8_t  forward = 0;
        bool operator<(const HalfEdge &rhs) const
        {
            if (key != rhs.key)
                return key < rhs.key;
            if (face != rhs.face)
                return face < rhs.face;
            if (slot != rhs.slot)
                return slot < rhs.slot;
            return forward < rhs.forward;
        }
    };

    std::vector<HalfEdge> edges;
    edges.reserve(num_faces * 3);

    for (size_t fid = 0; fid < num_faces; ++fid) {
        const auto &face = its.indices[fid];

        if (face[0] == face[1] || face[1] == face[2] || face[2] == face[0])
            continue;

        const size_t v[3] = {
            static_cast<size_t>(face[0]),
            static_cast<size_t>(face[1]),
            static_cast<size_t>(face[2])
        };

        for (int i = 0; i < 3; ++i) {
            size_t va = v[i], vb = v[(i + 1) % 3];
            if (va >= num_vertices || vb >= num_vertices)
                continue;
            const uint8_t forward = va < vb ? 1 : 0;
            if (!forward)
                std::swap(va, vb);
            edges.push_back({ (uint64_t(va) << 32) | uint64_t(vb), static_cast<uint32_t>(fid), static_cast<uint8_t>(i),
                              forward });
        }
    }

    tbb::parallel_sort(edges.begin(), edges.end());

    std::vector<uint32_t> plus_ids;
    std::vector<uint32_t> minus_ids;
    plus_ids.reserve(4);
    minus_ids.reserve(4);

    for (size_t i = 0; i < edges.size();) {
        size_t j = i + 1;
        while (j < edges.size() && edges[j].key == edges[i].key)
            ++j;

        const size_t count = j - i;
        plus_ids.clear();
        minus_ids.clear();
        for (size_t k = i; k < j; ++k) {
            if (edges[k].forward)
                plus_ids.push_back(static_cast<uint32_t>(k));
            else
                minus_ids.push_back(static_cast<uint32_t>(k));
        }
        if (plus_ids.size() >= 2 || minus_ids.size() >= 2)
            ++result.same_direction_edges;
        if (count == 1)
            ++result.open_edges;
        else if (count > 2)
            ++result.non_manifold_edges;

        if (neighbors) {
            const size_t n_pairs = std::min(plus_ids.size(), minus_ids.size());
            for (size_t p = 0; p < n_pairs; ++p) {
                const HalfEdge &a = edges[plus_ids[p]];
                const HalfEdge &b = edges[minus_ids[p]];
                (*neighbors)[a.face][a.slot] = static_cast<int>(b.face);
                (*neighbors)[b.face][b.slot] = static_cast<int>(a.face);
            }
        }

        i = j;
    }

    return result;
}

} // namespace Slic3r
