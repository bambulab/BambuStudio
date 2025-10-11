#include "VoronoiMesh.hpp"
#include "libslic3r/AABBMesh.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/MeshBoolean.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include <random>
#include <algorithm>
#include <set>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <array>
#include <cmath>
#include <limits>
#include <chrono>
#include <numeric>
#include <optional>
#include <list>
#include <mutex>
#include <atomic>

#ifdef _OPENMP
#include <omp.h>
#endif

#include <boost/log/trivial.hpp>

// CGAL headers for mesh operations (boolean, convex hull, etc)
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Delaunay_triangulation_3.h>
#include <CGAL/Regular_triangulation_3.h>
#include <CGAL/Triangulation_vertex_base_with_info_3.h>
#include <CGAL/Delaunay_triangulation_cell_base_with_circumcenter_3.h>
#include <CGAL/convex_hull_3.h>
#include <CGAL/Polyhedron_3.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/orientation.h>
#include <CGAL/Polygon_mesh_processing/triangulate_faces.h>
#include <CGAL/Polygon_mesh_processing/corefinement.h>
#include <CGAL/Convex_hull_3/dual/halfspace_intersection_3.h>
#include <CGAL/Side_of_triangle_mesh.h>
#include <CGAL/AABB_tree.h>
#include <CGAL/AABB_face_graph_triangle_primitive.h>
#include <CGAL/intersections.h>
#include <CGAL/Object.h>
#include <CGAL/boost/graph/copy_face_graph.h>
#include <CGAL/IO/io.h>

namespace Slic3r {

    namespace PMP = CGAL::Polygon_mesh_processing;

    // CGAL type definitions for 3D Delaunay/Voronoi
    // Fast kernel for triangulation
    using Kf = CGAL::Exact_predicates_inexact_constructions_kernel;
    // Exact kernel for robust boolean operations
    using Ke = CGAL::Exact_predicates_exact_constructions_kernel;
    
    // Delaunay triangulation (unweighted)
    using Vb = CGAL::Triangulation_vertex_base_with_info_3<int, Kf>;
    using Cb = CGAL::Delaunay_triangulation_cell_base_with_circumcenter_3<Kf>;
    using Tds = CGAL::Triangulation_data_structure_3<Vb, Cb>;
    using Delaunay = CGAL::Delaunay_triangulation_3<Kf, Tds>;
    
    // Regular triangulation (weighted/power diagram)
    using RT = CGAL::Regular_triangulation_3<Kf>;
    using Weighted_point = RT::Weighted_point;
    using Bare_point = RT::Bare_point;
    
    // Legacy aliases (keep for compatibility)
    using K = Kf;
    using Point_3 = Kf::Point_3;
    using Segment_3 = Kf::Segment_3;
    using Ray_3 = Kf::Ray_3;
    using Line_3 = Kf::Line_3;
    using Vector_3 = Kf::Vector_3;
    using Plane_3 = Kf::Plane_3;
    using Iso_cuboid_3 = Kf::Iso_cuboid_3;
    using CGALMesh = CGAL::Surface_mesh<Point_3>;
    using CGALMeshExact = CGAL::Surface_mesh<Ke::Point_3>;
    using Polyhedron = CGAL::Polyhedron_3<Kf>;
    using Primitive = CGAL::AABB_face_graph_triangle_primitive<CGALMesh>;
    using AABBTree = CGAL::AABB_tree<Primitive>;
    using SideTester = CGAL::Side_of_triangle_mesh<CGALMesh, Kf>;

    namespace {
        
        // Geometric tolerances for robust processing
        constexpr double EPSILON = 1e-6;
        constexpr double GEOM_EPSILON = 1e-6;
        constexpr double MIN_EDGE_LENGTH = 1e-4;
        constexpr double MIN_EDGE_LENGTH_SQ = 1e-8;
        constexpr double MIN_FACE_AREA = 1e-8;
        constexpr double WELD_DISTANCE = 1e-5;
        constexpr double WELD_THRESHOLD = 1e-7;  // REDUCED from 1e-5 for accuracy
        constexpr double BOUNDARY_MARGIN_PERCENT = 0.05;  // 5% expansion
        constexpr double MIN_SEED_SEPARATION_FACTOR = 0.5;  // Relative to cell size
        
        // Forward declarations for helper functions
        void clip_wireframe_to_mesh(indexed_triangle_set& wireframe, const indexed_triangle_set& mesh);
        
        // Type aliases for convenience
        using Config = VoronoiMesh::Config;
        using EdgeShape = VoronoiMesh::EdgeShape;
        using CellStyle = VoronoiMesh::CellStyle;
        
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
            
            bool inside = hit.is_inside();
            
            // DEBUG: Log for first few points to verify inside test works
            static int debug_count = 0;
            static std::mutex debug_mutex;
            
            std::lock_guard<std::mutex> lock(debug_mutex);
            if (debug_count < 10) {
                BOOST_LOG_TRIVIAL(debug) << "is_point_inside_mesh() - Point " << point.transpose() 
                                          << " inside=" << inside;
                debug_count++;
            }
            
            return inside;
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

        struct DelaunayDiagram {
            Delaunay dt;
            std::vector<Delaunay::Vertex_handle> vertex_by_index;

            explicit DelaunayDiagram(const std::vector<Vec3d>& seeds)
            {
                std::vector<std::pair<Point_3, int>> points;
                points.reserve(seeds.size());
                for (size_t i = 0; i < seeds.size(); ++i) {
                    points.emplace_back(Point_3(seeds[i].x(), seeds[i].y(), seeds[i].z()), int(i));
                }

                dt.insert(points.begin(), points.end());

                vertex_by_index.resize(seeds.size(), Delaunay::Vertex_handle());
                for (auto vit = dt.finite_vertices_begin(); vit != dt.finite_vertices_end(); ++vit) {
                    int idx = vit->info();
                    if (idx >= 0 && idx < static_cast<int>(vertex_by_index.size())) {
                        vertex_by_index[idx] = vit;
                    }
                }
            }

            Delaunay::Vertex_handle vertex(int idx) const
            {
                if (idx < 0 || idx >= static_cast<int>(vertex_by_index.size()))
                    return Delaunay::Vertex_handle();
                return vertex_by_index[idx];
            }
        };

        struct VorEdge {
            Point_3 a;
            Point_3 b;
        };

        struct VoronoiCellData {
            int seed_index = -1;
            Point_3 seed_point = Point_3(0, 0, 0);
            Vec3d centroid = Vec3d::Zero();
            double volume = 0.0;
            double surface_area = 0.0;
            Vec3d bbox_min = Vec3d(std::numeric_limits<double>::max(),
                                   std::numeric_limits<double>::max(),
                                   std::numeric_limits<double>::max());
            Vec3d bbox_max = Vec3d(std::numeric_limits<double>::lowest(),
                                   std::numeric_limits<double>::lowest(),
                                   std::numeric_limits<double>::lowest());
            indexed_triangle_set geometry;
            std::vector<int> neighbor_ids;
        std::vector<double> face_areas;
        std::vector<Vec3d> face_normals;
        std::vector<int> face_vertex_counts;
    };

        struct RvdPolygon {
            std::vector<Vec3d> vertices;
            int seed_index = -1;
        };

        inline Vec3d to_vec3d(const Point_3& p)
        {
            return Vec3d(CGAL::to_double(p.x()), CGAL::to_double(p.y()), CGAL::to_double(p.z()));
        }

        Iso_cuboid_3 make_cuboid(const BoundingBoxf3& bounds)
        {
            return Iso_cuboid_3(
                Point_3(bounds.min.x(), bounds.min.y(), bounds.min.z()),
                Point_3(bounds.max.x(), bounds.max.y(), bounds.max.z())
            );
        }

        std::vector<Plane_3> build_bounding_planes(const BoundingBoxf3& bounds)
        {
            std::vector<Plane_3> planes;
            planes.reserve(6);

            planes.emplace_back(Point_3(bounds.min.x(), bounds.min.y(), bounds.min.z()), Vector_3(1, 0, 0));
            planes.emplace_back(Point_3(bounds.max.x(), bounds.min.y(), bounds.min.z()), Vector_3(-1, 0, 0));
            planes.emplace_back(Point_3(bounds.min.x(), bounds.min.y(), bounds.min.z()), Vector_3(0, 1, 0));
            planes.emplace_back(Point_3(bounds.min.x(), bounds.max.y(), bounds.min.z()), Vector_3(0, -1, 0));
            planes.emplace_back(Point_3(bounds.min.x(), bounds.min.y(), bounds.min.z()), Vector_3(0, 0, 1));
            planes.emplace_back(Point_3(bounds.min.x(), bounds.min.y(), bounds.max.z()), Vector_3(0, 0, -1));

            return planes;
        }

        std::optional<Segment_3> clip_ray_to_box(const Ray_3& ray, const Iso_cuboid_3& box)
        {
            CGAL::Object obj = CGAL::intersection(ray, box);
            if (const Segment_3* seg = CGAL::object_cast<Segment_3>(&obj)) {
                return *seg;
            }
            return std::nullopt;
        }

        std::optional<Segment_3> clip_line_to_box(const Line_3& line, const Iso_cuboid_3& box)
        {
            CGAL::Object obj = CGAL::intersection(line, box);
            if (const Segment_3* seg = CGAL::object_cast<Segment_3>(&obj)) {
                return *seg;
            }
            return std::nullopt;
        }

        bool segment_long_enough(const Segment_3& seg, double min_length_sq)
        {
            return CGAL::to_double(seg.squared_length()) > min_length_sq;
        }

        void accumulate_polygon_metrics(
            const std::vector<Vec3d>& polygon,
            double& area_out,
            Vec3d& normal_out)
        {
            if (polygon.size() < 3) {
                area_out = 0.0;
                normal_out = Vec3d::Zero();
                return;
            }

            Vec3d normal = Vec3d::Zero();
            for (size_t i = 0; i < polygon.size(); ++i) {
                const Vec3d& current = polygon[i];
                const Vec3d& next = polygon[(i + 1) % polygon.size()];
                normal.x() += (current.y() - next.y()) * (current.z() + next.z());
                normal.y() += (current.z() - next.z()) * (current.x() + next.x());
                normal.z() += (current.x() - next.x()) * (current.y() + next.y());
            }

            double area = 0.5 * normal.norm();
            if (area < 1e-12) {
                area_out = 0.0;
                normal_out = Vec3d::Zero();
                return;
            }

            area_out = area;
            normal_out = normal.normalized();
        }

        bool compute_mesh_metrics(
            const indexed_triangle_set& mesh,
            VoronoiCellData& data)
        {
            if (mesh.indices.empty())
                return false;

            double volume_sum = 0.0;
            Vec3d centroid_sum = Vec3d::Zero();
            double surface_area = 0.0;

            Vec3d bbox_min = Vec3d(std::numeric_limits<double>::max(),
                                   std::numeric_limits<double>::max(),
                                   std::numeric_limits<double>::max());
            Vec3d bbox_max = Vec3d(std::numeric_limits<double>::lowest(),
                                   std::numeric_limits<double>::lowest(),
                                   std::numeric_limits<double>::lowest());

            for (const auto& tri : mesh.indices) {
                const Vec3f& af = mesh.vertices[tri(0)];
                const Vec3f& bf = mesh.vertices[tri(1)];
                const Vec3f& cf = mesh.vertices[tri(2)];

                Vec3d a = af.cast<double>();
                Vec3d b = bf.cast<double>();
                Vec3d c = cf.cast<double>();

                bbox_min = bbox_min.cwiseMin(a).cwiseMin(b).cwiseMin(c);
                bbox_max = bbox_max.cwiseMax(a).cwiseMax(b).cwiseMax(c);

                Vec3d ab = b - a;
                Vec3d ac = c - a;
                Vec3d cross = ab.cross(ac);
                double triangle_area = 0.5 * cross.norm();
                surface_area += triangle_area;

                double volume = a.dot(b.cross(c)) / 6.0;
                volume_sum += volume;

                Vec3d tetra_centroid = (a + b + c) / 4.0;
                centroid_sum += tetra_centroid * volume;
            }

            if (std::abs(volume_sum) < 1e-12)
                return false;

            data.volume = std::abs(volume_sum);
            data.surface_area = surface_area;
            Vec3d centroid = centroid_sum / volume_sum;
            data.centroid = centroid;
            data.bbox_min = bbox_min;
            data.bbox_max = bbox_max;
            return true;
        }

        // ============================================================================
        // ROBUST CGAL-ONLY SOLID VORONOI CELL GENERATION
        // ============================================================================
        
        struct Halfspace {
            Plane_3 plane;
            bool keep_positive;  // whether we keep the positive side
        };
        
        /**
         * Build halfspace planes for a seed from its Delaunay neighbors
         * For unweighted Voronoi: bisector planes between seed and neighbors
         */
        std::vector<Halfspace> build_halfspaces_for_seed(
            const Delaunay& dt,
            Delaunay::Vertex_handle vh)
        {
            const Point_3 s = vh->point();
            std::vector<Halfspace> halfspaces;
            
            // Get all Delaunay neighbors (the only sites that can bound this cell)
            std::vector<Delaunay::Vertex_handle> neighbors;
            dt.finite_adjacent_vertices(vh, std::back_inserter(neighbors));
            
            for (auto nh : neighbors) {
                const Point_3 sp = nh->point();  // neighbor site
                
                // Skip if same point (degenerate)
                if (s == sp) continue;
                
                // Midplane between s and sp
                Vector_3 n = sp - s;  // plane normal points towards neighbor
                Point_3 m = CGAL::midpoint(s, sp);
                Plane_3 bis(m, n);   // plane through m with normal n
                
                // We want the half-space that contains s (the "closer-to-s" side)
                // Check which side of the plane contains s
                bool keep_positive = (bis.oriented_side(s) == CGAL::ON_POSITIVE_SIDE);
                halfspaces.push_back({bis, keep_positive});
            }
            
            return halfspaces;
        }
        
        /**
         * Add bounding box halfspaces to ensure cell is bounded
         */
        void add_bbox_halfspaces(
            const BoundingBoxf3& bbox,
            std::vector<Halfspace>& halfspaces,
            double pad_fraction = 0.25)
        {
            // Expand a bit to ensure seeds on boundary have valid cells
            double dx = (bbox.max.x() - bbox.min.x());
            double dy = (bbox.max.y() - bbox.min.y());
            double dz = (bbox.max.z() - bbox.min.z());
            double diag = std::sqrt(dx*dx + dy*dy + dz*dz);
            double pad = pad_fraction * diag;
            
            double xmin = bbox.min.x() - pad, xmax = bbox.max.x() + pad;
            double ymin = bbox.min.y() - pad, ymax = bbox.max.y() + pad;
            double zmin = bbox.min.z() - pad, zmax = bbox.max.z() + pad;
            
            // 6 planes of the box; keep the interior
            halfspaces.push_back({ Plane_3( 1, 0, 0, -xmin), true  }); // x >= xmin
            halfspaces.push_back({ Plane_3(-1, 0, 0,  xmax), true  }); // x <= xmax
            halfspaces.push_back({ Plane_3( 0, 1, 0, -ymin), true  }); // y >= ymin
            halfspaces.push_back({ Plane_3( 0,-1, 0,  ymax), true  }); // y <= ymax
            halfspaces.push_back({ Plane_3( 0, 0, 1, -zmin), true  }); // z >= zmin
            halfspaces.push_back({ Plane_3( 0, 0,-1,  zmax), true  }); // z <= zmax
        }
        
        /**
         * Intersect halfspaces to create a convex polyhedron (the Voronoi cell)
         * Uses CGAL::halfspace_intersection_3 for robust computation
         */
        bool make_cell_polyhedron(
            const std::vector<Halfspace>& halfspaces,
            const Point_3& inside_pt,
            CGALMesh& cell_out)
        {
            if (halfspaces.size() < 4) {
                return false;  // Need at least 4 planes for a 3D polyhedron
            }
            
            // Orient all planes so positive side is kept
            std::vector<Plane_3> planes;
            planes.reserve(halfspaces.size());
            for (const auto& h : halfspaces) {
                planes.push_back(h.keep_positive ? h.plane : h.plane.opposite());
            }
            
            try {
                // CGAL's robust halfspace intersection
                CGAL::halfspace_intersection_3(
                    planes.begin(),
                    planes.end(),
                    cell_out,
                    inside_pt
                );
                
                // Check if result is valid
                if (cell_out.is_empty() || cell_out.number_of_vertices() < 4) {
                    return false;
                }
                
                return true;
            }
            catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(warning) << "Halfspace intersection failed: " << e.what();
                return false;
            }
            catch (...) {
                BOOST_LOG_TRIVIAL(warning) << "Halfspace intersection failed with unknown error";
                return false;
            }
        }
        
        /**
         * Promote EPICK mesh to EPECK mesh for robust boolean operations
         */
        CGALMeshExact promote_mesh_to_exact(const CGALMesh& m)
        {
            CGALMeshExact out;
            std::vector<CGALMeshExact::Vertex_index> vmap;
            vmap.reserve(m.number_of_vertices());
            
            for (auto v : m.vertices()) {
                const auto& p = m.point(v);
                vmap.push_back(out.add_vertex(
                    Ke::Point_3(
                        CGAL::to_double(p.x()),
                        CGAL::to_double(p.y()),
                        CGAL::to_double(p.z())
                    )
                ));
            }
            
            for (auto f : m.faces()) {
                std::vector<CGALMeshExact::Vertex_index> ring;
                for (auto hv : CGAL::halfedges_around_face(m.halfedge(f), m)) {
                    ring.push_back(vmap[static_cast<size_t>(m.target(hv))]);
                }
                if (ring.size() >= 3) {
                    CGAL::Euler::add_face(ring, out);
                }
            }
            
            return out;
        }
        
        /**
         * Demote EPECK mesh back to EPICK mesh
         */
        CGALMesh demote_mesh_to_inexact(const CGALMeshExact& m)
        {
            CGALMesh out;
            std::vector<CGALMesh::Vertex_index> vmap;
            vmap.reserve(m.number_of_vertices());
            
            for (auto v : m.vertices()) {
                const auto& p = m.point(v);
                vmap.push_back(out.add_vertex(
                    Point_3(
                        CGAL::to_double(p.x()),
                        CGAL::to_double(p.y()),
                        CGAL::to_double(p.z())
                    )
                ));
            }
            
            for (auto f : m.faces()) {
                std::vector<CGALMesh::Vertex_index> ring;
                for (auto hv : CGAL::halfedges_around_face(m.halfedge(f), m)) {
                    ring.push_back(vmap[static_cast<size_t>(m.target(hv))]);
                }
                if (ring.size() >= 3) {
                    CGAL::Euler::add_face(ring, out);
                }
            }
            
            return out;
        }
        
        /**
         * Clip cell to model volume using robust boolean intersection
         * This produces a watertight solid cell inside the object
         */
        bool clip_cell_to_model(
            const CGALMesh& cell,
            const CGALMesh& model,
            CGALMesh& result_out)
        {
            try {
                // Promote to exact kernel for robust booleans
                CGALMeshExact cell_exact = promote_mesh_to_exact(cell);
                CGALMeshExact model_exact = promote_mesh_to_exact(model);
                
                CGALMeshExact result_exact;
                
                // Compute boolean intersection
                bool ok = PMP::corefine_and_compute_intersection(
                    cell_exact,
                    model_exact,
                    result_exact
                );
                
                if (!ok || result_exact.is_empty() || result_exact.number_of_faces() == 0) {
                    return false;  // Empty intersection = cell is outside model
                }
                
                // Demote back to inexact kernel
                result_out = demote_mesh_to_inexact(result_exact);
                
                return true;
            }
            catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(warning) << "Boolean intersection failed: " << e.what();
                return false;
            }
            catch (...) {
                BOOST_LOG_TRIVIAL(warning) << "Boolean intersection failed with unknown error";
                return false;
            }
        }
        
        /**
         * Generate a single solid Voronoi cell for one seed using robust CGAL-only method
         * This is the main entry point for the new robust implementation
         */
        bool compute_solid_voronoi_cell_robust(
            VoronoiCellData& out,
            const Delaunay& dt,
            int seed_index,
            Delaunay::Vertex_handle vh,
            const BoundingBoxf3& bounds,
            const CGALMesh* clip_model = nullptr)
        {
            if (vh == Delaunay::Vertex_handle()) {
                return false;
            }
            
            const Point_3 s = vh->point();
            out.seed_index = seed_index;
            out.seed_point = s;
            
            // Step 1: Build halfspaces from Delaunay neighbors
            std::vector<Halfspace> halfspaces = build_halfspaces_for_seed(dt, vh);
            
            if (halfspaces.empty()) {
                return false;  // No neighbors = degenerate cell
            }
            
            // Step 2: Add bounding box halfspaces
            add_bbox_halfspaces(bounds, halfspaces, 0.25);
            
            // Step 3: Intersect halfspaces to get cell polyhedron
            CGALMesh cell;
            if (!make_cell_polyhedron(halfspaces, s, cell)) {
                return false;
            }
            
            // Step 4: Clip to model volume if provided
            CGALMesh final_cell;
            if (clip_model != nullptr) {
                if (!clip_cell_to_model(cell, *clip_model, final_cell)) {
                    return false;  // Cell is outside model or clipping failed
                }
            } else {
                final_cell = cell;
            }
            
            // Step 5: Convert to indexed_triangle_set
            out.geometry = surface_mesh_to_indexed(final_cell);
            
            // Step 6: Compute metrics
            if (!compute_mesh_metrics(out.geometry, out)) {
                return false;
            }
            
            return true;
        }

        bool compute_voronoi_cell_data(
            VoronoiCellData& out,
            const DelaunayDiagram& diagram,
            int seed_index,
            const std::vector<Plane_3>& bounding_planes,
            const indexed_triangle_set* clip_mesh)
        {
            auto vh = diagram.vertex(seed_index);
            if (vh == Delaunay::Vertex_handle())
                return false;

            out.seed_index = seed_index;
            out.seed_point = vh->point();

            std::vector<Plane_3> planes = bounding_planes;
            std::unordered_set<int> neighbor_ids;

            auto add_neighbor = [&](Delaunay::Vertex_handle nh) {
                if (nh == Delaunay::Vertex_handle() || diagram.dt.is_infinite(nh))
                    return;
                int nid = nh->info();
                if (nid < 0)
                    return;
                if (!neighbor_ids.insert(nid).second)
                    return;

                Point_3 q = nh->point();
                Point_3 p = vh->point();
                if (p == q)
                    return;
                Point_3 mid = CGAL::midpoint(p, q);
                Vector_3 normal = Vector_3(
                    p.x() - q.x(),
                    p.y() - q.y(),
                    p.z() - q.z()
                );
                planes.emplace_back(mid, normal);
            };

            std::vector<Delaunay::Vertex_handle> adjacent;
            diagram.dt.finite_adjacent_vertices(vh, std::back_inserter(adjacent));
            for (auto nh : adjacent) {
                add_neighbor(nh);
            }

            out.neighbor_ids.assign(neighbor_ids.begin(), neighbor_ids.end());

            if (planes.size() < 4)
                return false;

            Polyhedron poly;
            if (!CGAL::halfspace_intersection_3(planes.begin(), planes.end(), poly, vh->point()))
                return false;

            if (poly.is_empty() || poly.size_of_vertices() < 4)
                return false;

            out.face_areas.clear();
            out.face_normals.clear();
            out.face_vertex_counts.clear();

            for (auto fit = poly.facets_begin(); fit != poly.facets_end(); ++fit) {
                std::vector<Vec3d> polygon_points;
                auto h = fit->facet_begin();
                auto h_end = h;
                do {
                    polygon_points.push_back(to_vec3d(h->vertex()->point()));
                    ++h;
                } while (h != h_end);

                double area = 0.0;
                Vec3d normal = Vec3d::Zero();
                accumulate_polygon_metrics(polygon_points, area, normal);

                out.face_vertex_counts.push_back(int(polygon_points.size()));
                out.face_areas.push_back(area);
                out.face_normals.push_back(normal);
            }

            CGALMesh cell_mesh;
            CGAL::copy_face_graph(poly, cell_mesh);
            PMP::triangulate_faces(cell_mesh);

            indexed_triangle_set cell_its = surface_mesh_to_indexed(cell_mesh);
            remove_degenerate_faces(cell_its);

            if (clip_mesh && !clip_mesh->vertices.empty()) {
                TriangleMesh tmp(cell_its);
                try {
                    MeshBoolean::cgal::intersect(tmp, *clip_mesh);
                } catch (...) {
                    return false;
                }

                if (tmp.its.vertices.empty() || tmp.its.indices.empty())
                    return false;
                cell_its = tmp.its;
                remove_degenerate_faces(cell_its);
            }

            if (cell_its.indices.empty())
                return false;

            out.geometry = std::move(cell_its);
            if (!compute_mesh_metrics(out.geometry, out))
                return false;

            return true;
        }

        std::vector<VoronoiCellData> compute_all_cells(
            const std::vector<Vec3d>& seeds,
            const BoundingBoxf3& bounds,
            const indexed_triangle_set* clip_mesh)
        {
            DelaunayDiagram diagram(seeds);
            std::vector<Plane_3> bounding_planes = build_bounding_planes(bounds);
            std::vector<VoronoiCellData> cells;
            cells.reserve(seeds.size());

            for (size_t i = 0; i < seeds.size(); ++i) {
                VoronoiCellData cell;
                if (compute_voronoi_cell_data(cell, diagram, int(i), bounding_planes, clip_mesh)) {
                    cells.push_back(std::move(cell));
                }
            }

            return cells;
        }

        static std::vector<Vec3d> clip_polygon_by_bisector(
            const std::vector<Vec3d>& polygon,
            const Vec3d& seed_i,
            const Vec3d& seed_j,
            double epsilon)
        {
            if (polygon.size() < 3)
                return {};

            Vec3d normal = seed_j - seed_i;
            double normal_norm = normal.norm();
            if (normal_norm < 1e-12)
                return {};
            Vec3d mid = (seed_i + seed_j) * 0.5;
            double denom_eps = normal_norm * 1e-12;

            auto is_inside = [&](const Vec3d& p) {
                return (p - mid).dot(normal) <= epsilon;
            };

            std::vector<Vec3d> output;
            output.reserve(polygon.size());

            for (size_t idx = 0; idx < polygon.size(); ++idx) {
                const Vec3d& curr = polygon[idx];
                const Vec3d& next = polygon[(idx + 1) % polygon.size()];
                bool curr_inside = is_inside(curr);
                bool next_inside = is_inside(next);

                if (curr_inside && next_inside) {
                    output.push_back(next);
                } else if (curr_inside && !next_inside) {
                    Vec3d dir = next - curr;
                    double denom = dir.dot(normal);
                    if (std::abs(denom) > denom_eps) {
                        double t = -((curr - mid).dot(normal)) / denom;
                        t = std::clamp(t, 0.0, 1.0);
                        Vec3d inter = curr + dir * t;
                        output.push_back(inter);
                    }
                } else if (!curr_inside && next_inside) {
                    Vec3d dir = next - curr;
                    double denom = dir.dot(normal);
                    if (std::abs(denom) > denom_eps) {
                        double t = -((curr - mid).dot(normal)) / denom;
                        t = std::clamp(t, 0.0, 1.0);
                        Vec3d inter = curr + dir * t;
                        output.push_back(inter);
                    }
                    output.push_back(next);
                }
            }

            if (output.size() < 3)
                return {};

            std::vector<Vec3d> cleaned;
            cleaned.reserve(output.size());
            for (const Vec3d& p : output) {
                if (cleaned.empty() || (p - cleaned.back()).norm() > epsilon * 1e-2)
                    cleaned.push_back(p);
            }
            if (cleaned.size() >= 2 && (cleaned.front() - cleaned.back()).squaredNorm() < epsilon * epsilon * 1e-2) {
                cleaned.back() = cleaned.front();
            }

            // Remove duplicated last == first if present
            if (cleaned.size() >= 2 && (cleaned.front() - cleaned.back()).squaredNorm() < epsilon * epsilon * 1e-4) {
                cleaned.pop_back();
            }

            if (cleaned.size() < 3)
                return {};

            return cleaned;
        }

        static double polygon_area(const std::vector<Vec3d>& polygon)
        {
            if (polygon.size() < 3)
                return 0.0;

            Vec3d accum = Vec3d::Zero();
            for (size_t i = 0; i < polygon.size(); ++i) {
                const Vec3d& a = polygon[i];
                const Vec3d& b = polygon[(i + 1) % polygon.size()];
                accum += a.cross(b);
            }
            return 0.5 * accum.norm();
        }

        static void enqueue_seed_neighbors(
            const DelaunayDiagram& diagram,
            int seed_index,
            int depth_limit,
            std::set<int>& candidates,
            std::unordered_set<int>& visited_seeds)
        {
            if (seed_index < 0)
                return;

            std::queue<std::pair<int, int>> q;
            if (visited_seeds.insert(seed_index).second) {
                q.emplace(seed_index, 0);
            }

            while (!q.empty()) {
                auto [current_idx, depth] = q.front();
                q.pop();
                candidates.insert(current_idx);

                if (depth >= depth_limit)
                    continue;

                auto vh = diagram.vertex(current_idx);
                if (vh == Delaunay::Vertex_handle() || diagram.dt.is_infinite(vh))
                    continue;

                std::vector<Delaunay::Vertex_handle> neighbors;
                diagram.dt.finite_adjacent_vertices(vh, std::back_inserter(neighbors));
                for (auto nh : neighbors) {
                    if (nh == Delaunay::Vertex_handle() || diagram.dt.is_infinite(nh))
                        continue;
                    int neighbor_idx = nh->info();
                    if (neighbor_idx < 0)
                        continue;
                    if (visited_seeds.insert(neighbor_idx).second) {
                        q.emplace(neighbor_idx, depth + 1);
                    }
                }
            }
        }

        static std::vector<RvdPolygon> compute_surface_rvd_polygons(
            const DelaunayDiagram& diagram,
            const std::vector<Vec3d>& seeds,
            const indexed_triangle_set& surface,
            double epsilon)
        {
            std::vector<RvdPolygon> polygons;
            if (surface.indices.empty() || surface.vertices.empty())
                return polygons;

            auto nearest_seed_index = [&](const Vec3d& point) -> int {
                Point_3 query(point.x(), point.y(), point.z());
                auto vh = diagram.dt.nearest_vertex(query);
                if (vh == Delaunay::Vertex_handle() || diagram.dt.is_infinite(vh))
                    return -1;
                int idx = vh->info();
                return (idx >= 0 && idx < static_cast<int>(seeds.size())) ? idx : -1;
            };

            constexpr int neighbor_depth = 2;

            for (const auto& tri : surface.indices) {
                Vec3d p0 = surface.vertices[tri[0]].cast<double>();
                Vec3d p1 = surface.vertices[tri[1]].cast<double>();
                Vec3d p2 = surface.vertices[tri[2]].cast<double>();

                std::set<int> candidates;
                std::unordered_set<int> visited;

                int idx0 = nearest_seed_index(p0);
                int idx1 = nearest_seed_index(p1);
                int idx2 = nearest_seed_index(p2);
                Vec3d centroid = (p0 + p1 + p2) / 3.0;
                int idx_centroid = nearest_seed_index(centroid);

                enqueue_seed_neighbors(diagram, idx0, neighbor_depth, candidates, visited);
                enqueue_seed_neighbors(diagram, idx1, neighbor_depth, candidates, visited);
                enqueue_seed_neighbors(diagram, idx2, neighbor_depth, candidates, visited);
                enqueue_seed_neighbors(diagram, idx_centroid, neighbor_depth, candidates, visited);

                if (candidates.empty())
                    continue;

                for (int seed_idx : candidates) {
                    if (seed_idx < 0 || seed_idx >= static_cast<int>(seeds.size()))
                        continue;

                    std::vector<Vec3d> poly = { p0, p1, p2 };
                    for (int other_idx : candidates) {
                        if (other_idx == seed_idx || other_idx < 0 || other_idx >= static_cast<int>(seeds.size()))
                            continue;
                        poly = clip_polygon_by_bisector(poly, seeds[seed_idx], seeds[other_idx], epsilon);
                        if (poly.size() < 3)
                            break;
                    }

                    if (poly.size() < 3)
                        continue;

                    double area = polygon_area(poly);
                    if (area < epsilon * epsilon)
                        continue;

                    polygons.push_back({ poly, seed_idx });
                }
            }

            return polygons;
        }

        std::vector<Vec3d> VoronoiMesh::prepare_seed_points(
            const indexed_triangle_set& input_mesh,
            const Config& config,
            BoundingBoxf3* out_bounds)
        {
            std::vector<Vec3d> seed_points = generate_seed_points(input_mesh, config);
            BOOST_LOG_TRIVIAL(info) << "prepare_seed_points() - Initial seeds: " << seed_points.size();

            if (seed_points.empty()) {
                BOOST_LOG_TRIVIAL(error) << "prepare_seed_points() - No seed points generated";
                if (out_bounds) *out_bounds = BoundingBoxf3();
                return {};
            }

            BoundingBoxf3 bbox;
            for (const auto& v : input_mesh.vertices)
                bbox.merge(v.cast<double>());

            if (!bbox.defined || bbox.size().minCoeff() <= 0.0) {
                BOOST_LOG_TRIVIAL(error) << "prepare_seed_points() - Invalid bounding box";
                if (out_bounds) *out_bounds = BoundingBoxf3();
                return {};
            }

            if (out_bounds)
                *out_bounds = bbox;

            size_t original_seed_count = seed_points.size();
            seed_points = filter_seed_points(seed_points, bbox);

            if (seed_points.empty()) {
                BOOST_LOG_TRIVIAL(error) << "prepare_seed_points() - All seeds filtered out after separation test";
                return {};
            }

            if (seed_points.size() < original_seed_count) {
                BOOST_LOG_TRIVIAL(info) << "prepare_seed_points() - Filtered to " << seed_points.size()
                                        << " seeds (removed " << (original_seed_count - seed_points.size())
                                        << " that were too close)";
            }

            if (config.relax_seeds && config.relaxation_iterations > 0) {
                BOOST_LOG_TRIVIAL(info) << "prepare_seed_points() - Applying Lloyd relaxation ("
                                         << config.relaxation_iterations << " iterations)";
                seed_points = lloyd_relaxation(seed_points, bbox, config.relaxation_iterations);
            }

            if (config.use_weighted_cells) {
                if (!config.cell_weights.empty() && config.cell_weights.size() == seed_points.size()) {
                    BOOST_LOG_TRIVIAL(info) << "prepare_seed_points() - Using " << config.cell_weights.size()
                                             << " provided weights";
                } else {
                    auto weights = generate_density_weights(seed_points, config.density_center, config.density_falloff);
                    BOOST_LOG_TRIVIAL(info) << "prepare_seed_points() - Generated " << weights.size()
                                             << " density weights";
                    (void)weights;
                }
            }

            if (config.optimize_for_load) {
                BOOST_LOG_TRIVIAL(info) << "prepare_seed_points() - Optimizing for load direction";
                seed_points = optimize_for_load_direction(seed_points, config.load_direction, config.load_stretch_factor);
            }

            if (config.anisotropic && config.anisotropy_ratio > 0.0f) {
                BOOST_LOG_TRIVIAL(info) << "prepare_seed_points() - Applying anisotropic transform";
                seed_points = apply_anisotropic_transform(seed_points, config.anisotropy_direction, config.anisotropy_ratio);
            }

            if (config.validate_printability && config.min_feature_size > 0.0f) {
                std::string error_msg;
                if (!validate_printability(seed_points, bbox, config.min_feature_size, error_msg)) {
                    BOOST_LOG_TRIVIAL(warning) << "prepare_seed_points() - Printability warning: " << error_msg;
                    if (config.error_callback)
                        config.error_callback(error_msg);
                }
            }

            BOOST_LOG_TRIVIAL(info) << "prepare_seed_points() - Final seed count: " << seed_points.size();
            return seed_points;
        }

        std::vector<VorEdge> extract_voronoi_edges(
            const DelaunayDiagram& diagram,
            const Iso_cuboid_3& bbox,
            double min_length_sq)
        {
            std::vector<VorEdge> edges;

            for (auto fit = diagram.dt.finite_facets_begin(); fit != diagram.dt.finite_facets_end(); ++fit) {
                CGAL::Object dual = diagram.dt.dual(*fit);

                if (const Segment_3* seg = CGAL::object_cast<Segment_3>(&dual)) {
                    if (segment_long_enough(*seg, min_length_sq)) {
                        edges.push_back({ seg->source(), seg->target() });
                    }
                } else if (const Ray_3* ray = CGAL::object_cast<Ray_3>(&dual)) {
                    auto clipped = clip_ray_to_box(*ray, bbox);
                    if (clipped && segment_long_enough(*clipped, min_length_sq)) {
                        edges.push_back({ clipped->source(), clipped->target() });
                    }
                } else if (const Line_3* line = CGAL::object_cast<Line_3>(&dual)) {
                    auto clipped = clip_line_to_box(*line, bbox);
                    if (clipped && segment_long_enough(*clipped, min_length_sq)) {
                        edges.push_back({ clipped->source(), clipped->target() });
                    }
                }
            }

            return edges;
        }

        static std::unique_ptr<indexed_triangle_set> create_wireframe_from_restricted_voronoi(
            const DelaunayDiagram& diagram,
            const std::vector<Vec3d>& seeds,
            const indexed_triangle_set& surface_mesh,
            const Config& config,
            double abs_eps)
        {
            auto result = std::make_unique<indexed_triangle_set>();
            auto polygons = compute_surface_rvd_polygons(diagram, seeds, surface_mesh, abs_eps);

            if (polygons.empty()) {
                BOOST_LOG_TRIVIAL(warning) << "create_wireframe_from_restricted_voronoi() - No surface polygons generated";
                return result;
            }

            BOOST_LOG_TRIVIAL(info) << "create_wireframe_from_restricted_voronoi() - Surface polygons: " << polygons.size();

            const double min_edge_length_sq = abs_eps * abs_eps;

            struct Vec3dHash {
                double scale;
                explicit Vec3dHash(double s) : scale(s) {}
                size_t operator()(const Vec3d& v) const {
                    int64_t x = int64_t(std::llround(v.x() * scale));
                    int64_t y = int64_t(std::llround(v.y() * scale));
                    int64_t z = int64_t(std::llround(v.z() * scale));
                    return std::hash<int64_t>()(x) ^ (std::hash<int64_t>()(y) << 1) ^ (std::hash<int64_t>()(z) << 2);
                }
            };

            struct Vec3dEqual {
                double eps;
                explicit Vec3dEqual(double e) : eps(e) {}
                bool operator()(const Vec3d& a, const Vec3d& b) const {
                    return (a - b).norm() <= eps;
                }
            };

            struct EdgeHash {
                Vec3dHash hasher;
                explicit EdgeHash(double scale) : hasher(scale) {}
                size_t operator()(const std::pair<Vec3d, Vec3d>& e) const {
                    return hasher(e.first) ^ (hasher(e.second) << 1);
                }
            };

            struct EdgeEqual {
                Vec3dEqual eq;
                explicit EdgeEqual(double eps) : eq(eps) {}
                bool operator()(const std::pair<Vec3d, Vec3d>& a, const std::pair<Vec3d, Vec3d>& b) const {
                    return (eq(a.first, b.first) && eq(a.second, b.second)) ||
                           (eq(a.first, b.second) && eq(a.second, b.first));
                }
            };

            const double hash_scale = 1.0 / std::max(abs_eps, 1e-9);

            if (config.hollow_cells) {
                std::unordered_set<std::pair<Vec3d, Vec3d>, EdgeHash, EdgeEqual> surface_edges(
                    64, EdgeHash(hash_scale), EdgeEqual(abs_eps));

                auto canonical_edge = [&](Vec3d a, Vec3d b) {
                    auto lt_eps = [abs_eps](double lhs, double rhs) {
                        return lhs + abs_eps < rhs;
                    };
                    auto eq_eps = [abs_eps](double lhs, double rhs) {
                        return std::abs(lhs - rhs) <= abs_eps;
                    };
                    if (lt_eps(b.x(), a.x()) ||
                        (eq_eps(a.x(), b.x()) && lt_eps(b.y(), a.y())) ||
                        (eq_eps(a.x(), b.x()) && eq_eps(a.y(), b.y()) && lt_eps(b.z(), a.z()))) {
                        std::swap(a, b);
                    }
                    return std::make_pair(a, b);
                };

                for (const auto& poly : polygons) {
                    const auto& verts = poly.vertices;
                    size_t count = verts.size();
                    if (count < 2)
                        continue;
                    for (size_t i = 0; i < count; ++i) {
                        const Vec3d& a = verts[i];
                        const Vec3d& b = verts[(i + 1) % count];
                        if ((b - a).squaredNorm() < min_edge_length_sq)
                            continue;
                        surface_edges.insert(canonical_edge(a, b));
                    }
                }

                if (surface_edges.empty()) {
                    BOOST_LOG_TRIVIAL(warning) << "create_wireframe_from_restricted_voronoi() - No edges after dedup";
                    return result;
                }

                const float radius = config.edge_thickness * 0.5f;
                const int segments = std::max(3, config.edge_segments);

                int skipped_degenerate = 0;
                for (const auto& edge : surface_edges) {
                    Vec3d p1 = edge.first;
                    Vec3d p2 = edge.second;
                    Vec3d dir = (p2 - p1);
                    double len_sq = dir.squaredNorm();
                    if (len_sq < min_edge_length_sq) {
                        skipped_degenerate++;
                        continue;
                    }

                    dir.normalize();
                    Vec3d perp1 = dir.cross(std::abs(dir.z()) < 0.9 ? Vec3d(0, 0, 1) : Vec3d(1, 0, 0));
                    if (perp1.squaredNorm() < 1e-12) {
                        perp1 = dir.cross(Vec3d(0, 1, 0));
                    }
                    perp1.normalize();
                    Vec3d perp2 = dir.cross(perp1).normalized();

                    size_t base_idx = result->vertices.size();
                    for (int i = 0; i < segments; ++i) {
                        float angle = 2.0f * float(M_PI) * float(i) / float(segments);
                        float x = radius * std::cos(angle);
                        float y = radius * std::sin(angle);
                        Vec3d offset = perp1 * x + perp2 * y;
                        result->vertices.emplace_back((p1 + offset).cast<float>());
                    }
                    for (int i = 0; i < segments; ++i) {
                        float angle = 2.0f * float(M_PI) * float(i) / float(segments);
                        float x = radius * std::cos(angle);
                        float y = radius * std::sin(angle);
                        Vec3d offset = perp1 * x + perp2 * y;
                        result->vertices.emplace_back((p2 + offset).cast<float>());
                    }

                    for (int i = 0; i < segments; ++i) {
                        int next = (i + 1) % segments;
                        result->indices.emplace_back(base_idx + i, base_idx + segments + i, base_idx + segments + next);
                        result->indices.emplace_back(base_idx + i, base_idx + segments + next, base_idx + next);
                    }

                    if (config.edge_caps) {
                        int cap1_center = result->vertices.size();
                        result->vertices.emplace_back(p1.cast<float>());
                        for (int i = 0; i < segments; ++i) {
                            int next = (i + 1) % segments;
                            result->indices.emplace_back(cap1_center, base_idx + next, base_idx + i);
                        }

                        int cap2_center = result->vertices.size();
                        result->vertices.emplace_back(p2.cast<float>());
                        for (int i = 0; i < segments; ++i) {
                            int next = (i + 1) % segments;
                            result->indices.emplace_back(cap2_center, base_idx + segments + i, base_idx + segments + next);
                        }
                    }
                }

                if (skipped_degenerate > 0) {
                    BOOST_LOG_TRIVIAL(info) << "create_wireframe_from_restricted_voronoi() - Skipped " << skipped_degenerate << " degenerate edges";
                }

                remove_degenerate_faces(*result);
                weld_vertices(*result, radius * 0.1f);
                remove_degenerate_faces(*result);
            } else {
                for (const auto& poly : polygons) {
                    const auto& verts = poly.vertices;
                    if (verts.size() < 3)
                        continue;

                    size_t base_idx = result->vertices.size();
                    for (const Vec3d& v : verts) {
                        result->vertices.emplace_back(v.cast<float>());
                    }
                    for (size_t i = 1; i + 1 < verts.size(); ++i) {
                        result->indices.emplace_back(int(base_idx), int(base_idx + i), int(base_idx + i + 1));
                    }
                }

                remove_degenerate_faces(*result);
                weld_vertices(*result, abs_eps);
                remove_degenerate_faces(*result);
            }

            BOOST_LOG_TRIVIAL(info) << "create_wireframe_from_restricted_voronoi() - Result: "
                                     << result->vertices.size() << " vertices, "
                                     << result->indices.size() << " faces";

            return result;
        }

        std::vector<VorEdge> clip_edges_to_volume(
            const CGALMesh& mesh,
            const std::vector<VorEdge>& input_edges,
            double min_length_sq)
        {
            if (input_edges.empty() || mesh.is_empty())
                return {};

            AABBTree tree(faces(mesh).begin(), faces(mesh).end(), mesh);
            tree.accelerate_distance_queries();

            SideTester inside(mesh);

            std::vector<VorEdge> result;
            result.reserve(input_edges.size());

            for (const auto& edge : input_edges) {
                Segment_3 seg(edge.a, edge.b);
                if (!segment_long_enough(seg, min_length_sq))
                    continue;

                auto point_status = [&](const Point_3& p) {
                    CGAL::Oriented_side side = inside(p);
                    return side == CGAL::ON_BOUNDED_SIDE || side == CGAL::ON_BOUNDARY;
                };

                bool a_inside = point_status(edge.a);
                bool b_inside = point_status(edge.b);

                if (a_inside && b_inside) {
                    result.push_back(edge);
                    continue;
                }

                std::list<CGAL::Object> intersections;
                tree.all_intersections(seg, std::back_inserter(intersections));

                std::vector<Point_3> points;
                points.reserve(2 + intersections.size() * 2);
                points.push_back(edge.a);
                points.push_back(edge.b);

                for (const auto& obj : intersections) {
                    if (const Point_3* ip = CGAL::object_cast<Point_3>(&obj)) {
                        points.push_back(*ip);
                    } else if (const Segment_3* sp = CGAL::object_cast<Segment_3>(&obj)) {
                        points.push_back(sp->source());
                        points.push_back(sp->target());
                    }
                }

                Vec3d source = to_vec3d(edge.a);
                Vec3d target = to_vec3d(edge.b);
                Vec3d dir = target - source;
                double len_sq = dir.squaredNorm();
                if (len_sq < min_length_sq)
                    continue;

                auto parameter = [&](const Point_3& p) {
                    Vec3d pv = to_vec3d(p);
                    return (pv - source).dot(dir) / len_sq;
                };

                std::sort(points.begin(), points.end(), [&](const Point_3& lhs, const Point_3& rhs) {
                    return parameter(lhs) < parameter(rhs);
                });

                std::vector<Point_3> unique_points;
                unique_points.reserve(points.size());
                const double param_eps = 1e-9;
                for (const auto& p : points) {
                    if (unique_points.empty() || std::abs(parameter(p) - parameter(unique_points.back())) > param_eps) {
                        unique_points.push_back(p);
                    }
                }

                for (size_t i = 0; i + 1 < unique_points.size(); ++i) {
                    Point_3 a = unique_points[i];
                    Point_3 b = unique_points[i + 1];
                    Point_3 mid = CGAL::midpoint(a, b);

                    if (point_status(mid)) {
                        Segment_3 clipped(a, b);
                        if (segment_long_enough(clipped, min_length_sq)) {
                            result.push_back({ a, b });
                        }
                    }
                }
            }

            return result;
        }
        
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
            
            // Compute mesh center for outward detection
            Vec3f mesh_center = Vec3f::Zero();
            for (const Vec3f& v : mesh.vertices) {
                mesh_center += v;
            }
            mesh_center /= float(mesh.vertices.size());
            
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
            
            // Check if seed face normal points outward, flip if not
            {
                const auto& face = mesh.indices[seed_face];
                const Vec3f& v0 = mesh.vertices[face[0]];
                const Vec3f& v1 = mesh.vertices[face[1]];
                const Vec3f& v2 = mesh.vertices[face[2]];
                
                Vec3f normal = (v1 - v0).cross(v2 - v0);
                Vec3f face_center = (v0 + v1 + v2) / 3.0f;
                Vec3f to_face = face_center - mesh_center;
                
                // If normal points inward (toward center), flip the seed face
                if (normal.dot(to_face) < 0) {
                    should_flip[seed_face] = true;
                    BOOST_LOG_TRIVIAL(debug) << "Seed face points inward, will flip";
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
        
        // ========== MANIFOLD DETECTION AND REPAIR ==========
        
        // Check if mesh is manifold (each edge used by exactly 1 or 2 faces)
        bool is_mesh_manifold(const indexed_triangle_set& mesh) {
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
                if (count > 2) {
                    return false;  // Non-manifold edge (used by >2 faces)
                }
            }
            
            return true;
        }
        
        // Diagnose non-manifold issues in detail
        void diagnose_non_manifold(const indexed_triangle_set& mesh) {
            BOOST_LOG_TRIVIAL(info) << "=== NON-MANIFOLD DIAGNOSIS ===";
            
            // Check 1: Edge usage count
            std::map<std::pair<int, int>, std::vector<int>> edge_to_faces;
            
            for (size_t fi = 0; fi < mesh.indices.size(); ++fi) {
                const auto& face = mesh.indices[fi];
                for (int i = 0; i < 3; ++i) {
                    int v1 = face[i];
                    int v2 = face[(i + 1) % 3];
                    auto edge = (v1 < v2) ? std::make_pair(v1, v2) : std::make_pair(v2, v1);
                    edge_to_faces[edge].push_back(fi);
                }
            }
            
            int boundary_edges = 0;
            int non_manifold_edges = 0;
            
            for (const auto& [edge, faces] : edge_to_faces) {
                if (faces.size() == 1) {
                    boundary_edges++;
                    if (boundary_edges <= 10) {
                        BOOST_LOG_TRIVIAL(warning) << "  Boundary edge: " << edge.first << "-" << edge.second
                                                    << " (face " << faces[0] << ")";
                    }
                } else if (faces.size() > 2) {
                    non_manifold_edges++;
                    if (non_manifold_edges <= 10) {
                        BOOST_LOG_TRIVIAL(warning) << "  Non-manifold edge: " << edge.first << "-" << edge.second
                                                    << " (used by " << faces.size() << " faces)";
                    }
                }
            }
            
            BOOST_LOG_TRIVIAL(info) << "Total boundary edges: " << boundary_edges;
            BOOST_LOG_TRIVIAL(info) << "Total non-manifold edges: " << non_manifold_edges;
            
            // Check 2: Duplicate vertices (using tuple as key for comparison)
            std::map<std::tuple<int, int, int>, std::vector<int>> pos_to_vertices;
            for (size_t i = 0; i < mesh.vertices.size(); ++i) {
                Vec3f v = mesh.vertices[i];
                auto key = std::make_tuple(int(v.x() * 1000), int(v.y() * 1000), int(v.z() * 1000));
                pos_to_vertices[key].push_back(i);
            }
            
            int duplicate_count = 0;
            for (const auto& [pos, verts] : pos_to_vertices) {
                if (verts.size() > 1) {
                    duplicate_count++;
                }
            }
            BOOST_LOG_TRIVIAL(info) << "Potential duplicate vertex positions: " << duplicate_count;
            
            // Check 3: Degenerate faces
            int degenerate = 0;
            for (const auto& face : mesh.indices) {
                if (face[0] == face[1] || face[1] == face[2] || face[2] == face[0]) {
                    degenerate++;
                }
            }
            BOOST_LOG_TRIVIAL(info) << "Degenerate faces: " << degenerate;
        }
        
        // Repair non-manifold edges by duplicating vertices
        void repair_non_manifold(indexed_triangle_set& mesh) {
            BOOST_LOG_TRIVIAL(info) << "Attempting non-manifold repair...";
            
            // Step 1: Remove degenerate faces
            std::vector<Vec3i> clean_faces;
            for (const auto& face : mesh.indices) {
                if (face[0] != face[1] && face[1] != face[2] && face[2] != face[0]) {
                    clean_faces.push_back(face);
                }
            }
            mesh.indices = clean_faces;
            
            // Step 2: Weld duplicate vertices
            weld_vertices(mesh, 1e-6);
            
            // Step 3: Remove non-manifold edges by duplicating vertices
            std::map<std::pair<int, int>, std::vector<int>> edge_to_faces;
            
            for (size_t fi = 0; fi < mesh.indices.size(); ++fi) {
                const auto& face = mesh.indices[fi];
                for (int i = 0; i < 3; ++i) {
                    int v1 = face[i];
                    int v2 = face[(i + 1) % 3];
                    auto edge = (v1 < v2) ? std::make_pair(v1, v2) : std::make_pair(v2, v1);
                    edge_to_faces[edge].push_back(fi);
                }
            }
            
            // Find non-manifold edges (used by >2 faces)
            for (const auto& [edge, faces] : edge_to_faces) {
                if (faces.size() > 2) {
                    BOOST_LOG_TRIVIAL(info) << "  Fixing non-manifold edge " << edge.first 
                                             << "-" << edge.second << " (used by " << faces.size() << " faces)";
                    
                    // Split this edge by duplicating vertices for excess faces
                    // Keep first 2 faces with original vertices, duplicate for others
                    for (size_t i = 2; i < faces.size(); ++i) {
                        int face_idx = faces[i];
                        auto& face = mesh.indices[face_idx];
                        
                        // Duplicate both vertices of this edge for this face
                        for (int vi = 0; vi < 3; ++vi) {
                            if (face[vi] == edge.first) {
                                int new_idx = mesh.vertices.size();
                                mesh.vertices.push_back(mesh.vertices[edge.first]);
                                face[vi] = new_idx;
                            } else if (face[vi] == edge.second) {
                                int new_idx = mesh.vertices.size();
                                mesh.vertices.push_back(mesh.vertices[edge.second]);
                                face[vi] = new_idx;
                            }
                        }
                    }
                }
            }
            
            BOOST_LOG_TRIVIAL(info) << "After repair: vertices=" << mesh.vertices.size()
                                     << ", faces=" << mesh.indices.size();
        }
        
        // Fill holes to make mesh watertight
        void fill_holes(indexed_triangle_set& mesh) {
            BOOST_LOG_TRIVIAL(info) << "Filling holes to make watertight...";
            
            auto boundary_loops = find_boundary_loops(mesh);
            
            BOOST_LOG_TRIVIAL(info) << "Found " << boundary_loops.size() << " boundary loops";
            
            for (size_t li = 0; li < boundary_loops.size(); ++li) {
                const auto& loop = boundary_loops[li];
                
                if (loop.size() < 3) {
                    continue;
                }
                
                BOOST_LOG_TRIVIAL(info) << "  Filling loop " << li << " with " << loop.size() << " edges";
                
                if (loop.size() == 3) {
                    // Simple triangle
                    mesh.indices.emplace_back(loop[0], loop[1], loop[2]);
                } else if (loop.size() == 4) {
                    // Quad - split into 2 triangles
                    mesh.indices.emplace_back(loop[0], loop[1], loop[2]);
                    mesh.indices.emplace_back(loop[0], loop[2], loop[3]);
                } else {
                    // Fan triangulation from centroid
                    Vec3f centroid = Vec3f::Zero();
                    for (int v : loop) {
                        centroid += mesh.vertices[v];
                    }
                    centroid /= float(loop.size());
                    
                    int centroid_idx = mesh.vertices.size();
                    mesh.vertices.push_back(centroid);
                    
                    for (size_t i = 0; i < loop.size(); ++i) {
                        int next = (i + 1) % loop.size();
                        
                        // Determine winding
                        Vec3f v0 = mesh.vertices[loop[i]];
                        Vec3f v1 = mesh.vertices[loop[next]];
                        Vec3f edge = v1 - v0;
                        Vec3f to_center = centroid - v0;
                        Vec3f normal = edge.cross(to_center);
                        
                        // Check if normal points outward
                        Vec3f mesh_center = Vec3f::Zero();
                        for (const auto& mv : mesh.vertices) mesh_center += mv;
                        mesh_center /= mesh.vertices.size();
                        
                        Vec3f to_mesh_center = mesh_center - v0;
                        
                        if (normal.dot(to_mesh_center) > 0) {
                            // Points inward, reverse winding
                            mesh.indices.emplace_back(loop[i], centroid_idx, loop[next]);
                        } else {
                            mesh.indices.emplace_back(loop[i], loop[next], centroid_idx);
                        }
                    }
                }
            }
            
            BOOST_LOG_TRIVIAL(info) << "Hole filling complete";
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
            if (initial_seeds.empty() || iterations <= 0)
                return initial_seeds;

            std::vector<Vec3d> seeds = initial_seeds;
            BoundingBoxf3 expanded_bounds = expand_bounds(bounds, BOUNDARY_MARGIN_PERCENT);
            auto bounding_planes = build_bounding_planes(expanded_bounds);
            AABBMesh aabb(mesh);

            BOOST_LOG_TRIVIAL(info) << "Lloyd relaxation: " << iterations << " iterations on "
                                     << seeds.size() << " seeds";

            for (int iter = 0; iter < iterations; ++iter) {
                DelaunayDiagram diagram(seeds);
                std::vector<Vec3d> new_seeds;
                new_seeds.reserve(seeds.size());

                for (size_t i = 0; i < seeds.size(); ++i) {
                    VoronoiCellData cell;
                    if (compute_voronoi_cell_data(cell, diagram, int(i), bounding_planes, &mesh)) {
                        Vec3d centroid = cell.centroid;
                        if (!mesh.vertices.empty() && !is_point_inside_mesh(aabb, centroid)) {
                            centroid = seeds[i];
                        }
                        new_seeds.push_back(centroid);
                    } else {
                        new_seeds.push_back(seeds[i]);
                    }
                }

                seeds = std::move(new_seeds);
                BOOST_LOG_TRIVIAL(info) << "Lloyd iteration " << (iter + 1) << "/" << iterations << " complete";
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

        BoundingBoxf3 bbox;
        std::vector<Vec3d> seed_points = prepare_seed_points(input_mesh, config, &bbox);
        if (seed_points.empty()) {
            BOOST_LOG_TRIVIAL(error) << "VoronoiMesh::generate() - Failed to prepare seed points";
            return nullptr;
        }

        if (config.progress_callback && !config.progress_callback(10))
            return nullptr;

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

        // Generate weights for weighted Voronoi if enabled and not provided
        Config working_config = config;  // Make a mutable copy
        if (working_config.use_weighted_cells) {
            if (working_config.cell_weights.empty() || working_config.cell_weights.size() != seed_points.size()) {
                BOOST_LOG_TRIVIAL(info) << "VoronoiMesh::generate() - Weighted Voronoi enabled, auto-generating weights for " 
                                         << seed_points.size() << " seeds";
                BOOST_LOG_TRIVIAL(info) << "  Density center: (" << working_config.density_center.transpose() << ")";
                BOOST_LOG_TRIVIAL(info) << "  Density falloff: " << working_config.density_falloff;
                working_config.cell_weights = generate_density_weights(
                    seed_points, 
                    working_config.density_center, 
                    working_config.density_falloff
                );
                BOOST_LOG_TRIVIAL(info) << "  Generated " << working_config.cell_weights.size() << " weights "
                                         << "(min: " << *std::min_element(working_config.cell_weights.begin(), working_config.cell_weights.end())
                                         << ", max: " << *std::max_element(working_config.cell_weights.begin(), working_config.cell_weights.end()) << ")";
            } else {
                BOOST_LOG_TRIVIAL(info) << "VoronoiMesh::generate() - Using " << working_config.cell_weights.size() 
                                         << " custom weights";
            }
        } else {
            BOOST_LOG_TRIVIAL(info) << "VoronoiMesh::generate() - Standard (unweighted) Voronoi mode";
        }

        // Step 3: Create Voronoi cells (polyhedral cell-based approach)
        auto result = std::make_unique<indexed_triangle_set>();

        BOOST_LOG_TRIVIAL(info) << "VoronoiMesh::generate() - Creating polyhedral Voronoi cells";
        result = tessellate_voronoi_cells(seed_points, bbox, working_config, &input_mesh);
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

    // Sphere-specific seed generation for surface patterns
    static std::vector<Vec3d> generate_sphere_surface_seeds(
        const indexed_triangle_set& sphere_mesh,
        int num_seeds,
        int random_seed)
    {
        BOOST_LOG_TRIVIAL(info) << "Generating seeds ON sphere surface using Fibonacci sphere";
        
        // Calculate sphere center and radius
        BoundingBoxf3 bbox;
        for (const auto& v : sphere_mesh.vertices) {
            bbox.merge(v.cast<double>());
        }
        
        Vec3d center = (bbox.min + bbox.max) * 0.5;
        double radius = (bbox.max - bbox.min).norm() * 0.5;
        
        BOOST_LOG_TRIVIAL(info) << "Sphere center: " << center.transpose() 
                                 << ", radius: " << radius;
        
        std::vector<Vec3d> seeds;
        seeds.reserve(num_seeds);
        
        // Use Fibonacci sphere for uniform distribution
        double phi = M_PI * (3.0 - std::sqrt(5.0));  // Golden angle in radians
        
        for (int i = 0; i < num_seeds; ++i) {
            double y = 1.0 - (i / double(num_seeds - 1)) * 2.0;  // y goes from 1 to -1
            double r = std::sqrt(1.0 - y * y);  // radius at y
            
            double theta = phi * i;  // golden angle increment
            
            double x = std::cos(theta) * r;
            double z = std::sin(theta) * r;
            
            // Scale to actual sphere radius and translate to center
            Vec3d point(x * radius, y * radius, z * radius);
            point += center;
            
            seeds.push_back(point);
        }
        
        BOOST_LOG_TRIVIAL(info) << "Generated " << seeds.size() << " surface seeds";
        return seeds;
    }

    // Sphere-specific seed generation for volumetric patterns
    static std::vector<Vec3d> generate_sphere_volume_seeds(
        const indexed_triangle_set& sphere_mesh,
        int num_seeds,
        int random_seed)
    {
        BOOST_LOG_TRIVIAL(info) << "Generating seeds INSIDE sphere volume";
        
        // Calculate sphere center and radius
        BoundingBoxf3 bbox;
        for (const auto& v : sphere_mesh.vertices) {
            bbox.merge(v.cast<double>());
        }
        
        Vec3d center = (bbox.min + bbox.max) * 0.5;
        double radius = (bbox.max - bbox.min).norm() * 0.5;
        
        std::mt19937 gen(random_seed);
        std::uniform_real_distribution<double> dist_theta(0.0, 2.0 * M_PI);
        std::uniform_real_distribution<double> dist_phi(0.0, M_PI);
        std::uniform_real_distribution<double> dist_u(0.0, 1.0);
        
        AABBMesh aabb(sphere_mesh);
        std::vector<Vec3d> seeds;
        seeds.reserve(num_seeds);
        
        int attempts = 0;
        int max_attempts = num_seeds * 100;
        
        while (seeds.size() < size_t(num_seeds) && attempts < max_attempts) {
            attempts++;
            
            // Generate point uniformly within sphere using spherical coordinates
            // Use cubic root of uniform random for radial to get uniform volume distribution
            double u = dist_u(gen);
            double r = radius * std::cbrt(u);  // Cubic root for uniform volume
            double theta = dist_theta(gen);
            double phi = dist_phi(gen);
            
            // Convert to Cartesian
            double x = r * std::sin(phi) * std::cos(theta);
            double y = r * std::sin(phi) * std::sin(theta);
            double z = r * std::cos(phi);
            
            Vec3d point = center + Vec3d(x, y, z);
            
            // Verify it's actually inside the mesh
            if (is_point_inside_mesh(aabb, point)) {
                seeds.push_back(point);
            }
        }
        
        BOOST_LOG_TRIVIAL(info) << "Generated " << seeds.size() << " volume seeds in " 
                                 << attempts << " attempts";
        
        if (seeds.size() < size_t(num_seeds * 0.9)) {
            BOOST_LOG_TRIVIAL(warning) << "Only generated " << seeds.size() << " seeds, "
                                        << "target was " << num_seeds;
        }
        
        return seeds;
    }

    std::vector<Vec3d> VoronoiMesh::generate_seed_points(
        const indexed_triangle_set& mesh,
        const Config& config)
    {
        // Detect if mesh is sphere-like
        BoundingBoxf3 bbox;
        for (const auto& v : mesh.vertices) {
            bbox.merge(v.cast<double>());
        }
        
        Vec3d size = bbox.size();
        double aspect_ratio = std::max(std::max(size.x(), size.y()), size.z()) / 
                              std::min(std::min(size.x(), size.y()), size.z());
        
        bool is_sphere_like = (aspect_ratio < 1.5);  // Roughly spherical
        
        if (is_sphere_like) {
            BOOST_LOG_TRIVIAL(info) << "Detected sphere-like mesh (aspect ratio: " 
                                     << aspect_ratio << ")";
            
            switch (config.seed_type) {
            case SeedType::Surface:
                return generate_sphere_surface_seeds(mesh, config.num_seeds, config.random_seed);
            case SeedType::Random:
                return generate_sphere_volume_seeds(mesh, config.num_seeds, config.random_seed);
            default:
                // Fall through to regular generation
                break;
            }
        }
        
        // Regular generation for non-spherical meshes
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
        
        BOOST_LOG_TRIVIAL(info) << "generate_grid_seeds() - Mesh bounding box: min=" 
                                 << bbox.min.transpose() << ", max=" << bbox.max.transpose();

        // **CRITICAL FIX**: Shrink bbox by 10% on each side to ensure seeds are WELL INSIDE
        // This avoids unreliable point-in-mesh tests and ensures seeds aren't on/near boundaries
        Vec3d shrink = bbox.size() * 0.1;  // 10% shrink on each axis
        bbox.min += shrink;
        bbox.max -= shrink;
        
        BOOST_LOG_TRIVIAL(info) << "generate_grid_seeds() - Shrunk bbox (10% margin): min=" 
                                 << bbox.min.transpose() << ", max=" << bbox.max.transpose();

        // Calculate grid dimensions
        int dim = std::max(2, int(std::cbrt(double(num_seeds)) + 0.5));
        
        BOOST_LOG_TRIVIAL(info) << "generate_grid_seeds() - Grid dimensions: " 
                                 << dim << "x" << dim << "x" << dim << " = " << (dim*dim*dim) << " points";

        Vec3d size = bbox.size();
        Vec3d step = size / double(dim - 1);  // Space between grid points

        seeds.reserve(dim * dim * dim);

        // Generate grid WITHOUT inside test (seeds are guaranteed inside due to shrunk bbox)
        for (int x = 0; x < dim; ++x) {
            for (int y = 0; y < dim; ++y) {
                for (int z = 0; z < dim; ++z) {
                    Vec3d point = bbox.min + Vec3d(x * step.x(), y * step.y(), z * step.z());
                    seeds.push_back(point);
                }
            }
        }

        BOOST_LOG_TRIVIAL(info) << "generate_grid_seeds() - Generated " << seeds.size() << " seeds";

        // If we have too many seeds, uniformly sample
        if (seeds.size() > size_t(num_seeds)) {
            std::vector<Vec3d> sampled;
            sampled.reserve(num_seeds);
            
            int step_size = std::max(1, int(seeds.size()) / num_seeds);
            for (size_t i = 0; i < seeds.size() && sampled.size() < size_t(num_seeds); i += step_size) {
                sampled.push_back(seeds[i]);
            }
            
            seeds = sampled;
            BOOST_LOG_TRIVIAL(info) << "generate_grid_seeds() - Sampled down to " << seeds.size() << " seeds";
        }

        return seeds;
    }

    // PHASE 4: Surface-based seeding - place seeds offset inward from mesh surface
    // This guarantees coverage and avoids inside test failures
    std::vector<Vec3d> VoronoiMesh::generate_surface_seeds(
        const indexed_triangle_set& mesh,
        int num_seeds,
        int random_seed)
    {
        std::vector<Vec3d> seeds;
        
        if (mesh.indices.empty() || num_seeds <= 0) {
            return seeds;
        }
        
        BOOST_LOG_TRIVIAL(info) << "generate_surface_seeds() - Generating " << num_seeds 
                                 << " surface seeds with inward offset";
        
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
        
        BOOST_LOG_TRIVIAL(info) << "generate_surface_seeds() - Total mesh surface area: " << total_area;
        
        // Compute mesh center for determining inward direction
        Vec3d center = Vec3d::Zero();
        for (const auto& v : mesh.vertices) {
            center += v.cast<double>();
        }
        center /= mesh.vertices.size();
        
        BOOST_LOG_TRIVIAL(info) << "generate_surface_seeds() - Mesh center: " << center.transpose();
        
        // Calculate average edge length for offset distance
        double total_edge_length = 0.0;
        int edge_count = 0;
        for (const auto& face : mesh.indices) {
            const Vec3f& v0 = mesh.vertices[face[0]];
            const Vec3f& v1 = mesh.vertices[face[1]];
            const Vec3f& v2 = mesh.vertices[face[2]];
            
            total_edge_length += (v1 - v0).norm();
            total_edge_length += (v2 - v1).norm();
            total_edge_length += (v0 - v2).norm();
            edge_count += 3;
        }
        double avg_edge = total_edge_length / edge_count;
        double inset_distance = avg_edge * 2.0;  // Offset seeds inside by 2x avg edge length
        
        BOOST_LOG_TRIVIAL(info) << "generate_surface_seeds() - Average edge length: " << avg_edge 
                                 << ", inset distance: " << inset_distance;
        
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
            
            // Compute point on triangle surface
            const auto& face = mesh.indices[triangle_idx];
            const Vec3f& v0 = mesh.vertices[face[0]];
            const Vec3f& v1 = mesh.vertices[face[1]];
            const Vec3f& v2 = mesh.vertices[face[2]];
            
            Vec3d surface_point = (v0 * u + v1 * v + v2 * w).cast<double>();
            
            // Calculate face normal
            Vec3f edge1 = v1 - v0;
            Vec3f edge2 = v2 - v0;
            Vec3d normal = edge1.cross(edge2).normalized().cast<double>();
            
            // Determine if normal points inward or outward
            Vec3d to_center = center - surface_point;
            if (normal.dot(to_center) < 0) {
                normal = -normal;  // Flip to point inward
            }
            
            // Offset point inward
            Vec3d interior_point = surface_point + normal * inset_distance;
            
            seeds.push_back(interior_point);
        }
        
        BOOST_LOG_TRIVIAL(info) << "generate_surface_seeds() - Generated " << seeds.size() 
                                 << " seeds (offset " << inset_distance << " units inward)";
        
        // Verify seeds are reasonable
        if (!seeds.empty()) {
            BOOST_LOG_TRIVIAL(info) << "generate_surface_seeds() - First seed: " << seeds.front().transpose();
            BOOST_LOG_TRIVIAL(info) << "generate_surface_seeds() - Last seed: " << seeds.back().transpose();
        }
        
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
        
        BOOST_LOG_TRIVIAL(info) << "generate_random_seeds() - Original bbox: min=" 
                                 << bbox.min.transpose() << ", max=" << bbox.max.transpose();

        // **CRITICAL FIX**: Shrink bbox to ensure seeds are well inside
        Vec3d shrink = bbox.size() * 0.1;  // 10% shrink
        bbox.min += shrink;
        bbox.max -= shrink;
        
        BOOST_LOG_TRIVIAL(info) << "generate_random_seeds() - Shrunk bbox: min=" 
                                 << bbox.min.transpose() << ", max=" << bbox.max.transpose();

        std::mt19937 gen(random_seed);
        std::uniform_real_distribution<double> dist_x(bbox.min.x(), bbox.max.x());
        std::uniform_real_distribution<double> dist_y(bbox.min.y(), bbox.max.y());
        std::uniform_real_distribution<double> dist_z(bbox.min.z(), bbox.max.z());

        seeds.reserve(num_seeds);

        // Generate seeds WITHOUT inside test
        for (int i = 0; i < num_seeds; ++i) {
            Vec3d point(dist_x(gen), dist_y(gen), dist_z(gen));
            seeds.push_back(point);
        }

        BOOST_LOG_TRIVIAL(info) << "generate_random_seeds() - Generated " << seeds.size() << " random seeds";

        return seeds;
    }

    std::unique_ptr<indexed_triangle_set> VoronoiMesh::tessellate_voronoi_cells(
        const std::vector<Vec3d>& seed_points,
        const BoundingBoxf3& bounds,
        const Config& config,
        const indexed_triangle_set* clip_mesh)
    {
        BOOST_LOG_TRIVIAL(info) << "tessellate_voronoi_cells() - START with " << seed_points.size() << " seeds, hollow_cells=" << config.hollow_cells;
        
        // CRITICAL: Log bounds to verify correctness
        BOOST_LOG_TRIVIAL(info) << "tessellate_voronoi_cells() - Voronoi container bounds: min=" 
                                 << bounds.min.transpose() << ", max=" << bounds.max.transpose();
        BOOST_LOG_TRIVIAL(info) << "tessellate_voronoi_cells() - Container size: " << bounds.size().transpose();

        if (seed_points.empty()) {
            return std::make_unique<indexed_triangle_set>();
        }
        
        // CRITICAL: Verify all seeds are inside bounds
        int outside_count = 0;
        for (const auto& seed : seed_points) {
            if (!bounds.contains(seed)) {
                outside_count++;
                if (outside_count <= 5) {  // Log first few
                    BOOST_LOG_TRIVIAL(error) << "tessellate_voronoi_cells() - Seed OUTSIDE bounds: " 
                                              << seed.transpose();
                }
            }
        }
        
        if (outside_count > 0) {
            BOOST_LOG_TRIVIAL(error) << "tessellate_voronoi_cells() - ERROR: " << outside_count 
                                      << " seeds are OUTSIDE container bounds!";
        }

        if (config.progress_callback && !config.progress_callback(25))
            return nullptr;

        // Check for weighted Voronoi (power diagram)
        if (config.use_weighted_cells && !config.cell_weights.empty() && 
            config.cell_weights.size() == seed_points.size()) {
            BOOST_LOG_TRIVIAL(info) << "tessellate_voronoi_cells() - Using WEIGHTED Voronoi (power diagram) with " 
                                     << config.cell_weights.size() << " weights";
            return tessellate_weighted_voronoi(seed_points, config.cell_weights, bounds, config, clip_mesh);
        } else if (config.use_weighted_cells) {
            // Weighted requested but weights not available - log warning and fall back to standard
            BOOST_LOG_TRIVIAL(warning) << "tessellate_voronoi_cells() - Weighted Voronoi requested but weights not available "
                                        << "(have " << config.cell_weights.size() << " weights, need " << seed_points.size() 
                                        << "). Falling back to STANDARD Voronoi";
        } else {
            BOOST_LOG_TRIVIAL(info) << "tessellate_voronoi_cells() - Using STANDARD (unweighted) Voronoi";
        }

        if (config.hollow_cells) {
            BOOST_LOG_TRIVIAL(info) << "tessellate_voronoi_cells() - Mode: Wireframe (hollow cells)";
            return create_wireframe_from_delaunay(seed_points, bounds, config, clip_mesh);
        }

        if (config.restricted_voronoi) {
            if (clip_mesh && !clip_mesh->indices.empty()) {
                BOOST_LOG_TRIVIAL(info) << "tessellate_voronoi_cells() - Restricted Voronoi on surface";
                return create_wireframe_from_delaunay(seed_points, bounds, config, clip_mesh);
            } else {
                BOOST_LOG_TRIVIAL(warning) << "tessellate_voronoi_cells() - Restricted Voronoi requested without surface mesh";
            }
        }

        BOOST_LOG_TRIVIAL(info) << "tessellate_voronoi_cells() - Using CGAL halfspace intersection for solid cells";
        return tessellate_voronoi_with_cgal(seed_points, bounds, config, clip_mesh);
    }

    // CGAL-based solid cell implementation with optional robust method
    std::unique_ptr<indexed_triangle_set> VoronoiMesh::tessellate_voronoi_with_cgal(
        const std::vector<Vec3d>& seed_points,
        const BoundingBoxf3& bounds,
        const Config& config,
        const indexed_triangle_set* clip_mesh)
    {
        auto result = std::make_unique<indexed_triangle_set>();

        if (seed_points.empty())
            return result;

        if (config.progress_callback && !config.progress_callback(40))
            return nullptr;

        const double bbox_diag = (bounds.max - bounds.min).norm();
        const double abs_eps = std::max(1e-7, 1e-8 * bbox_diag);

        BoundingBoxf3 expanded_bounds = expand_bounds(bounds, BOUNDARY_MARGIN_PERCENT);
        
        // Build Delaunay triangulation
        DelaunayDiagram diagram(seed_points);
        
        // Decide whether to use robust method
        // Use robust method if:
        // 1. Clipping to mesh is requested (requires boolean ops)
        // 2. Cell style requires post-processing
        // 3. User explicitly enabled robust mode
        bool use_robust_method = (clip_mesh != nullptr && config.clip_to_mesh);
        
        // Convert clip_mesh to CGAL mesh if using robust method
        CGALMesh cgal_clip_model;
        if (use_robust_method && clip_mesh != nullptr) {
            if (!indexed_to_surface_mesh(*clip_mesh, cgal_clip_model)) {
                BOOST_LOG_TRIVIAL(warning) << "Failed to convert clip mesh to CGAL format, using standard method";
                use_robust_method = false;
            } else {
                BOOST_LOG_TRIVIAL(info) << "Using robust CGAL-only solid cell generation with mesh clipping";
            }
        }

        const size_t total = seed_points.size();
        size_t generated_cells = 0;

        // Parallel cell generation using OpenMP
        std::vector<VoronoiCellData> cells(seed_points.size());
        std::atomic<bool> cancelled(false);

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 4) if(seed_points.size() > 20)
#endif
        for (int i = 0; i < static_cast<int>(seed_points.size()); ++i) {
            if (cancelled.load())
                continue;
            
            // Progress callback (thread-safe, only for some threads to reduce overhead)
            if (config.progress_callback && i % 10 == 0) {
                int progress = 40 + int(((i + 1) * 40) / seed_points.size());
#ifdef _OPENMP
#pragma omp critical
#endif
                {
                    if (!config.progress_callback(progress))
                        cancelled.store(true);
                }
            }
            
            VoronoiCellData& cell = cells[i];
            bool cell_generated = false;
            
            if (use_robust_method) {
                // Use new robust CGAL-only method
                auto vh = diagram.vertex(i);
                if (vh != Delaunay::Vertex_handle()) {
                    const CGALMesh* clip_ptr = (clip_mesh != nullptr) ? &cgal_clip_model : nullptr;
                    cell_generated = compute_solid_voronoi_cell_robust(
                        cell, diagram.dt, i, vh, bounds, clip_ptr
                    );
                }
            } else {
                // Use legacy halfspace intersection method
                auto bounding_planes = build_bounding_planes(expanded_bounds);
                const indexed_triangle_set* clipping_mesh = (clip_mesh && config.clip_to_mesh) ? clip_mesh : nullptr;
                cell_generated = compute_voronoi_cell_data(
                    cell, diagram, i, bounding_planes, clipping_mesh
                );
            }
        }

        // Check for cancellation
        if (cancelled.load())
            return nullptr;

        // Merge cells into result (serial)
        for (const auto& cell : cells) {
            if (cell.geometry.vertices.empty())
                continue;
            
            size_t vertex_offset = result->vertices.size();
            result->vertices.insert(result->vertices.end(), 
                                   cell.geometry.vertices.begin(), 
                                   cell.geometry.vertices.end());
            for (const auto& tri : cell.geometry.indices) {
                result->indices.emplace_back(
                    tri(0) + vertex_offset,
                    tri(1) + vertex_offset,
                    tri(2) + vertex_offset
                );
            }
            generated_cells++;
        }

        if (generated_cells == 0) {
            BOOST_LOG_TRIVIAL(warning) << "tessellate_voronoi_with_cgal() - No Voronoi cells were generated.";
            return result;
        }

        BOOST_LOG_TRIVIAL(info) << "tessellate_voronoi_with_cgal() - Generated " << generated_cells
                                << " cells (vertices=" << result->vertices.size()
                                << ", faces=" << result->indices.size() << ")"
                                << " using " << (use_robust_method ? "robust" : "standard") << " method";

        remove_degenerate_faces(*result);
        weld_vertices(*result, WELD_DISTANCE);
        remove_degenerate_faces(*result);

        if (!config.hollow_cells && config.wall_thickness > 0.0f) {
            create_hollow_cells(*result, config.wall_thickness);
        }

        if (!config.hollow_cells && config.cell_style != CellStyle::Pure) {
            apply_cell_styling(*result, config);
        }

        return result;
    }


    // ========== WEIGHTED VORONOI (POWER DIAGRAM) IMPLEMENTATION ==========

    // Helper: Compute a single power cell from Regular Triangulation
    namespace {
        bool compute_power_cell(
            VoronoiCellData& cell,
            const RT& rt,
            RT::Vertex_handle vh,
            const std::vector<Plane_3>& bounding_planes,
            const indexed_triangle_set* clip_mesh,
            const Config& config)
        {
            if (rt.is_infinite(vh))
                return false;
            
            // Get the weighted point for this vertex
            auto wp_current = vh->point();
            auto p_current = wp_current.point();
            double w_current = wp_current.weight();
            
            // Start with bounding planes
            std::vector<Plane_3> planes = bounding_planes;
            
            // Get all finite adjacent vertices (Delaunay neighbors in the weighted sense)
            std::vector<RT::Vertex_handle> neighbors;
            rt.finite_adjacent_vertices(vh, std::back_inserter(neighbors));
            
            std::unordered_set<int> neighbor_ids;
            
            for (auto nh : neighbors) {
                if (rt.is_infinite(nh))
                    continue;
                    
                // Get neighbor's weighted point
                auto wp_neighbor = nh->point();
                auto p_neighbor = wp_neighbor.point();
                double w_neighbor = wp_neighbor.weight();
                
                // Skip degenerate case
                if (p_current == p_neighbor)
                    continue;
                
                // Compute power bisector plane
                // Power bisector: |x - p1|² - w1 = |x - p2|² - w2
                // Simplifies to: 2(p2 - p1)·x = |p2|² - |p1|² + w1 - w2
                
                Vector_3 normal = p_neighbor - p_current;
                
                // Calculate d such that the plane equation is: normal·x = d
                double p1_sq = CGAL::squared_distance(Point_3(CGAL::ORIGIN), p_current);
                double p2_sq = CGAL::squared_distance(Point_3(CGAL::ORIGIN), p_neighbor);
                double d = (p2_sq - p1_sq + w_current - w_neighbor) / 2.0;
                
                // Create plane through a point such that normal·point = d
                double normal_sq_len = normal.squared_length();
                if (normal_sq_len < 1e-12)
                    continue;
                    
                Point_3 plane_point = Point_3(CGAL::ORIGIN) + normal * (d / normal_sq_len);
                Plane_3 bisector(plane_point, normal);
                
                // Ensure orientation: p_current should be on the "kept" side
                // Orient the plane so that p_current is on the positive side
                if (bisector.oriented_side(p_current) == CGAL::ON_NEGATIVE_SIDE) {
                    bisector = bisector.opposite();
                }
                
                planes.push_back(bisector);
                
                // Track neighbor
                int nid = nh->info();
                if (nid >= 0) {
                    neighbor_ids.insert(nid);
                }
            }
            
            cell.neighbor_ids.assign(neighbor_ids.begin(), neighbor_ids.end());
            
            if (planes.size() < 4)
                return false;
            
            // Use CGAL halfspace intersection (same as compute_voronoi_cell_data)
            Polyhedron poly;
            if (!CGAL::halfspace_intersection_3(planes.begin(), planes.end(), poly, p_current))
                return false;
            
            if (poly.is_empty() || poly.size_of_vertices() < 4)
                return false;
            
            // Extract geometry from polyhedron
            cell.face_areas.clear();
            cell.face_normals.clear();
            cell.face_vertex_counts.clear();
            
            for (auto fit = poly.facets_begin(); fit != poly.facets_end(); ++fit) {
                std::vector<Vec3d> polygon_points;
                auto h = fit->facet_begin();
                auto h_end = h;
                do {
                    const auto& p = h->vertex()->point();
                    polygon_points.emplace_back(p.x(), p.y(), p.z());
                    ++h;
                } while (h != h_end);
                
                cell.face_vertex_counts.push_back(static_cast<int>(polygon_points.size()));
                
                if (polygon_points.size() >= 3) {
                    Vec3d v0 = polygon_points[0];
                    Vec3d v1 = polygon_points[1];
                    Vec3d v2 = polygon_points[2];
                    Vec3d n = (v1 - v0).cross(v2 - v0);
                    double area = 0.5 * n.norm();
                    cell.face_areas.push_back(area);
                    if (area > 1e-10) {
                        cell.face_normals.push_back(n.normalized());
                    } else {
                        cell.face_normals.push_back(Vec3d(0, 0, 1));
                    }
                }
            }
            
            // Convert polyhedron to triangle mesh
            for (auto fit = poly.facets_begin(); fit != poly.facets_end(); ++fit) {
                std::vector<int> face_verts;
                auto h = fit->facet_begin();
                auto h_end = h;
                do {
                    const auto& p = h->vertex()->point();
                    // Find or add vertex
                    int vidx = -1;
                    for (size_t i = 0; i < cell.geometry.vertices.size(); ++i) {
                        Vec3f diff = cell.geometry.vertices[i] - Vec3f(p.x(), p.y(), p.z());
                        if (diff.squaredNorm() < 1e-10) {
                            vidx = i;
                            break;
                        }
                    }
                    if (vidx < 0) {
                        vidx = cell.geometry.vertices.size();
                        cell.geometry.vertices.emplace_back(
                            static_cast<float>(p.x()),
                            static_cast<float>(p.y()),
                            static_cast<float>(p.z())
                        );
                    }
                    face_verts.push_back(vidx);
                    ++h;
                } while (h != h_end);
                
                // Triangulate face
                for (size_t i = 1; i + 1 < face_verts.size(); ++i) {
                    cell.geometry.indices.emplace_back(
                        face_verts[0],
                        face_verts[i],
                        face_verts[i + 1]
                    );
                }
            }
            
            // Compute centroid
            cell.centroid = Vec3d::Zero();
            for (const auto& v : cell.geometry.vertices) {
                cell.centroid += v.cast<double>();
            }
            if (!cell.geometry.vertices.empty()) {
                cell.centroid /= static_cast<double>(cell.geometry.vertices.size());
            }
            
            cell.seed_index = vh->info();
            cell.seed_point = p_current;
            
            return true;
        }
    } // anonymous namespace

    // Weighted Voronoi (Power Diagram) using Regular Triangulation
    std::unique_ptr<indexed_triangle_set> VoronoiMesh::tessellate_weighted_voronoi(
        const std::vector<Vec3d>& seed_points,
        const std::vector<double>& weights,
        const BoundingBoxf3& bounds,
        const Config& config,
        const indexed_triangle_set* clip_mesh)
    {
        auto result = std::make_unique<indexed_triangle_set>();
        
        if (seed_points.empty() || weights.size() != seed_points.size()) {
            BOOST_LOG_TRIVIAL(error) << "tessellate_weighted_voronoi() - Invalid input: "
                                      << seed_points.size() << " seeds, " << weights.size() << " weights";
            return result;
        }
        
        BOOST_LOG_TRIVIAL(info) << "tessellate_weighted_voronoi() - Building power diagram with " 
                                 << seed_points.size() << " weighted seeds";
        
        // Build Regular Triangulation (weighted Delaunay)
        RT rt;
        std::vector<RT::Vertex_handle> vertex_handles;
        vertex_handles.reserve(seed_points.size());
        
        for (size_t i = 0; i < seed_points.size(); ++i) {
            Weighted_point wp(
                Bare_point(seed_points[i].x(), seed_points[i].y(), seed_points[i].z()),
                weights[i]  // weight = radius² (power distance parameter)
            );
            auto vh = rt.insert(wp);
            vh->info() = static_cast<int>(i);
            vertex_handles.push_back(vh);
        }
        
        int num_hidden = 0;
        for (auto vh : vertex_handles) {
            if (rt.is_infinite(vh))
                num_hidden++;
        }
        
        BOOST_LOG_TRIVIAL(info) << "tessellate_weighted_voronoi() - Regular triangulation built: " 
                                 << rt.number_of_vertices() << " vertices, "
                                 << num_hidden << " hidden by weights";
        
        BoundingBoxf3 expanded_bounds = expand_bounds(bounds, BOUNDARY_MARGIN_PERCENT);
        auto bounding_planes = build_bounding_planes(expanded_bounds);
        
        // Generate power cells (with parallel processing)
        std::vector<VoronoiCellData> cells(seed_points.size());
        std::atomic<bool> cancelled(false);
        
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 4) if(seed_points.size() > 20)
#endif
        for (int i = 0; i < static_cast<int>(seed_points.size()); ++i) {
            if (cancelled.load())
                continue;
                
            if (config.progress_callback && i % 10 == 0) {
                int progress = 40 + int(((i + 1) * 40) / seed_points.size());
#ifdef _OPENMP
#pragma omp critical
#endif
                {
                    if (!config.progress_callback(progress))
                        cancelled.store(true);
                }
            }
            
            auto vh = vertex_handles[i];
            if (rt.is_infinite(vh)) {
                BOOST_LOG_TRIVIAL(trace) << "Seed " << i << " is hidden by power diagram";
                continue;  // This seed is hidden by larger-weight neighbors
            }
            
            // Compute power cell using halfspace intersection
            compute_power_cell(cells[i], rt, vh, bounding_planes, clip_mesh, config);
        }
        
        // Check for cancellation
        if (cancelled.load())
            return nullptr;
        
        // Merge cells into result
        size_t generated_cells = 0;
        for (const auto& cell : cells) {
            if (cell.geometry.vertices.empty())
                continue;
                
            size_t vertex_offset = result->vertices.size();
            result->vertices.insert(result->vertices.end(), 
                                   cell.geometry.vertices.begin(), 
                                   cell.geometry.vertices.end());
            for (const auto& tri : cell.geometry.indices) {
                result->indices.emplace_back(
                    tri(0) + vertex_offset,
                    tri(1) + vertex_offset,
                    tri(2) + vertex_offset
                );
            }
            generated_cells++;
        }
        
        BOOST_LOG_TRIVIAL(info) << "tessellate_weighted_voronoi() - Generated " << generated_cells 
                                 << " power cells (vertices=" << result->vertices.size()
                                 << ", faces=" << result->indices.size() << ")";
        
        // Clean up geometry
        remove_degenerate_faces(*result);
        weld_vertices(*result, WELD_DISTANCE);
        remove_degenerate_faces(*result);
        
        // Apply styling if solid cells
        if (!config.hollow_cells && config.wall_thickness > 0.0f) {
            create_hollow_cells(*result, config.wall_thickness);
        }
        
        if (!config.hollow_cells && config.cell_style != CellStyle::Pure) {
            apply_cell_styling(*result, config);
        }
        
        return result;
    }


    // NEW: Extract Voronoi edges using Delaunay-Voronoi duality (mathematically correct method)
    // Based on: Ledoux (2007), Yan et al. (2016)
    std::unique_ptr<indexed_triangle_set> VoronoiMesh::create_wireframe_from_delaunay(
        const std::vector<Vec3d>& seed_points,
        const BoundingBoxf3& bounds,
        const Config& config,
        const indexed_triangle_set* clip_mesh)
    {
        auto result = std::make_unique<indexed_triangle_set>();

        if (seed_points.empty() || config.edge_thickness <= 0.0f) {
            return result;
        }

        DelaunayDiagram diagram(seed_points);

        BoundingBoxf3 expanded_bounds = expand_bounds(bounds, BOUNDARY_MARGIN_PERCENT);
        const double bbox_diag = (expanded_bounds.max - expanded_bounds.min).norm();
        const double abs_eps = std::max(1e-7, 1e-8 * bbox_diag);
        const double min_edge_length_sq = abs_eps * abs_eps;

        if (config.restricted_voronoi) {
            if (clip_mesh && !clip_mesh->indices.empty()) {
                return create_wireframe_from_restricted_voronoi(diagram, seed_points, *clip_mesh, config, abs_eps);
            } else {
                BOOST_LOG_TRIVIAL(warning) << "create_wireframe_from_delaunay() - Restricted Voronoi requires surface mesh; falling back to full Voronoi";
            }
        }

        Iso_cuboid_3 bbox = make_cuboid(expanded_bounds);
        auto raw_edges = extract_voronoi_edges(diagram, bbox, min_edge_length_sq);

        BOOST_LOG_TRIVIAL(info) << "create_wireframe_from_delaunay() - Extracted " << raw_edges.size() << " raw edges";

        if (clip_mesh && config.clip_to_mesh && !clip_mesh->vertices.empty()) {
            CGALMesh clip_surface;
            if (indexed_to_surface_mesh(*clip_mesh, clip_surface)) {
                raw_edges = clip_edges_to_volume(clip_surface, raw_edges, min_edge_length_sq);
                BOOST_LOG_TRIVIAL(info) << "create_wireframe_from_delaunay() - Edges after clipping: " << raw_edges.size();
            } else {
                BOOST_LOG_TRIVIAL(warning) << "create_wireframe_from_delaunay() - Failed to build CGAL surface mesh for clipping";
            }
        }

        struct Vec3dHash {
            double scale;
            explicit Vec3dHash(double s) : scale(s) {}
            size_t operator()(const Vec3d& v) const {
                int64_t x = int64_t(std::llround(v.x() * scale));
                int64_t y = int64_t(std::llround(v.y() * scale));
                int64_t z = int64_t(std::llround(v.z() * scale));
                return std::hash<int64_t>()(x) ^ (std::hash<int64_t>()(y) << 1) ^ (std::hash<int64_t>()(z) << 2);
            }
        };

        struct Vec3dEqual {
            double eps;
            explicit Vec3dEqual(double e) : eps(e) {}
            bool operator()(const Vec3d& a, const Vec3d& b) const {
                return (a - b).norm() <= eps;
            }
        };

        struct EdgeHash {
            Vec3dHash hasher;
            explicit EdgeHash(double scale) : hasher(scale) {}
            size_t operator()(const std::pair<Vec3d, Vec3d>& e) const {
                return hasher(e.first) ^ (hasher(e.second) << 1);
            }
        };

        struct EdgeEqual {
            Vec3dEqual eq;
            explicit EdgeEqual(double eps) : eq(eps) {}
            bool operator()(const std::pair<Vec3d, Vec3d>& a, const std::pair<Vec3d, Vec3d>& b) const {
                return (eq(a.first, b.first) && eq(a.second, b.second)) ||
                       (eq(a.first, b.second) && eq(a.second, b.first));
            }
        };

        const double hash_scale = 1.0 / abs_eps;
        std::unordered_set<std::pair<Vec3d, Vec3d>, EdgeHash, EdgeEqual> voronoi_edges(
            raw_edges.size() * 2 + 1, EdgeHash(hash_scale), EdgeEqual(abs_eps));

        auto canonical = [&](Vec3d p1, Vec3d p2) {
            auto lt_eps = [abs_eps](double a, double b) { return a + abs_eps < b; };
            auto eq_eps = [abs_eps](double a, double b) { return std::abs(a - b) <= abs_eps; };

            if (lt_eps(p2.x(), p1.x()) ||
                (eq_eps(p1.x(), p2.x()) && lt_eps(p2.y(), p1.y())) ||
                (eq_eps(p1.x(), p2.x()) && eq_eps(p1.y(), p2.y()) && lt_eps(p2.z(), p1.z()))) {
                std::swap(p1, p2);
            }
            return std::make_pair(p1, p2);
        };

        size_t skipped_short = 0;
        for (const auto& edge : raw_edges) {
            Vec3d p1 = to_vec3d(edge.a);
            Vec3d p2 = to_vec3d(edge.b);

            if ((p2 - p1).squaredNorm() < min_edge_length_sq) {
                ++skipped_short;
                continue;
            }

            voronoi_edges.insert(canonical(p1, p2));
        }

        if (skipped_short > 0) {
            BOOST_LOG_TRIVIAL(info) << "create_wireframe_from_delaunay() - Skipped " << skipped_short << " very short edges";
        }

        BOOST_LOG_TRIVIAL(info) << "create_wireframe_from_delaunay() - Unique edges: " << voronoi_edges.size();

        // Step 4: Generate cylinder geometry for each edge
        const float radius = config.edge_thickness * 0.5f;
        const int segments = std::max(3, config.edge_segments);
        
        BOOST_LOG_TRIVIAL(info) << "Creating cylindrical struts (radius=" << radius 
                                 << ", segments=" << segments << ")";

        int skipped_degenerate = 0;
        
        for (const auto& edge : voronoi_edges) {
            Vec3d p1 = edge.first;
            Vec3d p2 = edge.second;
            
            Vec3d dir = (p2 - p1);
            double len_sq = dir.squaredNorm();
            
            if (len_sq < MIN_EDGE_LENGTH_SQ) {
                skipped_degenerate++;
                continue;
            }
            
            dir.normalize();
            
            // Robust perpendicular frame
            Vec3d perp1 = dir.cross(std::abs(dir.z()) < 0.9 ? Vec3d(0, 0, 1) : Vec3d(1, 0, 0));
            if (perp1.squaredNorm() < 1e-12) {
                perp1 = dir.cross(Vec3d(0, 1, 0));
            }
            perp1.normalize();
            Vec3d perp2 = dir.cross(perp1).normalized();
            
            size_t base_idx = result->vertices.size();
            
            // Ring at p1
            for (int i = 0; i < segments; ++i) {
                float angle = 2.0f * M_PI * i / segments;
                float x = radius * std::cos(angle);
                float y = radius * std::sin(angle);
                Vec3d offset = perp1 * x + perp2 * y;
                result->vertices.emplace_back((p1 + offset).cast<float>());
            }
            
            // Ring at p2
            for (int i = 0; i < segments; ++i) {
                float angle = 2.0f * M_PI * i / segments;
                float x = radius * std::cos(angle);
                float y = radius * std::sin(angle);
                Vec3d offset = perp1 * x + perp2 * y;
                result->vertices.emplace_back((p2 + offset).cast<float>());
            }
            
            // Tube body
            for (int i = 0; i < segments; ++i) {
                int next = (i + 1) % segments;
                int i0 = base_idx + i;
                int i1 = base_idx + next;
                int i2 = base_idx + segments + i;
                int i3 = base_idx + segments + next;
                result->indices.emplace_back(i0, i2, i1);
                result->indices.emplace_back(i1, i2, i3);
            }
            
            // Optional caps
            if (config.edge_caps) {
                int cap1_center = result->vertices.size();
                result->vertices.emplace_back(p1.cast<float>());
                for (int i = 0; i < segments; ++i) {
                    int next = (i + 1) % segments;
                    result->indices.emplace_back(cap1_center, base_idx + i, base_idx + next);
                }
                
                int cap2_center = result->vertices.size();
                result->vertices.emplace_back(p2.cast<float>());
                for (int i = 0; i < segments; ++i) {
                    int next = (i + 1) % segments;
                    result->indices.emplace_back(cap2_center, base_idx + segments + next, base_idx + segments + i);
                }
            }
        }

        if (skipped_degenerate > 0) {
            BOOST_LOG_TRIVIAL(info) << "Skipped " << skipped_degenerate << " degenerate edges";
        }

        BOOST_LOG_TRIVIAL(info) << "Created wireframe: " << result->vertices.size() 
                                 << " vertices, " << result->indices.size() << " faces";

        // Cleanup
        remove_degenerate_faces(*result);
        weld_vertices(*result, radius * 0.1f);
        remove_degenerate_faces(*result);
        
        if (clip_mesh && !clip_mesh->vertices.empty() && config.clip_to_mesh) {
            BOOST_LOG_TRIVIAL(info) << "Clipping wireframe to mesh boundary...";
            
            // Improved clipping: clip edge segments before generating cylinders would be better,
            // but for now use the existing mesh-based clipping with improved tolerance
            clip_wireframe_to_mesh_improved(*result, *clip_mesh, abs_eps);
            remove_degenerate_faces(*result);
        }
        
        BOOST_LOG_TRIVIAL(info) << "Final wireframe: " << result->vertices.size() 
                                 << " vertices, " << result->indices.size() << " faces";

        return result;
    }

    // Improved wireframe clipping that respects edge structure
    void VoronoiMesh::clip_wireframe_to_mesh_improved(
        indexed_triangle_set& wireframe,
        const indexed_triangle_set& clip_mesh,
        double epsilon)
    {
        if (wireframe.vertices.empty() || clip_mesh.vertices.empty()) {
            return;
        }

        BOOST_LOG_TRIVIAL(info) << "clip_wireframe_to_mesh_improved() - Clipping " 
                                 << wireframe.vertices.size() << " vertices";

        // Build AABB tree for inside/outside testing
        AABBMesh aabb(clip_mesh);
        
        // Mark vertices that are inside or on the surface
        std::vector<bool> keep_vertex(wireframe.vertices.size(), false);
        int kept_count = 0;
        
        for (size_t i = 0; i < wireframe.vertices.size(); ++i) {
            Vec3d pt = wireframe.vertices[i].cast<double>();
            
            // Ray casting for inside test
            Vec3d ray_dir(0.123456, 0.234567, 0.876543); // Arbitrary non-axis-aligned direction
            auto hit = aabb.query_ray_hit(pt, ray_dir);
            
            // Consider point inside if:
            // 1. Ray test says inside
            // 2. OR point is very close to surface (on boundary)
            bool is_inside = hit.is_inside();
            
            if (!is_inside && hit.distance() != -1) {
                // Check if point is on surface (within epsilon)
                double dist_to_surface = std::abs(hit.distance());
                if (dist_to_surface < epsilon * 10.0) { // Slightly larger tolerance for boundary
                    is_inside = true;
                }
            }
            
            if (is_inside) {
                keep_vertex[i] = true;
                kept_count++;
            }
        }
        
        BOOST_LOG_TRIVIAL(info) << "Keeping " << kept_count << " / " 
                                 << wireframe.vertices.size() << " vertices";

        // Build vertex remapping
        std::vector<int> vertex_map(wireframe.vertices.size(), -1);
        std::vector<Vec3f> new_vertices;
        new_vertices.reserve(kept_count);
        
        for (size_t i = 0; i < wireframe.vertices.size(); ++i) {
            if (keep_vertex[i]) {
                vertex_map[i] = new_vertices.size();
                new_vertices.push_back(wireframe.vertices[i]);
            }
        }
        
        // Keep faces where ALL vertices are inside
        // (Partial clipping would require splitting faces, which is complex)
        std::vector<Vec3i> new_faces;
        int dropped_faces = 0;
        
        for (const auto& face : wireframe.indices) {
            if (keep_vertex[face[0]] && keep_vertex[face[1]] && keep_vertex[face[2]]) {
                new_faces.emplace_back(
                    vertex_map[face[0]],
                    vertex_map[face[1]],
                    vertex_map[face[2]]
                );
            } else {
                dropped_faces++;
            }
        }
        
        BOOST_LOG_TRIVIAL(info) << "Dropped " << dropped_faces << " faces with outside vertices";
        
        wireframe.vertices = new_vertices;
        wireframe.indices = new_faces;
    }

    // Extract wireframe edges directly from Voro++ cells - creates independent cylinders per edge
            if (vl.start()) do {
            if (con.compute_cell(cell, vl)) {
                // Get cell vertices
                std::vector<double> verts;
                cell.vertices(vl.x(), vl.y(), vl.z(), verts);

                std::vector<Vec3d> cell_vertices;
                for (size_t i = 0; i + 2 < verts.size(); i += 3) {
                    cell_vertices.emplace_back(verts[i], verts[i + 1], verts[i + 2]);
                }

                // Get face structure
                std::vector<int> face_vertex_indices;
                std::vector<int> face_orders;
                cell.face_vertices(face_vertex_indices);
                cell.face_orders(face_orders);

                // Extract edges from face polygons
                int vertex_idx = 0;
                for (int face_order : face_orders) {
                    for (int v = 0; v < face_order; ++v) {
                        int v_next = (v + 1) % face_order;
                        int vi1 = face_vertex_indices[vertex_idx + v];
                        int vi2 = face_vertex_indices[vertex_idx + v_next];

                        if (vi1 >= 0 && vi1 < (int)cell_vertices.size() && 
                            vi2 >= 0 && vi2 < (int)cell_vertices.size()) {
                            
                            Vec3d p1 = cell_vertices[vi1];
                            Vec3d p2 = cell_vertices[vi2];

                            // Normalize edge ordering
                            if (p1.x() > p2.x() || (p1.x() == p2.x() && p1.y() > p2.y()) || 
                                (p1.x() == p2.x() && p1.y() == p2.y() && p1.z() > p2.z())) {
                                std::swap(p1, p2);
                            }
                            
                            unique_edges.insert(std::make_pair(p1, p2));
                        }
                    }
                    vertex_idx += face_order;
                }
            }
        } while (vl.inc());

        BOOST_LOG_TRIVIAL(info) << "Found " << unique_edges.size() << " unique edges";

        if (unique_edges.empty()) {
            BOOST_LOG_TRIVIAL(error) << "No edges extracted!";
            return result;
        }

        // Generate cylindrical strut for each edge
        const float radius = config.edge_thickness * 0.5f;
        const int segments = std::max(3, config.edge_segments);
        
        BOOST_LOG_TRIVIAL(info) << "Creating cylindrical struts (radius=" << radius 
                                 << ", segments=" << segments << ")";

        for (const auto& edge : unique_edges) {
            Vec3d p1 = edge.first;
            Vec3d p2 = edge.second;
            
            Vec3d dir = (p2 - p1);
            float length = dir.norm();
            
            if (length < 1e-6f) continue;
            
            dir /= length;  // Normalize
            
            // Create perpendicular frame
            Vec3d perp1, perp2;
            if (std::abs(dir.z()) < 0.9) {
                perp1 = dir.cross(Vec3d(0, 0, 1)).normalized();
            } else {
                perp1 = dir.cross(Vec3d(1, 0, 0)).normalized();
            }
            perp2 = dir.cross(perp1).normalized();
            
            // Generate cylinder vertices
            size_t base_idx = result->vertices.size();
            
            // Ring at p1
            for (int i = 0; i < segments; ++i) {
                float angle = 2.0f * M_PI * i / segments;
                float x = radius * std::cos(angle);
                float y = radius * std::sin(angle);
                Vec3d offset = perp1 * x + perp2 * y;
                result->vertices.emplace_back((p1 + offset).cast<float>());
            }
            
            // Ring at p2
            for (int i = 0; i < segments; ++i) {
                float angle = 2.0f * M_PI * i / segments;
                float x = radius * std::cos(angle);
                float y = radius * std::sin(angle);
                Vec3d offset = perp1 * x + perp2 * y;
                result->vertices.emplace_back((p2 + offset).cast<float>());
            }
            
            // Create cylinder body (tube connecting the two rings)
            for (int i = 0; i < segments; ++i) {
                int next = (i + 1) % segments;
                
                int i0 = base_idx + i;                      // Current vertex on ring 1
                int i1 = base_idx + next;                   // Next vertex on ring 1
                int i2 = base_idx + segments + i;           // Current vertex on ring 2
                int i3 = base_idx + segments + next;        // Next vertex on ring 2
                
                // Create two triangles for this quad
                result->indices.emplace_back(i0, i2, i1);
                result->indices.emplace_back(i1, i2, i3);
            }
            
            // Create caps at both ends
            // Cap at p1
            int cap1_center = result->vertices.size();
            result->vertices.emplace_back(p1.cast<float>());
            
            for (int i = 0; i < segments; ++i) {
                int next = (i + 1) % segments;
                result->indices.emplace_back(cap1_center, base_idx + i, base_idx + next);
            }
            
            // Cap at p2
            int cap2_center = result->vertices.size();
            result->vertices.emplace_back(p2.cast<float>());
            
            for (int i = 0; i < segments; ++i) {
                int next = (i + 1) % segments;
                result->indices.emplace_back(cap2_center, base_idx + segments + next, base_idx + segments + i);
            }
        }

        BOOST_LOG_TRIVIAL(info) << "Created wireframe: " << result->vertices.size() 
                                 << " vertices, " << result->indices.size() << " faces";

        // Cleanup
        BOOST_LOG_TRIVIAL(info) << "Cleaning up wireframe...";
        
        // Remove degenerate faces first
        remove_degenerate_faces(*result);
        
        // Weld vertices at junctions (where tubes meet)
        weld_vertices(*result, radius * 0.1f);  // Weld within 10% of radius
        
        // Remove any degenerates created by welding
        remove_degenerate_faces(*result);
        
        // Final cleanup
        remove_degenerate_faces(*result);
        
        BOOST_LOG_TRIVIAL(info) << "Final wireframe: " << result->vertices.size() 
                                 << " vertices, " << result->indices.size() << " faces";

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

        float min_dimension = std::min(std::min(cell_bbox.size().x(),
                                                 cell_bbox.size().y()),
                                        cell_bbox.size().z());

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
        auto start_time = std::chrono::high_resolution_clock::now();

        std::vector<Vec3d> seed_points = generate_seed_points(input_mesh, config);
        if (seed_points.empty()) {
            BOOST_LOG_TRIVIAL(error) << "generate_with_stats() - No seed points generated";
            return nullptr;
        }

        BoundingBoxf3 bbox;
        for (const auto& v : input_mesh.vertices)
            bbox.merge(v.cast<double>());

        BoundingBoxf3 expanded_bounds = expand_bounds(bbox, BOUNDARY_MARGIN_PERCENT);
        const indexed_triangle_set* clip_mesh = (config.clip_to_mesh) ? &input_mesh : nullptr;

        auto cells = compute_all_cells(seed_points, expanded_bounds, clip_mesh);
        stats.num_cells = static_cast<int>(cells.size());
        stats.min_cell_volume = std::numeric_limits<float>::max();
        stats.max_cell_volume = 0.0f;
        stats.total_volume = 0.0f;

        for (const auto& cell : cells) {
            stats.min_cell_volume = std::min(stats.min_cell_volume, float(cell.volume));
            stats.max_cell_volume = std::max(stats.max_cell_volume, float(cell.volume));
            stats.total_volume += float(cell.volume);
        }

        if (stats.num_cells > 0)
            stats.avg_cell_volume = stats.total_volume / stats.num_cells;
        else
            stats.min_cell_volume = 0.0f;

        BOOST_LOG_TRIVIAL(info) << "generate_with_stats() - CGAL statistics:";
        BOOST_LOG_TRIVIAL(info) << "  Cells: " << stats.num_cells;
        BOOST_LOG_TRIVIAL(info) << "  Cell volume: " << stats.min_cell_volume << " - "
                                 << stats.max_cell_volume << " (avg: " << stats.avg_cell_volume << ")";
        BOOST_LOG_TRIVIAL(info) << "  Total volume: " << stats.total_volume;

        auto result = generate(input_mesh, config);

        auto end_time = std::chrono::high_resolution_clock::now();
        stats.generation_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

        if (!result || result->vertices.empty())
            return result;

        stats.num_vertices = result->vertices.size();
        stats.num_faces = result->indices.size();

        std::set<std::pair<int, int>> unique_edges;
        for (const auto& face : result->indices) {
            for (int i = 0; i < 3; ++i) {
                int v1 = face[i];
                int v2 = face[(i + 1) % 3];
                auto edge = (v1 < v2) ? std::make_pair(v1, v2) : std::make_pair(v2, v1);
                unique_edges.insert(edge);
            }
        }
        stats.num_edges = static_cast<int>(unique_edges.size());

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
    
    // ========== ADVANCED VORO++ ANALYSIS FUNCTIONS ==========
    
    std::vector<VoronoiMesh::CellInfo> VoronoiMesh::analyze_cells(
        const std::vector<Vec3d>& seed_points,
        const BoundingBoxf3& bounds)
    {
        std::vector<CellInfo> cell_infos;
        if (seed_points.empty())
            return cell_infos;

        BoundingBoxf3 expanded_bounds = expand_bounds(bounds, BOUNDARY_MARGIN_PERCENT);
        auto cells = compute_all_cells(seed_points, expanded_bounds, nullptr);
        std::sort(cells.begin(), cells.end(), [](const VoronoiCellData& a, const VoronoiCellData& b) {
            return a.seed_index < b.seed_index;
        });

        cell_infos.reserve(cells.size());
        for (const auto& cell : cells) {
            CellInfo info;
            info.cell_id = cell.seed_index;
            info.seed_position = to_vec3d(cell.seed_point);
            info.centroid = cell.centroid;
            info.volume = cell.volume;
            info.surface_area = cell.surface_area;
            info.num_faces = static_cast<int>(cell.geometry.indices.size());
            info.num_vertices = static_cast<int>(cell.geometry.vertices.size());

            std::set<std::pair<int, int>> edges;
            for (const auto& face : cell.geometry.indices) {
                for (int i = 0; i < 3; ++i) {
                    int a = face[i];
                    int b = face[(i + 1) % 3];
                    if (a > b) std::swap(a, b);
                    edges.emplace(a, b);
                }
            }
            info.num_edges = static_cast<int>(edges.size());

            info.neighbor_ids = cell.neighbor_ids;
            info.face_areas = cell.face_areas;
            info.face_normals = cell.face_normals;
            info.face_vertex_counts = cell.face_vertex_counts;

            cell_infos.push_back(std::move(info));
        }

        BOOST_LOG_TRIVIAL(info) << "analyze_cells() - Analyzed " << cell_infos.size() << " cells successfully";

        return cell_infos;
    }


    
    VoronoiMesh::CellInfo VoronoiMesh::get_cell_info(
        const std::vector<Vec3d>& seed_points,
        const BoundingBoxf3& bounds,
        int cell_id)
    {
        CellInfo info;
        info.cell_id = cell_id;

        if (cell_id < 0 || cell_id >= static_cast<int>(seed_points.size())) {
            BOOST_LOG_TRIVIAL(error) << "get_cell_info() - Invalid cell_id: " << cell_id;
            return info;
        }

        BoundingBoxf3 expanded_bounds = expand_bounds(bounds, BOUNDARY_MARGIN_PERCENT);
        auto bounding_planes = build_bounding_planes(expanded_bounds);
        DelaunayDiagram diagram(seed_points);

        VoronoiCellData cell;
        if (!compute_voronoi_cell_data(cell, diagram, cell_id, bounding_planes, nullptr)) {
            BOOST_LOG_TRIVIAL(warning) << "get_cell_info() - Failed to compute cell " << cell_id;
            return info;
        }

        info.seed_position = to_vec3d(cell.seed_point);
        info.centroid = cell.centroid;
        info.volume = cell.volume;
        info.surface_area = cell.surface_area;
        info.num_faces = static_cast<int>(cell.geometry.indices.size());
        info.num_vertices = static_cast<int>(cell.geometry.vertices.size());

        std::set<std::pair<int, int>> edges;
        for (const auto& face : cell.geometry.indices) {
            for (int i = 0; i < 3; ++i) {
                int a = face[i];
                int b = face[(i + 1) % 3];
                if (a > b) std::swap(a, b);
                edges.emplace(a, b);
            }
        }
        info.num_edges = static_cast<int>(edges.size());
        info.neighbor_ids = cell.neighbor_ids;
        info.face_areas = cell.face_areas;
        info.face_normals = cell.face_normals;
        info.face_vertex_counts = cell.face_vertex_counts;

        return info;
    }


    
    std::vector<int> VoronoiMesh::find_cell_neighbors(
        const std::vector<Vec3d>& seed_points,
        const BoundingBoxf3& bounds,
        int cell_id)
    {
        std::vector<int> neighbors;

        if (cell_id < 0 || cell_id >= static_cast<int>(seed_points.size())) {
            BOOST_LOG_TRIVIAL(error) << "find_cell_neighbors() - Invalid cell_id: " << cell_id;
            return neighbors;
        }

        DelaunayDiagram diagram(seed_points);
        auto vh = diagram.vertex(cell_id);
        if (vh == Delaunay::Vertex_handle())
            return neighbors;

        std::vector<Delaunay::Vertex_handle> adjacent;
        diagram.dt.finite_adjacent_vertices(vh, std::back_inserter(adjacent));
        for (auto nh : adjacent) {
            if (!diagram.dt.is_infinite(nh)) {
                int nid = nh->info();
                if (nid >= 0)
                    neighbors.push_back(nid);
            }
        }

        std::sort(neighbors.begin(), neighbors.end());
        neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
        return neighbors;
    }


    
    std::vector<Vec3d> VoronoiMesh::compute_cell_centroids(
        const std::vector<Vec3d>& seed_points,
        const BoundingBoxf3& bounds)
    {
        std::vector<Vec3d> centroids(seed_points.size(), Vec3d::Zero());
        if (seed_points.empty())
            return centroids;

        for (size_t i = 0; i < seed_points.size(); ++i)
            centroids[i] = seed_points[i];

        BoundingBoxf3 expanded_bounds = expand_bounds(bounds, BOUNDARY_MARGIN_PERCENT);
        auto cells = compute_all_cells(seed_points, expanded_bounds, nullptr);

        for (const auto& cell : cells) {
            if (cell.seed_index >= 0 && cell.seed_index < static_cast<int>(centroids.size()))
                centroids[cell.seed_index] = cell.centroid;
        }

        BOOST_LOG_TRIVIAL(info) << "compute_cell_centroids() - Computed " << cells.size() << " centroids";
        return centroids;
    }


    
    // ========== ADVANCED OPTIMIZATION FUNCTIONS ==========
    
    std::vector<Vec3d> VoronoiMesh::lloyd_relaxation(
        std::vector<Vec3d> seeds,
        const BoundingBoxf3& bounds,
        int iterations)
    {
        BOOST_LOG_TRIVIAL(info) << "lloyd_relaxation() - Relaxing " << seeds.size()
                                 << " seeds over " << iterations << " iterations";

        if (seeds.empty() || iterations <= 0)
            return seeds;

        BoundingBoxf3 expanded_bounds = expand_bounds(bounds, BOUNDARY_MARGIN_PERCENT);
        auto bounding_planes = build_bounding_planes(expanded_bounds);

        for (int iter = 0; iter < iterations; ++iter) {
            DelaunayDiagram diagram(seeds);
            std::vector<Vec3d> new_seeds;
            new_seeds.reserve(seeds.size());

            for (size_t i = 0; i < seeds.size(); ++i) {
                VoronoiCellData cell;
                if (compute_voronoi_cell_data(cell, diagram, int(i), bounding_planes, nullptr)) {
                    new_seeds.push_back(cell.centroid);
                } else {
                    new_seeds.push_back(seeds[i]);
                }
            }

            seeds = std::move(new_seeds);
            BOOST_LOG_TRIVIAL(info) << "lloyd_relaxation() - Iteration " << (iter + 1)
                                     << "/" << iterations << " complete";
        }

        BOOST_LOG_TRIVIAL(info) << "lloyd_relaxation() - Relaxation complete";
        return seeds;
    }


    
    std::vector<double> VoronoiMesh::generate_density_weights(
        const std::vector<Vec3d>& seeds,
        const Vec3d& dense_point,
        float density_falloff)
    {
        BOOST_LOG_TRIVIAL(info) << "generate_density_weights() - Generating weights for " 
                                 << seeds.size() << " seeds";
        
        std::vector<double> weights;
        weights.reserve(seeds.size());
        
        for (const auto& seed : seeds) {
            double dist = (seed - dense_point).norm();
            
            // Closer to dense_point = smaller weight = smaller cell = higher density
            // weight represents radius², so smaller radius² = smaller cell
            double weight = 1.0 + dist * density_falloff;
            weights.push_back(weight * weight);  // Store radius²
        }
        
        BOOST_LOG_TRIVIAL(info) << "generate_density_weights() - Generated " << weights.size() 
                                 << " weights";
        return weights;
    }
    
    std::vector<Vec3d> VoronoiMesh::optimize_for_load_direction(
        const std::vector<Vec3d>& seeds,
        const Vec3d& load_direction,
        float stretch_factor)
    {
        BOOST_LOG_TRIVIAL(info) << "optimize_for_load_direction() - Optimizing " << seeds.size() 
                                 << " seeds, stretch=" << stretch_factor;
        
        std::vector<Vec3d> optimized;
        optimized.reserve(seeds.size());
        
        Vec3d dir = load_direction.normalized();
        
        for (const auto& seed : seeds) {
            // Decompose seed into parallel and perpendicular components
            double projection = seed.dot(dir);
            Vec3d parallel = dir * projection;
            Vec3d perpendicular = seed - parallel;
            
            // Stretch parallel component by stretch_factor
            Vec3d stretched = parallel * stretch_factor + perpendicular;
            optimized.push_back(stretched);
        }
        
        BOOST_LOG_TRIVIAL(info) << "optimize_for_load_direction() - Optimization complete";
        return optimized;
    }
    
    std::set<int> VoronoiMesh::find_connected_cells(
        const std::vector<Vec3d>& seed_points,
        const BoundingBoxf3& bounds,
        int start_cell_id)
    {
        std::set<int> connected;

        if (start_cell_id < 0 || start_cell_id >= static_cast<int>(seed_points.size())) {
            BOOST_LOG_TRIVIAL(error) << "find_connected_cells() - Invalid start_cell_id: " << start_cell_id;
            return connected;
        }

        DelaunayDiagram diagram(seed_points);
        std::queue<int> to_visit;
        to_visit.push(start_cell_id);
        connected.insert(start_cell_id);

        while (!to_visit.empty()) {
            int current = to_visit.front();
            to_visit.pop();

            auto vh = diagram.vertex(current);
            if (vh == Delaunay::Vertex_handle())
                continue;

            std::vector<Delaunay::Vertex_handle> adjacent;
            diagram.dt.finite_adjacent_vertices(vh, std::back_inserter(adjacent));
            for (auto nh : adjacent) {
                if (!diagram.dt.is_infinite(nh)) {
                    int nid = nh->info();
                    if (nid >= 0 && connected.insert(nid).second)
                        to_visit.push(nid);
                }
            }
        }

        BOOST_LOG_TRIVIAL(info) << "find_connected_cells() - Found " << connected.size() << " connected cells";
        return connected;
    }


    
    bool VoronoiMesh::validate_printability(
        const std::vector<::Slic3r::Vec3d>& seed_points,
        const ::Slic3r::BoundingBoxf3& bounds,
        float min_feature_size,
        std::string& error_message)
    {
        BOOST_LOG_TRIVIAL(info) << "validate_printability() - Validating " << seed_points.size()
                                 << " cells, min_feature_size=" << min_feature_size;

        if (seed_points.empty()) {
            error_message = "No seed points provided";
            return false;
        }

        BoundingBoxf3 expanded_bounds = expand_bounds(bounds, BOUNDARY_MARGIN_PERCENT);
        auto cells = compute_all_cells(seed_points, expanded_bounds, nullptr);

        int too_small_count = 0;
        double min_dimension = std::numeric_limits<double>::max();
        int problematic_cell_id = -1;

        for (const auto& cell : cells) {
            Vec3d size = cell.bbox_max - cell.bbox_min;
            double min_dim = std::min(std::min(size.x(), size.y()), size.z());
            if (min_dim < min_dimension) {
                min_dimension = min_dim;
                problematic_cell_id = cell.seed_index;
            }
            if (min_dim < min_feature_size)
                ++too_small_count;
        }

        if (too_small_count > 0) {
            error_message = "Printability issue: " + std::to_string(too_small_count) +
                           " cells have features smaller than " + std::to_string(min_feature_size) + "mm. " +
                           "Minimum cell dimension found: " + std::to_string(min_dimension) + "mm " +
                           "(cell #" + std::to_string(problematic_cell_id) + "). ";
            BOOST_LOG_TRIVIAL(warning) << "validate_printability() - FAILED: " << error_message;
            return false;
        }

        BOOST_LOG_TRIVIAL(info) << "validate_printability() - PASSED: All cells meet minimum feature size";
        return true;
    }



} // namespace Slic3r
