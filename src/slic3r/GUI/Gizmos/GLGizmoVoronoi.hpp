#ifndef slic3r_GLGizmoVoronoi_hpp_
#define slic3r_GLGizmoVoronoi_hpp_

#include "GLGizmoPainterBase.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include <imgui/imgui.h>

#include <mutex>
#include <thread>
#include <map>
#include <wx/string.h>

namespace Slic3r {
    class ModelVolume;
    class Model;

    namespace GUI {

        class GLGizmoVoronoi : public GLGizmoPainterBase
        {
        public:
            GLGizmoVoronoi(GLCanvas3D& parent, unsigned int sprite_id);
            virtual ~GLGizmoVoronoi();

            bool on_esc_key_down();

            std::string get_icon_filename(bool is_dark_mode) const override;

        protected:
            virtual std::string on_get_name() const override;
            virtual std::string on_get_name_str() override { return "Voronoi"; }
            virtual void on_render_input_window(float x, float y, float bottom_limit) override;
            virtual bool on_is_selectable() const override { return GLGizmoPainterBase::on_is_selectable(); }
            virtual bool on_is_activable() const override;
            virtual void on_set_state() override;

            virtual bool on_init() override;
            virtual void on_render() override;
            virtual void on_render_for_picking() override {}
            virtual void data_changed(bool is_serializing) override;

            // Painting integration
            void render_painter_gizmo() const override;
            void render_triangles(const Selection& selection) const override;
            void update_model_object() override;
            void update_from_model_object(bool first_update) override;
            PainterGizmoType get_painter_type() const override { return PainterGizmoType::VORONOI; }
            void on_opening() override;
            void on_shutdown() override;
            wxString handle_snapshot_action_name(bool shift_down, Button button_down) const override;

            CommonGizmosDataID on_get_requirements() const override;

        private:
            void apply_voronoi();
            void close();

            void process();
            void stop_worker_thread_request();
            void worker_finished();

            void create_gui_cfg();
            void request_rerender();

            void set_center_position();

            // Seed preview and randomization
            void update_seed_preview();
            void randomize_seed();
            void render_seed_preview();

            // 2D Voronoi preview
            void render_2d_voronoi_preview();
            void update_2d_voronoi_preview();
            void generate_fallback_hexagonal_preview();
            void render_ui_content();  // Extracted UI rendering for safer error handling

            struct VoronoiCell2D {
                std::vector<Vec2f> vertices;
                Vec2f seed_point;
                ImU32 color;
            };
            std::vector<VoronoiCell2D> m_2d_voronoi_cells;
            struct DelaunayEdge2D {
                Vec2f a;
                Vec2f b;
            };
            std::vector<DelaunayEdge2D> m_2d_delaunay_edges;

            struct Configuration
            {
                enum SeedType {
                    SEED_VERTICES,
                    SEED_GRID,
                    SEED_RANDOM
                };

                enum EdgeShape {
                    EDGE_CYLINDER,
                    EDGE_SQUARE,
                    EDGE_HEXAGON,
                    EDGE_OCTAGON,
                    EDGE_STAR
                };

                enum CellStyle {
                    STYLE_PURE,
                    STYLE_ROUNDED,
                    STYLE_CHAMFERED,
                    STYLE_CRYSTALLINE,
                    STYLE_ORGANIC,
                    STYLE_FACETED
                };

                SeedType seed_type = SEED_VERTICES;
                int num_seeds = 50;
                float wall_thickness = 1.0f;
                float edge_thickness = 1.0f;  // Thickness of wireframe edges/struts
                EdgeShape edge_shape = EDGE_CYLINDER;
                CellStyle cell_style = STYLE_PURE;
                int edge_segments = 8;  // Number of sides/segments for the edge shape
                float edge_curvature = 0.0f;  // 0 = straight, 1 = maximum curve
                int edge_subdivisions = 0;  // 0 = straight line, 1+ = curved segments
                bool hollow_cells = true;
                int random_seed = 42;
                bool show_seed_preview = false;
                bool enable_triangle_painting = false;

                // Advanced features
                // Lloyd's relaxation for uniform cells
                bool relax_seeds = false;
                int relaxation_iterations = 3;
                
                // Multi-scale hierarchical Voronoi
                bool multi_scale = false;
                std::vector<int> scale_seed_counts = {50, 200, 800};
                std::vector<float> scale_thicknesses = {2.0f, 1.0f, 0.5f};
                
                // Anisotropic transformation for directional strength
                bool anisotropic = false;
                Vec3d anisotropy_direction = Vec3d(0, 0, 1);  // Default: vertical (Z)
                float anisotropy_ratio = 2.0f;
                
                // Weighted Voronoi for variable density
                bool use_weighted_cells = false;
                std::vector<double> cell_weights;  // Weight per seed (radius²)
                Vec3d density_center = Vec3d(0, 0, 0);  // Point of high density
                float density_falloff = 2.0f;  // Distance decay for auto-weighting
                
                // Load-bearing optimization
                bool optimize_for_load = false;
                Vec3d load_direction = Vec3d(0, 0, -1);  // Default: gravity (down)
                float load_stretch_factor = 1.2f;  // Stretch factor along load direction
                
                // Printability constraints
                bool enforce_watertight = false;
                bool auto_repair = true;
                float min_wall_thickness = 0.4f;
                float min_feature_size = 0.2f;
                bool validate_printability = false;  // Pre-validate before generation

                bool operator==(const Configuration& rhs) const {
                    return seed_type == rhs.seed_type &&
                        num_seeds == rhs.num_seeds &&
                        wall_thickness == rhs.wall_thickness &&
                        edge_thickness == rhs.edge_thickness &&
                        edge_shape == rhs.edge_shape &&
                        cell_style == rhs.cell_style &&
                        edge_segments == rhs.edge_segments &&
                        edge_curvature == rhs.edge_curvature &&
                        edge_subdivisions == rhs.edge_subdivisions &&
                        hollow_cells == rhs.hollow_cells &&
                        random_seed == rhs.random_seed &&
                        relax_seeds == rhs.relax_seeds &&
                        relaxation_iterations == rhs.relaxation_iterations &&
                        multi_scale == rhs.multi_scale &&
                        scale_seed_counts == rhs.scale_seed_counts &&
                        scale_thicknesses == rhs.scale_thicknesses &&
                        anisotropic == rhs.anisotropic &&
                        anisotropy_direction == rhs.anisotropy_direction &&
                        anisotropy_ratio == rhs.anisotropy_ratio &&
                        use_weighted_cells == rhs.use_weighted_cells &&
                        cell_weights == rhs.cell_weights &&
                        density_center == rhs.density_center &&
                        density_falloff == rhs.density_falloff &&
                        optimize_for_load == rhs.optimize_for_load &&
                        load_direction == rhs.load_direction &&
                        load_stretch_factor == rhs.load_stretch_factor &&
                        enforce_watertight == rhs.enforce_watertight &&
                        auto_repair == rhs.auto_repair &&
                        min_wall_thickness == rhs.min_wall_thickness &&
                        min_feature_size == rhs.min_feature_size &&
                        validate_printability == rhs.validate_printability;
                }
                bool operator!=(const Configuration& rhs) const {
                    return !(*this == rhs);
                }
            };

            Configuration m_configuration;

            // UI state variables
            bool m_is_dark_mode = false;
            float m_cursor_radius = 2.0f;

            // Seed preview
            std::vector<Vec3f> m_seed_preview_points;
            GLModel m_seed_preview_model;

            bool m_move_to_center;
            const ModelVolume* m_volume;
            indexed_triangle_set m_original_mesh;  // Store original mesh for repeated generation
            GLModel m_glmodel;

            struct State {
                enum Status {
                    idle,
                    running,
                    cancelling
                };

                Status status = idle;
                int progress = 0;
                Configuration config;
                const ModelVolume* mv = nullptr;  // Only for identity check, DO NOT dereference!
                indexed_triangle_set mesh_copy;   // Safe copy of mesh data for worker thread
                std::unique_ptr<indexed_triangle_set> result;
            };

            std::thread m_worker;
            std::mutex m_state_mutex;
            State m_state;

            struct GuiCfg
            {
                int top_left_width = 100;
                int bottom_left_width = 100;
                int input_width = 100;
                int window_offset_x = 100;
                int window_offset_y = 100;
                int window_padding = 0;
                size_t max_char_in_name = 30;
            };
            std::optional<GuiCfg> m_gui_cfg;

            std::string tr_mesh_name;
            std::string tr_seed_type;
            std::string tr_num_seeds;
            std::string tr_wall_thickness;
            std::string tr_random_seed;
            std::string tr_seed_preview;
            
            // Advanced feature translations
            std::string tr_advanced_options;
            std::string tr_relax_seeds;
            std::string tr_relaxation_iterations;
            std::string tr_multi_scale;
            std::string tr_scale_levels;
            std::string tr_anisotropic;
            std::string tr_anisotropy_direction;
            std::string tr_anisotropy_ratio;
            std::string tr_printability;

            std::map<std::string, wxString> m_desc;

            class VoronoiCanceledException : public std::exception
            {
            public:
                const char* what() const throw() {
                    return L("Voronoi generation has been canceled");
                }
            };
        };

    } // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoVoronoi_hpp_