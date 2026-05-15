#include "KdTree.hpp"

#include <algorithm>
#include <numeric>

#ifndef FLOAT_LIMITS
#define FLOAT_LIMITS
#define FLOAT_MAX std::numeric_limits<float>::max()
#define FLOAT_MIN -std::numeric_limits<float>::max()
#endif  // !FLOAT_LIMITS

namespace Slic3r { namespace tex2color {

namespace {

// Pick the axis (0=x, 1=y, 2=z) along which the bounding-box is longest.
std::size_t pick_longest_axis(const Vec3f& extent) {
    std::size_t axis = 0;
    float length = extent[0];
    if (extent[1] > length) {
        length = extent[1];
        axis = 1;
    }
    if (extent[2] > length) {
        length = extent[2];
        axis = 2;
    }
    return axis;
}

// Clamp the parameter s onto [0, 1] and return the corresponding point on
// segment a-b. `edge` must equal (b - a). Behaviour:
//   s <= 0  -> a
//   s >= 1  -> b
//   else    -> a + edge * s
Vec3f place_on_edge(float s, const Vec3f& a, const Vec3f& b, const Vec3f& edge) {
    if (s <= 0.f) return a;
    if (s >= 1.f) return b;
    return a + edge * s;
}

}  // namespace

KdTree::KdTree(const TriMesh& _mesh, std::size_t max_faces, std::size_t max_depth)
    : mesh(_mesh) {
    root = std::make_shared<Node>();
    root->faces = std::make_shared<Triangles>();
    root->faces->resize(mesh.facets_count());
    std::iota(root->faces->begin(), root->faces->end(), 0);
    build_recurse(root, max_faces, max_depth);
}

KdTree::~KdTree() {
}

std::size_t KdTree::build_recurse(NodeSPtr node, std::size_t max_faces, std::size_t depth) {
    if (depth == 0 || node->faces->size() <= max_faces) {
        return depth;
    }

    const Bounds bounds       = compute_face_bounds(*node->faces);
    const Vec3f  extent       = bounds.max - bounds.min;
    const std::size_t axis    = pick_longest_axis(extent);
    const float  split        = compute_median_split(*node->faces, axis);

    auto left = std::make_shared<Node>();
    left->faces = std::make_shared<Triangles>();
    left->faces->reserve(node->faces->size() / 2);
    auto right = std::make_shared<Node>();
    right->faces = std::make_shared<Triangles>();
    right->faces->reserve(node->faces->size() / 2);

    partition_faces_by_plane(*node->faces, axis, split, *left->faces, *right->faces);

    // The split is useless if every face ended up on a single side; turn this
    // node into a leaf and stop subdividing.
    if (left->faces->size() == node->faces->size() || right->faces->size() == node->faces->size()) {
        node->faces->shrink_to_fit();
        return depth;
    }

    node->faces       = nullptr;
    node->axis        = axis;
    node->split       = split;
    node->left_child  = left;
    node->right_child = right;
    const std::size_t depth_left  = build_recurse(node->left_child,  max_faces, depth - 1);
    const std::size_t depth_right = build_recurse(node->right_child, max_faces, depth - 1);
    return std::min(depth_left, depth_right);
}

KdTree::Bounds KdTree::compute_face_bounds(const Triangles& faces) const {
    Bounds bounds;
    bounds.max = Vec3f(FLOAT_MIN, FLOAT_MIN, FLOAT_MIN);
    bounds.min = Vec3f(FLOAT_MAX, FLOAT_MAX, FLOAT_MAX);
    for (const auto& fid : faces) {
        const auto& face = mesh.indices[fid];
        for (int vi = 0; vi < 3; ++vi) {
            const auto& v = mesh.vertices[face[vi]];
            bounds.max[0] = std::max(bounds.max[0], v.x());
            bounds.max[1] = std::max(bounds.max[1], v.y());
            bounds.max[2] = std::max(bounds.max[2], v.z());
            bounds.min[0] = std::min(bounds.min[0], v.x());
            bounds.min[1] = std::min(bounds.min[1], v.y());
            bounds.min[2] = std::min(bounds.min[2], v.z());
        }
    }
    return bounds;
}

float KdTree::compute_median_split(const Triangles& faces, std::size_t axis) const {
    std::vector<float> axis_values;
    axis_values.reserve(faces.size() * 3);
    for (const auto& fid : faces) {
        const auto& face = mesh.indices[fid];
        for (int vi = 0; vi < 3; ++vi) {
            const auto& v = mesh.vertices[face[vi]];
            axis_values.push_back(v[axis]);
        }
    }

    const std::size_t median_index = axis_values.size() / 2;
    std::nth_element(axis_values.begin(), axis_values.begin() + median_index, axis_values.end());
    return axis_values[median_index];
}

void KdTree::partition_faces_by_plane(const Triangles& faces, std::size_t axis, float split,
                                      Triangles& left, Triangles& right) const {
    // A face whose vertices straddle the split plane is added to BOTH children;
    // a face that is entirely on one side is added only to that side.
    for (const auto& fid : faces) {
        const auto& face = mesh.indices[fid];
        bool goes_left  = false;
        bool goes_right = false;
        for (int vi = 0; vi < 3; ++vi) {
            const auto& v = mesh.vertices[face[vi]];
            if (v[axis] <= split) {
                goes_left = true;
            } else {
                goes_right = true;
            }
        }
        if (goes_left)  left.push_back(fid);
        if (goes_right) right.push_back(fid);
    }
}

KdTree::NearestNeighbor KdTree::nearest(const Vec3f& v) const {
    NearestNeighbor data;
    data.dist = FLOAT_MAX;
    data.tests = 0;
    nearest_recurse(root, v, data);
    return data;
}

void KdTree::nearest_recurse(NodeSPtr node, const Vec3f& v, NearestNeighbor& data) const {
    if (!node->left_child) {
        test_leaf(node, v, data);
        return;
    }

    // Visit the near side first; only descend into the far side if the
    // splitting plane is closer than the best distance found so far.
    const float signed_dist = v[node->axis] - node->split;
    if (signed_dist <= 0.f) {
        nearest_recurse(node->left_child, v, data);
        if (std::fabs(signed_dist) < data.dist) {
            nearest_recurse(node->right_child, v, data);
        }
    } else {
        nearest_recurse(node->right_child, v, data);
        if (std::fabs(signed_dist) < data.dist) {
            nearest_recurse(node->left_child, v, data);
        }
    }
}

void KdTree::test_leaf(NodeSPtr node, const Vec3f& v, NearestNeighbor& data) const {
    Vec3f closest;
    for (auto& fid : *node->faces) {
        const float d = dist_point_triangle(v, fid, closest);
        ++data.tests;
        if (d < data.dist) {
            data.dist = d;
            data.face_id = fid;
            data.nearest = closest;
        }
    }
}

float KdTree::dist_point_triangle(const Vec3f& v, std::size_t& fid, Vec3f& nearest_vertex) const {
    auto& face = mesh.indices[fid];
    auto v0 = mesh.vertices[face[0]], v1 = mesh.vertices[face[1]], v2 = mesh.vertices[face[2]];
    Vec3f vec01 = v1 - v0;
    Vec3f vec02 = v2 - v0;
    Vec3f n = vec01.cross(vec02);
    float squared_norm = n.squaredNorm();

    if (std::fabs(squared_norm) < 1e-6f) {
        float dist;
        const Vec3f closest = closest_on_degenerate_triangle(v, v0, v1, v2, dist);
        nearest_vertex = closest;
        return dist;
    }

    const float inv_d = 1.f / squared_norm;
    Vec3f vec12 = v2 - v1;
    Vec3f v0v   = v - v0;
    Vec3f t     = v0v.cross(n);
    const float a = (t.dot(vec02)) * (-inv_d);
    const float b = (t.dot(vec01)) * ( inv_d);

    Vec3f closest;
    float s01, s02, s12;
    if (a < 0.f) {
        // Projection lies past edge v0-v2 (away from v2); see which adjacent
        // edge or vertex actually carries the closest point.
        s02 = vec02.dot(v0v) / vec02.squaredNorm();
        if (s02 < 0.f) {
            s01 = vec01.dot(v0v) / vec01.squaredNorm();
            closest = place_on_edge(s01, v0, v1, vec01);
        } else if (s02 > 1.f) {
            s12 = vec12.dot(v - v1) / vec12.squaredNorm();
            closest = place_on_edge(s12, v1, v2, vec12);
        } else {
            closest = v0 + vec02 * s02;
        }
    } else if (b < 0.f) {
        // Projection lies past edge v0-v1 (away from v1).
        s01 = vec01.dot(v0v) / vec01.squaredNorm();
        if (s01 < 0.f) {
            // Project (v - v0) onto edge v0-v2 so we can clamp onto it,
            // matching the symmetric branch in `a < 0`.
            s02 = vec02.dot(v0v) / vec02.squaredNorm();
            closest = place_on_edge(s02, v0, v2, vec02);
        } else if (s01 > 1.f) {
            s12 = vec12.dot(v - v1) / vec12.squaredNorm();
            closest = place_on_edge(s12, v1, v2, vec12);
        } else {
            closest = v0 + vec01 * s01;
        }
    } else if (a + b > 1.f) {
        // Projection lies past edge v1-v2.
        s12 = vec12.dot(v - v1) / vec12.squaredNorm();
        if (s12 >= 1.f) {
            s02 = vec02.dot(v0v) / vec02.squaredNorm();
            closest = place_on_edge(s02, v0, v2, vec02);
        } else if (s12 <= 0.f) {
            s01 = vec01.dot(v0v) / vec01.squaredNorm();
            closest = place_on_edge(s01, v0, v1, vec01);
        } else {
            closest = v1 + vec12 * s12;
        }
    } else {
        // Projection lies inside the triangle; subtract the perpendicular
        // component from v to land on the triangle plane.
        n *= ((n.dot(v0v)) * inv_d);
        closest = v - n;
    }

    nearest_vertex = closest;
    return (closest - v).norm();
}

Vec3f KdTree::closest_on_degenerate_triangle(const Vec3f& v,
                                             const Vec3f& v0_in, const Vec3f& v1_in, const Vec3f& v2_in,
                                             float& out_dist) const {
    // dist_point_line_segment requires non-const Vec3f& arguments (it does
    // not actually mutate them); take local copies so we can match the API.
    Vec3f v0 = v0_in;
    Vec3f v1 = v1_in;
    Vec3f v2 = v2_in;

    Vec3f closest_on_edge;
    Vec3f best_closest;
    float d         = dist_point_line_segment(v, v1, v2, closest_on_edge);
    float best_dist = dist_point_line_segment(v, v0, v1, best_closest);
    if (d < best_dist) {
        best_dist    = d;
        best_closest = closest_on_edge;
    }
    d = dist_point_line_segment(v, v2, v0, closest_on_edge);
    if (d < best_dist) {
        best_dist    = d;
        best_closest = closest_on_edge;
    }
    out_dist = best_dist;
    return best_closest;
}

float KdTree::dist_point_line_segment(const Vec3f& v, Vec3f& v0, Vec3f& v1, Vec3f& nearest_vertex) const {
    // Squared-length threshold below which the segment is treated as a point.
    constexpr float kDegenerateSqLength = 1e-6f;

    Vec3f       to_point = v  - v0;
    const Vec3f edge     = v1 - v0;
    float       t        = edge.dot(edge);
    Vec3f       closest  = v0;
    if (t > kDegenerateSqLength) {
        t = to_point.dot(edge) / t;
        if (t > 1.f) {
            closest  = v1;
            to_point = v - closest;
        } else if (t > 0.f) {
            closest  = v0 + edge * t;
            to_point = v - closest;
        }
    }
    nearest_vertex = closest;
    return to_point.norm();
}

}  // namespace tex2color
}  // namespace Slic3r
