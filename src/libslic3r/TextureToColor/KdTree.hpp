#pragma once

#include "TriMesh.hpp"

namespace Slic3r { namespace tex2color {
/**
 * @brief Spatial acceleration structure for the mesh, used to quickly find the
 *        closest point on the mesh to a given query point (including point-in-face).
 */
class KdTree
{
  public:
    /**
    * @brief Construct a KdTree.
    *
    * @param[in] mesh         The mesh used for the KdTree; an internal copy is made, so external modifications won't affect this tree.
    * @param[in] max_faces    Maximum number of faces allowed per leaf node; smaller values produce more leaves. The default is usually fine.
    * @param[in] max_depth    Maximum depth of the KdTree, preventing unlimited subdivision on very large meshes. The default is usually fine.
    */
    KdTree(const TriMesh& mesh, std::size_t max_faces = 10, std::size_t max_depth = 30);

    ~KdTree();

    struct NearestNeighbor {
        float dist;  // Distance from the query point to the closest point on the mesh
        std::size_t face_id;  // Face id that contains the closest point
        Vec3f nearest;  // Coordinates of the closest point on the mesh
        int tests;  // Number of nodes tested during the query (can usually be ignored)
    };

    /**
    * @brief Return the nearest-neighbor information for a query point.
    *
    * @param[in] query_point The query vertex.
    * @return NearestNeighbor
    */
    NearestNeighbor nearest(const Vec3f& query_point) const;

  private:
    struct Node;

    typedef std::vector<std::size_t> Triangles;
    typedef std::shared_ptr<Triangles> TrianglesSPtr;
    typedef std::shared_ptr<Node> NodeSPtr;

    struct Node {
        std::size_t axis = 0;
        float split = 0.f;
        TrianglesSPtr faces = nullptr;
        NodeSPtr left_child = nullptr;
        NodeSPtr right_child = nullptr;
    };

    struct Bounds {
        Vec3f min;
        Vec3f max;
    };

    TriMesh mesh;

    NodeSPtr root = nullptr;

    std::size_t build_recurse(NodeSPtr node, std::size_t max_faces, std::size_t depth);

    Bounds compute_face_bounds(const Triangles& faces) const;
    float  compute_median_split(const Triangles& faces, std::size_t axis) const;
    void   partition_faces_by_plane(const Triangles& faces, std::size_t axis, float split,
                                    Triangles& left, Triangles& right) const;

    float dist_point_triangle(const Vec3f& v, std::size_t& fid, Vec3f& nearest_vertex) const;

    float dist_point_line_segment(const Vec3f& v, Vec3f& v0, Vec3f& v1, Vec3f& nearest_vertex) const;

    Vec3f closest_on_degenerate_triangle(const Vec3f& v,
                                         const Vec3f& v0, const Vec3f& v1, const Vec3f& v2,
                                         float& out_dist) const;

    void nearest_recurse(NodeSPtr node, const Vec3f& v, NearestNeighbor& data) const;

    void test_leaf(NodeSPtr node, const Vec3f& v, NearestNeighbor& data) const;
};

}  // namespace tex2color
}  // namespace Slic3r
