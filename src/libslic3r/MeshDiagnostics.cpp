#include "MeshDiagnostics.hpp"
#include "AABBTreeIndirect.hpp"

#include <Eigen/Core>
#include <igl/Hit.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

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
    size_t face;
    bool   plus;
};

struct DirectedEdge {
    size_t v0;
    size_t v1;
    size_t face;
    bool   plus;
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

// Layer 2: outward test per face-connected component (shared opposite-direction
// manifold edges — the same "shell / patch" unit as its_number_of_patches).
// A mesh with several outward shells and one inward shell is reported as
// reversed as soon as that inward component is found.
// Closed components use signed volume; open components use the libigl-style
// area-weighted centroid test. Magnitudes below k_orient_eps are treated as
// inconclusive (near-planar sheets) and do not set the flag.
constexpr double k_orient_eps = 1e-4;

static bool component_is_inward(const indexed_triangle_set &its,
                                const std::vector<size_t>  &faces,
                                bool                        closed)
{
    if (faces.empty() || its.vertices.empty())
        return false;

    if (closed) {
        const stl_vertex p0 = its.vertices.front();
        double volume  = 0.;
        double abs_acc = 0.;
        for (size_t fid : faces) {
            const auto &idx = its.indices[fid];
            if (!face_vertices_valid(its, idx))
                continue;
            const stl_vertex &a = its.vertices[idx[0]];
            const stl_vertex &b = its.vertices[idx[1]];
            const stl_vertex &c = its.vertices[idx[2]];
            const stl_vertex  U = b - a;
            const stl_vertex  V = c - a;
            const stl_vertex  C = U.cross(V);
            const float       n2 = C.squaredNorm();
            if (n2 == 0.f)
                continue;
            const float n      = std::sqrt(n2);
            const float area   = 0.5f * n;
            const float height = (C / n).dot(a - p0);
            const double tet   = (area * height) / 3.0;
            volume  += tet;
            abs_acc += std::abs(tet);
        }
        if (std::abs(volume) < k_orient_eps * abs_acc)
            return false;
        return volume < 0.;
    }

    Eigen::Vector3d weighted_bc = Eigen::Vector3d::Zero();
    double          totA        = 0.;
    struct FaceGeom {
        Eigen::Vector3d N;
        Eigen::Vector3d bc;
        double          area;
    };
    std::vector<FaceGeom> geoms;
    geoms.reserve(faces.size());

    for (size_t fid : faces) {
        const auto &idx = its.indices[fid];
        if (!face_vertices_valid(its, idx))
            continue;
        const Eigen::Vector3d a  = its.vertices[idx[0]].cast<double>();
        const Eigen::Vector3d b  = its.vertices[idx[1]].cast<double>();
        const Eigen::Vector3d c  = its.vertices[idx[2]].cast<double>();
        const Eigen::Vector3d C  = (b - a).cross(c - a);
        const double          n  = C.norm();
        if (n == 0.)
            continue;
        const double          area = 0.5 * n;
        const Eigen::Vector3d bc   = (a + b + c) / 3.0;
        const Eigen::Vector3d N    = C / n;
        weighted_bc += area * bc;
        totA        += area;
        geoms.push_back({ N, bc, area });
    }

    if (totA <= 0.)
        return false;

    const Eigen::Vector3d centroid = weighted_bc / totA;
    double                dot      = 0.;
    for (const FaceGeom &g : geoms)
        dot += g.area * g.N.dot(g.bc - centroid);
    if (std::abs(dot) < k_orient_eps * totA)
        return false;
    return dot < 0.;
}

static bool detect_inward_orientation(const indexed_triangle_set          &its,
                                      const std::vector<std::vector<size_t>> &adj)
{
    const size_t n = its.indices.size();
    if (n == 0)
        return false;

    std::vector<char>   seen(n, 0);
    std::vector<size_t> component;
    component.reserve(n);
    std::vector<size_t> stack;

    for (size_t seed = 0; seed < n; ++seed) {
        if (seen[seed])
            continue;

        component.clear();
        stack.clear();
        stack.push_back(seed);
        seen[seed] = 1;
        bool closed = true;

        while (!stack.empty()) {
            const size_t f = stack.back();
            stack.pop_back();
            component.push_back(f);
            if (adj[f].size() < 3)
                closed = false;
            for (size_t nb : adj[f]) {
                if (!seen[nb]) {
                    seen[nb] = 1;
                    stack.push_back(nb);
                }
            }
        }

        if (component_is_inward(its, component, closed))
            return true;
    }

    return false;
}

// Layer 3: visible backfaces from outside the AABB. Layers 1–2 miss geometric
// self-intersections / folds that still look reversed in the viewport (first
// hit from outside is a back-facing triangle). Closed, manifold meshes only.
static bool detect_visible_backfaces(const indexed_triangle_set &its)
{
    if (its.indices.empty() || its.vertices.empty())
        return false;

    // Skip layer 3 on malformed indices; AABBTreeIndirect indexes vertices
    // without a bounds check. Face count is not a reason to skip.
    for (const auto &idx : its.indices) {
        if (!face_vertices_valid(its, idx))
            return false;
    }

    Eigen::Vector3f bmin = its.vertices.front();
    Eigen::Vector3f bmax = bmin;
    for (const auto &v : its.vertices) {
        bmin = bmin.cwiseMin(v);
        bmax = bmax.cwiseMax(v);
    }

    const Vec3d  center  = 0.5 * (bmin + bmax).cast<double>();
    const Vec3d  extent  = (bmax - bmin).cast<double>();
    const double radius  = 0.5 * extent.norm();
    if (radius <= 0.)
        return false;
    const double standoff = radius + std::max(extent.maxCoeff() * 0.05, 1.0);

    const AABBTreeIndirect::Tree3f tree =
        AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(its.vertices, its.indices);

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

    constexpr double graze    = 0.08;
    constexpr double backface = 1e-4;
    // nlen = ||(b-a)×(c-a)|| = 2 * area. Drop slivers whose area is tiny
    // relative to the AABB; their normals are not reliable.
    const double min_nlen = 2.0 * radius * radius * 1e-12;

    for (Vec3d dir : dirs) {
        const double len = dir.norm();
        if (len == 0.)
            continue;
        dir /= len;
        const Vec3d origin  = center + dir * standoff;
        const Vec3d ray_dir = -dir;

        igl::Hit hit;
        if (!AABBTreeIndirect::intersect_ray_first_hit(its.vertices, its.indices, tree, origin, ray_dir, hit))
            continue;
        if (hit.t < 1e-6f)
            continue;
        if (hit.id < 0 || static_cast<size_t>(hit.id) >= its.indices.size())
            continue;

        const auto &idx = its.indices[hit.id];
        if (!face_vertices_valid(its, idx))
            continue;

        const Vec3d a = its.vertices[idx[0]].cast<double>();
        const Vec3d b = its.vertices[idx[1]].cast<double>();
        const Vec3d c = its.vertices[idx[2]].cast<double>();
        Vec3d       N = (b - a).cross(c - a);
        const double nlen = N.norm();
        if (nlen < min_nlen)
            continue;
        N /= nlen;

        if (std::abs(N.dot(ray_dir)) < graze)
            continue;

        const Vec3d hit_pt = origin + ray_dir * double(hit.t);
        const Vec3d V      = origin - hit_pt;
        if (N.dot(V) < -backface)
            return true;
    }

    return false;
}

static void maybe_detect_visible_backfaces(const indexed_triangle_set &its, MeshDiagnosticStats &result)
{
    if (result.has_reversed_faces)
        return;
    if (result.open_edges != 0 || result.non_manifold_edges != 0)
        return;
    result.has_reversed_faces = detect_visible_backfaces(its);
}

} // anonymous namespace

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
                edge_refs.push_back({ va, vb, fan_indices[i], fan_indices[j], fid, true });
            else
                edge_refs.push_back({ vb, va, fan_indices[j], fan_indices[i], fid, false });
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
    std::vector<bool>              on_nm_edge(num_vertices, false);
    std::vector<std::vector<size_t>> adj(num_faces);

    std::sort(edge_refs.begin(), edge_refs.end(), [](const EdgeRef &a, const EdgeRef &b) {
        return a.v0 < b.v0 || (a.v0 == b.v0 && a.v1 < b.v1);
    });

    for (size_t i = 0; i < edge_refs.size();) {
        size_t j = i + 1;
        while (j < edge_refs.size() && edge_refs[j].v0 == edge_refs[i].v0 && edge_refs[j].v1 == edge_refs[i].v1)
            ++j;

        const size_t edge_face_count = j - i;
        size_t plus = 0;
        for (size_t k = i; k < j; ++k)
            if (edge_refs[k].plus)
                ++plus;
        const size_t minus = edge_face_count - plus;
        if (plus >= 2 || minus >= 2)
            result.has_reversed_faces = true;

        if (edge_face_count == 1) {
            ++result.open_edges;
        } else if (edge_face_count == 2) {
            fan_union(vertex_fans[edge_refs[i].v0], edge_refs[i].v0_fan_idx, edge_refs[i + 1].v0_fan_idx);
            fan_union(vertex_fans[edge_refs[i].v1], edge_refs[i].v1_fan_idx, edge_refs[i + 1].v1_fan_idx);
            if (plus == 1 && minus == 1) {
                adj[edge_refs[i].face].push_back(edge_refs[i + 1].face);
                adj[edge_refs[i + 1].face].push_back(edge_refs[i].face);
            }
        } else {
            ++result.non_manifold_edges;
            on_nm_edge[edge_refs[i].v0] = true;
            on_nm_edge[edge_refs[i].v1] = true;
        }

        i = j;
    }

    if (!result.has_reversed_faces)
        result.has_reversed_faces = detect_inward_orientation(its, adj);
    maybe_detect_visible_backfaces(its, result);

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

    return result;
}

MeshDiagnosticStats its_edge_diagnostics(const indexed_triangle_set &its)
{
    MeshDiagnosticStats result;
    const size_t num_vertices = its.vertices.size();
    const size_t num_faces    = its.indices.size();

    if (num_faces == 0)
        return result;

    std::vector<DirectedEdge> edges;
    edges.reserve(num_faces * 3);

    for (size_t fid = 0; fid < num_faces; ++fid) {
        const auto &face = its.indices[fid];

        if (face[0] == face[1] || face[1] == face[2] || face[2] == face[0])
            continue;

        size_t v[3] = {
            static_cast<size_t>(face[0]),
            static_cast<size_t>(face[1]),
            static_cast<size_t>(face[2])
        };

        for (int i = 0; i < 3; ++i) {
            size_t va = v[i], vb = v[(i + 1) % 3];
            if (va >= num_vertices || vb >= num_vertices)
                continue;
            const bool plus = va < vb;
            if (!plus)
                std::swap(va, vb);
            edges.push_back({ va, vb, fid, plus });
        }
    }

    std::sort(edges.begin(), edges.end(), [](const DirectedEdge &a, const DirectedEdge &b) {
        return a.v0 < b.v0 || (a.v0 == b.v0 && a.v1 < b.v1);
    });

    std::vector<std::vector<size_t>> adj(num_faces);

    for (size_t i = 0; i < edges.size();) {
        size_t j = i + 1;
        while (j < edges.size() && edges[j].v0 == edges[i].v0 && edges[j].v1 == edges[i].v1)
            ++j;

        const size_t count = j - i;
        size_t plus = 0;
        for (size_t k = i; k < j; ++k)
            if (edges[k].plus)
                ++plus;
        const size_t minus = count - plus;
        if (plus >= 2 || minus >= 2)
            result.has_reversed_faces = true;

        if (count == 1)
            ++result.open_edges;
        else if (count == 2) {
            if (plus == 1 && minus == 1) {
                adj[edges[i].face].push_back(edges[i + 1].face);
                adj[edges[i + 1].face].push_back(edges[i].face);
            }
        } else if (count > 2)
            ++result.non_manifold_edges;

        i = j;
    }

    if (!result.has_reversed_faces)
        result.has_reversed_faces = detect_inward_orientation(its, adj);
    maybe_detect_visible_backfaces(its, result);

    return result;
}


} // namespace Slic3r
