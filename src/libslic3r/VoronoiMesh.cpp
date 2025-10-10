#include "VoronoiMesh.hpp"
#include "libslic3r/AABBMesh.hpp"
#include "libslic3r/MeshBoolean.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include <random>
#include <algorithm>
#include <set>
#include <map>
#include <queue>
#include <array>
#include <cmath>
#include <limits>
#include <chrono>
#include <numeric>

#include <boost/log/trivial.hpp>

// Voro++ for 3D Voronoi tessellation
#include "src/voro++.hh"

// CGAL headers for mesh operations (boolean, convex hull, etc)
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Delaunay_triangulation_3.h>
#include <CGAL/Triangulation_vertex_base_with_info_3.h>
#include <CGAL/Delaunay_triangulation_cell_base_with_circumcenter_3.h>
#include <CGAL/convex_hull_3.h>
#include <CGAL/Polyhedron_3.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/orientation.h>
#include <CGAL/IO/io.h>

namespace Slic3r {

    namespace PMP = CGAL::Polygon_mesh_processing;

    // CGAL type definitions for 3D Delaunay/Voronoi
    using K = CGAL::Exact_predicates_inexact_constructions_kernel;
    using Vb = CGAL::Triangulation_vertex_base_with_info_3<int, K>;
    using Cb = CGAL::Delaunay_triangulation_cell_base_with_circumcenter_3<K>;
    using Tds = CGAL::Triangulation_data_structure_3<Vb, Cb>;
    using Delaunay = CGAL::Delaunay_triangulation_3<K, Tds>;
    using Point_3 = K::Point_3;
    using CGALMesh = CGAL::Surface_mesh<Point_3>;

    namespace {
        
        // Geometric tolerances for robust processing
        constexpr double EPSILON = 1e-6;
        constexpr double MIN_EDGE_LENGTH = 1e-4;
        constexpr double MIN_FACE_AREA = 1e-8;
        constexpr double WELD_DISTANCE = 1e-5;
        constexpr double BOUNDARY_MARGIN_PERCENT = 0.05;  // 5% expansion
        
        // Forward declarations for helper functions
        void clip_wireframe_to_mesh(indexed_triangle_set& wireframe, const indexed_triangle_set& mesh);
        
        // Hash function for Vec3i to use in unordered_map
        struct Vec3iHash {
            std::size_t operator()(const Vec3i &cell_id) const {
                return std::hash<int>()(cell_id.x()) ^ 
                       std::hash<int>()(cell_id.y() * 593) ^
                       std::hash<int>()(cell_id.z() * 7919);
            }
        };
        
        // Spatial hash for efficient vertex welding
        struct SpatialHash {
            std::unordered_map<Vec3i, std::vector<size_t>, Vec3iHash> grid;
            double cell_size;
            
            explicit SpatialHash(double cs) : cell_size(cs) {}
            
            Vec3i hash_pos(const Vec3f& p) const {
                return Vec3i(
                    int(std::floor(p.x() / cell_size)),
                    int(std::floor(p.y() / cell_size)),
                    int(std::floor(p.z() / cell_size))
                );
            }
            
            void insert(size_t vertex_id, const Vec3f& pos) {
                Vec3i h = hash_pos(pos);
                grid[h].push_back(vertex_id);
            }
            
            std::vector<size_t> query_neighbors(const Vec3f& pos, double radius) {
                std::vector<size_t> result;
                int search_range = int(std::ceil(radius / cell_size));
                Vec3i center = hash_pos(pos);
                
                for (int dx = -search_range; dx <= search_range; ++dx) {
                    for (int dy = -search_range; dy <= search_range; ++dy) {
                        for (int dz = -search_range; dz <= search_range; ++dz) {
                            Vec3i h = center + Vec3i(dx, dy, dz);
                            auto it = grid.find(h);
                            if (it != grid.end()) {
                                result.insert(result.end(), it->second.begin(), it->second.end());
                            }
                        }
                    }
                }
                return result;
            }
        };
        
        // Weld nearby vertices to eliminate near-duplicates
        void weld_vertices(indexed_triangle_set& mesh, double weld_threshold = WELD_DISTANCE) {
            if (mesh.vertices.empty()) return;
            
            BOOST_LOG_TRIVIAL(info) << "weld_vertices: Input vertices=" << mesh.vertices.size();
            
            // Build spatial hash
            SpatialHash hash(weld_threshold * 2.0);
            for (size_t i = 0; i < mesh.vertices.size(); ++i) {
                hash.insert(i, mesh.vertices[i]);
            }
            
            // Find vertex clusters and build remap table
            std::vector<int> vertex_remap(mesh.vertices.size());
            std::iota(vertex_remap.begin(), vertex_remap.end(), 0);
            
            std::vector<bool> visited(mesh.vertices.size(), false);
            std::vector<Vec3f> new_vertices;
            new_vertices.reserve(mesh.vertices.size());
            
            for (size_t i = 0; i < mesh.vertices.size(); ++i) {
                if (visited[i]) continue;
                
                // Find all vertices within weld distance
                auto neighbors = hash.query_neighbors(mesh.vertices[i], weld_threshold);
                
                std::vector<size_t> cluster;
                for (size_t j : neighbors) {
                    if (!visited[j] && (mesh.vertices[i] - mesh.vertices[j]).norm() < weld_threshold) {
                        cluster.push_back(j);
                        visited[j] = true;
                    }
                }
                
                if (cluster.empty()) {
                    cluster.push_back(i);
                    visited[i] = true;
                }
                
                // Compute centroid of cluster
                Vec3f centroid = Vec3f::Zero();
                for (size_t j : cluster) {
                    centroid += mesh.vertices[j];
                }
                centroid /= float(cluster.size());
                
                // Add welded vertex
                int new_idx = new_vertices.size();
                new_vertices.push_back(centroid);
                
                // Remap all vertices in cluster to new index
                for (size_t j : cluster) {
                    vertex_remap[j] = new_idx;
                }
            }
            
            // Remap face indices
            for (auto& face : mesh.indices) {
                face[0] = vertex_remap[face[0]];
                face[1] = vertex_remap[face[1]];
                face[2] = vertex_remap[face[2]];
            }
            
            mesh.vertices = std::move(new_vertices);
            
            BOOST_LOG_TRIVIAL(info) << "weld_vertices: Output vertices=" << mesh.vertices.size() 
                                     << " (removed " << (visited.size() - new_vertices.size()) << " duplicates)";
        }
        
        // Remove degenerate edges and faces
        void remove_degenerates(indexed_triangle_set& mesh) {
            if (mesh.indices.empty()) return;
            
            BOOST_LOG_TRIVIAL(info) << "remove_degenerates: Input faces=" << mesh.indices.size();
            
            std::vector<Vec3i> new_faces;
            new_faces.reserve(mesh.indices.size());
            
            int removed_degenerate = 0;
            int removed_zero_area = 0;
            
            for (const auto& face : mesh.indices) {
                // Check for degenerate face (repeated indices)
                if (face[0] == face[1] || face[1] == face[2] || face[2] == face[0]) {
                    removed_degenerate++;
                    continue;
                }
                
                // Check for zero-area face
                const Vec3f& v0 = mesh.vertices[face[0]];
                const Vec3f& v1 = mesh.vertices[face[1]];
                const Vec3f& v2 = mesh.vertices[face[2]];
                
                Vec3f e1 = v1 - v0;
                Vec3f e2 = v2 - v0;
                double area = 0.5 * e1.cross(e2).norm();
                
                if (area < MIN_FACE_AREA) {
                    removed_zero_area++;
                    continue;
                }
                
                new_faces.push_back(face);
            }
            
            mesh.indices = std::move(new_faces);
            
            BOOST_LOG_TRIVIAL(info) << "remove_degenerates: Output faces=" << mesh.indices.size()
                                     << " (removed " << removed_degenerate << " degenerate, "
                                     << removed_zero_area << " zero-area)";
        }
        
        // Expand bounding box by margin
        BoundingBoxf3 expand_bounds(const BoundingBoxf3& bbox, double margin_percent) {
            Vec3d size = bbox.size();
            double margin = size.minCoeff() * margin_percent;
            
            BoundingBoxf3 expanded = bbox;
            expanded.min -= Vec3d(margin, margin, margin);
            expanded.max += Vec3d(margin, margin, margin);
            
            return expanded;
        }

        // Helper function to test if a point is inside a mesh using raycast
        bool is_point_inside_mesh(const AABBMesh& aabb_mesh, const Vec3d& point) {
            // Cast a ray in arbitrary direction (we use +Z)
            Vec3d dir(0.0, 0.0, 1.0);
            auto hit = aabb_mesh.query_ray_hit(point, dir);
            
            // If we hit the mesh and the hit is from inside, point is inside
            return hit.is_inside();
        }

        indexed_triangle_set surface_mesh_to_indexed(const CGALMesh& mesh)
        {
            indexed_triangle_set its;
            its.vertices.reserve(mesh.number_of_vertices());
            its.indices.reserve(mesh.number_of_faces());
            std::map<CGALMesh::Vertex_index, size_t> vertex_map;
            size_t idx = 0;
            for (auto v : mesh.vertices()) {
                const auto& p = mesh.point(v);
                its.vertices.emplace_back(float(p.x()), float(p.y()), float(p.z()));
                vertex_map[v] = idx++;
            }
            for (auto f : mesh.faces()) {
                auto he = mesh.halfedge(f);
                std::vector<size_t> face_vertices;
                auto start = he;
                do {
                    auto v = mesh.target(he);
                    face_vertices.push_back(vertex_map[v]);
                    he = mesh.next(he);
                } while (he != start);

                if (face_vertices.size() == 3) {
                    its.indices.emplace_back(int(face_vertices[0]),
                        int(face_vertices[1]),
                        int(face_vertices[2]));
                }
                else if (face_vertices.size() > 3) {
                    for (size_t i = 1; i + 1 < face_vertices.size(); ++i) {
                        its.indices.emplace_back(int(face_vertices[0]),
                            int(face_vertices[i]),
                            int(face_vertices[i + 1]));
                    }
                }
            }
            return its;
        }

        bool indexed_to_surface_mesh(const indexed_triangle_set& its, CGALMesh& mesh)
        {
            if (its.vertices.empty() || its.indices.empty())
                return false;

            std::vector<Point_3> points;
            points.reserve(its.vertices.size());
            for (const auto& v : its.vertices)
                points.emplace_back(v.x(), v.y(), v.z());

            std::vector<std::array<size_t, 3>> faces;
            faces.reserve(its.indices.size());
            for (const auto& tri : its.indices) {
                faces.push_back({ size_t(tri(0)), size_t(tri(1)), size_t(tri(2)) });
            }

            try {
                PMP::orient_polygon_soup(points, faces);
                PMP::polygon_soup_to_polygon_mesh(points, faces, mesh);
                if (mesh.is_empty() || !CGAL::is_closed(mesh))
                    return false;
                PMP::orient_to_bound_a_volume(mesh);
            }
            catch (...) {
                return false;
            }
            return true;
        }
        
        // Robust geometric processing utilities
        // UPDATED: Tighter tolerances for mathematical accuracy
        constexpr double GEOM_EPSILON = 1e-6;
        constexpr double MIN_EDGE_LENGTH_SQ = 1e-8;
        constexpr double MIN_FACE_AREA = 1e-8;
        constexpr double WELD_THRESHOLD = 1e-7;  // REDUCED from 1e-5 for accuracy
        constexpr double MIN_SEED_SEPARATION_FACTOR = 0.5;  // Relative to cell size
        
        // Filter seed points to ensure minimum separation
        std::vector<Vec3d> filter_seed_points(
            const std::vector<Vec3d>& seeds,
            const BoundingBoxf3& bounds)
        {
            if (seeds.empty()) return seeds;
            
            double bbox_size = bounds.size().norm();
            double min_separation = bbox_size / std::sqrt(double(seeds.size())) * MIN_SEED_SEPARATION_FACTOR;
            
            BOOST_LOG_TRIVIAL(info) << "Filtering seeds: min_separation=" << min_separation 
                                     << " (bbox_size=" << bbox_size << ", num_seeds=" << seeds.size() << ")";
            
            std::vector<Vec3d> filtered;
            filtered.reserve(seeds.size());
            
            for (const auto& seed : seeds) {
                bool too_close = false;
                for (const auto& existing : filtered) {
                    if ((seed - existing).norm() < min_separation) {
                        too_close = true;
                        break;
                    }
                }
                if (!too_close) {
                    filtered.push_back(seed);
                }
            }
            
            if (filtered.size() < seeds.size()) {
                BOOST_LOG_TRIVIAL(info) << "Filtered out " << (seeds.size() - filtered.size()) 
                                         << " seeds that were too close (prevents needle-thin cells)";
            }
            
            return filtered;
        }
        
        // Validate geometry for NaN/Inf and report issues
        bool validate_geometry(const indexed_triangle_set& mesh, const std::string& label) {
            bool has_issues = false;
            int nan_vertices = 0;
            int inf_vertices = 0;
            int zero_area_faces = 0;
            int negative_area_faces = 0;
            
            // Check vertices for NaN/Inf
            for (size_t i = 0; i < mesh.vertices.size(); ++i) {
                const Vec3f& v = mesh.vertices[i];
                if (std::isnan(v.x()) || std::isnan(v.y()) || std::isnan(v.z())) {
                    nan_vertices++;
                    has_issues = true;
                }
                if (std::isinf(v.x()) || std::isinf(v.y()) || std::isinf(v.z())) {
                    inf_vertices++;
                    has_issues = true;
                }
            }
            
            // Check faces for zero/negative area
            for (const auto& face : mesh.indices) {
                if (face[0] >= mesh.vertices.size() || 
                    face[1] >= mesh.vertices.size() || 
                    face[2] >= mesh.vertices.size()) {
                    continue; // Invalid index
                }
                
                const Vec3f& v0 = mesh.vertices[face[0]];
                const Vec3f& v1 = mesh.vertices[face[1]];
                const Vec3f& v2 = mesh.vertices[face[2]];
                
                Vec3f e1 = v1 - v0;
                Vec3f e2 = v2 - v0;
                Vec3f cross = e1.cross(e2);
                double area = 0.5 * cross.norm();
                
                if (area < MIN_FACE_AREA) {
                    zero_area_faces++;
                }
                
                // Check if face is inverted (negative area via signed volume)
                double signed_vol = cross.dot(v0);
                if (signed_vol < -GEOM_EPSILON) {
                    negative_area_faces++;
                }
            }
            
            if (has_issues || zero_area_faces > 0 || negative_area_faces > 0) {
                BOOST_LOG_TRIVIAL(warning) << label << " geometry validation:";
                if (nan_vertices > 0)
                    BOOST_LOG_TRIVIAL(warning) << "  - NaN vertices: " << nan_vertices;
                if (inf_vertices > 0)
                    BOOST_LOG_TRIVIAL(warning) << "  - Inf vertices: " << inf_vertices;
                if (zero_area_faces > 0)
                    BOOST_LOG_TRIVIAL(warning) << "  - Zero-area faces: " << zero_area_faces;
                if (negative_area_faces > 0)
                    BOOST_LOG_TRIVIAL(warning) << "  - Potentially inverted faces: " << negative_area_faces;
            } else {
                BOOST_LOG_TRIVIAL(info) << label << " geometry validation: CLEAN";
            }
            
            return !has_issues;
        }
        
        // Check manifoldness: all edges should have exactly 2 adjacent faces
        void validate_manifoldness(const indexed_triangle_set& mesh, const std::string& label) {
            std::map<std::pair<int, int>, int> edge_count;
            
            for (const auto& face : mesh.indices) {
                // Add all three edges (with consistent ordering)
                auto add_edge = [&](int v0, int v1) {
                    if (v0 > v1) std::swap(v0, v1);
                    edge_count[{v0, v1}]++;
                };
                
                add_edge(face[0], face[1]);
                add_edge(face[1], face[2]);
                add_edge(face[2], face[0]);
            }
            
            int boundary_edges = 0;
            int non_manifold_edges = 0;
            
            for (const auto& [edge, count] : edge_count) {
                if (count == 1) {
                    boundary_edges++;
                } else if (count > 2) {
                    non_manifold_edges++;
                }
            }
            
            if (boundary_edges > 0 || non_manifold_edges > 0) {
                BOOST_LOG_TRIVIAL(warning) << label << " manifold check:";
                BOOST_LOG_TRIVIAL(warning) << "  - Boundary edges (open): " << boundary_edges;
                BOOST_LOG_TRIVIAL(warning) << "  - Non-manifold edges: " << non_manifold_edges;
            } else {
                BOOST_LOG_TRIVIAL(info) << label << " manifold check: PASS (watertight)";
            }
        }
        
        // Orient faces consistently using normal propagation
        void orient_faces_consistently(indexed_triangle_set& mesh) {
            if (mesh.indices.empty()) return;
            
            BOOST_LOG_TRIVIAL(info) << "Orienting faces consistently...";
            
            // Build adjacency graph
            std::map<std::pair<int, int>, std::vector<size_t>> edge_to_faces;
            
            for (size_t i = 0; i < mesh.indices.size(); ++i) {
                const auto& face = mesh.indices[i];
                
                auto add_edge = [&](int v0, int v1) {
                    auto key = (v0 < v1) ? std::make_pair(v0, v1) : std::make_pair(v1, v0);
                    edge_to_faces[key].push_back(i);
                };
                
                add_edge(face[0], face[1]);
                add_edge(face[1], face[2]);
                add_edge(face[2], face[0]);
            }
            
            // Propagate orientation from seed face
            std::vector<bool> visited(mesh.indices.size(), false);
            std::vector<bool> should_flip(mesh.indices.size(), false);
            std::queue<size_t> to_process;
            
            // Start from face with largest area (most likely correct)
            size_t seed_face = 0;
            double max_area = 0.0;
            
            for (size_t i = 0; i < mesh.indices.size(); ++i) {
                const auto& face = mesh.indices[i];
                const Vec3f& v0 = mesh.vertices[face[0]];
                const Vec3f& v1 = mesh.vertices[face[1]];
                const Vec3f& v2 = mesh.vertices[face[2]];
                
                double area = 0.5 * (v1 - v0).cross(v2 - v0).norm();
                if (area > max_area) {
                    max_area = area;
                    seed_face = i;
                }
            }
            
            to_process.push(seed_face);
            visited[seed_face] = true;
            
            int flipped_count = 0;
            
            while (!to_process.empty()) {
                size_t current_face = to_process.front();
                to_process.pop();
                
                const auto& face = mesh.indices[current_face];
                
                // Check all three edges
                for (int e = 0; e < 3; ++e) {
                    int v0 = face[e];
                    int v1 = face[(e + 1) % 3];
                    
                    auto key = (v0 < v1) ? std::make_pair(v0, v1) : std::make_pair(v1, v0);
                    auto it = edge_to_faces.find(key);
                    if (it == edge_to_faces.end()) continue;
                    
                    for (size_t neighbor_idx : it->second) {
                        if (neighbor_idx == current_face || visited[neighbor_idx]) continue;
                        
                        // Check if neighbor needs flipping
                        const auto& neighbor = mesh.indices[neighbor_idx];
                        
                        // Find shared edge in neighbor
                        bool same_direction = false;
                        for (int ne = 0; ne < 3; ++ne) {
                            int nv0 = neighbor[ne];
                            int nv1 = neighbor[(ne + 1) % 3];
                            
                            // If edge is in same direction in both faces, they have opposite normals
                            if ((v0 == nv0 && v1 == nv1) || (v0 == nv1 && v1 == nv0)) {
                                same_direction = (v0 == nv0 && v1 == nv1);
                                break;
                            }
                        }
                        
                        // If current face is flipped, neighbor should match; if not, opposite
                        if (should_flip[current_face]) {
                            should_flip[neighbor_idx] = !same_direction;
                        } else {
                            should_flip[neighbor_idx] = same_direction;
                        }
                        
                        if (should_flip[neighbor_idx]) {
                            flipped_count++;
                        }
                        
                        visited[neighbor_idx] = true;
                        to_process.push(neighbor_idx);
                    }
                }
            }
            
            // Apply flips
            for (size_t i = 0; i < mesh.indices.size(); ++i) {
                if (should_flip[i]) {
                    std::swap(mesh.indices[i][1], mesh.indices[i][2]);
                }
            }
            
            if (flipped_count > 0) {
                BOOST_LOG_TRIVIAL(info) << "Flipped " << flipped_count << " faces for consistent orientation";
            }
        }
        
        // Weld nearby vertices to eliminate near-duplicates after clipping
        void weld_nearby_vertices(indexed_triangle_set& mesh) {
            if (mesh.vertices.empty()) return;
            
            size_t original_count = mesh.vertices.size();
            
            // Simple O(n²) welding - could optimize with spatial hash for large meshes
            std::vector<int> vertex_map(mesh.vertices.size());
            std::iota(vertex_map.begin(), vertex_map.end(), 0);
            
            std::vector<Vec3f> welded_vertices;
            welded_vertices.reserve(mesh.vertices.size());
            std::vector<bool> processed(mesh.vertices.size(), false);
            
            for (size_t i = 0; i < mesh.vertices.size(); ++i) {
                if (processed[i]) continue;
                
                // Find all vertices within weld threshold
                std::vector<size_t> cluster;
                cluster.push_back(i);
                processed[i] = true;
                
                for (size_t j = i + 1; j < mesh.vertices.size(); ++j) {
                    if (processed[j]) continue;
                    
                    double dist_sq = (mesh.vertices[i] - mesh.vertices[j]).squaredNorm();
                    if (dist_sq < WELD_THRESHOLD * WELD_THRESHOLD) {
                        cluster.push_back(j);
                        processed[j] = true;
                    }
                }
                
                // Compute centroid of cluster
                Vec3f centroid = Vec3f::Zero();
                for (size_t idx : cluster) {
                    centroid += mesh.vertices[idx];
                }
                centroid /= float(cluster.size());
                
                // Map all vertices in cluster to the new welded vertex
                int new_index = welded_vertices.size();
                welded_vertices.push_back(centroid);
                
                for (size_t idx : cluster) {
                    vertex_map[idx] = new_index;
                }
            }
            
            // Remap face indices
            for (auto& face : mesh.indices) {
                face[0] = vertex_map[face[0]];
                face[1] = vertex_map[face[1]];
                face[2] = vertex_map[face[2]];
            }
            
            mesh.vertices = std::move(welded_vertices);
            
            size_t welded_count = original_count - mesh.vertices.size();
            if (welded_count > 0) {
                BOOST_LOG_TRIVIAL(info) << "Welded " << welded_count << " nearby vertices (tolerance=" 
                                         << WELD_THRESHOLD << ")";
            }
        }
        
        // Remove degenerate triangles (zero-length edges, zero-area)
        void remove_degenerate_faces(indexed_triangle_set& mesh) {
            if (mesh.indices.empty()) return;
            
            size_t original_count = mesh.indices.size();
            std::vector<Vec3i> clean_faces;
            clean_faces.reserve(mesh.indices.size());
            
            for (const auto& face : mesh.indices) {
                // Skip if any vertices are the same (degenerate)
                if (face[0] == face[1] || face[1] == face[2] || face[2] == face[0]) {
                    continue;
                }
                
                // Check edge lengths
                const Vec3f& v0 = mesh.vertices[face[0]];
                const Vec3f& v1 = mesh.vertices[face[1]];
                const Vec3f& v2 = mesh.vertices[face[2]];
                
                double e01_sq = (v1 - v0).squaredNorm();
                double e12_sq = (v2 - v1).squaredNorm();
                double e20_sq = (v0 - v2).squaredNorm();
                
                if (e01_sq < MIN_EDGE_LENGTH_SQ || e12_sq < MIN_EDGE_LENGTH_SQ || e20_sq < MIN_EDGE_LENGTH_SQ) {
                    continue;
                }
                
                // Check face area
                Vec3f e1 = v1 - v0;
                Vec3f e2 = v2 - v0;
                double area = 0.5 * e1.cross(e2).norm();
                
                if (area < MIN_FACE_AREA) {
                    continue;
                }
                
                clean_faces.push_back(face);
            }
            
            size_t removed_count = original_count - clean_faces.size();
            if (removed_count > 0) {
                BOOST_LOG_TRIVIAL(info) << "Removed " << removed_count << " degenerate faces";
                mesh.indices = std::move(clean_faces);
            }
        }
        
        // Cleanup pipeline: validate + weld + remove degenerates + orient
        void robust_mesh_cleanup(indexed_triangle_set& mesh) {
            if (mesh.vertices.empty() || mesh.indices.empty()) return;
            
            BOOST_LOG_TRIVIAL(info) << "Robust cleanup: input vertices=" << mesh.vertices.size() 
                                     << ", faces=" << mesh.indices.size();
            
            // Step 1: Validate input geometry
            validate_geometry(mesh, "Pre-cleanup");
            
            // Step 2: Weld nearby vertices
            weld_nearby_vertices(mesh);
            
            // Step 3: Remove degenerate faces
            remove_degenerate_faces(mesh);
            
            // Step 4: Orient faces consistently (FIX FOR SHREDDED APPEARANCE!)
            orient_faces_consistently(mesh);
            
            // Step 5: Final validation
            validate_geometry(mesh, "Post-cleanup");
            validate_manifoldness(mesh, "Post-cleanup");
            
            BOOST_LOG_TRIVIAL(info) << "Robust cleanup: output vertices=" << mesh.vertices.size() 
                                     << ", faces=" << mesh.indices.size();
        }
        
        // ========== WATERTIGHT MESH REPAIR FUNCTIONS ==========
        
        // Check if mesh is watertight (all edges have exactly 2 adjacent faces)
        bool is_mesh_watertight(const indexed_triangle_set& mesh) {
            std::map<std::pair<int, int>, int> edge_count;
            
            for (const auto& face : mesh.indices) {
                for (int i = 0; i < 3; ++i) {
                    int v1 = face[i];
                    int v2 = face[(i + 1) % 3];
                    auto edge = (v1 < v2) ? std::make_pair(v1, v2) : std::make_pair(v2, v1);
                    edge_count[edge]++;
                }
            }
            
            for (const auto& [edge, count] : edge_count) {
                if (count != 2) {
                    return false;  // Not watertight (hole or non-manifold)
                }
            }
            
            return true;
        }
        
        // Find boundary loops (chains of edges that appear only once)
        std::vector<std::vector<int>> find_boundary_loops(const indexed_triangle_set& mesh) {
            // Track directed edges
            std::map<std::pair<int, int>, int> edge_count;
            
            for (const auto& face : mesh.indices) {
                for (int i = 0; i < 3; ++i) {
                    int v1 = face[i];
                    int v2 = face[(i + 1) % 3];
                    edge_count[{v1, v2}]++;
                }
            }
            
            // Build adjacency map for boundary edges
            std::map<int, std::vector<int>> adjacency;
            for (const auto& [edge, count] : edge_count) {
                if (count == 1) {  // Boundary edge
                    adjacency[edge.first].push_back(edge.second);
                }
            }
            
            // Extract loops
            std::vector<std::vector<int>> loops;
            std::set<int> visited;
            
            for (const auto& [start_v, _] : adjacency) {
                if (visited.count(start_v)) continue;
                
                std::vector<int> loop;
                int current = start_v;
                
                // Follow chain until we return to start or reach dead end
                while (current != -1 && loop.size() < mesh.vertices.size()) {
                    if (visited.count(current) && current != start_v) {
                        break;  // Reached another loop
                    }
                    
                    loop.push_back(current);
                    visited.insert(current);
                    
                    // Find next vertex
                    auto it = adjacency.find(current);
                    if (it != adjacency.end() && !it->second.empty()) {
                        int next = it->second[0];
                        if (next == start_v && loop.size() >= 3) {
                            // Completed loop!
                            break;
                        }
                        current = next;
                    } else {
                        break;  // Dead end
                    }
                }
                
                if (loop.size() >= 3) {
                    loops.push_back(loop);
                }
            }
            
            return loops;
        }
        
        // Fill a hole with fan triangulation
        void fill_hole(indexed_triangle_set& mesh, const std::vector<int>& boundary_loop) {
            if (boundary_loop.size() < 3) return;
            
            // Use first vertex as fan center
            int v0 = boundary_loop[0];
            
            // Compute centroid to determine winding direction
            Vec3f centroid = Vec3f::Zero();
            for (int v : boundary_loop) {
                centroid += mesh.vertices[v];
            }
            centroid /= float(boundary_loop.size());
            
            // Create fan triangles
            for (size_t i = 1; i + 1 < boundary_loop.size(); ++i) {
                int v1 = boundary_loop[i];
                int v2 = boundary_loop[i + 1];
                
                // Compute normal
                Vec3f edge1 = mesh.vertices[v1] - mesh.vertices[v0];
                Vec3f edge2 = mesh.vertices[v2] - mesh.vertices[v0];
                Vec3f normal = edge1.cross(edge2);
                
                // Check orientation relative to centroid
                Vec3f to_centroid = centroid - mesh.vertices[v0];
                
                if (normal.dot(to_centroid) > 0) {
                    // Inward-facing, flip for outward normal
                    mesh.indices.emplace_back(v0, v2, v1);
                } else {
                    mesh.indices.emplace_back(v0, v1, v2);
                }
            }
        }
        
        // Comprehensive mesh repair
        void repair_mesh(indexed_triangle_set& mesh) {
            if (mesh.vertices.empty() || mesh.indices.empty()) return;
            
            BOOST_LOG_TRIVIAL(info) << "repair_mesh: Starting repair...";
            
            size_t orig_v = mesh.vertices.size();
            size_t orig_f = mesh.indices.size();
            
            // Phase 1: Clean degenerate geometry
            remove_degenerate_faces(mesh);
            
            // Phase 2: Weld vertices to create manifold connections
            weld_nearby_vertices(mesh);
            
            // Phase 3: Find and fill holes
            auto loops = find_boundary_loops(mesh);
            if (!loops.empty()) {
                BOOST_LOG_TRIVIAL(info) << "repair_mesh: Found " << loops.size() << " holes to fill";
                
                for (const auto& loop : loops) {
                    if (loop.size() >= 3 && loop.size() <= 100) {
                        fill_hole(mesh, loop);
                    } else {
                        BOOST_LOG_TRIVIAL(warning) << "repair_mesh: Skipping hole with " << loop.size() << " vertices";
                    }
                }
            }
            
            // Phase 4: Orient faces consistently
            orient_faces_consistently(mesh);
            
            // Phase 5: Final cleanup
            remove_degenerate_faces(mesh);
            
            // Phase 6: Validate result
            bool watertight = is_mesh_watertight(mesh);
            
            BOOST_LOG_TRIVIAL(info) << "repair_mesh: Complete. Vertices " << orig_v << "→" << mesh.vertices.size()
                                     << ", Faces " << orig_f << "→" << mesh.indices.size()
                                     << ", Watertight: " << (watertight ? "YES" : "NO");
        }
        
        // ========== ADVANCED VORONOI ALGORITHMS ==========
        
        // Lloyd's relaxation: move seeds to cell centroids for uniform cells
        std::vector<Vec3d> relax_seeds_lloyd(
            const std::vector<Vec3d>& initial_seeds,
            const indexed_triangle_set& mesh,
            const BoundingBoxf3& bounds,
            int iterations)
        {
            if (initial_seeds.empty() || iterations <= 0) return initial_seeds;
            
            std::vector<Vec3d> seeds = initial_seeds;
            AABBMesh aabb(mesh);
            
            BOOST_LOG_TRIVIAL(info) << "Lloyd relaxation: " << iterations << " iterations on " 
                                     << seeds.size() << " seeds";
            
            for (int iter = 0; iter < iterations; ++iter) {
                // Create Voro++ container
                double margin = 0.1;
                voro::container con(
                    bounds.min.x() - margin, bounds.max.x() + margin,
                    bounds.min.y() - margin, bounds.max.y() + margin,
                    bounds.min.z() - margin, bounds.max.z() + margin,
                    std::max(3, int(std::cbrt(seeds.size()))),
                    std::max(3, int(std::cbrt(seeds.size()))),
                    std::max(3, int(std::cbrt(seeds.size()))),
                    false, false, false, 8
                );
                
                // Add seed points
                for (size_t i = 0; i < seeds.size(); ++i) {
                    con.put(i, seeds[i].x(), seeds[i].y(), seeds[i].z());
                }
                
                // Compute cell centroids
                std::vector<Vec3d> new_seeds;
                new_seeds.reserve(seeds.size());
                
                voro::c_loop_all vl(con);
                voro::voronoicell cell;
                
                if (vl.start()) do {
                    if (con.compute_cell(cell, vl)) {
                        // Get cell vertices
                        std::vector<double> verts;
                        cell.vertices(vl.x(), vl.y(), vl.z(), verts);
                        
                        // Compute centroid
                        Vec3d centroid = Vec3d::Zero();
                        int vert_count = verts.size() / 3;
                        
                        for (int i = 0; i < vert_count; ++i) {
                            centroid.x() += verts[i * 3 + 0];
                            centroid.y() += verts[i * 3 + 1];
                            centroid.z() += verts[i * 3 + 2];
                        }
                        
                        if (vert_count > 0) {
                            centroid /= double(vert_count);
                            
                            // Keep centroid if inside mesh, otherwise keep original
                            if (is_point_inside_mesh(aabb, centroid)) {
                                new_seeds.push_back(centroid);
                            } else {
                                new_seeds.push_back(seeds[vl.pid()]);
                            }
                        } else {
                            new_seeds.push_back(seeds[vl.pid()]);
                        }
                    }
                } while (vl.inc());
                
                seeds = new_seeds;
                
                BOOST_LOG_TRIVIAL(info) << "Lloyd iteration " << (iter + 1) << "/" << iterations 
                                         << " complete";
            }
            
            return seeds;
        }
        
        // Apply anisotropic transformation to seeds
        std::vector<Vec3d> apply_anisotropic_transform(
            const std::vector<Vec3d>& seeds,
            const Vec3d& direction,
            float ratio)
        {
            if (ratio <= 0.0f || std::abs(ratio - 1.0f) < 1e-6) {
                return seeds;  // No transformation needed
            }
            
            Vec3d dir = direction.normalized();
            
            std::vector<Vec3d> transformed;
            transformed.reserve(seeds.size());
            
            BOOST_LOG_TRIVIAL(info) << "Applying anisotropic transform: direction=(" 
                                     << dir.x() << "," << dir.y() << "," << dir.z() 
                                     << "), ratio=" << ratio;
            
            for (const auto& seed : seeds) {
                // Decompose into parallel and perpendicular components
                double projection = seed.dot(dir);
                Vec3d parallel = dir * projection;
                Vec3d perpendicular = seed - parallel;
                
                // Stretch parallel component
                Vec3d stretched = parallel * ratio + perpendicular;
                transformed.push_back(stretched);
            }
            
            return transformed;
        }
        
        // Inverse anisotropic transformation for geometry
        void apply_inverse_anisotropic_transform(
            indexed_triangle_set& mesh,
            const Vec3d& direction,
            float ratio)
        {
            if (ratio <= 0.0f || std::abs(ratio - 1.0f) < 1e-6) {
                return;  // No transformation needed
            }
            
            Vec3d dir = direction.normalized();
            float inv_ratio = 1.0f / ratio;
            
            for (auto& vertex : mesh.vertices) {
                Vec3d v = vertex.cast<double>();
                
                // Decompose
                double projection = v.dot(dir);
                Vec3d parallel = dir * projection;
                Vec3d perpendicular = v - parallel;
                
                // Apply inverse stretch
                Vec3d unstretched = parallel * inv_ratio + perpendicular;
                vertex = unstretched.cast<float>();
            }
        }
        
        // Generate multi-scale hierarchical Voronoi
        std::unique_ptr<indexed_triangle_set> generate_multiscale(
            const indexed_triangle_set& input_mesh,
            const Config& config)
        {
            if (!config.multi_scale || config.scale_seed_counts.empty()) {
                return nullptr;
            }
            
            BOOST_LOG_TRIVIAL(info) << "Generating multi-scale Voronoi: " 
                                     << config.scale_seed_counts.size() << " levels";
            
            auto result = std::make_unique<indexed_triangle_set>();
            
            // Generate each scale level
            for (size_t level = 0; level < config.scale_seed_counts.size(); ++level) {
                Config level_config = config;
                level_config.num_seeds = config.scale_seed_counts[level];
                level_config.multi_scale = false;  // Prevent recursion
                
                // Adjust thickness if provided
                if (level < config.scale_thicknesses.size()) {
                    level_config.edge_thickness = config.scale_thicknesses[level];
                }
                
                BOOST_LOG_TRIVIAL(info) << "Multi-scale level " << (level + 1) 
                                         << ": seeds=" << level_config.num_seeds
                                         << ", thickness=" << level_config.edge_thickness;
                
                // Generate this scale level
                auto level_mesh = VoronoiMesh::generate(input_mesh, level_config);
                
                if (!level_mesh) {
                    BOOST_LOG_TRIVIAL(warning) << "Multi-scale level " << (level + 1) 
                                                << " failed, skipping";
                    continue;
                }
                
                // Merge with result
                size_t vertex_offset = result->vertices.size();
                
                result->vertices.insert(
                    result->vertices.end(),
                    level_mesh->vertices.begin(),
                    level_mesh->vertices.end()
                );
                
                for (const auto& face : level_mesh->indices) {
                    result->indices.emplace_back(
                        face[0] + vertex_offset,
                        face[1] + vertex_offset,
                        face[2] + vertex_offset
                    );
                }
            }
            
            if (result->vertices.empty()) {
                BOOST_LOG_TRIVIAL(error) << "Multi-scale generation produced no geometry";
                return nullptr;
            }
            
            // Weld interfaces between scales
            BOOST_LOG_TRIVIAL(info) << "Welding multi-scale interfaces...";
            weld_nearby_vertices(*result);
            
            // Final cleanup
            robust_mesh_cleanup(*result);
            
            BOOST_LOG_TRIVIAL(info) << "Multi-scale generation complete: " 
                                     << result->vertices.size() << " vertices, "
                                     << result->indices.size() << " faces";
            
            return result;
        }

        // Clip wireframe mesh to stay within mesh bounds
        void clip_wireframe_to_mesh(indexed_triangle_set& wireframe, const indexed_triangle_set& mesh) {
            if (wireframe.vertices.empty() || mesh.vertices.empty()) {
                return;
            }
            
            // Build AABB mesh for proper inside/outside testing
            AABBMesh aabb_mesh(mesh);
            
            // Mark vertices that are outside the mesh
            std::vector<bool> keep_vertex(wireframe.vertices.size(), true);
            int removed_count = 0;
            
            for (size_t i = 0; i < wireframe.vertices.size(); ++i) {
                Vec3d point = wireframe.vertices[i].cast<double>();
                
                // Use proper ray-based inside test
                Vec3d ray_dir(0, 0, 1);
                auto hit = aabb_mesh.query_ray_hit(point, ray_dir);
                
                if (!hit.is_inside()) {
                    keep_vertex[i] = false;
                    removed_count++;
                }
            }
            
            if (removed_count == 0) {
                BOOST_LOG_TRIVIAL(info) << "clip_wireframe_to_mesh: All vertices inside mesh";
                return;
            }
            
            BOOST_LOG_TRIVIAL(info) << "clip_wireframe_to_mesh: Removing " << removed_count << " vertices out of " << wireframe.vertices.size();
            
            // Build vertex remapping
            std::vector<int> vertex_map(wireframe.vertices.size(), -1);
            std::vector<Vec3f> new_vertices;
            new_vertices.reserve(wireframe.vertices.size() - removed_count);
            
            for (size_t i = 0; i < wireframe.vertices.size(); ++i) {
                if (keep_vertex[i]) {
                    vertex_map[i] = new_vertices.size();
                    new_vertices.push_back(wireframe.vertices[i]);
                }
            }
            
            // Rebuild faces, skipping those with removed vertices
            std::vector<Vec3i> new_indices;
            new_indices.reserve(wireframe.indices.size());
            
            for (const auto& face : wireframe.indices) {
                if (keep_vertex[face[0]] && keep_vertex[face[1]] && keep_vertex[face[2]]) {
                    new_indices.emplace_back(
                        vertex_map[face[0]],
                        vertex_map[face[1]],
                        vertex_map[face[2]]
                    );
                }
            }
            
            wireframe.vertices = std::move(new_vertices);
            wireframe.indices = std::move(new_indices);
        }

    } // namespace

    std::unique_ptr<indexed_triangle_set> VoronoiMesh::generate(
        const indexed_triangle_set& input_mesh,
        const Config& config)
    {
        BOOST_LOG_TRIVIAL(info) << "VoronoiMesh::generate() - START, input vertices: " << input_mesh.vertices.size() << ", faces: " << input_mesh.indices.size();

        // Check for cancellation
        if (config.progress_callback && !config.progress_callback(0))
            return nullptr;

        // Step 1: Generate seed points (10% progress)
        BOOST_LOG_TRIVIAL(info) << "VoronoiMesh::generate() - Generating seed points, type: " << (int)config.seed_type << ", num_seeds: " << config.num_seeds;
        std::vector<Vec3d> seed_points = generate_seed_points(input_mesh, config);
        BOOST_LOG_TRIVIAL(info) << "VoronoiMesh::generate() - Generated " << seed_points.size() << " seed points";
        if (seed_points.empty()) {
            BOOST_LOG_TRIVIAL(error) << "VoronoiMesh::generate() - No seed points generated!";
            return nullptr;
        }

        if (config.progress_callback && !config.progress_callback(10))
            return nullptr;

        // Step 2: Compute bounding box
        BoundingBoxf3 bbox;
        for (const auto& v : input_mesh.vertices) {
            bbox.merge(v.cast<double>());
        }
        
        // CRITICAL: Filter seeds to ensure minimum separation (prevents needle-thin cells)
        size_t original_seed_count = seed_points.size();
        seed_points = filter_seed_points(seed_points, bbox);
        
        if (seed_points.empty()) {
            BOOST_LOG_TRIVIAL(error) << "VoronoiMesh::generate() - All seeds filtered out (too close together)!";
            return nullptr;
        }
        
        if (seed_points.size() < original_seed_count) {
            BOOST_LOG_TRIVIAL(info) << "VoronoiMesh::generate() - Filtered to " << seed_points.size() 
                                     << " seeds (removed " << (original_seed_count - seed_points.size()) 
                                     << " that were too close for clean Voronoi structure)";
        }
        
        // ADVANCED: Apply Lloyd's relaxation for uniform cells
        if (config.relax_seeds && config.relaxation_iterations > 0) {
            BOOST_LOG_TRIVIAL(info) << "VoronoiMesh::generate() - Applying Lloyd's relaxation";
            seed_points = relax_seeds_lloyd(seed_points, input_mesh, bbox, config.relaxation_iterations);
        }
        
        // ADVANCED: Apply anisotropic transformation
        if (config.anisotropic && config.anisotropy_ratio > 0.0f) {
            BOOST_LOG_TRIVIAL(info) << "VoronoiMesh::generate() - Applying anisotropic transformation";
            seed_points = apply_anisotropic_transform(seed_points, config.anisotropy_direction, config.anisotropy_ratio);
        }
        
        // ADVANCED: Multi-scale generation (early exit if enabled)
        if (config.multi_scale) {
            BOOST_LOG_TRIVIAL(info) << "VoronoiMesh::generate() - Using multi-scale mode";
            auto result = generate_multiscale(input_mesh, config);
            
            if (result && config.auto_repair && !is_mesh_watertight(*result)) {
                repair_mesh(*result);
            }
            
            return result;
        }

        // Don't expand bbox for wireframe (expansion was for solid cells)
        // Wireframe should stay within the original mesh bounds
        BOOST_LOG_TRIVIAL(info) << "VoronoiMesh::generate() - Bounding box computed: min(" << bbox.min.x() << "," << bbox.min.y() << "," << bbox.min.z() << "), max(" << bbox.max.x() << "," << bbox.max.y() << "," << bbox.max.z() << ")";

        if (config.progress_callback && !config.progress_callback(20))
            return nullptr;

        // Step 3: Create Voronoi cells (polyhedral cell-based approach)
        auto result = std::make_unique<indexed_triangle_set>();

        BOOST_LOG_TRIVIAL(info) << "VoronoiMesh::generate() - Creating polyhedral Voronoi cells";
        result = tessellate_voronoi_cells(seed_points, bbox, config, &input_mesh);
        BOOST_LOG_TRIVIAL(info) << "VoronoiMesh::generate() - Voronoi cells created, vertices: "
                                 << result->vertices.size() << ", faces: " << result->indices.size();

        if (!result || result->vertices.empty()) {
            BOOST_LOG_TRIVIAL(error) << "VoronoiMesh::generate() - Edge structure is empty or null!";
            return nullptr;
        }

        if (config.progress_callback && !config.progress_callback(90))
            return nullptr;

        // Step 4: For wireframe, don't use boolean clipping (wireframe is not manifold)
        // The wireframe is already bounded by the seed points which are inside the mesh
        BOOST_LOG_TRIVIAL(info) << "VoronoiMesh::generate() - Result: vertices=" << result->vertices.size() << ", faces=" << result->indices.size();
        BOOST_LOG_TRIVIAL(info) << "VoronoiMesh::generate() - Skipping boolean clipping for wireframe (not manifold)";

        // Step 5: Finalize progress
        if (config.progress_callback && !config.progress_callback(100))
            return nullptr;
        
        // ADVANCED: Apply inverse anisotropic transform to geometry
        if (config.anisotropic && config.anisotropy_ratio > 0.0f) {
            BOOST_LOG_TRIVIAL(info) << "VoronoiMesh::generate() - Applying inverse anisotropic transform";
            apply_inverse_anisotropic_transform(*result, config.anisotropy_direction, config.anisotropy_ratio);
        }

        return result;
    }

    std::vector<Vec3d> VoronoiMesh::generate_seed_points(
        const indexed_triangle_set& mesh,
        const Config& config)
    {
        switch (config.seed_type) {
        case SeedType::Vertices:
            return generate_vertex_seeds(mesh, config.num_seeds);
        case SeedType::Grid:
            return generate_grid_seeds(mesh, config.num_seeds);
        case SeedType::Random:
            return generate_random_seeds(mesh, config.num_seeds, config.random_seed);
        case SeedType::Surface:
            return generate_surface_seeds(mesh, config.num_seeds, config.random_seed);
        case SeedType::Adaptive:
            return generate_adaptive_seeds(mesh, config.num_seeds, config.adaptive_factor, config.random_seed);
        default:
            return {};
        }
    }

    std::vector<Vec3d> VoronoiMesh::generate_vertex_seeds(
        const indexed_triangle_set& mesh,
        int max_seeds)
    {
        std::vector<Vec3d> seeds;

        const size_t vertex_count = mesh.vertices.size();
        if (vertex_count == 0)
            return seeds;

        if (max_seeds <= 0) {
            seeds.reserve(vertex_count);
            for (const Vec3f& vertex : mesh.vertices)
                seeds.push_back(vertex.cast<double>());
            return seeds;
        }

        const size_t target_count = std::min(vertex_count, static_cast<size_t>(max_seeds));
        const size_t step = std::max<size_t>(1, (vertex_count + target_count - 1) / target_count);

        seeds.reserve(target_count);
        for (size_t i = 0; i < vertex_count && seeds.size() < target_count; i += step)
            seeds.push_back(mesh.vertices[i].cast<double>());

        return seeds;
    }

    std::vector<Vec3d> VoronoiMesh::generate_grid_seeds(
        const indexed_triangle_set& mesh,
        int num_seeds)
    {
        std::vector<Vec3d> seeds;

        // Compute bounding box
        BoundingBoxf3 bbox;
        for (const auto& v : mesh.vertices) {
            bbox.merge(v.cast<double>());
        }

        // Calculate grid dimensions - generate more candidates than needed for filtering
        int dim = std::max(2, int(std::cbrt(double(num_seeds * 3)) + 0.5));

        Vec3d size = bbox.size();
        Vec3d step = size / double(dim - 1);

        // Build AABB mesh for point-in-mesh testing
        AABBMesh aabb_mesh(mesh);

        std::vector<Vec3d> candidates;
        candidates.reserve(dim * dim * dim);

        for (int x = 0; x < dim; ++x) {
            for (int y = 0; y < dim; ++y) {
                for (int z = 0; z < dim; ++z) {
                    Vec3d point = bbox.min + Vec3d(x * step.x(), y * step.y(), z * step.z());

                    // Test if point is inside mesh using AABB mesh
                    if (is_point_inside_mesh(aabb_mesh, point)) {
                        candidates.push_back(point);
                    }
                }
            }
        }

        // If we got enough seeds, use them directly
        if (candidates.size() <= size_t(num_seeds)) {
            BOOST_LOG_TRIVIAL(info) << "generate_grid_seeds() - Generated " << candidates.size()
                                     << " grid seeds (all inside mesh)";
            return candidates;
        }

        // Otherwise, uniformly sample from candidates
        seeds.reserve(num_seeds);
        int step_size = std::max(1, int(candidates.size()) / num_seeds);
        for (size_t i = 0; i < candidates.size() && seeds.size() < size_t(num_seeds); i += step_size) {
            seeds.push_back(candidates[i]);
        }

        BOOST_LOG_TRIVIAL(info) << "generate_grid_seeds() - Generated " << seeds.size()
                                 << " grid seeds from " << candidates.size() << " candidates (all inside mesh)";

        return seeds;
    }

    // PHASE 4: Surface-based seeding - place seeds on mesh surface
    std::vector<Vec3d> VoronoiMesh::generate_surface_seeds(
        const indexed_triangle_set& mesh,
        int num_seeds,
        int random_seed)
    {
        std::vector<Vec3d> seeds;
        
        if (mesh.indices.empty() || num_seeds <= 0) {
            return seeds;
        }
        
        BOOST_LOG_TRIVIAL(info) << "generate_surface_seeds() - Generating " << num_seeds << " surface seeds";
        
        std::mt19937 gen(random_seed);
        
        // Calculate triangle areas for weighted sampling
        std::vector<float> triangle_areas;
        triangle_areas.reserve(mesh.indices.size());
        float total_area = 0.0f;
        
        for (const auto& face : mesh.indices) {
            const Vec3f& v0 = mesh.vertices[face[0]];
            const Vec3f& v1 = mesh.vertices[face[1]];
            const Vec3f& v2 = mesh.vertices[face[2]];
            
            Vec3f edge1 = v1 - v0;
            Vec3f edge2 = v2 - v0;
            float area = 0.5f * edge1.cross(edge2).norm();
            
            triangle_areas.push_back(area);
            total_area += area;
        }
        
        // Create cumulative distribution for area-weighted sampling
        std::vector<float> cumulative_areas;
        cumulative_areas.reserve(triangle_areas.size());
        float cumsum = 0.0f;
        for (float area : triangle_areas) {
            cumsum += area;
            cumulative_areas.push_back(cumsum);
        }
        
        std::uniform_real_distribution<float> dist(0.0f, total_area);
        std::uniform_real_distribution<float> bary_dist(0.0f, 1.0f);
        
        seeds.reserve(num_seeds);
        
        for (int i = 0; i < num_seeds; ++i) {
            // Select triangle weighted by area
            float random_area = dist(gen);
            auto it = std::lower_bound(cumulative_areas.begin(), cumulative_areas.end(), random_area);
            size_t triangle_idx = std::distance(cumulative_areas.begin(), it);
            
            if (triangle_idx >= mesh.indices.size()) {
                triangle_idx = mesh.indices.size() - 1;
            }
            
            // Generate random barycentric coordinates
            float u = bary_dist(gen);
            float v = bary_dist(gen);
            if (u + v > 1.0f) {
                u = 1.0f - u;
                v = 1.0f - v;
            }
            float w = 1.0f - u - v;
            
            // Compute point on triangle
            const auto& face = mesh.indices[triangle_idx];
            const Vec3f& v0 = mesh.vertices[face[0]];
            const Vec3f& v1 = mesh.vertices[face[1]];
            const Vec3f& v2 = mesh.vertices[face[2]];
            
            Vec3d point = (v0 * u + v1 * v + v2 * w).cast<double>();
            seeds.push_back(point);
        }
        
        BOOST_LOG_TRIVIAL(info) << "generate_surface_seeds() - Generated " << seeds.size() << " surface seeds";
        return seeds;
    }

    // PHASE 4: Adaptive seeding with Poisson disk sampling approximation
    std::vector<Vec3d> VoronoiMesh::generate_adaptive_seeds(
        const indexed_triangle_set& mesh,
        int num_seeds,
        float adaptive_factor,
        int random_seed)
    {
        std::vector<Vec3d> seeds;
        
        if (mesh.vertices.empty() || num_seeds <= 0) {
            return seeds;
        }
        
        BOOST_LOG_TRIVIAL(info) << "generate_adaptive_seeds() - Generating " << num_seeds 
                                 << " seeds with adaptive factor " << adaptive_factor;
        
        // Compute bounding box
        BoundingBoxf3 bbox;
        for (const auto& v : mesh.vertices) {
            bbox.merge(v.cast<double>());
        }
        
        // Build AABB mesh for point-in-mesh testing
        AABBMesh aabb_mesh(mesh);
        
        // Calculate mesh features for adaptive density
        // Use vertex curvature as a proxy for feature density
        std::vector<float> vertex_importance(mesh.vertices.size(), 1.0f);
        
        // Compute vertex normals and curvature estimation
        std::vector<Vec3f> vertex_normals(mesh.vertices.size(), Vec3f::Zero());
        std::vector<int> vertex_valence(mesh.vertices.size(), 0);
        
        for (const auto& face : mesh.indices) {
            const Vec3f& v0 = mesh.vertices[face[0]];
            const Vec3f& v1 = mesh.vertices[face[1]];
            const Vec3f& v2 = mesh.vertices[face[2]];
            
            Vec3f normal = (v1 - v0).cross(v2 - v0).normalized();
            
            for (int i = 0; i < 3; ++i) {
                vertex_normals[face[i]] += normal;
                vertex_valence[face[i]]++;
            }
        }
        
        // Normalize and compute curvature estimate
        for (size_t i = 0; i < vertex_normals.size(); ++i) {
            if (vertex_valence[i] > 0) {
                vertex_normals[i] /= float(vertex_valence[i]);
                vertex_normals[i].normalize();
                
                // High valence or large normal variation indicates features
                float feature_score = std::abs(vertex_valence[i] - 6.0f) / 6.0f; // 6 is typical
                vertex_importance[i] = 1.0f + feature_score * adaptive_factor;
            }
        }
        
        // Poisson disk sampling with adaptive density
        std::mt19937 gen(random_seed);
        std::uniform_real_distribution<double> dist_x(bbox.min.x(), bbox.max.x());
        std::uniform_real_distribution<double> dist_y(bbox.min.y(), bbox.max.y());
        std::uniform_real_distribution<double> dist_z(bbox.min.z(), bbox.max.z());
        
        // Calculate base minimum distance
        float volume = bbox.size().x() * bbox.size().y() * bbox.size().z();
        float base_min_distance = std::cbrt(volume / num_seeds) * 0.5f;
        
        BOOST_LOG_TRIVIAL(info) << "generate_adaptive_seeds() - Base min distance: " << base_min_distance;
        
        seeds.reserve(num_seeds);
        int attempts = 0;
        int max_attempts = num_seeds * 100;
        
        // Initial seed
        for (int initial_tries = 0; initial_tries < 100 && seeds.empty(); ++initial_tries) {
            Vec3d point(dist_x(gen), dist_y(gen), dist_z(gen));
            if (is_point_inside_mesh(aabb_mesh, point)) {
                seeds.push_back(point);
                break;
            }
        }
        
        if (seeds.empty()) {
            BOOST_LOG_TRIVIAL(warning) << "generate_adaptive_seeds() - Could not find initial seed point";
            return seeds;
        }
        
        // Poisson disk sampling with adaptive distance
        while (seeds.size() < size_t(num_seeds) && attempts < max_attempts) {
            Vec3d candidate(dist_x(gen), dist_y(gen), dist_z(gen));
            attempts++;
            
            // Check if inside mesh
            if (!is_point_inside_mesh(aabb_mesh, candidate)) {
                continue;
            }
            
            // Find nearest vertex to determine local feature density
            float nearest_dist = std::numeric_limits<float>::max();
            size_t nearest_vertex = 0;
            for (size_t i = 0; i < mesh.vertices.size(); ++i) {
                float d = (mesh.vertices[i].cast<double>() - candidate).norm();
                if (d < nearest_dist) {
                    nearest_dist = d;
                    nearest_vertex = i;
                }
            }
            
            // Adaptive minimum distance based on local importance
            float local_importance = vertex_importance[nearest_vertex];
            float adaptive_min_distance = base_min_distance / std::sqrt(local_importance);
            
            // Check distance to existing seeds
            bool too_close = false;
            for (const auto& existing : seeds) {
                if ((existing - candidate).norm() < adaptive_min_distance) {
                    too_close = true;
                    break;
                }
            }
            
            if (!too_close) {
                seeds.push_back(candidate);
            }
        }
        
        // Fill remaining with relaxed constraints if needed
        if (seeds.size() < size_t(num_seeds)) {
            BOOST_LOG_TRIVIAL(info) << "generate_adaptive_seeds() - Filling remaining " 
                                     << (num_seeds - seeds.size()) << " seeds with relaxed constraints";
            
            attempts = 0;
            while (seeds.size() < size_t(num_seeds) && attempts < max_attempts) {
                Vec3d point(dist_x(gen), dist_y(gen), dist_z(gen));
                if (is_point_inside_mesh(aabb_mesh, point)) {
                    seeds.push_back(point);
                }
                attempts++;
            }
        }
        
        BOOST_LOG_TRIVIAL(info) << "generate_adaptive_seeds() - Generated " << seeds.size() 
                                 << " adaptive seeds (target: " << num_seeds << ")";
        
        return seeds;
    }

    std::vector<Vec3d> VoronoiMesh::generate_random_seeds(
        const indexed_triangle_set& mesh,
        int num_seeds,
        int random_seed)
    {
        std::vector<Vec3d> seeds;

        // Compute bounding box
        BoundingBoxf3 bbox;
        for (const auto& v : mesh.vertices) {
            bbox.merge(v.cast<double>());
        }

        // Use provided seed for reproducibility
        std::mt19937 gen(random_seed);

        std::uniform_real_distribution<double> dist_x(bbox.min.x(), bbox.max.x());
        std::uniform_real_distribution<double> dist_y(bbox.min.y(), bbox.max.y());
        std::uniform_real_distribution<double> dist_z(bbox.min.z(), bbox.max.z());

        // Build AABB mesh for point-in-mesh testing
        AABBMesh aabb_mesh(mesh);

        seeds.reserve(num_seeds);
        int attempts = 0;
        int max_attempts = num_seeds * 100; // Prevent infinite loop for very sparse meshes

        while (seeds.size() < size_t(num_seeds) && attempts < max_attempts) {
            Vec3d point(dist_x(gen), dist_y(gen), dist_z(gen));

            // Test if point is inside mesh using AABB mesh
            if (is_point_inside_mesh(aabb_mesh, point)) {
                seeds.push_back(point);
            }

            ++attempts;
        }

        if (seeds.size() < size_t(num_seeds)) {
            BOOST_LOG_TRIVIAL(warning) << "generate_random_seeds() - Only generated " << seeds.size()
                                        << " seeds (target: " << num_seeds << ") after " << attempts << " attempts";
        } else {
            BOOST_LOG_TRIVIAL(info) << "generate_random_seeds() - Generated " << seeds.size()
                                     << " random seeds (all inside mesh) in " << attempts << " attempts";
        }

        return seeds;
    }

    std::unique_ptr<indexed_triangle_set> VoronoiMesh::tessellate_voronoi_cells(
        const std::vector<Vec3d>& seed_points,
        const BoundingBoxf3& bounds,
        const Config& config,
        const indexed_triangle_set* clip_mesh)
    {
        BOOST_LOG_TRIVIAL(info) << "tessellate_voronoi_cells() - START with " << seed_points.size() << " seeds, hollow_cells=" << config.hollow_cells;

        if (seed_points.empty()) {
            return std::make_unique<indexed_triangle_set>();
        }

        if (config.progress_callback && !config.progress_callback(25))
            return nullptr;

        // Both modes now use Voro++ - it's faster and provides all data we need!
        if (config.hollow_cells) {
            // Hollow/Wireframe mode: Extract edges from Voro++ cells
            BOOST_LOG_TRIVIAL(info) << "tessellate_voronoi_cells() - Using Voro++ for wireframe edge extraction";
            return create_wireframe_from_voropp(seed_points, bounds, config, clip_mesh);
        } else {
            // Solid cells mode: Generate polyhedral cells using Voro++
            BOOST_LOG_TRIVIAL(info) << "tessellate_voronoi_cells() - Using Voro++ for solid polyhedral cells";
            return tessellate_voronoi_with_voropp(seed_points, bounds, config, clip_mesh);
        }
    }

    // Voro++ implementation - FAST!
    std::unique_ptr<indexed_triangle_set> VoronoiMesh::tessellate_voronoi_with_voropp(
        const std::vector<Vec3d>& seed_points,
        const BoundingBoxf3& bounds,
        const Config& config,
        const indexed_triangle_set* clip_mesh)
    {
        using namespace voro;

        auto result = std::make_unique<indexed_triangle_set>();

        // Create Voro++ container with bounding box
        // Parameters: (xmin, xmax, ymin, ymax, zmin, zmax, nx, ny, nz, periodic_x, periodic_y, periodic_z, allocate, max_particles)
        // Grid size affects performance: ~(particles^(1/3))
        int grid_size = std::max(3, int(std::cbrt(seed_points.size()) + 0.5));

        container con(
            bounds.min.x(), bounds.max.x(),
            bounds.min.y(), bounds.max.y(),
            bounds.min.z(), bounds.max.z(),
            grid_size, grid_size, grid_size,
            false, false, false,  // Non-periodic
            8  // Initial memory allocation per grid cell
        );

        // Insert seed points
        BOOST_LOG_TRIVIAL(info) << "Voro++: Inserting " << seed_points.size() << " particles into container";
        for (size_t i = 0; i < seed_points.size(); ++i) {
            const Vec3d& p = seed_points[i];
            con.put(i, p.x(), p.y(), p.z());
        }

        if (config.progress_callback && !config.progress_callback(40))
            return nullptr;

        // Extract cells
        BOOST_LOG_TRIVIAL(info) << "Voro++: Extracting Voronoi cells";
        c_loop_all vl(con);
        voronoicell_neighbor cell;

        int processed = 0;
        int total = seed_points.size();

        if (vl.start()) do {
            processed++;
            if (processed % 10 == 0) {
                int progress = 40 + (processed * 40) / total;
                if (config.progress_callback && !config.progress_callback(progress))
                    return nullptr;
            }

            if (con.compute_cell(cell, vl)) {
                // Create a separate mesh for this cell
                indexed_triangle_set cell_mesh;

                // Get cell vertices
                std::vector<double> verts;
                cell.vertices(vl.x(), vl.y(), vl.z(), verts);

                // Get face information
                std::vector<int> face_vertices;
                std::vector<int> face_orders;
                cell.face_vertices(face_vertices);
                cell.face_orders(face_orders);

                // Add vertices (verts contains x1,y1,z1,x2,y2,z2,...)
                for (size_t i = 0; i + 2 < verts.size(); i += 3) {
                    cell_mesh.vertices.emplace_back(
                        float(verts[i]),
                        float(verts[i + 1]),
                        float(verts[i + 2])
                    );
                }

                // Triangulate each face
                int face_start = 0;
                for (int face_order : face_orders) {
                    if (face_order >= 3) {
                        // Fan triangulation from first vertex
                        int v0 = face_vertices[face_start];
                        for (int j = 1; j + 1 < face_order; ++j) {
                            int v1 = face_vertices[face_start + j];
                            int v2 = face_vertices[face_start + j + 1];

                            // REVERSED WINDING: Voro++ faces point inward, so reverse them
                            cell_mesh.indices.emplace_back(v0, v2, v1);
                        }
                    }
                    face_start += face_order;
                }

                // Clip cell to input mesh if provided
                if (clip_mesh && !clip_mesh->vertices.empty()) {
                    try {
                        BOOST_LOG_TRIVIAL(debug) << "Voro++: Clipping cell " << processed 
                                                  << " (vertices=" << cell_mesh.vertices.size() 
                                                  << ", faces=" << cell_mesh.indices.size() << ")";
                        
                        // Perform boolean intersection: cell ∩ original_mesh
                        MeshBoolean::cgal::intersect(cell_mesh, *clip_mesh);

                        // Skip cells that were completely outside the mesh
                        if (cell_mesh.vertices.empty() || cell_mesh.indices.size() == 0) {
                            BOOST_LOG_TRIVIAL(debug) << "Voro++: Cell " << processed << " completely outside mesh, skipping";
                            continue;
                        }
                        
                        BOOST_LOG_TRIVIAL(debug) << "Voro++: Cell " << processed << " clipped successfully "
                                                  << "(vertices=" << cell_mesh.vertices.size() 
                                                  << ", faces=" << cell_mesh.indices.size() << ")";
                    }
                    catch (const std::exception& e) {
                        BOOST_LOG_TRIVIAL(warning) << "Voro++: Failed to clip cell " << processed
                                                   << " - " << e.what() << " - using unclipped cell";
                    }
                    catch (...) {
                        BOOST_LOG_TRIVIAL(warning) << "Voro++: Failed to clip cell " << processed
                                                   << " - unknown error - using unclipped cell";
                    }
                } else {
                    BOOST_LOG_TRIVIAL(debug) << "Voro++: Cell " << processed << " - no clipping (clip_mesh=" 
                                              << (clip_mesh ? "provided" : "NULL") << ")";
                }

                // Apply hollowing for solid cells with wall thickness
                // NOTE: If hollow_cells is true, we're in wireframe mode, not here
                if (!config.hollow_cells && config.wall_thickness > 0.0f) {
                    // Create shell with inward offset (for solid cells that need hollowing)
                    create_hollow_cells(cell_mesh, config.wall_thickness);
                }
                // If hollow_cells is true, this path shouldn't be reached (we use wireframe instead)

                // Merge this cell into the result
                size_t vertex_offset = result->vertices.size();

                result->vertices.insert(result->vertices.end(),
                                       cell_mesh.vertices.begin(),
                                       cell_mesh.vertices.end());

                for (const auto& face : cell_mesh.indices) {
                    result->indices.emplace_back(
                        face(0) + vertex_offset,
                        face(1) + vertex_offset,
                        face(2) + vertex_offset
                    );
                }
            }
        } while (vl.inc());

        BOOST_LOG_TRIVIAL(info) << "Voro++: Generated " << result->vertices.size() << " vertices, "
                                 << result->indices.size() << " faces";

        // Apply robust cleanup to fix boundary degeneracies
        if (!result->vertices.empty() && !result->indices.empty()) {
            BOOST_LOG_TRIVIAL(info) << "Voro++: Applying robust cleanup to fix near-boundary degeneracies";
            robust_mesh_cleanup(*result);
        }

        // Apply styling if in solid mode
        if (!config.hollow_cells && config.cell_style != VoronoiMesh::CellStyle::Pure) {
            BOOST_LOG_TRIVIAL(info) << "Applying cell styling: " << static_cast<int>(config.cell_style);
            apply_cell_styling(*result, config);
        }

        return result;
    }

    // Extract wireframe edges directly from Voro++ cells (MUCH faster than CGAL Delaunay!)
    std::unique_ptr<indexed_triangle_set> VoronoiMesh::create_wireframe_from_voropp(
        const std::vector<Vec3d>& seed_points,
        const BoundingBoxf3& bounds,
        const Config& config,
        const indexed_triangle_set* clip_mesh)
    {
        using namespace voro;

        BOOST_LOG_TRIVIAL(info) << "create_wireframe_from_voropp() - START, seeds: " << seed_points.size();

        auto result = std::make_unique<indexed_triangle_set>();

        if (seed_points.empty() || config.edge_thickness <= 0.0f) {
            BOOST_LOG_TRIVIAL(warning) << "create_wireframe_from_voropp() - Invalid parameters";
            return result;
        }

        // Create Voro++ container
        int grid_size = std::max(3, int(std::cbrt(seed_points.size()) + 0.5));
        container con(
            bounds.min.x(), bounds.max.x(),
            bounds.min.y(), bounds.max.y(),
            bounds.min.z(), bounds.max.z(),
            grid_size, grid_size, grid_size,
            false, false, false,
            8
        );

        // Insert seed points
        BOOST_LOG_TRIVIAL(info) << "Voro++ Wireframe: Inserting " << seed_points.size() << " particles";
        for (size_t i = 0; i < seed_points.size(); ++i) {
            const Vec3d& p = seed_points[i];
            con.put(i, p.x(), p.y(), p.z());
        }

        if (config.progress_callback && !config.progress_callback(35))
            return nullptr;

        // Extract Voronoi edges from cells
        // Key insight: Each cell face edge is a Voronoi edge
        // Define comparison function for Vec3d with epsilon tolerance
        struct Vec3dCompare {
            bool operator()(const Vec3d& a, const Vec3d& b) const {
                constexpr double eps = 1e-9;
                if (std::abs(a.x() - b.x()) > eps) return a.x() < b.x();
                if (std::abs(a.y() - b.y()) > eps) return a.y() < b.y();
                if (std::abs(a.z() - b.z()) > eps) return a.z() < b.z();
                return false;  // Equal within epsilon
            }
        };

        // Define comparison function for pairs of Vec3d
        struct Vec3dPairCompare {
            Vec3dCompare vec3d_comp;
            bool operator()(const std::pair<Vec3d, Vec3d>& a, const std::pair<Vec3d, Vec3d>& b) const {
                if (vec3d_comp(a.first, b.first)) return true;
                if (vec3d_comp(b.first, a.first)) return false;
                return vec3d_comp(a.second, b.second);
            }
        };

        std::set<std::pair<Vec3d, Vec3d>, Vec3dPairCompare> unique_edges((Vec3dPairCompare()));
        std::map<Vec3d, std::vector<Vec3d>, Vec3dCompare> vertex_connections((Vec3dCompare()));

        c_loop_all vl(con);
        voronoicell_neighbor cell;

        int processed = 0;
        int total = seed_points.size();

        BOOST_LOG_TRIVIAL(info) << "Voro++ Wireframe: Extracting edges from cells";

        if (vl.start()) do {
            processed++;
            if (processed % 10 == 0) {
                int progress = 35 + (processed * 35) / total;
                if (config.progress_callback && !config.progress_callback(progress))
                    return nullptr;
            }

            if (con.compute_cell(cell, vl)) {
                // Get vertices of this cell
                std::vector<double> verts;
                cell.vertices(vl.x(), vl.y(), vl.z(), verts);

                std::vector<Vec3d> cell_vertices;
                for (size_t i = 0; i + 2 < verts.size(); i += 3) {
                    cell_vertices.emplace_back(verts[i], verts[i + 1], verts[i + 2]);
                }

                // Get face information
                std::vector<int> face_vertices_indices;
                std::vector<int> face_orders;
                cell.face_vertices(face_vertices_indices);
                cell.face_orders(face_orders);

                // Extract edges from face perimeters
                int vertex_idx = 0;
                for (int face_order : face_orders) {
                    for (int v = 0; v < face_order; ++v) {
                        int v_next = (v + 1) % face_order;
                        int vi1 = face_vertices_indices[vertex_idx + v];
                        int vi2 = face_vertices_indices[vertex_idx + v_next];

                        if (vi1 < (int)cell_vertices.size() && vi2 < (int)cell_vertices.size()) {
                            Vec3d p1 = cell_vertices[vi1];
                            Vec3d p2 = cell_vertices[vi2];

                            // Store edge with consistent ordering using the comparator
                            Vec3dCompare vec_comp;
                            auto edge = vec_comp(p1, p2) ? std::make_pair(p1, p2) : std::make_pair(p2, p1);
                            unique_edges.insert(edge);

                            // Track connections
                            vertex_connections[p1].push_back(p2);
                            vertex_connections[p2].push_back(p1);
                        }
                    }
                    vertex_idx += face_order;
                }
            }
        } while (vl.inc());

        BOOST_LOG_TRIVIAL(info) << "Voro++ Wireframe: Found " << unique_edges.size() << " edges, " 
                                 << vertex_connections.size() << " vertices";

        if (config.progress_callback && !config.progress_callback(70))
            return nullptr;

        // Generate strut geometry
        const float radius = config.edge_thickness * 0.5f;

        auto get_profile_point = [&](int i, float r) -> Vec3d {
            float angle = (2.0f * M_PI * i) / config.edge_segments;
            switch (config.edge_shape) {
                case VoronoiMesh::EdgeShape::Square: {
                    float t = fmod(angle / (M_PI / 2.0f), 4.0f);
                    int side = int(t);
                    float blend = t - side;
                    float x, y;
                    switch (side) {
                        case 0: x = 1.0f; y = blend * 2.0f - 1.0f; break;
                        case 1: x = 1.0f - blend * 2.0f; y = 1.0f; break;
                        case 2: x = -1.0f; y = 1.0f - blend * 2.0f; break;
                        default: x = blend * 2.0f - 1.0f; y = -1.0f; break;
                    }
                    return Vec3d(x * r, y * r, 0);
                }
                case VoronoiMesh::EdgeShape::Hexagon:
                case VoronoiMesh::EdgeShape::Octagon: {
                    int sides = (config.edge_shape == VoronoiMesh::EdgeShape::Hexagon) ? 6 : 8;
                    float snap_angle = std::round(angle / (2.0f * M_PI / sides)) * (2.0f * M_PI / sides);
                    return Vec3d(r * std::cos(snap_angle), r * std::sin(snap_angle), 0);
                }
                case VoronoiMesh::EdgeShape::Star: {
                    int point = int(angle / (2.0f * M_PI / 10.0f));
                    float point_angle = point * (2.0f * M_PI / 10.0f);
                    float next_angle = (point + 1) * (2.0f * M_PI / 10.0f);
                    float blend = (angle - point_angle) / (next_angle - point_angle);
                    float r1 = (point % 2 == 0) ? r : r * 0.4f;
                    float r2 = (point % 2 == 0) ? r * 0.4f : r;
                    float current_r = r1 * (1.0f - blend) + r2 * blend;
                    return Vec3d(current_r * std::cos(angle), current_r * std::sin(angle), 0);
                }
                default:
                    return Vec3d(r * std::cos(angle), r * std::sin(angle), 0);
            }
        };

        // Create struts for each edge
        for (const auto& edge : unique_edges) {
            Vec3d p1 = edge.first;
            Vec3d p2 = edge.second;
            Vec3d dir = (p2 - p1).normalized();
            float length = (p2 - p1).norm();
            if (length < 1e-6) continue;

            std::vector<Vec3d> curve_points;
            int num_segments = config.edge_subdivisions + 1;

            if (config.edge_subdivisions == 0 || config.edge_curvature <= 0.0f) {
                curve_points.push_back(p1);
                curve_points.push_back(p2);
            } else {
                Vec3d midpoint = (p1 + p2) * 0.5;
                Vec3d curve_perp = (std::abs(dir.z()) < 0.9) ? dir.cross(Vec3d(0, 0, 1)).normalized() 
                                                              : dir.cross(Vec3d(1, 0, 0)).normalized();
                float offset_amount = length * config.edge_curvature * 0.5f;
                Vec3d control_point = midpoint + curve_perp * offset_amount;

                for (int s = 0; s <= num_segments; ++s) {
                    float t = float(s) / float(num_segments);
                    float b0 = (1.0f - t) * (1.0f - t);
                    float b1 = 2.0f * (1.0f - t) * t;
                    float b2 = t * t;
                    curve_points.push_back(p1 * b0 + control_point * b1 + p2 * b2);
                }
            }

            // Create strut geometry along the curve
            // We'll create one continuous tube, reusing vertices between segments
            std::vector<size_t> segment_base_indices;
            segment_base_indices.reserve(curve_points.size());
            
            for (size_t seg = 0; seg < curve_points.size(); ++seg) {
                Vec3d point = curve_points[seg];
                
                // Calculate perpendicular axes for this point
                Vec3d dir;
                if (seg == 0) {
                    dir = (curve_points[seg + 1] - curve_points[seg]).normalized();
                } else if (seg == curve_points.size() - 1) {
                    dir = (curve_points[seg] - curve_points[seg - 1]).normalized();
                } else {
                    dir = (curve_points[seg + 1] - curve_points[seg - 1]).normalized();
                }
                
                Vec3d perp1 = (std::abs(dir.z()) < 0.9) ? dir.cross(Vec3d(0, 0, 1)).normalized() 
                                                         : dir.cross(Vec3d(1, 0, 0)).normalized();
                Vec3d perp2 = dir.cross(perp1).normalized();
                
                // Store the base index for this ring of vertices
                segment_base_indices.push_back(result->vertices.size());
                
                // Create ring of vertices around this point
                for (int i = 0; i <= config.edge_segments; ++i) {
                    Vec3d profile = get_profile_point(i, radius);
                    Vec3d offset = perp1 * profile.x() + perp2 * profile.y();
                    result->vertices.emplace_back((point + offset).cast<float>());
                }
            }
            
            // Connect consecutive rings to form the tube
            for (size_t seg = 0; seg < curve_points.size() - 1; ++seg) {
                size_t ring1_start = segment_base_indices[seg];
                size_t ring2_start = segment_base_indices[seg + 1];
                
                for (int i = 0; i < config.edge_segments; ++i) {
                    int i0 = ring1_start + i;
                    int i1 = ring1_start + i + 1;
                    int i2 = ring2_start + i;
                    int i3 = ring2_start + i + 1;
                    
                    result->indices.emplace_back(i0, i2, i1);
                    result->indices.emplace_back(i1, i2, i3);
                }
            }
        }

        // Create junctions
        for (const auto& [vertex, connections] : vertex_connections) {
            if (connections.size() < 2) continue;
            const int sphere_rings = 4, sphere_segments = 8;
            size_t base_idx = result->vertices.size();
            result->vertices.emplace_back(vertex.cast<float>());

            for (int ring = 1; ring <= sphere_rings; ++ring) {
                float phi = M_PI * float(ring) / float(sphere_rings + 1);
                float ring_radius = radius * std::sin(phi);
                float ring_z = radius * std::cos(phi);
                for (int seg = 0; seg < sphere_segments; ++seg) {
                    float theta = 2.0f * M_PI * float(seg) / float(sphere_segments);
                    Vec3d offset(ring_radius * std::cos(theta), ring_radius * std::sin(theta), ring_z);
                    result->vertices.emplace_back((vertex + offset).cast<float>());
                }
            }

            for (int seg = 0; seg < sphere_segments; ++seg) {
                int next = (seg + 1) % sphere_segments;
                result->indices.emplace_back(base_idx, base_idx + 1 + seg, base_idx + 1 + next);
            }
            for (int ring = 0; ring < sphere_rings - 1; ++ring) {
                int ring_start = base_idx + 1 + ring * sphere_segments;
                int next_ring = ring_start + sphere_segments;
                for (int seg = 0; seg < sphere_segments; ++seg) {
                    int next = (seg + 1) % sphere_segments;
                    result->indices.emplace_back(ring_start + seg, next_ring + seg, ring_start + next);
                    result->indices.emplace_back(ring_start + next, next_ring + seg, next_ring + next);
                }
            }
            int last_ring = base_idx + 1 + (sphere_rings - 1) * sphere_segments;
            for (int seg = 0; seg < sphere_segments; ++seg) {
                int next = (seg + 1) % sphere_segments;
                result->indices.emplace_back(base_idx, last_ring + next, last_ring + seg);
            }
        }

        BOOST_LOG_TRIVIAL(info) << "Voro++ Wireframe: Complete! vertices=" << result->vertices.size() 
                                 << ", faces=" << result->indices.size();

        // Apply robust cleanup to wireframe geometry
        if (!result->vertices.empty() && !result->indices.empty()) {
            BOOST_LOG_TRIVIAL(info) << "Voro++ Wireframe: Applying robust cleanup";
            robust_mesh_cleanup(*result);
        }

        // Clip wireframe to mesh bounds if requested
        if (clip_mesh && !clip_mesh->vertices.empty() && config.clip_to_mesh) {
            BOOST_LOG_TRIVIAL(info) << "Voro++ Wireframe: Clipping to mesh bounds...";
            BOOST_LOG_TRIVIAL(info) << "Voro++ Wireframe: Before clipping - vertices=" << result->vertices.size() 
                                     << ", faces=" << result->indices.size();
            clip_wireframe_to_mesh(*result, *clip_mesh);
            BOOST_LOG_TRIVIAL(info) << "Voro++ Wireframe: After clipping - vertices=" << result->vertices.size() 
                                     << ", faces=" << result->indices.size();
                                     
            // Cleanup again after clipping (may create new degeneracies)
            BOOST_LOG_TRIVIAL(info) << "Voro++ Wireframe: Post-clip cleanup";
            robust_mesh_cleanup(*result);
        } else {
            BOOST_LOG_TRIVIAL(info) << "Voro++ Wireframe: Skipping clipping (clip_to_mesh=" 
                                     << config.clip_to_mesh << ")";
        }

        if (config.progress_callback)
            config.progress_callback(90);

        return result;
    }

    void VoronoiMesh::clip_to_mesh_boundary(
        indexed_triangle_set& voronoi_mesh,
        const indexed_triangle_set& original_mesh)
    {
        if (voronoi_mesh.vertices.empty() || original_mesh.vertices.empty()) {
            BOOST_LOG_TRIVIAL(warning) << "clip_to_mesh_boundary() - Empty mesh provided";
            return;
        }

        BOOST_LOG_TRIVIAL(info) << "clip_to_mesh_boundary() - Input: voronoi vertices=" << voronoi_mesh.vertices.size()
                                 << ", faces=" << voronoi_mesh.indices.size()
                                 << ", original vertices=" << original_mesh.vertices.size()
                                 << ", faces=" << original_mesh.indices.size();

        try {
            TriangleMesh voronoi_tm(voronoi_mesh);
            TriangleMesh original_tm(original_mesh);

            BOOST_LOG_TRIVIAL(info) << "clip_to_mesh_boundary() - Attempting boolean intersection...";
            MeshBoolean::cgal::intersect(voronoi_tm, original_tm);

            BOOST_LOG_TRIVIAL(info) << "clip_to_mesh_boundary() - Intersection succeeded, result vertices=" << voronoi_tm.its.vertices.size();
            voronoi_mesh = voronoi_tm.its;
        }
        catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "clip_to_mesh_boundary() - Exception during boolean intersection: " << e.what();
            // Keep original Voronoi mesh if boolean operation fails
        }
        catch (...) {
            BOOST_LOG_TRIVIAL(error) << "clip_to_mesh_boundary() - Unknown exception during boolean intersection";
            // Keep original Voronoi mesh if boolean operation fails
        }
    }

    void VoronoiMesh::create_hollow_cells(
        indexed_triangle_set& mesh,
        float wall_thickness)
    {
        if (mesh.vertices.empty() || mesh.indices.empty() || wall_thickness <= 0.0f)
            return;

        // For very small cells, don't hollow them to avoid self-intersections
        BoundingBoxf3 cell_bbox;
        for (const auto& v : mesh.vertices) {
            cell_bbox.merge(v.cast<double>());
        }

        float min_dimension = std::min({ cell_bbox.size().x(),
                                        cell_bbox.size().y(),
                                        cell_bbox.size().z() });

        // Skip hollowing for cells too small relative to wall thickness
        if (min_dimension < wall_thickness * 3.0f) {
            return;
        }

        indexed_triangle_set original = mesh;

        const size_t vertex_count = original.vertices.size();
        const size_t face_count = original.indices.size();

        // Calculate face normals and areas
        std::vector<Vec3f> face_normals(face_count);
        std::vector<float> face_areas(face_count);

        for (size_t i = 0; i < face_count; ++i) {
            const auto& face = original.indices[i];
            const Vec3f& v0 = original.vertices[face[0]];
            const Vec3f& v1 = original.vertices[face[1]];
            const Vec3f& v2 = original.vertices[face[2]];

            Vec3f edge1 = v1 - v0;
            Vec3f edge2 = v2 - v0;
            Vec3f normal = edge1.cross(edge2);
            float area = 0.5f * normal.norm();

            if (area > 1e-6f) {
                face_normals[i] = normal.normalized();
                face_areas[i] = area;
            }
            else {
                face_normals[i] = Vec3f(0, 0, 1);
                face_areas[i] = 0.0f;
            }
        }

        // Compute smooth vertex normals using angle-weighted averaging
        std::vector<Vec3f> vertex_normals(vertex_count, Vec3f::Zero());

        for (size_t fi = 0; fi < face_count; ++fi) {
            if (face_areas[fi] <= 0.0f)
                continue;

            const auto& face = original.indices[fi];
            const Vec3f& v0 = original.vertices[face[0]];
            const Vec3f& v1 = original.vertices[face[1]];
            const Vec3f& v2 = original.vertices[face[2]];

            // Calculate angles at each vertex
            Vec3f e0 = (v1 - v0).normalized();
            Vec3f e1 = (v2 - v1).normalized();
            Vec3f e2 = (v0 - v2).normalized();

            float angle0 = std::acos(std::max(-1.0f, std::min(1.0f, e0.dot(-e2))));
            float angle1 = std::acos(std::max(-1.0f, std::min(1.0f, e1.dot(-e0))));
            float angle2 = std::acos(std::max(-1.0f, std::min(1.0f, e2.dot(-e1))));

            vertex_normals[face[0]] += face_normals[fi] * angle0;
            vertex_normals[face[1]] += face_normals[fi] * angle1;
            vertex_normals[face[2]] += face_normals[fi] * angle2;
        }

        // Normalize vertex normals and ensure they point outward
        Vec3f center = Vec3f::Zero();
        for (const auto& v : original.vertices) {
            center += v;
        }
        center /= float(vertex_count);

        for (size_t i = 0; i < vertex_count; ++i) {
            float len = vertex_normals[i].norm();
            if (len > 1e-6f) {
                vertex_normals[i] /= len;

                // Ensure normal points outward from center
                Vec3f to_vertex = original.vertices[i] - center;
                if (vertex_normals[i].dot(to_vertex) < 0) {
                    vertex_normals[i] = -vertex_normals[i];
                }
            }
            else {
                // Fallback: use direction from center
                Vec3f to_vertex = original.vertices[i] - center;
                float dist = to_vertex.norm();
                if (dist > 1e-6f) {
                    vertex_normals[i] = to_vertex / dist;
                }
                else {
                    vertex_normals[i] = Vec3f(0, 0, 1);
                }
            }
        }

        // Create inner vertices by offsetting along normals
        std::vector<Vec3f> inner_vertices(vertex_count);
        for (size_t i = 0; i < vertex_count; ++i) {
            inner_vertices[i] = original.vertices[i] - vertex_normals[i] * wall_thickness;
        }

        // Build the hollow mesh
        mesh.vertices.clear();
        mesh.indices.clear();
        mesh.vertices.reserve(vertex_count * 2);
        mesh.indices.reserve(face_count * 2 + vertex_count * 6); // outer + inner + sides

        // Add all vertices (outer first, then inner)
        mesh.vertices.insert(mesh.vertices.end(), original.vertices.begin(), original.vertices.end());
        mesh.vertices.insert(mesh.vertices.end(), inner_vertices.begin(), inner_vertices.end());

        // Add outer faces (original orientation)
        for (const auto& face : original.indices) {
            mesh.indices.push_back(face);
        }

        // Add inner faces (reversed orientation)
        for (const auto& face : original.indices) {
            mesh.indices.emplace_back(
                face[0] + static_cast<int>(vertex_count),
                face[2] + static_cast<int>(vertex_count),  // Reversed winding
                face[1] + static_cast<int>(vertex_count)
            );
        }

        // Build edge map for side wall generation
        std::map<std::pair<int, int>, std::vector<int>> edge_to_faces;
        for (size_t fi = 0; fi < original.indices.size(); ++fi) {
            const auto& face = original.indices[fi];
            for (int j = 0; j < 3; ++j) {
                int v0 = face[j];
                int v1 = face[(j + 1) % 3];
                auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
                edge_to_faces[edge].push_back(fi);
            }
        }

        // Add side walls only for boundary edges
        for (const auto& [edge, faces] : edge_to_faces) {
            if (faces.size() == 1) {  // Boundary edge
                int v0 = edge.first;
                int v1 = edge.second;
                int v0_inner = v0 + vertex_count;
                int v1_inner = v1 + vertex_count;

                // Determine correct winding based on face normal
                const auto& face = original.indices[faces[0]];
                int face_v0_idx = -1, face_v1_idx = -1;
                for (int i = 0; i < 3; ++i) {
                    if (face[i] == v0) face_v0_idx = i;
                    if (face[i] == v1) face_v1_idx = i;
                }

                bool forward = (face_v1_idx == (face_v0_idx + 1) % 3);

                if (forward) {
                    mesh.indices.emplace_back(v0, v1, static_cast<int>(v0_inner));
                    mesh.indices.emplace_back(v1, static_cast<int>(v1_inner), static_cast<int>(v0_inner));
                }
                else {
                    mesh.indices.emplace_back(v1, v0, static_cast<int>(v1_inner));
                    mesh.indices.emplace_back(v0, static_cast<int>(v0_inner), static_cast<int>(v1_inner));
                }
            }
        }
    }

    // Helper function for creating edge structures (currently unused but kept for future wireframe improvements)
    void create_edge_structure_helper(
        indexed_triangle_set& result,
        const std::vector<Vec3d>& seed_points,
        const BoundingBoxf3& bounds,
        float edge_thickness,
        VoronoiMesh::EdgeShape edge_shape,
        int edge_segments,
        const VoronoiMesh::Config& config)
    {
        BOOST_LOG_TRIVIAL(info) << "create_edge_structure() - START, seed_points: " << seed_points.size() << ", edge_thickness: " << edge_thickness;

        if (seed_points.size() < 4 || edge_thickness <= 0.0f) {
            BOOST_LOG_TRIVIAL(warning) << "create_edge_structure() - Early return: seed_points.size()=" << seed_points.size() << ", edge_thickness=" << edge_thickness;
            return;
        }

        // Build Delaunay triangulation to get Voronoi edges
        Delaunay dt;
        try {
            for (size_t i = 0; i < seed_points.size(); ++i) {
                const auto& p = seed_points[i];
                if (std::isnan(p.x()) || std::isnan(p.y()) || std::isnan(p.z()) ||
                    std::isinf(p.x()) || std::isinf(p.y()) || std::isinf(p.z())) {
                    continue;
                }
                auto vh = dt.insert(Point_3(p.x(), p.y(), p.z()));
                if (vh != Delaunay::Vertex_handle()) {
                    vh->info() = static_cast<int>(i);
                }
            }

            BOOST_LOG_TRIVIAL(info) << "create_edge_structure() - Delaunay built, vertices: " << dt.number_of_vertices() << ", valid: " << dt.is_valid();

            if (dt.number_of_vertices() < 4 || !dt.is_valid()) {
                BOOST_LOG_TRIVIAL(warning) << "create_edge_structure() - Delaunay validation failed!";
                return;
            }
        }
        catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "create_edge_structure() - Exception during Delaunay: " << e.what();
            return;
        }
        catch (...) {
            BOOST_LOG_TRIVIAL(error) << "create_edge_structure() - Unknown exception during Delaunay";
            return;
        }

        // Extract Voronoi edges - CORRECT DUAL: Delaunay FACETS -> Voronoi EDGES
        // Each Delaunay facet (triangular face) is shared by two tetrahedra
        // The Voronoi edge connects the circumcenters of these two tetrahedra
        
        // Define comparators for CGAL Point_3
        struct Point3Compare {
            bool operator()(const Point_3& a, const Point_3& b) const {
                constexpr double eps = 1e-9;
                if (std::abs(CGAL::to_double(a.x()) - CGAL::to_double(b.x())) > eps) 
                    return CGAL::to_double(a.x()) < CGAL::to_double(b.x());
                if (std::abs(CGAL::to_double(a.y()) - CGAL::to_double(b.y())) > eps) 
                    return CGAL::to_double(a.y()) < CGAL::to_double(b.y());
                if (std::abs(CGAL::to_double(a.z()) - CGAL::to_double(b.z())) > eps) 
                    return CGAL::to_double(a.z()) < CGAL::to_double(b.z());
                return false;
            }
        };
        
        struct Point3PairCompare {
            Point3Compare pt_comp;
            bool operator()(const std::pair<Point_3, Point_3>& a, const std::pair<Point_3, Point_3>& b) const {
                if (pt_comp(a.first, b.first)) return true;
                if (pt_comp(b.first, a.first)) return false;
                return pt_comp(a.second, b.second);
            }
        };
        
        std::set<std::pair<Point_3, Point_3>, Point3PairCompare> unique_edges((Point3PairCompare()));
        std::map<Point_3, std::vector<Point_3>, Point3Compare> vertex_connections((Point3Compare()));  // Track which edges meet at each vertex

        // Iterate over all finite FACETS in the Delaunay triangulation
        int total_facets = 0;
        int finite_both = 0;
        int passed_bounds = 0;

        for (auto fit = dt.finite_facets_begin(); fit != dt.finite_facets_end(); ++fit) {
            total_facets++;

            // A facet is a pair (Cell_handle c, int i) where i is the index of the opposite vertex
            Delaunay::Cell_handle cell1 = fit->first;
            int facet_index = fit->second;

            // Get the neighboring cell across this facet
            Delaunay::Cell_handle cell2 = cell1->neighbor(facet_index);

            // Both cells must be finite (not infinite)
            if (!dt.is_infinite(cell1) && !dt.is_infinite(cell2)) {
                finite_both++;

                // Get circumcenters (Voronoi vertices)
                Point_3 cc1 = cell1->circumcenter();
                Point_3 cc2 = cell2->circumcenter();

                // Relaxed bounds check (allow circumcenters slightly outside)
                double margin = (bounds.max - bounds.min).norm() * 0.5;
                if (cc1.x() >= bounds.min.x() - margin && cc1.x() <= bounds.max.x() + margin &&
                    cc1.y() >= bounds.min.y() - margin && cc1.y() <= bounds.max.y() + margin &&
                    cc1.z() >= bounds.min.z() - margin && cc1.z() <= bounds.max.z() + margin &&
                    cc2.x() >= bounds.min.x() - margin && cc2.x() <= bounds.max.x() + margin &&
                    cc2.y() >= bounds.min.y() - margin && cc2.y() <= bounds.max.y() + margin &&
                    cc2.z() >= bounds.min.z() - margin && cc2.z() <= bounds.max.z() + margin) {

                    passed_bounds++;

                    // Store edge (ensure consistent ordering to avoid duplicates)
                    // Use the Point3Compare to order the edge consistently
                    Point3Compare pt_comp;
                    auto edge = pt_comp(cc1, cc2) ? std::make_pair(cc1, cc2) : std::make_pair(cc2, cc1);
                    unique_edges.insert(edge);

                    // Track vertex connections for proper junctions
                    vertex_connections[cc1].push_back(cc2);
                    vertex_connections[cc2].push_back(cc1);
                }
            }
        }

        BOOST_LOG_TRIVIAL(info) << "create_edge_structure() - Facet stats: total=" << total_facets
                                 << ", finite_both=" << finite_both << ", passed_bounds=" << passed_bounds;

        BOOST_LOG_TRIVIAL(info) << "create_edge_structure() - Found " << unique_edges.size() << " edges and " << vertex_connections.size() << " vertices";

        if (config.progress_callback && !config.progress_callback(50))
            return;

        // Generate cross-section profile based on shape type
        auto get_profile_point = [&](int i, float radius) -> Vec3d {
            float angle = (2.0f * M_PI * i) / edge_segments;
            float r = radius;

            switch (edge_shape) {
                case VoronoiMesh::EdgeShape::Square: {
                    // Square cross-section
                    float t = fmod(angle / (M_PI / 2.0f), 4.0f);
                    int side = int(t);
                    float blend = t - side;
                    float x, y;
                    switch (side) {
                        case 0: x = 1.0f; y = blend * 2.0f - 1.0f; break;
                        case 1: x = 1.0f - blend * 2.0f; y = 1.0f; break;
                        case 2: x = -1.0f; y = 1.0f - blend * 2.0f; break;
                        default: x = blend * 2.0f - 1.0f; y = -1.0f; break;
                    }
                    return Vec3d(x * r, y * r, 0);
                }

                case VoronoiMesh::EdgeShape::Hexagon:
                case VoronoiMesh::EdgeShape::Octagon: {
                    // Regular polygon
                    int sides = (edge_shape == VoronoiMesh::EdgeShape::Hexagon) ? 6 : 8;
                    float snap_angle = std::round(angle / (2.0f * M_PI / sides)) * (2.0f * M_PI / sides);
                    return Vec3d(r * std::cos(snap_angle), r * std::sin(snap_angle), 0);
                }

                case VoronoiMesh::EdgeShape::Star: {
                    // 5-pointed star
                    int point = int(angle / (2.0f * M_PI / 10.0f));
                    float point_angle = point * (2.0f * M_PI / 10.0f);
                    float next_angle = (point + 1) * (2.0f * M_PI / 10.0f);
                    float blend = (angle - point_angle) / (next_angle - point_angle);

                    float r1 = (point % 2 == 0) ? r : r * 0.4f;  // Outer/inner radius
                    float r2 = (point % 2 == 0) ? r * 0.4f : r;

                    float current_r = r1 * (1.0f - blend) + r2 * blend;
                    return Vec3d(current_r * std::cos(angle), current_r * std::sin(angle), 0);
                }

                default: // Cylinder
                    return Vec3d(r * std::cos(angle), r * std::sin(angle), 0);
            }
        };

        const float radius = edge_thickness * 0.5f;

        for (const auto& edge : unique_edges) {
            Vec3d p1(edge.first.x(), edge.first.y(), edge.first.z());
            Vec3d p2(edge.second.x(), edge.second.y(), edge.second.z());

            Vec3d dir = (p2 - p1).normalized();
            float length = (p2 - p1).norm();

            if (length < 1e-6)
                continue;

            // Generate curve points along the edge
            std::vector<Vec3d> curve_points;
            int num_curve_segments = config.edge_subdivisions + 1;

            if (config.edge_subdivisions == 0 || config.edge_curvature <= 0.0f) {
                // Straight line
                curve_points.push_back(p1);
                curve_points.push_back(p2);
            } else {
                // Create curved path using perpendicular offset
                Vec3d midpoint = (p1 + p2) * 0.5;

                // Find perpendicular direction for curve offset
                Vec3d curve_perp;
                if (std::abs(dir.z()) < 0.9) {
                    curve_perp = dir.cross(Vec3d(0, 0, 1)).normalized();
                } else {
                    curve_perp = dir.cross(Vec3d(1, 0, 0)).normalized();
                }

                // Offset amount based on curvature and edge length
                float offset_amount = length * config.edge_curvature * 0.5f;

                // Generate curve points using quadratic Bezier
                for (int s = 0; s <= num_curve_segments; ++s) {
                    float t = float(s) / float(num_curve_segments);

                    // Quadratic Bezier: B(t) = (1-t)²P0 + 2(1-t)t*P1 + t²P2
                    Vec3d control_point = midpoint + curve_perp * offset_amount;

                    float b0 = (1.0f - t) * (1.0f - t);
                    float b1 = 2.0f * (1.0f - t) * t;
                    float b2 = t * t;

                    Vec3d point = p1 * b0 + control_point * b1 + p2 * b2;
                    curve_points.push_back(point);
                }
            }

            // Create strut geometry along the curve
            for (size_t seg = 0; seg < curve_points.size() - 1; ++seg) {
                Vec3d seg_p1 = curve_points[seg];
                Vec3d seg_p2 = curve_points[seg + 1];
                Vec3d seg_dir = (seg_p2 - seg_p1).normalized();

                // Find perpendicular vectors for this segment
                Vec3d perp1;
                if (std::abs(seg_dir.z()) < 0.9) {
                    perp1 = seg_dir.cross(Vec3d(0, 0, 1)).normalized();
                } else {
                    perp1 = seg_dir.cross(Vec3d(1, 0, 0)).normalized();
                }
                Vec3d perp2 = seg_dir.cross(perp1).normalized();

                size_t base_idx = result.vertices.size();

                // Create strut vertices using the profile
                for (int i = 0; i <= edge_segments; ++i) {
                    Vec3d profile = get_profile_point(i, radius);
                    Vec3d offset = perp1 * profile.x() + perp2 * profile.y();

                    result.vertices.emplace_back((seg_p1 + offset).cast<float>());
                    result.vertices.emplace_back((seg_p2 + offset).cast<float>());
                }

                // Create strut faces for this segment
                for (int i = 0; i < edge_segments; ++i) {
                    int i0 = base_idx + i * 2;
                    int i1 = base_idx + i * 2 + 1;
                    int i2 = base_idx + (i + 1) * 2;
                    int i3 = base_idx + (i + 1) * 2 + 1;

                    result.indices.emplace_back(i0, i2, i1);
                    result.indices.emplace_back(i1, i2, i3);
                }

                // Caps are added later at proper junction vertices
            }
        }

        // Create spherical junctions at vertices where multiple edges meet
        BOOST_LOG_TRIVIAL(info) << "create_edge_structure() - Creating vertex junctions";
        
        // Define comparator for this map too
        struct Point3Compare_Local {
            bool operator()(const Point_3& a, const Point_3& b) const {
                constexpr double eps = 1e-9;
                if (std::abs(CGAL::to_double(a.x()) - CGAL::to_double(b.x())) > eps) 
                    return CGAL::to_double(a.x()) < CGAL::to_double(b.x());
                if (std::abs(CGAL::to_double(a.y()) - CGAL::to_double(b.y())) > eps) 
                    return CGAL::to_double(a.y()) < CGAL::to_double(b.y());
                if (std::abs(CGAL::to_double(a.z()) - CGAL::to_double(b.z())) > eps) 
                    return CGAL::to_double(a.z()) < CGAL::to_double(b.z());
                return false;
            }
        };
        
        std::map<Point_3, size_t, Point3Compare_Local> vertex_junction_map((Point3Compare_Local()));  // Maps vertex to its junction center index

        for (const auto& [vertex, connections] : vertex_connections) {
            if (connections.size() < 2)
                continue;  // Skip isolated vertices

            Vec3d center(vertex.x(), vertex.y(), vertex.z());

            // Create a small sphere at the junction with same radius as edges
            const int sphere_rings = 4;
            const int sphere_segments = 8;
            size_t base_vertex_idx = result.vertices.size();

            // Add center point
            result.vertices.emplace_back(center.cast<float>());
            vertex_junction_map[vertex] = base_vertex_idx;

            // Create sphere vertices
            for (int ring = 1; ring <= sphere_rings; ++ring) {
                float phi = M_PI * float(ring) / float(sphere_rings + 1);
                float ring_radius = radius * std::sin(phi);
                float ring_z = radius * std::cos(phi);

                for (int seg = 0; seg < sphere_segments; ++seg) {
                    float theta = 2.0f * M_PI * float(seg) / float(sphere_segments);
                    Vec3d offset(ring_radius * std::cos(theta),
                                ring_radius * std::sin(theta),
                                ring_z);
                    result.vertices.emplace_back((center + offset).cast<float>());
                }
            }

            // Create sphere faces
            // Top cap (center to first ring)
            for (int seg = 0; seg < sphere_segments; ++seg) {
                int next_seg = (seg + 1) % sphere_segments;
                result.indices.emplace_back(
                    base_vertex_idx,
                    base_vertex_idx + 1 + seg,
                    base_vertex_idx + 1 + next_seg
                );
            }

            // Middle rings
            for (int ring = 0; ring < sphere_rings - 1; ++ring) {
                int ring_start = base_vertex_idx + 1 + ring * sphere_segments;
                int next_ring_start = ring_start + sphere_segments;

                for (int seg = 0; seg < sphere_segments; ++seg) {
                    int next_seg = (seg + 1) % sphere_segments;

                    result.indices.emplace_back(
                        ring_start + seg,
                        next_ring_start + seg,
                        ring_start + next_seg
                    );
                    result.indices.emplace_back(
                        ring_start + next_seg,
                        next_ring_start + seg,
                        next_ring_start + next_seg
                    );
                }
            }

            // Bottom cap (last ring back to center)
            int last_ring_start = base_vertex_idx + 1 + (sphere_rings - 1) * sphere_segments;
            for (int seg = 0; seg < sphere_segments; ++seg) {
                int next_seg = (seg + 1) % sphere_segments;
                result.indices.emplace_back(
                    base_vertex_idx,
                    last_ring_start + next_seg,
                    last_ring_start + seg
                );
            }
        }

        BOOST_LOG_TRIVIAL(info) << "create_edge_structure() - Created " << vertex_junction_map.size() << " junction spheres";

        if (config.progress_callback)
            config.progress_callback(80);
    }

    // PHASE 5: Generate with statistics using Voro++
    std::unique_ptr<indexed_triangle_set> VoronoiMesh::generate_with_stats(
        const indexed_triangle_set& input_mesh,
        const Config& config,
        Statistics& stats)
    {
        using namespace voro;
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Generate seed points first
        std::vector<Vec3d> seed_points = generate_seed_points(input_mesh, config);
        
        if (seed_points.empty()) {
            BOOST_LOG_TRIVIAL(error) << "generate_with_stats() - No seed points generated";
            return nullptr;
        }
        
        // Compute bounding box
        BoundingBoxf3 bbox;
        for (const auto& v : input_mesh.vertices) {
            bbox.merge(v.cast<double>());
        }
        
        // Create Voro++ container to compute cell statistics
        int grid_size = std::max(3, int(std::cbrt(seed_points.size()) + 0.5));
        container con(
            bbox.min.x(), bbox.max.x(),
            bbox.min.y(), bbox.max.y(),
            bbox.min.z(), bbox.max.z(),
            grid_size, grid_size, grid_size,
            false, false, false,
            8
        );
        
        // Insert seeds into Voro++ container
        for (size_t i = 0; i < seed_points.size(); ++i) {
            const Vec3d& p = seed_points[i];
            con.put(i, p.x(), p.y(), p.z());
        }
        
        // Compute Voronoi cell statistics
        c_loop_all vl(con);
        voronoicell_neighbor cell;
        
        stats.num_cells = 0;
        stats.min_cell_volume = std::numeric_limits<float>::max();
        stats.max_cell_volume = 0.0f;
        stats.total_volume = 0.0f;
        
        if (vl.start()) do {
            if (con.compute_cell(cell, vl)) {
                stats.num_cells++;
                
                // Get cell volume
                double volume = cell.volume();
                stats.min_cell_volume = std::min(stats.min_cell_volume, float(volume));
                stats.max_cell_volume = std::max(stats.max_cell_volume, float(volume));
                stats.total_volume += float(volume);
            }
        } while (vl.inc());
        
        if (stats.num_cells > 0) {
            stats.avg_cell_volume = stats.total_volume / stats.num_cells;
        }
        
        BOOST_LOG_TRIVIAL(info) << "generate_with_stats() - Voro++ statistics:";
        BOOST_LOG_TRIVIAL(info) << "  Cells: " << stats.num_cells;
        BOOST_LOG_TRIVIAL(info) << "  Cell volume: " << stats.min_cell_volume << " - " 
                                 << stats.max_cell_volume << " (avg: " << stats.avg_cell_volume << ")";
        BOOST_LOG_TRIVIAL(info) << "  Total volume: " << stats.total_volume;
        
        // Now generate the actual mesh
        auto result = generate(input_mesh, config);
        
        auto end_time = std::chrono::high_resolution_clock::now();
        stats.generation_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time).count();
        
        if (!result || result->vertices.empty()) {
            return result;
        }
        
        // Calculate mesh statistics
        stats.num_vertices = result->vertices.size();
        stats.num_faces = result->indices.size();
        
        // Calculate edge statistics
        std::set<std::pair<int, int>> unique_edges;
        for (const auto& face : result->indices) {
            for (int i = 0; i < 3; ++i) {
                int v1 = face[i];
                int v2 = face[(i + 1) % 3];
                auto edge = (v1 < v2) ? std::make_pair(v1, v2) : std::make_pair(v2, v1);
                unique_edges.insert(edge);
            }
        }
        stats.num_edges = unique_edges.size();
        
        // Calculate edge lengths
        if (!unique_edges.empty()) {
            stats.min_edge_length = std::numeric_limits<float>::max();
            stats.max_edge_length = 0.0f;
            float total_edge_length = 0.0f;
            
            for (const auto& edge : unique_edges) {
                Vec3f v1 = result->vertices[edge.first];
                Vec3f v2 = result->vertices[edge.second];
                float length = (v2 - v1).norm();
                
                stats.min_edge_length = std::min(stats.min_edge_length, length);
                stats.max_edge_length = std::max(stats.max_edge_length, length);
                total_edge_length += length;
            }
            
            stats.avg_edge_length = total_edge_length / unique_edges.size();
        }
        
        BOOST_LOG_TRIVIAL(info) << "generate_with_stats() - Mesh statistics:";
        BOOST_LOG_TRIVIAL(info) << "  Vertices: " << stats.num_vertices;
        BOOST_LOG_TRIVIAL(info) << "  Faces: " << stats.num_faces;
        BOOST_LOG_TRIVIAL(info) << "  Edges: " << stats.num_edges;
        BOOST_LOG_TRIVIAL(info) << "  Edge length: " << stats.min_edge_length << " - " 
                                 << stats.max_edge_length << " (avg: " << stats.avg_edge_length << ")";
        BOOST_LOG_TRIVIAL(info) << "  Generation time: " << stats.generation_time_ms << "ms";
        
        return result;
    }

    // STYLING FUNCTIONS - Apply creative flair to Voronoi cells
    
    // Main styling dispatcher
    void VoronoiMesh::apply_cell_styling(
        indexed_triangle_set& mesh,
        const Config& config)
    {
        if (mesh.vertices.empty() || mesh.indices.empty()) {
            return;
        }
        
        BOOST_LOG_TRIVIAL(info) << "apply_cell_styling() - Applying style: " << static_cast<int>(config.cell_style);
        
        auto start = std::chrono::high_resolution_clock::now();
        
        switch (config.cell_style) {
            case VoronoiMesh::CellStyle::Pure:
                // No styling - pure mathematical Voronoi
                break;
                
            case VoronoiMesh::CellStyle::Rounded:
                apply_rounding(mesh, config.rounding_radius);
                if (config.subdivision_level > 0) {
                    apply_organic_smoothing(mesh, config.subdivision_level);
                }
                break;
                
            case VoronoiMesh::CellStyle::Chamfered:
                apply_chamfering(mesh, config.chamfer_distance);
                break;
                
            case VoronoiMesh::CellStyle::Crystalline:
                apply_crystalline_cuts(mesh);
                break;
                
            case VoronoiMesh::CellStyle::Organic:
                apply_organic_smoothing(mesh, std::max(1, config.subdivision_level));
                break;
                
            case VoronoiMesh::CellStyle::Faceted:
                apply_faceting(mesh);
                break;
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        BOOST_LOG_TRIVIAL(info) << "apply_cell_styling() - Styling complete in " << duration.count() << "ms";
    }
    
    // Rounded style - smooth corners and edges
    void VoronoiMesh::apply_rounding(indexed_triangle_set& mesh, float radius)
    {
        if (radius <= 0.0f || mesh.vertices.empty()) {
            return;
        }
        
        BOOST_LOG_TRIVIAL(info) << "apply_rounding() - Rounding with radius: " << radius;
        
        // Build adjacency information
        std::vector<std::vector<int>> vertex_neighbors(mesh.vertices.size());
        
        for (const auto& face : mesh.indices) {
            for (int i = 0; i < 3; ++i) {
                int v1 = face[i];
                int v2 = face[(i + 1) % 3];
                
                if (std::find(vertex_neighbors[v1].begin(), vertex_neighbors[v1].end(), v2) == vertex_neighbors[v1].end()) {
                    vertex_neighbors[v1].push_back(v2);
                }
                if (std::find(vertex_neighbors[v2].begin(), vertex_neighbors[v2].end(), v1) == vertex_neighbors[v2].end()) {
                    vertex_neighbors[v2].push_back(v1);
                }
            }
        }
        
        // Apply Laplacian smoothing with limited iterations
        std::vector<Vec3f> new_positions = mesh.vertices;
        int iterations = 3;
        float strength = std::min(radius, 0.8f);
        
        for (int iter = 0; iter < iterations; ++iter) {
            for (size_t i = 0; i < mesh.vertices.size(); ++i) {
                if (vertex_neighbors[i].empty()) {
                    continue;
                }
                
                // Compute average of neighbors
                Vec3f avg = Vec3f::Zero();
                for (int neighbor : vertex_neighbors[i]) {
                    avg += mesh.vertices[neighbor];
                }
                avg /= static_cast<float>(vertex_neighbors[i].size());
                
                // Move vertex toward average
                new_positions[i] = mesh.vertices[i] * (1.0f - strength) + avg * strength;
            }
            
            mesh.vertices = new_positions;
            strength *= 0.7f;  // Reduce strength each iteration
        }
        
        BOOST_LOG_TRIVIAL(info) << "apply_rounding() - Applied " << iterations << " smoothing iterations";
    }
    
    // Chamfered style - beveled edges
    void VoronoiMesh::apply_chamfering(indexed_triangle_set& mesh, float distance)
    {
        if (distance <= 0.0f || mesh.vertices.empty()) {
            return;
        }
        
        BOOST_LOG_TRIVIAL(info) << "apply_chamfering() - Chamfering with distance: " << distance;
        
        // Find edges and create bevels
        std::map<std::pair<int, int>, std::vector<int>> edge_faces;
        
        for (size_t fi = 0; fi < mesh.indices.size(); ++fi) {
            const auto& face = mesh.indices[fi];
            for (int i = 0; i < 3; ++i) {
                int v1 = face[i];
                int v2 = face[(i + 1) % 3];
                auto edge = (v1 < v2) ? std::make_pair(v1, v2) : std::make_pair(v2, v1);
                edge_faces[edge].push_back(fi);
            }
        }
        
        // For each edge shared by 2+ faces, create a bevel
        std::vector<Vec3i> new_faces;
        std::vector<Vec3f> new_vertices = mesh.vertices;
        
        for (const auto& [edge, faces] : edge_faces) {
            if (faces.size() >= 2) {
                // Move edge vertices inward slightly
                int v1 = edge.first;
                int v2 = edge.second;
                
                Vec3f edge_center = (mesh.vertices[v1] + mesh.vertices[v2]) * 0.5f;
                Vec3f edge_vec = (mesh.vertices[v2] - mesh.vertices[v1]).normalized();
                
                // Shrink edge slightly
                float shrink = std::min(distance, 0.3f);
                new_vertices[v1] = mesh.vertices[v1] + edge_vec * shrink;
                new_vertices[v2] = mesh.vertices[v2] - edge_vec * shrink;
            }
        }
        
        mesh.vertices = new_vertices;
        
        BOOST_LOG_TRIVIAL(info) << "apply_chamfering() - Chamfered " << edge_faces.size() << " edges";
    }
    
    // Crystalline style - add extra cuts for gem-like appearance
    void VoronoiMesh::apply_crystalline_cuts(indexed_triangle_set& mesh)
    {
        if (mesh.vertices.empty() || mesh.indices.empty()) {
            return;
        }
        
        BOOST_LOG_TRIVIAL(info) << "apply_crystalline_cuts() - Adding crystalline facets";
        
        // Subdivide larger faces to add detail
        std::vector<Vec3i> new_indices;
        new_indices.reserve(mesh.indices.size() * 2);
        
        for (const auto& face : mesh.indices) {
            const Vec3f& v0 = mesh.vertices[face[0]];
            const Vec3f& v1 = mesh.vertices[face[1]];
            const Vec3f& v2 = mesh.vertices[face[2]];
            
            // Calculate face area
            Vec3f edge1 = v1 - v0;
            Vec3f edge2 = v2 - v0;
            float area = 0.5f * edge1.cross(edge2).norm();
            
            // Subdivide large faces
            if (area > 5.0f) {
                // Add center vertex
                int center_idx = mesh.vertices.size();
                Vec3f center = (v0 + v1 + v2) / 3.0f;
                
                // Slightly perturb center for angular look
                Vec3f normal = edge1.cross(edge2).normalized();
                center += normal * 0.1f;
                
                mesh.vertices.push_back(center);
                
                // Create 3 new triangles
                new_indices.emplace_back(face[0], face[1], center_idx);
                new_indices.emplace_back(face[1], face[2], center_idx);
                new_indices.emplace_back(face[2], face[0], center_idx);
            } else {
                new_indices.push_back(face);
            }
        }
        
        mesh.indices = new_indices;
        
        BOOST_LOG_TRIVIAL(info) << "apply_crystalline_cuts() - Created " << mesh.indices.size() << " facets";
    }
    
    // Organic style - subdivision surface smoothing
    void VoronoiMesh::apply_organic_smoothing(indexed_triangle_set& mesh, int subdivisions)
    {
        if (subdivisions <= 0 || mesh.vertices.empty()) {
            return;
        }
        
        BOOST_LOG_TRIVIAL(info) << "apply_organic_smoothing() - Applying " << subdivisions << " subdivision levels";
        
        for (int level = 0; level < subdivisions; ++level) {
            // Simple Loop subdivision approximation
            std::map<std::pair<int, int>, int> edge_midpoints;
            std::vector<Vec3f> new_vertices = mesh.vertices;
            std::vector<Vec3i> new_faces;
            
            new_faces.reserve(mesh.indices.size() * 4);
            
            auto get_or_create_midpoint = [&](int v1, int v2) -> int {
                auto edge = (v1 < v2) ? std::make_pair(v1, v2) : std::make_pair(v2, v1);
                auto it = edge_midpoints.find(edge);
                
                if (it != edge_midpoints.end()) {
                    return it->second;
                }
                
                int new_idx = new_vertices.size();
                Vec3f midpoint = (mesh.vertices[v1] + mesh.vertices[v2]) * 0.5f;
                new_vertices.push_back(midpoint);
                edge_midpoints[edge] = new_idx;
                return new_idx;
            };
            
            // Subdivide each triangle into 4
            for (const auto& face : mesh.indices) {
                int v0 = face[0];
                int v1 = face[1];
                int v2 = face[2];
                
                int m01 = get_or_create_midpoint(v0, v1);
                int m12 = get_or_create_midpoint(v1, v2);
                int m20 = get_or_create_midpoint(v2, v0);
                
                // Create 4 new triangles
                new_faces.emplace_back(v0, m01, m20);
                new_faces.emplace_back(v1, m12, m01);
                new_faces.emplace_back(v2, m20, m12);
                new_faces.emplace_back(m01, m12, m20);
            }
            
            mesh.vertices = new_vertices;
            mesh.indices = new_faces;
            
            // Apply smoothing
            apply_rounding(mesh, 0.3f);
        }
        
        BOOST_LOG_TRIVIAL(info) << "apply_organic_smoothing() - Result: " << mesh.vertices.size() 
                                 << " vertices, " << mesh.indices.size() << " faces";
    }
    
    // Faceted style - add extra triangulation for visual interest
    void VoronoiMesh::apply_faceting(indexed_triangle_set& mesh)
    {
        if (mesh.vertices.empty() || mesh.indices.empty()) {
            return;
        }
        
        BOOST_LOG_TRIVIAL(info) << "apply_faceting() - Adding facet detail";
        
        // Add strategic subdivisions for low-poly look
        std::vector<Vec3i> new_indices;
        new_indices.reserve(mesh.indices.size() * 2);
        
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        
        for (const auto& face : mesh.indices) {
            const Vec3f& v0 = mesh.vertices[face[0]];
            const Vec3f& v1 = mesh.vertices[face[1]];
            const Vec3f& v2 = mesh.vertices[face[2]];
            
            // Calculate face area
            Vec3f edge1 = v1 - v0;
            Vec3f edge2 = v2 - v0;
            float area = 0.5f * edge1.cross(edge2).norm();
            
            // Randomly subdivide some larger faces
            if (area > 3.0f && dist(rng) > 0.5f) {
                // Split along longest edge
                float len01 = (v1 - v0).norm();
                float len12 = (v2 - v1).norm();
                float len20 = (v0 - v2).norm();
                
                int new_idx = mesh.vertices.size();
                
                if (len01 >= len12 && len01 >= len20) {
                    // Split edge 0-1
                    mesh.vertices.push_back((v0 + v1) * 0.5f);
                    new_indices.emplace_back(face[0], new_idx, face[2]);
                    new_indices.emplace_back(new_idx, face[1], face[2]);
                } else if (len12 >= len20) {
                    // Split edge 1-2
                    mesh.vertices.push_back((v1 + v2) * 0.5f);
                    new_indices.emplace_back(face[0], face[1], new_idx);
                    new_indices.emplace_back(face[0], new_idx, face[2]);
                } else {
                    // Split edge 2-0
                    mesh.vertices.push_back((v2 + v0) * 0.5f);
                    new_indices.emplace_back(face[0], face[1], new_idx);
                    new_indices.emplace_back(new_idx, face[1], face[2]);
                }
            } else {
                new_indices.push_back(face);
            }
        }
        
        mesh.indices = new_indices;
        
        BOOST_LOG_TRIVIAL(info) << "apply_faceting() - Created " << mesh.indices.size() << " faceted triangles";
    }

    // PHASE 5: Mesh validation
    bool VoronoiMesh::validate_mesh(
        const indexed_triangle_set& mesh,
        std::vector<std::string>* issues)
    {
        bool is_valid = true;
        
        if (issues) {
            issues->clear();
        }
        
        // Check 1: Empty mesh
        if (mesh.vertices.empty()) {
            if (issues) issues->push_back("Mesh has no vertices");
            return false;
        }
        
        if (mesh.indices.empty()) {
            if (issues) issues->push_back("Mesh has no faces");
            return false;
        }
        
        BOOST_LOG_TRIVIAL(info) << "validate_mesh() - Validating mesh with " 
                                 << mesh.vertices.size() << " vertices and " 
                                 << mesh.indices.size() << " faces";
        
        // Check 2: Invalid face indices
        for (size_t i = 0; i < mesh.indices.size(); ++i) {
            const auto& face = mesh.indices[i];
            for (int j = 0; j < 3; ++j) {
                if (face[j] < 0 || face[j] >= static_cast<int>(mesh.vertices.size())) {
                    if (issues) {
                        issues->push_back("Face " + std::to_string(i) + " has invalid vertex index: " + std::to_string(face[j]));
                    }
                    is_valid = false;
                }
            }
        }
        
        // Check 3: Degenerate faces
        int degenerate_count = 0;
        for (size_t i = 0; i < mesh.indices.size(); ++i) {
            const auto& face = mesh.indices[i];
            const Vec3f& v0 = mesh.vertices[face[0]];
            const Vec3f& v1 = mesh.vertices[face[1]];
            const Vec3f& v2 = mesh.vertices[face[2]];
            
            Vec3f edge1 = v1 - v0;
            Vec3f edge2 = v2 - v0;
            float area = 0.5f * edge1.cross(edge2).norm();
            
            if (area < 1e-8f) {
                degenerate_count++;
            }
        }
        
        if (degenerate_count > 0) {
            if (issues) {
                issues->push_back("Total degenerate faces: " + std::to_string(degenerate_count));
            }
            is_valid = false;
        }
        
        BOOST_LOG_TRIVIAL(info) << "validate_mesh() - Validation complete: " << (is_valid ? "VALID" : "INVALID");
        BOOST_LOG_TRIVIAL(info) << "  Degenerate faces: " << degenerate_count;
        
        return is_valid;
    }

} // namespace Slic3r