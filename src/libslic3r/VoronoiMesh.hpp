#ifndef slic3r_VoronoiMesh_hpp_
#define slic3r_VoronoiMesh_hpp_

#include "TriangleMesh.hpp"
#include "Point.hpp"
#include <vector>
#include <memory>
#include <functional>

namespace Slic3r {

    // 3D Voronoi tessellation for mesh generation
    class VoronoiMesh
    {
    public:
        enum class SeedType {
            Vertices,    // Use mesh vertices as seed points
            Grid,        // Use regular grid of points
            Random       // Use random points within bounding box
        };

        // Edge shape types for wireframe edges
        enum class EdgeShape {
            Cylinder,
            Square,
            Hexagon,
            Octagon,
            Star
        };

        struct Config {
            SeedType seed_type = SeedType::Vertices;
            int num_seeds = 50;
            float wall_thickness = 1.0f;
            float edge_thickness = 1.0f;  // Thickness of wireframe edges/struts connecting Voronoi vertices
            EdgeShape edge_shape = EdgeShape::Cylinder;
            int edge_segments = 8;
            float edge_curvature = 0.0f;  // 0 = straight, 1 = maximum curve
            int edge_subdivisions = 0;  // 0 = straight line, 1+ = curved segments
            bool hollow_cells = true;
            int random_seed = 42;  // For reproducible random generation

            // Progress callback - returns false to cancel
            std::function<bool(int)> progress_callback = nullptr;
        };

        // Generate a Voronoi mesh from an input mesh
        // Returns nullptr if generation fails or is cancelled
        static std::unique_ptr<indexed_triangle_set> generate(
            const indexed_triangle_set& input_mesh,
            const Config& config
        );

    private:
        // Generate seed points based on configuration
        static std::vector<Vec3d> generate_seed_points(
            const indexed_triangle_set& mesh,
            const Config& config
        );

        // Generate seed points from mesh vertices
        static std::vector<Vec3d> generate_vertex_seeds(
            const indexed_triangle_set& mesh,
            int max_seeds
        );

        // Generate seed points on a regular grid
        static std::vector<Vec3d> generate_grid_seeds(
            const indexed_triangle_set& mesh,
            int num_seeds
        );

        // Generate random seed points with configurable seed
        static std::vector<Vec3d> generate_random_seeds(
            const indexed_triangle_set& mesh,
            int num_seeds,
            int random_seed
        );

        // Perform 3D Voronoi tessellation - main entry point
        static std::unique_ptr<indexed_triangle_set> tessellate_voronoi_cells(
            const std::vector<Vec3d>& seed_points,
            const BoundingBoxf3& bounds,
            const Config& config,
            const indexed_triangle_set* clip_mesh = nullptr
        );

        // Voro++ implementation (fast polyhedral cell generation)
        static std::unique_ptr<indexed_triangle_set> tessellate_voronoi_with_voropp(
            const std::vector<Vec3d>& seed_points,
            const BoundingBoxf3& bounds,
            const Config& config,
            const indexed_triangle_set* clip_mesh
        );

        // Clip Voronoi cells to original mesh boundary
        static void clip_to_mesh_boundary(
            indexed_triangle_set& voronoi_mesh,
            const indexed_triangle_set& original_mesh
        );

        // Create hollow cells by offsetting cell walls inward
        static void create_hollow_cells(
            indexed_triangle_set& mesh,
            float wall_thickness
        );

        // Create wireframe structure from Voro++ edge data
        static std::unique_ptr<indexed_triangle_set> create_wireframe_from_voropp(
            const std::vector<Vec3d>& seed_points,
            const BoundingBoxf3& bounds,
            const Config& config,
            const indexed_triangle_set* clip_mesh
        );
    };

} // namespace Slic3r

#endif // slic3r_VoronoiMesh_hpp_