#include "MinimumSpanningTree.hpp"

#include <iterator>
#include <algorithm>
#include "libslic3r.h"

namespace Slic3r
{

#define unscale_(val) ((val) * SCALING_FACTOR)

inline double dot_with_unscale(const Point a, const Point b)
{
    return unscale_(a(0)) * unscale_(b(0)) + unscale_(a(1)) * unscale_(b(1));
}

inline double vsize2_with_unscale(const Point pt)
{
    return dot_with_unscale(pt, pt);
}

MinimumSpanningTree::MinimumSpanningTree(std::vector<Point> vertices) : adjacency_graph(prim(vertices))
{
    //Just copy over the fields.
}

auto MinimumSpanningTree::prim(std::vector<Point> vertices) const -> AdjacencyGraph_t
{
    AdjacencyGraph_t result;
    if (vertices.empty())
    {
        return result; //No vertices, so we can't create edges either.
    }
    // If there's only one vertex, we can't go creating any edges so just add the point to the adjacency list with no
    // edges
    if (vertices.size() == 1)
    {
        // unordered_map::operator[]() will construct an empty vector in place for us when we try and access an element
        // that doesnt exist
        result[*vertices.begin()];
        return result;
    }
    result.reserve(vertices.size());
    const std::vector<Point> &vertices_list = vertices;
    const size_t num_vertices = vertices_list.size();

    // 用数组代替以点指针为键的哈希表；Prim 流程与并列取舍（距离、x、y）不变，生成的树完全相同。
    std::vector<coordf_t> smallest_distance(num_vertices);   //The shortest distance to the current tree.
    std::vector<size_t>   smallest_distance_to(num_vertices, 0); //Which point the shortest distance goes towards.
    std::vector<char>     in_tree(num_vertices, 0);
    in_tree[0] = 1;
    for (size_t vertex_index = 1; vertex_index < num_vertices; vertex_index++)
        smallest_distance[vertex_index] = vsize2_with_unscale(vertices_list[vertex_index] - vertices_list[0]);

    for (size_t num_added = 1; num_added < num_vertices; ++num_added)
    {
        //Choose the closest vertex to connect to that is not yet in the tree.
        size_t closest = size_t(-1);
        for (size_t i = 1; i < num_vertices; ++i) {
            if (in_tree[i])
                continue;
            if (closest == size_t(-1)) {
                closest = i;
                continue;
            }
            const coordf_t d  = smallest_distance[i];
            const coordf_t dc = smallest_distance[closest];
            const Point   &p  = vertices_list[i];
            const Point   &pc = vertices_list[closest];
            if (d != dc ? d < dc : (p.x() != pc.x() ? p.x() < pc.x() : p.y() < pc.y()))
                closest = i;
        }

        //Add this point to the graph and remove it from the candidates.
        const Point &closest_point = vertices_list[closest];
        const Point  other_end     = vertices_list[smallest_distance_to[closest]];
        result[closest_point].push_back({closest_point, other_end});
        result[other_end].push_back({other_end, closest_point});
        in_tree[closest] = 1;

        //Update the distances of all points that are not in the graph.
        for (size_t i = 1; i < num_vertices; ++i) {
            if (in_tree[i])
                continue;
            const coordf_t new_distance = vsize2_with_unscale(closest_point - vertices_list[i]);
            if (new_distance < smallest_distance[i]) //New point is closer.
            {
                smallest_distance[i]    = new_distance;
                smallest_distance_to[i] = closest;
            }
        }
    }

    return result;
}

std::vector<Point> MinimumSpanningTree::adjacent_nodes(Point node) const
{
    std::vector<Point> result;
    AdjacencyGraph_t::const_iterator adjacency_entry = adjacency_graph.find(node);
    if (adjacency_entry != adjacency_graph.end())
    {
        const auto& edges = adjacency_entry->second;
        std::transform(edges.begin(), edges.end(), std::back_inserter(result),
                       [&node](const Edge& e) { return (e.start == node) ? e.end : e.start; });
    }
    return result;
}

std::vector<Point> MinimumSpanningTree::leaves() const
{
    std::vector<Point> result;
    for (std::pair<Point, std::vector<Edge>> node : adjacency_graph)
    {
        if (node.second.size() <= 1)
        {
            result.push_back(node.first);
        }
    }
    return result;
}

std::vector<Point> MinimumSpanningTree::vertices() const
{
    std::vector<Point> result;
    using MapValue = std::pair<Point, std::vector<Edge>>;
    std::transform(adjacency_graph.begin(), adjacency_graph.end(), std::back_inserter(result),
                   [](const MapValue& node) { return node.first; });
    return result;
}

}
