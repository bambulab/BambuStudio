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
            Random,      // Use random points within bounding box
            Surface,     // Place seeds on mesh surface
            Adaptive     // Adaptive density based on mesh features
        };

        // Edge shape types for wireframe edges (mathematically correct Voronoi edges)
        enum class EdgeShape {
            Cylinder,
            Square,
            Hexagon,
            Octagon,
            Star
        };
        
        // Solid cell styling (for creative flair on mathematical Voronoi cells)
        enum class CellStyle {
            Pure,           // Pure mathematical Voronoi cells (polyhedral)
            Rounded,        // Rounded corners at vertices
            Chamfered,      // Chamfered edges
            Crystalline,    // Sharp angular cuts
            Organic,        // Smooth flowing surfaces
            Faceted         // Extra triangulation for visual interest
        };

        struct Config {
            SeedType seed_type = SeedType::Vertices;
            int num_seeds = 50;
            int random_seed = 42;  // For reproducible random generation
            
            // Mode selection
            bool hollow_cells = true;  // true = wireframe (pure Voronoi edges), false = solid cells
            
            // Wireframe mode parameters (mathematically correct Voronoi edge structure)
            float edge_thickness = 1.0f;       // Thickness of Voronoi edge struts
            EdgeShape edge_shape = EdgeShape::Cylinder;
            int edge_segments = 8;             // Cross-section resolution
            float edge_curvature = 0.0f;       // 0 = straight, 1 = curved
            int edge_subdivisions = 0;         // Curve smoothness
            bool edge_caps = false;            // Add end caps to cylinders (can cause overlap at junctions)
            
            // Solid mode parameters (mathematical Voronoi cells with flair)
            CellStyle cell_style = CellStyle::Rounded;  // Creative styling
            float wall_thickness = 0.0f;       // 0 = solid, >0 = shell thickness
            float rounding_radius = 0.5f;      // For Rounded style
            float chamfer_distance = 0.3f;     // For Chamfered style
            int subdivision_level = 1;         // Higher = smoother (for Organic/Rounded)
            
            // Advanced options
            bool clip_to_mesh = true;          // Clip to original mesh boundary
            float min_cell_size = 0.0f;        // Minimum cell size (0 = no limit)
            float adaptive_factor = 1.0f;      // For adaptive seeding
            
            // Lloyd's relaxation for uniform cells (NEW)
            bool relax_seeds = false;          // Enable seed relaxation
            int relaxation_iterations = 3;     // Number of Lloyd iterations (1-10)
            
            // Multi-scale hierarchical Voronoi (NEW)
            bool multi_scale = false;          // Generate multiple scales
            std::vector<int> scale_seed_counts = {50, 200, 800};     // Coarse→fine
            std::vector<float> scale_thicknesses = {2.0f, 1.0f, 0.5f}; // Thick→thin
            
            // Anisotropic stretching for directional strength (NEW)
            bool anisotropic = false;          // Enable anisotropic cells
            Vec3d anisotropy_direction = Vec3d(0, 0, 1);  // Stretch direction
            float anisotropy_ratio = 2.0f;     // Stretch factor (1.0 = isotropic)
            
            // Weighted Voronoi for variable density (NEW)
            bool use_weighted_cells = false;   // Enable weighted tessellation
            std::vector<double> cell_weights;  // Weight (radius²) per seed point
            Vec3d density_center;              // Point of high density (for auto-weighting)
            float density_falloff = 2.0f;      // Distance decay for auto-weighting
            
            // Load-bearing optimization (NEW)
            bool optimize_for_load = false;    // Optimize for directional load
            Vec3d load_direction = Vec3d(0, 0, -1);  // Load direction (default: gravity)
            float load_stretch_factor = 1.2f;  // How much to stretch (1.0 = no stretch)
            
            // Printability constraints
            bool enforce_watertight = false;   // Fail if not watertight after generation
            bool auto_repair = true;           // Attempt automatic repair of holes
            float min_wall_thickness = 0.4f;   // Minimum printable wall thickness (mm)
            float min_feature_size = 0.2f;     // Minimum printable feature size (mm)
            bool validate_printability = false; // Pre-validate before generation

            // Progress callback - returns false to cancel
            std::function<bool(int)> progress_callback = nullptr;
            
            // Error callback - called with validation errors
            std::function<void(const std::string&)> error_callback = nullptr;
        };
        
        // Statistics about generated Voronoi structure
        struct Statistics {
            int num_cells = 0;
            int num_vertices = 0;
            int num_faces = 0;
            int num_edges = 0;
            float min_cell_volume = 0.0f;
            float max_cell_volume = 0.0f;
            float avg_cell_volume = 0.0f;
            float total_volume = 0.0f;
            float min_edge_length = 0.0f;
            float max_edge_length = 0.0f;
            float avg_edge_length = 0.0f;
            int generation_time_ms = 0;
        };
        
        // Per-cell information from Voro++
        struct CellInfo {
            int cell_id = -1;                    // Cell index
            Vec3d seed_position;                 // Seed point location
            Vec3d centroid;                      // Cell centroid
            double volume = 0.0;                 // Cell volume
            double surface_area = 0.0;           // Cell surface area
            int num_faces = 0;                   // Number of cell faces
            int num_vertices = 0;                // Number of cell vertices
            int num_edges = 0;                   // Number of cell edges
            std::vector<int> neighbor_ids;       // IDs of neighboring cells
            std::vector<double> face_areas;      // Area of each face
            std::vector<Vec3d> face_normals;     // Normal of each face
            std::vector<int> face_vertex_counts; // Vertices per face
        };

        // Generate a Voronoi mesh from an input mesh
        // Returns nullptr if generation fails or is cancelled
        static std::unique_ptr<indexed_triangle_set> generate(
            const indexed_triangle_set& input_mesh,
            const Config& config
        );
        
        // Generate Voronoi mesh and return statistics
        static std::unique_ptr<indexed_triangle_set> generate_with_stats(
            const indexed_triangle_set& input_mesh,
            const Config& config,
            Statistics& stats
        );
        
        // Extract detailed information about all Voronoi cells
        static std::vector<CellInfo> analyze_cells(
            const std::vector<Vec3d>& seed_points,
            const BoundingBoxf3& bounds
        );
        
        // Get information about a specific cell
        static CellInfo get_cell_info(
            const std::vector<Vec3d>& seed_points,
            const BoundingBoxf3& bounds,
            int cell_id
        );
        
        // Find neighbors of a specific cell
        static std::vector<int> find_cell_neighbors(
            const std::vector<Vec3d>& seed_points,
            const BoundingBoxf3& bounds,
            int cell_id
        );
        
        // Compute centroid of each Voronoi cell
        static std::vector<Vec3d> compute_cell_centroids(
            const std::vector<Vec3d>& seed_points,
            const BoundingBoxf3& bounds
        );
        
        // Lloyd's relaxation for uniform cell distribution
        static std::vector<Vec3d> lloyd_relaxation(
            std::vector<Vec3d> seeds,
            const BoundingBoxf3& bounds,
            int iterations
        );
        
        // Generate weights for variable density
        static std::vector<double> generate_density_weights(
            const std::vector<Vec3d>& seeds,
            const Vec3d& dense_point,
            float density_falloff
        );
        
        // Optimize seeds for directional load
        static std::vector<Vec3d> optimize_for_load_direction(
            const std::vector<Vec3d>& seeds,
            const Vec3d& load_direction,
            float stretch_factor
        );
        
        // Find connected component of cells
        static std::set<int> find_connected_cells(
            const std::vector<Vec3d>& seed_points,
            const BoundingBoxf3& bounds,
            int start_cell_id
        );
        
        // Validate printability before generation
        static bool validate_printability(
            const std::vector<Vec3d>& seed_points,
            const BoundingBoxf3& bounds,
            float min_feature_size,
            std::string& error_message
        );
        
        // Validate Voronoi mesh (check for issues)
        static bool validate_mesh(
            const indexed_triangle_set& mesh,
            std::vector<std::string>* issues = nullptr
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
        
        // Generate seeds on mesh surface
        static std::vector<Vec3d> generate_surface_seeds(
            const indexed_triangle_set& mesh,
            int num_seeds,
            int random_seed
        );
        
        // Generate seeds with adaptive density
        static std::vector<Vec3d> generate_adaptive_seeds(
            const indexed_triangle_set& mesh,
            int num_seeds,
            float adaptive_factor,
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
        
        // Improved wireframe clipping with epsilon-aware boundary handling
        static void clip_wireframe_to_mesh_improved(
            indexed_triangle_set& wireframe,
            const indexed_triangle_set& clip_mesh,
            double epsilon
        );

        // Create hollow cells by offsetting cell walls inward
        static void create_hollow_cells(
            indexed_triangle_set& mesh,
            float wall_thickness
        );

        // Create wireframe using Delaunay-Voronoi duality (PRIMARY METHOD - mathematically correct)
        // Uses CGAL's Delaunay triangulation to extract proper Voronoi edges via circumcenters
        static std::unique_ptr<indexed_triangle_set> create_wireframe_from_delaunay(
            const std::vector<Vec3d>& seed_points,
            const BoundingBoxf3& bounds,
            const Config& config,
            const indexed_triangle_set* clip_mesh
        );
        
        // Legacy: Create wireframe from Voro++ cells (kept for reference, not used)
        // Note: Voro++ is still used for Lloyd's relaxation, just not edge extraction
        static std::unique_ptr<indexed_triangle_set> create_wireframe_from_voropp(
            const std::vector<Vec3d>& seed_points,
            const BoundingBoxf3& bounds,
            const Config& config,
            const indexed_triangle_set* clip_mesh
        );
        
        // Apply styling to Voronoi cells (for solid mode flair)
        static void apply_cell_styling(
            indexed_triangle_set& mesh,
            const Config& config
        );
        
        // Styling methods for different cell styles
        static void apply_rounding(indexed_triangle_set& mesh, float radius);
        static void apply_chamfering(indexed_triangle_set& mesh, float distance);
        static void apply_crystalline_cuts(indexed_triangle_set& mesh);
        static void apply_organic_smoothing(indexed_triangle_set& mesh, int subdivisions);
        static void apply_faceting(indexed_triangle_set& mesh);
    };

} // namespace Slic3r

#endif // slic3r_VoronoiMesh_hpp_