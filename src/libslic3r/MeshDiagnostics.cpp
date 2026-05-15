#include "MeshDiagnostics.hpp"

#include <algorithm>
#include <array>
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
                edge_refs.push_back({ va, vb, fan_indices[i], fan_indices[j] });
            else
                edge_refs.push_back({ vb, va, fan_indices[j], fan_indices[i] });
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

    return result;
}

MeshDiagnosticStats its_edge_diagnostics(const indexed_triangle_set &its)
{
    MeshDiagnosticStats result;
    const size_t num_vertices = its.vertices.size();
    const size_t num_faces    = its.indices.size();

    if (num_faces == 0)
        return result;

    std::vector<std::pair<size_t, size_t>> edges;
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
            if (va > vb)
                std::swap(va, vb);
            edges.emplace_back(va, vb);
        }
    }

    std::sort(edges.begin(), edges.end());

    for (size_t i = 0; i < edges.size();) {
        size_t j = i + 1;
        while (j < edges.size() && edges[j] == edges[i])
            ++j;

        const size_t count = j - i;
        if (count == 1)
            ++result.open_edges;
        else if (count > 2)
            ++result.non_manifold_edges;

        i = j;
    }

    return result;
}


} // namespace Slic3r
