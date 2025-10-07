#include "VoronoiMesh.hpp"
#include "libslic3r/AABBTreeIndirect.hpp"
#include "libslic3r/MeshBoolean.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include <random>
#include <algorithm>
#include <set>
#include <map>
#include <array>
#include <cmath>
#include <limits>

#include <boost/log/trivial.hpp>

// CGAL headers for 3D Voronoi/Delaunay
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

        // Expand bbox slightly to ensure all cells are within bounds
        Vec3d expansion = bbox.size() * 0.1;
        bbox.min -= expansion;
        bbox.max += expansion;
        BOOST_LOG_TRIVIAL(info) << "VoronoiMesh::generate() - Bounding box computed: min(" << bbox.min.x() << "," << bbox.min.y() << "," << bbox.min.z() << "), max(" << bbox.max.x() << "," << bbox.max.y() << "," << bbox.max.z() << ")";

        if (config.progress_callback && !config.progress_callback(20))
            return nullptr;

        // Step 3: Create wireframe structure from Voronoi edges
        BOOST_LOG_TRIVIAL(info) << "VoronoiMesh::generate() - Creating edge structure, thickness: " << config.edge_thickness << ", shape: " << (int)config.edge_shape << ", segments: " << config.edge_segments;
        auto result = std::make_unique<indexed_triangle_set>();
        create_edge_structure(*result, seed_points, bbox, config.edge_thickness,
                            config.edge_shape, config.edge_segments, config);
        BOOST_LOG_TRIVIAL(info) << "VoronoiMesh::generate() - Edge structure created, vertices: " << (result ? result->vertices.size() : 0) << ", faces: " << (result ? result->indices.size() : 0);

        if (!result || result->vertices.empty()) {
            BOOST_LOG_TRIVIAL(error) << "VoronoiMesh::generate() - Edge structure is empty or null!";
            return nullptr;
        }

        if (config.progress_callback && !config.progress_callback(90))
            return nullptr;

        // Always clip wireframe to input mesh boundary
        BOOST_LOG_TRIVIAL(info) << "VoronoiMesh::generate() - Clipping wireframe to mesh boundary";
        clip_to_mesh_boundary(*result, input_mesh);
        BOOST_LOG_TRIVIAL(info) << "VoronoiMesh::generate() - After clipping: vertices=" << result->vertices.size() << ", faces=" << result->indices.size();

        // Step 4: Finalize progress
        if (config.progress_callback && !config.progress_callback(100))
            return nullptr;

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

        // Calculate grid dimensions
        int dim = std::max(2, int(std::cbrt(double(num_seeds)) + 0.5));

        Vec3d size = bbox.size();
        Vec3d step = size / double(dim - 1);

        seeds.reserve(dim * dim * dim);
        for (int x = 0; x < dim; ++x) {
            for (int y = 0; y < dim; ++y) {
                for (int z = 0; z < dim; ++z) {
                    Vec3d point = bbox.min + Vec3d(x * step.x(), y * step.y(), z * step.z());
                    seeds.push_back(point);
                }
            }
        }

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

        seeds.reserve(num_seeds);
        for (int i = 0; i < num_seeds; ++i) {
            seeds.emplace_back(dist_x(gen), dist_y(gen), dist_z(gen));
        }

        return seeds;
    }

    std::unique_ptr<indexed_triangle_set> VoronoiMesh::tessellate_voronoi(
        const std::vector<Vec3d>& seed_points,
        const BoundingBoxf3& bounds,
        const Config& config,
        const indexed_triangle_set* clip_mesh)
    {
        if (seed_points.empty()) {
            return std::make_unique<indexed_triangle_set>();
        }

        // Need at least 4 non-coplanar points for 3D Delaunay
        if (seed_points.size() < 4) {
            // Add dummy points to ensure we have enough for triangulation
            std::vector<Vec3d> extended_seeds = seed_points;
            Vec3d center = bounds.center();
            Vec3d size = bounds.size() * 0.1;

            while (extended_seeds.size() < 4) {
                extended_seeds.push_back(center + Vec3d(size.x() * (extended_seeds.size() - 2), 0, 0));
            }
            return tessellate_voronoi(extended_seeds, bounds, config, clip_mesh);
        }

        if (config.progress_callback && !config.progress_callback(25))
            return nullptr;

        // Build Delaunay triangulation
        Delaunay dt;
        try {
            // Insert seed points directly without pairs for simpler handling
            for (size_t i = 0; i < seed_points.size(); ++i) {
                const auto& p = seed_points[i];

                // Skip invalid points
                if (std::isnan(p.x()) || std::isnan(p.y()) || std::isnan(p.z()) ||
                    std::isinf(p.x()) || std::isinf(p.y()) || std::isinf(p.z())) {
                    continue;
                }

                auto vh = dt.insert(Point_3(p.x(), p.y(), p.z()));
                if (vh != Delaunay::Vertex_handle()) {
                    vh->info() = static_cast<int>(i);
                }
            }

            if (dt.number_of_vertices() < 4 || !dt.is_valid()) {
                return std::make_unique<indexed_triangle_set>();
            }
        }
        catch (const std::exception&) {
            return std::make_unique<indexed_triangle_set>();
        }

        if (config.progress_callback && !config.progress_callback(40))
            return nullptr;

        // Compute Voronoi cells
        auto result = std::make_unique<indexed_triangle_set>();

        // Pre-allocate space
        result->vertices.reserve(dt.number_of_vertices() * 50);
        result->indices.reserve(dt.number_of_vertices() * 100);

        int processed = 0;
        const int total = static_cast<int>(dt.number_of_vertices());

        // For each Delaunay vertex, compute its dual Voronoi cell
        for (auto vit = dt.finite_vertices_begin(); vit != dt.finite_vertices_end(); ++vit) {
            ++processed;
            if (processed % 5 == 0) {
                int progress = 40 + (processed * 40) / total;
                if (config.progress_callback && !config.progress_callback(progress))
                    return nullptr;
            }

            // Get the seed point position
            Point_3 seed_point = vit->point();
            Vec3d seed_pos(seed_point.x(), seed_point.y(), seed_point.z());

            // Collect all incident tetrahedra
            std::vector<Delaunay::Cell_handle> incident_cells;
            dt.incident_cells(vit, std::back_inserter(incident_cells));

            // Collect Voronoi vertices (circumcenters of tetrahedra)
            std::vector<Point_3> voronoi_verts;
            for (const auto& cell : incident_cells) {
                if (!dt.is_infinite(cell)) {
                    // Get the circumcenter of the cell (this is the Voronoi vertex)
                    Point_3 cc = cell->circumcenter();

                    // Basic bounds check
                    if (cc.x() >= bounds.min.x() - 1.0 && cc.x() <= bounds.max.x() + 1.0 &&
                        cc.y() >= bounds.min.y() - 1.0 && cc.y() <= bounds.max.y() + 1.0 &&
                        cc.z() >= bounds.min.z() - 1.0 && cc.z() <= bounds.max.z() + 1.0) {
                        voronoi_verts.push_back(cc);
                    }
                }
            }

            // Need at least 4 vertices for a 3D cell
            if (voronoi_verts.size() < 4)
                continue;

            // Build convex hull to get Voronoi cell
            try {
                CGALMesh cell_mesh;
                CGAL::convex_hull_3(voronoi_verts.begin(), voronoi_verts.end(), cell_mesh);

                if (cell_mesh.number_of_vertices() < 4 || cell_mesh.number_of_faces() < 4)
                    continue;

                // Convert to indexed triangle set
                indexed_triangle_set cell_its = surface_mesh_to_indexed(cell_mesh);

                if (cell_its.vertices.empty() || cell_its.indices.empty())
                    continue;

                // Apply hollowing if requested
                if (config.hollow_cells) {
                    create_hollow_cells(cell_its, config.wall_thickness);
                }

                // Clip to input mesh if requested
                if (clip_mesh) {
                    try {
                        // Create temporary meshes for boolean operation
                        indexed_triangle_set clipped = cell_its;
                        MeshBoolean::cgal::intersect(clipped, *clip_mesh);

                        if (!clipped.indices.empty()) {
                            cell_its = std::move(clipped);
                        }
                    }
                    catch (...) {
                        // If clipping fails, use unclipped cell
                    }
                }

                // Add cell to result
                size_t vertex_offset = result->vertices.size();

                // Copy vertices
                result->vertices.insert(result->vertices.end(),
                    cell_its.vertices.begin(),
                    cell_its.vertices.end());

                // Copy faces with offset
                for (const auto& face : cell_its.indices) {
                    result->indices.emplace_back(
                        face(0) + static_cast<int>(vertex_offset),
                        face(1) + static_cast<int>(vertex_offset),
                        face(2) + static_cast<int>(vertex_offset)
                    );
                }

            }
            catch (...) {
                // Skip cells that fail to generate
                continue;
            }
        }

        if (config.progress_callback && !config.progress_callback(85))
            return nullptr;

        // If we got no cells, create a simple cubic lattice as fallback
        if (result->indices.empty() && seed_points.size() >= 2) {
            // Create a simple box for each seed point as a fallback
            for (size_t i = 0; i < std::min(seed_points.size(), size_t(10)); ++i) {
                Vec3d center = seed_points[i];
                float cell_size = (bounds.size().norm() / seed_points.size()) * 0.3f;

                // Create a simple cube
                size_t base = result->vertices.size();

                // 8 vertices of a cube
                for (int dx = -1; dx <= 1; dx += 2) {
                    for (int dy = -1; dy <= 1; dy += 2) {
                        for (int dz = -1; dz <= 1; dz += 2) {
                            result->vertices.emplace_back(
                                center.x() + dx * cell_size,
                                center.y() + dy * cell_size,
                                center.z() + dz * cell_size
                            );
                        }
                    }
                }

                // 12 triangles (2 per cube face)
                int cube_faces[12][3] = {
                    {0,2,1}, {1,2,3},  // Front
                    {4,5,6}, {5,7,6},  // Back
                    {0,1,4}, {1,5,4},  // Right
                    {2,6,3}, {3,6,7},  // Left
                    {0,4,2}, {2,4,6},  // Bottom
                    {1,3,5}, {3,7,5}   // Top
                };

                for (auto& face : cube_faces) {
                    result->indices.emplace_back(
                        face[0] + static_cast<int>(base),
                        face[1] + static_cast<int>(base),
                        face[2] + static_cast<int>(base)
                    );
                }
            }
        }

        return result;
    }

    void VoronoiMesh::clip_to_mesh_boundary(
        indexed_triangle_set& voronoi_mesh,
        const indexed_triangle_set& original_mesh)
    {
        if (voronoi_mesh.vertices.empty() || original_mesh.vertices.empty())
            return;

        try {
            TriangleMesh voronoi_tm(voronoi_mesh);
            TriangleMesh original_tm(original_mesh);

            MeshBoolean::cgal::intersect(voronoi_tm, original_tm);

            voronoi_mesh = voronoi_tm.its;
        }
        catch (...) {
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

    void VoronoiMesh::create_edge_structure(
        indexed_triangle_set& result,
        const std::vector<Vec3d>& seed_points,
        const BoundingBoxf3& bounds,
        float edge_thickness,
        EdgeShape edge_shape,
        int edge_segments,
        const Config& config)
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

        // Extract unique Voronoi edges (circumcenters of adjacent tetrahedra)
        std::set<std::pair<Point_3, Point_3>> unique_edges;
        std::map<Point_3, std::vector<Point_3>> vertex_connections;  // Track which edges meet at each vertex

        for (auto eit = dt.finite_edges_begin(); eit != dt.finite_edges_end(); ++eit) {
            auto cell1 = eit->first;
            auto cell2 = eit->first->neighbor(eit->second);

            if (!dt.is_infinite(cell1) && !dt.is_infinite(cell2)) {
                Point_3 cc1 = cell1->circumcenter();
                Point_3 cc2 = cell2->circumcenter();

                // Bounds check
                if (cc1.x() >= bounds.min.x() - 1.0 && cc1.x() <= bounds.max.x() + 1.0 &&
                    cc1.y() >= bounds.min.y() - 1.0 && cc1.y() <= bounds.max.y() + 1.0 &&
                    cc1.z() >= bounds.min.z() - 1.0 && cc1.z() <= bounds.max.z() + 1.0 &&
                    cc2.x() >= bounds.min.x() - 1.0 && cc2.x() <= bounds.max.x() + 1.0 &&
                    cc2.y() >= bounds.min.y() - 1.0 && cc2.y() <= bounds.max.y() + 1.0 &&
                    cc2.z() >= bounds.min.z() - 1.0 && cc2.z() <= bounds.max.z() + 1.0) {

                    // Store edge (ensure consistent ordering)
                    auto edge = (cc1 < cc2) ? std::make_pair(cc1, cc2) : std::make_pair(cc2, cc1);
                    unique_edges.insert(edge);

                    // Track vertex connections for proper junctions
                    vertex_connections[cc1].push_back(cc2);
                    vertex_connections[cc2].push_back(cc1);
                }
            }
        }

        BOOST_LOG_TRIVIAL(info) << "create_edge_structure() - Found " << unique_edges.size() << " edges and " << vertex_connections.size() << " vertices";

        if (config.progress_callback && !config.progress_callback(50))
            return;

        // Generate cross-section profile based on shape type
        auto get_profile_point = [&](int i, float radius) -> Vec3d {
            float angle = (2.0f * M_PI * i) / edge_segments;
            float r = radius;

            switch (edge_shape) {
                case EdgeShape::Square: {
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

                case EdgeShape::Hexagon:
                case EdgeShape::Octagon: {
                    // Regular polygon
                    int sides = (edge_shape == EdgeShape::Hexagon) ? 6 : 8;
                    float snap_angle = std::round(angle / (2.0f * M_PI / sides)) * (2.0f * M_PI / sides);
                    return Vec3d(r * std::cos(snap_angle), r * std::sin(snap_angle), 0);
                }

                case EdgeShape::Star: {
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
        std::map<Point_3, size_t> vertex_junction_map;  // Maps vertex to its junction center index

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
        }

        BOOST_LOG_TRIVIAL(info) << "create_edge_structure() - Created " << vertex_junction_map.size() << " junction spheres";

        if (config.progress_callback)
            config.progress_callback(80);
    }

} // namespace Slic3r