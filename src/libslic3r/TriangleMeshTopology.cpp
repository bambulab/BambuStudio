#include "TriangleMesh.hpp"
#include "MeshSplitImpl.hpp"

#include "Execution/ExecutionSeq.hpp"
#include "Execution/ExecutionTBB.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Slic3r {

void TriangleMesh::merge(const TriangleMesh &mesh)
{
    its_merge(this->its, mesh.its);
    m_stats = m_stats.merge(mesh.m_stats);
}

struct EdgeToFace {
    int vertex_low;
    int vertex_high;
    int face;
    int face_edge;

    bool operator<(const EdgeToFace &rhs) const
    {
        return this->vertex_low < rhs.vertex_low || (this->vertex_low == rhs.vertex_low && this->vertex_high < rhs.vertex_high);
    }

    bool operator==(const EdgeToFace &rhs) const
    {
        return this->vertex_low == rhs.vertex_low && this->vertex_high == rhs.vertex_high;
    }
};

template<typename FaceFilter, typename ThrowOnCancelCallback>
static std::vector<EdgeToFace> create_edge_map(const indexed_triangle_set &its, FaceFilter face_filter, ThrowOnCancelCallback throw_on_cancel)
{
    std::vector<EdgeToFace> edges_map;
    edges_map.reserve(its.indices.size() * 3);
    for (uint32_t facet_idx = 0; facet_idx < its.indices.size(); ++facet_idx)
        if (face_filter(facet_idx))
            for (int i = 0; i < 3; ++i) {
                edges_map.push_back({});
                EdgeToFace &e2f = edges_map.back();
                e2f.vertex_low  = its.indices[facet_idx][i];
                e2f.vertex_high = its.indices[facet_idx][(i + 1) % 3];
                e2f.face        = facet_idx;
                e2f.face_edge   = i + 1;
                if (e2f.vertex_low > e2f.vertex_high) {
                    std::swap(e2f.vertex_low, e2f.vertex_high);
                    e2f.face_edge = -e2f.face_edge;
                }
            }
    throw_on_cancel();
    std::sort(edges_map.begin(), edges_map.end());
    return edges_map;
}

template<typename FaceFilter, typename ThrowOnCancelCallback>
static inline std::vector<Vec3i> its_face_edge_ids_impl(const indexed_triangle_set &its, FaceFilter face_filter, ThrowOnCancelCallback throw_on_cancel)
{
    std::vector<Vec3i> out(its.indices.size(), Vec3i(-1, -1, -1));
    std::vector<EdgeToFace> edges_map = create_edge_map(its, face_filter, throw_on_cancel);

    int num_edges = 0;
    for (size_t i = 0; i < edges_map.size(); ++i) {
        EdgeToFace &edge_i = edges_map[i];
        if (edge_i.face == -1)
            continue;

        size_t j;
        bool found = false;
        for (j = i + 1; j < edges_map.size() && edge_i == edges_map[j]; ++j)
            if (edge_i.face_edge * edges_map[j].face_edge < 0 && edges_map[j].face != -1) {
                found = true;
                break;
            }

        if (!found)
            for (j = i + 1; j < edges_map.size() && edge_i == edges_map[j]; ++j)
                if (edges_map[j].face != -1) {
                    found = true;
                    break;
                }

        out[edge_i.face](std::abs(edge_i.face_edge) - 1) = num_edges;
        if (found) {
            EdgeToFace &edge_j = edges_map[j];
            out[edge_j.face](std::abs(edge_j.face_edge) - 1) = num_edges;
            edge_j.face = -1;
        }
        ++num_edges;
        if ((i & 0x0ffff) == 0)
            throw_on_cancel();
    }

    return out;
}

std::vector<Vec3i> its_face_edge_ids(const indexed_triangle_set &its)
{
    return its_face_edge_ids_impl(its, [](const uint32_t) { return true; }, []() {});
}

std::vector<Vec3i> its_face_edge_ids(const indexed_triangle_set &its, std::function<void()> throw_on_cancel_callback)
{
    return its_face_edge_ids_impl(its, [](const uint32_t) { return true; }, throw_on_cancel_callback);
}

std::vector<Vec3i> its_face_edge_ids(const indexed_triangle_set &its, const std::vector<bool> &face_mask)
{
    return its_face_edge_ids_impl(its, [&face_mask](const uint32_t idx) { return face_mask[idx]; }, []() {});
}

std::vector<Vec3i> its_face_edge_ids(const indexed_triangle_set &its, std::vector<Vec3i> &face_neighbors, bool assign_unbound_edges, int *num_edges)
{
    std::vector<Vec3i> out(face_neighbors.size());
    int last_edge_id = 0;
    for (int i = 0; i < int(face_neighbors.size()); ++i) {
        const stl_triangle_vertex_indices &triangle  = its.indices[i];
        const Vec3i &                      neighbors = face_neighbors[i];
        for (int j = 0; j < 3; ++j) {
            int n = neighbors[j];
            if (n > i) {
                const stl_triangle_vertex_indices &triangle2 = its.indices[n];
                int edge_id = last_edge_id++;
                Vec2i edge = its_triangle_edge(triangle, j);
                std::swap(edge(0), edge(1));
                int k = its_triangle_edge_index(triangle2, edge);
                if (k == -1) {
                    std::swap(edge(0), edge(1));
                    k = its_triangle_edge_index(triangle2, edge);
                }
                assert(k >= 0);
                out[i](j) = edge_id;
                out[n](k) = edge_id;
            } else if (n == -1) {
                out[i](j) = assign_unbound_edges ? last_edge_id++ : -1;
            } else {
                assert(n < i);
            }
        }
    }
    if (num_edges)
        *num_edges = last_edge_id;
    return out;
}

void its_flip_triangles(indexed_triangle_set &its)
{
    for (stl_triangle_vertex_indices &face : its.indices)
        std::swap(face(1), face(2));
}

int its_compactify_vertices(indexed_triangle_set &its, bool shrink_to_fit)
{
    std::vector<int> vertex_map(its.vertices.size(), 0);
    for (const stl_triangle_vertex_indices &face : its.indices)
        for (int i = 0; i < 3; ++i)
            vertex_map[face(i)] = 1;

    int last = 0;
    for (int i = 0; i < int(vertex_map.size()); ++i)
        if (vertex_map[i]) {
            if (last < i)
                its.vertices[last] = its.vertices[i];
            vertex_map[i] = last++;
        }

    int removed = int(its.vertices.size()) - last;
    if (removed) {
        its.vertices.erase(its.vertices.begin() + last, its.vertices.end());
        for (stl_triangle_vertex_indices &face : its.indices)
            for (int i = 0; i < 3; ++i)
                face(i) = vertex_map[face(i)];
        if (shrink_to_fit)
            its.vertices.shrink_to_fit();
    }

    return removed;
}

void its_merge(indexed_triangle_set &A, const indexed_triangle_set &B)
{
    auto N = int(A.vertices.size());
    auto N_f = A.indices.size();

    A.vertices.insert(A.vertices.end(), B.vertices.begin(), B.vertices.end());
    A.indices.insert(A.indices.end(), B.indices.begin(), B.indices.end());

    for (size_t n = N_f; n < A.indices.size(); n++)
        A.indices[n] += Vec3i{N, N, N};
}

void its_merge(indexed_triangle_set &A, const std::vector<Vec3f> &triangles)
{
    const size_t offs = A.vertices.size();
    A.vertices.insert(A.vertices.end(), triangles.begin(), triangles.end());
    A.indices.reserve(A.indices.size() + A.vertices.size() / 3);

    for (int i = int(offs); i < int(A.vertices.size()); i += 3)
        A.indices.emplace_back(i, i + 1, i + 2);
}

void its_merge(indexed_triangle_set &A, const Pointf3s &triangles)
{
    auto trianglesf = reserve_vector<Vec3f>(triangles.size());
    for (auto &t : triangles)
        trianglesf.emplace_back(t.cast<float>());
    its_merge(A, trianglesf);
}

void VertexFaceIndex::create(const indexed_triangle_set &its)
{
    m_vertex_to_face_start.assign(its.vertices.size() + 1, 0);
    for (auto &face : its.indices) {
        ++m_vertex_to_face_start[face(0) + 1];
        ++m_vertex_to_face_start[face(1) + 1];
        ++m_vertex_to_face_start[face(2) + 1];
    }
    for (size_t i = 2; i < m_vertex_to_face_start.size(); ++i)
        m_vertex_to_face_start[i] += m_vertex_to_face_start[i - 1];
    m_vertex_faces_all.assign(m_vertex_to_face_start.back(), 0);
    for (size_t face_idx = 0; face_idx < its.indices.size(); ++face_idx) {
        auto &face = its.indices[face_idx];
        for (int i = 0; i < 3; ++i)
            m_vertex_faces_all[m_vertex_to_face_start[face(i)]++] = face_idx;
    }
    for (auto i = int(m_vertex_to_face_start.size()) - 1; i > 0; --i)
        m_vertex_to_face_start[i] = m_vertex_to_face_start[i - 1];
    m_vertex_to_face_start.front() = 0;
}

std::vector<Vec3i> its_face_neighbors(const indexed_triangle_set &its)
{
    return create_face_neighbors_index(ex_seq, its);
}

std::vector<Vec3i> its_face_neighbors_par(const indexed_triangle_set &its)
{
    return create_face_neighbors_index(ex_tbb, its);
}

std::vector<Vec3f> its_face_normals(const indexed_triangle_set &its)
{
    std::vector<Vec3f> normals;
    normals.reserve(its.indices.size());
    for (stl_triangle_vertex_indices face : its.indices)
        normals.push_back(its_face_normal(its, face));
    return normals;
}

} // namespace Slic3r
