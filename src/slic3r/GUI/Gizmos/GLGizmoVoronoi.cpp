#include "GLGizmoVoronoi.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/VoronoiMesh.hpp"
#include "libslic3r/Geometry.hpp"

#ifdef Handle
// OpenCASCADE defines Handle(Class) macro which breaks CGAL type names.
#undef Handle
#endif

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Delaunay_triangulation_cell_base_with_circumcenter_3.h>
#include <CGAL/Triangulation_vertex_base_with_info_3.h>
#include <CGAL/Triangulation_data_structure_3.h>
#include <CGAL/Delaunay_triangulation_3.h>
#include <CGAL/Delaunay_triangulation_2.h>
#include <CGAL/Regular_triangulation_2.h>
#include <CGAL/Voronoi_diagram_2.h>
#include <CGAL/Delaunay_triangulation_adaptation_traits_2.h>
#include <CGAL/Regular_triangulation_adaptation_traits_2.h>
#include <CGAL/Delaunay_triangulation_adaptation_policies_2.h>
#include <CGAL/Regular_triangulation_adaptation_policies_2.h>

#include <GL/glew.h>
#include <thread>
#include <chrono>
#include <random>
#include <cmath>
#include <memory>
#include <algorithm>
#include <cfloat>
#include <limits>
#include <wx/string.h>
#include <boost/log/trivial.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Slic3r::GUI {

    namespace {
        // 3D types for 3D preview
        using PreviewKernel  = CGAL::Exact_predicates_inexact_constructions_kernel;
        using PreviewCellBase = CGAL::Delaunay_triangulation_cell_base_with_circumcenter_3<PreviewKernel>;
        using PreviewVertexBase = CGAL::Triangulation_vertex_base_with_info_3<int, PreviewKernel>;
        using PreviewTDS      = CGAL::Triangulation_data_structure_3<PreviewVertexBase, PreviewCellBase>;
        using PreviewDT       = CGAL::Delaunay_triangulation_3<PreviewKernel, PreviewTDS>;
        using PreviewPoint    = PreviewKernel::Point_3;
        using PreviewSegment  = PreviewKernel::Segment_3;

        // 2D types for Voronoi preview in the UI
        using K2 = CGAL::Exact_predicates_inexact_constructions_kernel;
        using Point_2 = K2::Point_2;

        using DT2 = CGAL::Delaunay_triangulation_2<K2>;
        using AT2 = CGAL::Delaunay_triangulation_adaptation_traits_2<DT2>;
        using AP2 = CGAL::Delaunay_triangulation_caching_degeneracy_removal_policy_2<DT2>;
        using VD2 = CGAL::Voronoi_diagram_2<DT2, AT2, AP2>;

        using RT2 = CGAL::Regular_triangulation_2<K2>;
        using Weighted_point_2 = RT2::Weighted_point;
        using AT2_RT = CGAL::Regular_triangulation_adaptation_traits_2<RT2>;
        using AP2_RT = CGAL::Regular_triangulation_degeneracy_removal_policy_2<RT2>;
        using VD2_RT = CGAL::Voronoi_diagram_2<RT2, AT2_RT, AP2_RT>;

        struct VoronoiPreviewData {
            std::vector<PreviewPoint> seeds;
            std::vector<PreviewSegment> edges;
        };

        VoronoiPreviewData compute_voronoi_preview(const std::vector<PreviewPoint>& seeds)
        {
            VoronoiPreviewData data;
            data.seeds = seeds;
            if (seeds.empty())
                return data;

            PreviewDT dt;
            dt.insert(seeds.begin(), seeds.end());

            for (auto it = dt.finite_facets_begin(); it != dt.finite_facets_end(); ++it) {
                CGAL::Object dual = dt.dual(*it);
                if (const PreviewSegment* seg = CGAL::object_cast<PreviewSegment>(&dual)) {
                    data.edges.push_back(*seg);
                }
            }
            return data;
        }

        static Vec3f hsv_to_rgb(float h, float s, float v)
        {
            h = std::fmod(h, 1.0f);
            if (h < 0.0f) h += 1.0f;

            float c = v * s;
            float x = c * (1.0f - std::fabs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
            float m = v - c;

            float r = 0.0f, g = 0.0f, b = 0.0f;
            if (h < 1.0f / 6.0f) {
                r = c; g = x; b = 0.0f;
            } else if (h < 2.0f / 6.0f) {
                r = x; g = c; b = 0.0f;
            } else if (h < 3.0f / 6.0f) {
                r = 0.0f; g = c; b = x;
            } else if (h < 4.0f / 6.0f) {
                r = 0.0f; g = x; b = c;
            } else if (h < 5.0f / 6.0f) {
                r = x; g = 0.0f; b = c;
            } else {
                r = c; g = 0.0f; b = x;
            }

            return Vec3f(r + m, g + m, b + m);
        }

        static Vec3f color_from_index(size_t idx)
        {
            constexpr float phi_conjugate = 0.6180339887498949f;
            float h = std::fmod(idx * phi_conjugate, 1.0f);
            return hsv_to_rgb(h, 0.6f, 0.9f);
        }

        static ImU32 color_from_index_imgui(size_t idx)
        {
            Vec3f rgb = color_from_index(idx);
            return ImGui::ColorConvertFloat4ToU32(ImVec4(rgb.x(), rgb.y(), rgb.z(), 0.6f));
        }
    } // namespace

    static void call_after_if_active(
        GLGizmoVoronoi* self,
        std::function<void(GLGizmoVoronoi&)> fn,
        GUI_App* app = &wxGetApp())
    {
        if (app == nullptr || self == nullptr)
            return;

        app->CallAfter([fn = std::move(fn), app, self]() {
            const Plater* plater = app->plater();
            if (plater == nullptr)
                return;

            const GLCanvas3D* canvas = plater->canvas3D();
            if (canvas == nullptr)
                return;

            GLGizmosManager& mgr = const_cast<GLGizmosManager&>(canvas->get_gizmos_manager());
            auto* gizmo = dynamic_cast<GLGizmoVoronoi*>(mgr.get_gizmo(GLGizmosManager::Voronoi));
            if (gizmo != self)
                return;

            fn(*gizmo);
            });
    }

    static ModelVolume* get_model_volume(const Selection& selection, Model& model)
    {
        const Selection::IndicesList& idxs = selection.get_volume_idxs();
        if (idxs.size() != 1)
            return nullptr;
        const GLVolume* selected_volume = selection.get_volume(*idxs.begin());
        if (selected_volume == nullptr)
            return nullptr;

        const GLVolume::CompositeID& cid = selected_volume->composite_id;
        const ModelObjectPtrs& objs = model.objects;
        if (cid.object_id < 0 || objs.size() <= static_cast<size_t>(cid.object_id))
            return nullptr;
        const ModelObject* obj = objs[cid.object_id];
        // Check for null object before dereferencing
        if (!obj || cid.volume_id < 0 || obj->volumes.size() <= static_cast<size_t>(cid.volume_id))
            return nullptr;
        return obj->volumes[cid.volume_id];
    }

    GLGizmoVoronoi::GLGizmoVoronoi(GLCanvas3D& parent, unsigned int sprite_id)
        : GLGizmoPainterBase(parent, sprite_id)
        , m_volume(nullptr)
        , m_move_to_center(false)
        , tr_mesh_name("Mesh name")  // Use plain string instead of _u8L during construction
        , tr_seed_type("Seed type")
        , tr_num_seeds("Number of seeds")
        , tr_wall_thickness("Wall thickness")
        , tr_random_seed("Random seed")
        , tr_seed_preview("Preview seeds")
    {
        try {
            // Immediate logging to stderr AND boost log
            fprintf(stderr, "GLGizmoVoronoi: Constructor START\n");
            fflush(stderr);
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: Constructor called";
            
            // Initialize translations properly after construction
            tr_mesh_name = _u8L("Mesh name");
            tr_seed_type = _u8L("Seed type");
            tr_num_seeds = _u8L("Number of seeds");
            tr_wall_thickness = _u8L("Wall thickness");
            tr_random_seed = _u8L("Random seed");
            tr_seed_preview = _u8L("Preview seeds");
            
            // Advanced feature translations
            tr_advanced_options = _u8L("Advanced Options");
            tr_relax_seeds = _u8L("Relax seeds (Lloyd's)");
            tr_relaxation_iterations = _u8L("Relaxation iterations");
            tr_multi_scale = _u8L("Multi-scale");
            tr_scale_levels = _u8L("Scale levels");
            tr_anisotropic = _u8L("Anisotropic");
            tr_anisotropy_direction = _u8L("Direction");
            tr_anisotropy_ratio = _u8L("Stretch ratio");
            tr_printability = _u8L("Printability");
            
            fprintf(stderr, "GLGizmoVoronoi: Constructor END\n");
            fflush(stderr);
        }
        catch (const std::exception& e) {
            fprintf(stderr, "GLGizmoVoronoi: Constructor EXCEPTION: %s\n", e.what());
            fflush(stderr);
            BOOST_LOG_TRIVIAL(error) << "GLGizmoVoronoi: Constructor EXCEPTION: " << e.what();
            throw;
        }
        catch (...) {
            fprintf(stderr, "GLGizmoVoronoi: Constructor UNKNOWN EXCEPTION\n");
            fflush(stderr);
            BOOST_LOG_TRIVIAL(error) << "GLGizmoVoronoi: Constructor UNKNOWN EXCEPTION";
            throw;
        }
    }

    GLGizmoVoronoi::~GLGizmoVoronoi()
    {
        stop_worker_thread_request();
        if (m_worker.joinable())
            m_worker.join();
        m_glmodel.reset();
        m_seed_preview_points.clear();
        m_seed_preview_points_exact.clear();
        m_seed_preview_colors.clear();
        m_voronoi_preview_edges.clear();
    }

    bool GLGizmoVoronoi::on_esc_key_down()
    {
        return false;
    }

    std::string GLGizmoVoronoi::get_icon_filename(bool is_dark_mode) const
    {
        return is_dark_mode ? "toolbar_voronoi_dark.svg" : "toolbar_voronoi.svg";
    }

    std::string GLGizmoVoronoi::on_get_name() const
    {
        if (!on_is_activable() && get_state() == EState::Off) {
            return _u8L("Voronoi") + ":\n" + _u8L("Please select single object.");
        } else {
            return _u8L("Voronoi");
        }
    }

    bool GLGizmoVoronoi::on_is_activable() const
    {
        const Selection& selection = m_parent.get_selection();

        // Require a single full instance to be selected (like BrimEars)
        if (!selection.is_single_full_instance())
            return false;

        return true;
    }

    bool GLGizmoVoronoi::on_init()
    {
        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::on_init()";
        m_tool_type = ToolType::BRUSH;
        m_cursor_type = TriangleSelector::CursorType::CIRCLE;
        m_cursor_radius = 2.0f;
        m_configuration.enable_triangle_painting = false;
        return true;
    }

    void GLGizmoVoronoi::on_render_input_window(float x, float y, float bottom_limit)
    {
        fprintf(stderr, "GLGizmoVoronoi: on_render_input_window() CALLED\n");
        fflush(stderr);
        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_render_input_window() START";

        if (!m_gui_cfg.has_value())
            create_gui_cfg();

        const float approx_height = m_gui_cfg->window_offset_y + m_gui_cfg->window_padding;
        GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 0.0f, 0.0f);

        const float scaling = m_parent.get_scale();
        const ImVec2 window_size(m_gui_cfg->bottom_left_width * scaling, approx_height * scaling);
        ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);

        if (GizmoImguiBegin(on_get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
            try {
                render_ui_content();
            }
            catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "Error in render_input_window: " << e.what();
            }
            catch (...) {
                BOOST_LOG_TRIVIAL(error) << "Unknown error in render_input_window";
            }
        }

        GizmoImguiEnd();
    }

    VoronoiMesh::Config GLGizmoVoronoi::build_voronoi_config(const Configuration& cfg) const
    {
        VoronoiMesh::Config out;

        switch (cfg.seed_type) {
        case Configuration::SEED_VERTICES: out.seed_type = VoronoiMesh::SeedType::Vertices; break;
        case Configuration::SEED_GRID:     out.seed_type = VoronoiMesh::SeedType::Grid;     break;
        case Configuration::SEED_RANDOM:   out.seed_type = VoronoiMesh::SeedType::Random;   break;
        default:                           out.seed_type = VoronoiMesh::SeedType::Vertices; break;
        }

        out.num_seeds = std::max(0, cfg.num_seeds);
        out.wall_thickness = cfg.wall_thickness;
        out.edge_thickness = cfg.edge_thickness;
        out.edge_segments = cfg.edge_segments;
        out.edge_curvature = cfg.edge_curvature;
        out.edge_subdivisions = cfg.edge_subdivisions;
        out.hollow_cells = cfg.hollow_cells;
        out.random_seed = cfg.random_seed;

        switch (cfg.edge_shape) {
        case Configuration::EDGE_CYLINDER: out.edge_shape = VoronoiMesh::EdgeShape::Cylinder; break;
        case Configuration::EDGE_SQUARE:   out.edge_shape = VoronoiMesh::EdgeShape::Square;   break;
        case Configuration::EDGE_HEXAGON:  out.edge_shape = VoronoiMesh::EdgeShape::Hexagon;  break;
        case Configuration::EDGE_OCTAGON:  out.edge_shape = VoronoiMesh::EdgeShape::Octagon;  break;
        case Configuration::EDGE_STAR:     out.edge_shape = VoronoiMesh::EdgeShape::Star;     break;
        default:                           out.edge_shape = VoronoiMesh::EdgeShape::Cylinder; break;
        }

        switch (cfg.cell_style) {
        case Configuration::STYLE_PURE:        out.cell_style = VoronoiMesh::CellStyle::Pure;        break;
        case Configuration::STYLE_ROUNDED:     out.cell_style = VoronoiMesh::CellStyle::Rounded;     break;
        case Configuration::STYLE_CHAMFERED:   out.cell_style = VoronoiMesh::CellStyle::Chamfered;   break;
        case Configuration::STYLE_CRYSTALLINE: out.cell_style = VoronoiMesh::CellStyle::Crystalline; break;
        case Configuration::STYLE_ORGANIC:     out.cell_style = VoronoiMesh::CellStyle::Organic;     break;
        case Configuration::STYLE_FACETED:     out.cell_style = VoronoiMesh::CellStyle::Faceted;     break;
        default:                               out.cell_style = VoronoiMesh::CellStyle::Pure;        break;
        }

        out.relax_seeds = cfg.relax_seeds;
        out.relaxation_iterations = cfg.relaxation_iterations;
        out.multi_scale = cfg.multi_scale;
        out.scale_seed_counts = cfg.scale_seed_counts;
        out.scale_thicknesses = cfg.scale_thicknesses;

        out.anisotropic = cfg.anisotropic;
        out.anisotropy_direction = cfg.anisotropy_direction;
        out.anisotropy_ratio = cfg.anisotropy_ratio;

        out.use_weighted_cells = cfg.use_weighted_cells;
        out.cell_weights = cfg.cell_weights;
        out.density_center = cfg.density_center;
        out.density_falloff = cfg.density_falloff;

        out.optimize_for_load = cfg.optimize_for_load;
        out.load_direction = cfg.load_direction;
        out.load_stretch_factor = cfg.load_stretch_factor;

        out.enforce_watertight = cfg.enforce_watertight;
        out.auto_repair = cfg.auto_repair;
        out.min_wall_thickness = cfg.min_wall_thickness;
        out.min_feature_size = cfg.min_feature_size;
        out.validate_printability = cfg.validate_printability;
        out.restricted_voronoi = cfg.restricted_voronoi;
        if (out.restricted_voronoi)
            out.clip_to_mesh = true;

        return out;
    }

    void GLGizmoVoronoi::update_voronoi_preview(const std::vector<Vec3d>& seeds)
    {
        if (!m_configuration.show_seed_preview) {
            clear_voronoi_preview();
            return;
        }

        if (seeds.empty()) {
            clear_voronoi_preview();
            return;
        }

        std::vector<PreviewPoint> seed_points;
        seed_points.reserve(seeds.size());
        for (const Vec3d& s : seeds)
            seed_points.emplace_back(s.x(), s.y(), s.z());

        VoronoiPreviewData preview = compute_voronoi_preview(seed_points);

        m_voronoi_preview_edges.clear();
        m_voronoi_preview_edges.reserve(preview.edges.size() * 2);
        for (const PreviewSegment& seg : preview.edges) {
            const PreviewPoint& a = seg.source();
            const PreviewPoint& b = seg.target();
            m_voronoi_preview_edges.emplace_back(Vec3f(float(a.x()), float(a.y()), float(a.z())));
            m_voronoi_preview_edges.emplace_back(Vec3f(float(b.x()), float(b.y()), float(b.z())));
        }

        request_rerender();
    }

    void GLGizmoVoronoi::clear_voronoi_preview()
    {
        m_voronoi_preview_edges.clear();
    }

    void GLGizmoVoronoi::render_voronoi_preview() const
    {
        if (!m_configuration.show_seed_preview)
            return;
        if (m_seed_preview_points.empty())
            return;

        glsafe(::glPushAttrib(GL_ENABLE_BIT | GL_POINT_BIT | GL_LINE_BIT));
        glsafe(::glDisable(GL_LIGHTING));

        glsafe(::glPointSize(6.0f));
        glsafe(::glBegin(GL_POINTS));
        for (size_t i = 0; i < m_seed_preview_points.size(); ++i) {
            const Vec3f& p = m_seed_preview_points[i];
            const Vec3f& c = m_seed_preview_colors.empty() ? Vec3f(0.2f, 0.8f, 0.3f) : m_seed_preview_colors[i % m_seed_preview_colors.size()];
            glsafe(::glColor3f(c.x(), c.y(), c.z()));
            glsafe(::glVertex3f(p.x(), p.y(), p.z()));
        }
        glsafe(::glEnd());

        if (!m_voronoi_preview_edges.empty()) {
            glsafe(::glLineWidth(1.0f));
            glsafe(::glColor3f(0.95f, 0.85f, 0.2f));
            glsafe(::glBegin(GL_LINES));
            for (size_t i = 0; i + 1 < m_voronoi_preview_edges.size(); i += 2) {
                const Vec3f& a = m_voronoi_preview_edges[i];
                const Vec3f& b = m_voronoi_preview_edges[i + 1];
                glsafe(::glVertex3f(a.x(), a.y(), a.z()));
                glsafe(::glVertex3f(b.x(), b.y(), b.z()));
            }
            glsafe(::glEnd());
        }

        glsafe(::glPopAttrib());
    }

    void GLGizmoVoronoi::render_ui_content()
    {
        // Ensure m_volume is set before rendering UI
        if (!m_volume) {
            Plater* plater = wxGetApp().plater();
            if (plater) {
                Model& model = plater->model();
                const Selection& selection = m_parent.get_selection();
                m_volume = get_model_volume(selection, model);
            }
        }

        // Display selected volume info
        if (m_volume) {
            ImGui::Text("%s: %s", tr_mesh_name.c_str(), m_volume->name.c_str());
            ImGui::Separator();
        }
        else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No model selected!");
            ImGui::Separator();
        }

        // Seed type selection
        ImGui::Text("%s:", tr_seed_type.c_str());
        const char* seed_types[] = { "Vertices", "Grid", "Random" };
        int current_seed = static_cast<int>(m_configuration.seed_type);
        if (ImGui::Combo("##seed_type", &current_seed, seed_types, IM_ARRAYSIZE(seed_types))) {
            m_configuration.seed_type = static_cast<Configuration::SeedType>(current_seed);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", into_u8(_u8L("Vertices: Use mesh vertices\nGrid: Regular 3D lattice\nRandom: Randomized points")).c_str());
        }

        // Number of seeds with manual input (controls wireframe density)
        ImGui::Text("%s:", tr_num_seeds.c_str());
        ImGui::SliderInt("##num_seeds", &m_configuration.num_seeds, 10, 500);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", into_u8(_u8L("Controls wireframe density - more seeds = denser structure")).c_str());
        }

        // Manual input for precise seed count
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60);
        ImGui::InputInt("##num_seeds_input", &m_configuration.num_seeds);
        m_configuration.num_seeds = std::max(10, std::min(500, m_configuration.num_seeds));

        // Wall thickness (only for solid cells)
        if (!m_configuration.hollow_cells) {
            ImGui::Text("%s:", tr_wall_thickness.c_str());
            ImGui::SliderFloat("##wall_thickness", &m_configuration.wall_thickness, 0.0f, 5.0f);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", into_u8(_u8L("Shell thickness for solid cells (0 = completely solid)")).c_str());
            }
            
            // Cell style selection (only for solid cells)
            ImGui::Text("%s:", into_u8(_u8L("Cell Style")).c_str());
            const char* cell_styles[] = { "Pure", "Rounded", "Chamfered", "Crystalline", "Organic", "Faceted" };
            int current_style = static_cast<int>(m_configuration.cell_style);
            if (ImGui::Combo("##cell_style", &current_style, cell_styles, IM_ARRAYSIZE(cell_styles))) {
                m_configuration.cell_style = static_cast<Configuration::CellStyle>(current_style);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", into_u8(_u8L("Pure: Mathematical Voronoi\nRounded: Smooth corners\nChamfered: Beveled edges\nCrystalline: Angular cuts\nOrganic: Flowing surfaces\nFaceted: Extra detail")).c_str());
            }
        }

        // Edge thickness (only for wireframe)
        if (m_configuration.hollow_cells) {
            ImGui::Text("%s:", into_u8(_u8L("Edge thickness")).c_str());
            ImGui::SliderFloat("##edge_thickness", &m_configuration.edge_thickness, 0.1f, 5.0f);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", into_u8(_u8L("Thickness of wireframe struts between Voronoi vertices")).c_str());
            }

            // Edge shape selection (only for wireframe)
            ImGui::Text("%s:", into_u8(_u8L("Edge shape")).c_str());
            const char* edge_shapes[] = { "Cylinder", "Square", "Hexagon", "Octagon", "Star" };
            int current_shape = static_cast<int>(m_configuration.edge_shape);
            if (ImGui::Combo("##edge_shape", &current_shape, edge_shapes, IM_ARRAYSIZE(edge_shapes))) {
                m_configuration.edge_shape = static_cast<Configuration::EdgeShape>(current_shape);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", into_u8(_u8L("Cross-section shape of wireframe struts")).c_str());
            }

            // Edge detail/segments (only for wireframe)
            ImGui::Text("%s:", into_u8(_u8L("Edge detail")).c_str());
            ImGui::SliderInt("##edge_segments", &m_configuration.edge_segments, 3, 32);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", into_u8(_u8L("Number of segments for edge cross-section (higher = smoother)")).c_str());
            }

            // Edge curvature control (only for wireframe)
            ImGui::Text("%s:", into_u8(_u8L("Edge curvature")).c_str());
            ImGui::SliderFloat("##edge_curvature", &m_configuration.edge_curvature, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", into_u8(_u8L("Amount of curve/bend in struts (0 = straight, 1 = maximum curve)")).c_str());
            }

            // Edge subdivisions control (only for wireframe)
            ImGui::Text("%s:", into_u8(_u8L("Edge subdivisions")).c_str());
            ImGui::SliderInt("##edge_subdivisions", &m_configuration.edge_subdivisions, 0, 10);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", into_u8(_u8L("Number of curve segments per edge (0 = straight, higher = smoother curves)")).c_str());
            }
        }

        // Cell generation mode toggle with info
        ImGui::Text("%s:", into_u8(_u8L("Structure")).c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", into_u8(_u8L("Choose between solid Voronoi cells or wireframe edges\nBoth modes use Voro++ for fast generation!")).c_str());
        }

        if (ImGui::RadioButton(into_u8(_u8L("Solid cells")).c_str(), !m_configuration.hollow_cells)) {
            m_configuration.hollow_cells = false;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", into_u8(_u8L("Generate solid polyhedral Voronoi cells\n(Voro++ tessellation)")).c_str());
        }
        ImGui::SameLine();
        if (ImGui::RadioButton(into_u8(_u8L("Wireframe")).c_str(), m_configuration.hollow_cells)) {
            m_configuration.hollow_cells = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", into_u8(_u8L("Generate Voronoi wireframe structure\n(Voro++ cell edges)")).c_str());
        }

        // Show current mode info
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.4f, 1.0f));
        if (m_configuration.hollow_cells) {
            ImGui::TextWrapped("Mode: Voro++ wireframe (edges from cell faces)");
        } else {
            ImGui::TextWrapped("Mode: Voro++ solid cells (polyhedral tessellation)");
        }
        ImGui::PopStyleColor();

        ImGui::Separator();

        // Random seed control (controls wireframe pattern/layout)
        ImGui::Text("%s:", tr_random_seed.c_str());
        ImGui::InputInt("##random_seed", &m_configuration.random_seed);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", into_u8(_u8L("Controls the pattern/layout of the wireframe structure")).c_str());
        }

        // Seed preview checkbox with randomize button on same line
        if (ImGui::Checkbox(tr_seed_preview.c_str(), &m_configuration.show_seed_preview)) {
            if (m_configuration.show_seed_preview) {
                update_seed_preview();
            } else {
                m_seed_preview_points.clear();
                m_seed_preview_points_exact.clear();
                m_seed_preview_colors.clear();
                m_2d_voronoi_cells.clear();
                m_2d_delaunay_edges.clear();
                clear_voronoi_preview();
            }
        }

        // Randomize button on same line as preview seeds checkbox
        ImGui::SameLine();

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, m_is_dark_mode ? ImVec4(60 / 255.0f, 60 / 255.0f, 60 / 255.0f, 1.0f) : ImVec4(0.75f, 0.75f, 0.75f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_is_dark_mode ? ImVec4(70 / 255.0f, 70 / 255.0f, 70 / 255.0f, 1.0f) : ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, m_is_dark_mode ? ImVec4(50 / 255.0f, 50 / 255.0f, 50 / 255.0f, 1.0f) : ImVec4(0.65f, 0.65f, 0.65f, 1.0f));

        if (ImGui::Button(into_u8(_u8L("Randomize")).c_str())) {
            randomize_seed();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", into_u8(_u8L("Generate new random seed")).c_str());
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(1);

        if (m_configuration.show_seed_preview) {
            // Update Preview button with secondary styling
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, m_is_dark_mode ? ImVec4(60 / 255.0f, 60 / 255.0f, 60 / 255.0f, 1.0f) : ImVec4(0.75f, 0.75f, 0.75f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_is_dark_mode ? ImVec4(70 / 255.0f, 70 / 255.0f, 70 / 255.0f, 1.0f) : ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, m_is_dark_mode ? ImVec4(50 / 255.0f, 50 / 255.0f, 50 / 255.0f, 1.0f) : ImVec4(0.65f, 0.65f, 0.65f, 1.0f));

            if (ImGui::Button(into_u8(_u8L("Update Preview")).c_str()))
                update_seed_preview();

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(1);

            // 2D Voronoi Preview
            ImGui::Separator();
            ImGui::Text("%s:", into_u8(_u8L("2D Preview")).c_str());
            render_2d_voronoi_preview();
        }

        ImGui::Separator();

        // Advanced Options Section (collapsible)
        if (ImGui::CollapsingHeader(tr_advanced_options.c_str())) {

            ImGui::Text("%s:", into_u8(_u8L("Voronoi Mode")).c_str());
            bool restricted_changed = ImGui::Checkbox(into_u8(_u8L("Restricted to surface (RVD)")).c_str(), &m_configuration.restricted_voronoi);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", into_u8(_u8L("Clip Voronoi to the mesh surface using Restricted Voronoi Diagram (RVD)")).c_str());
            }
            if (restricted_changed && m_configuration.show_seed_preview) {
                update_seed_preview();
            }

            ImGui::Separator();
            
            // Lloyd's Relaxation
            ImGui::Text("%s:", into_u8(_u8L("Uniformity")).c_str());
            if (ImGui::Checkbox(tr_relax_seeds.c_str(), &m_configuration.relax_seeds)) {
                // Config changed
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", into_u8(_u8L("Iteratively move seeds to cell centroids for uniform cell sizes\nRecommended: 3-5 iterations\nCost: +150-500ms")).c_str());
            }
            
            if (m_configuration.relax_seeds) {
                ImGui::Indent();
                ImGui::Text("%s:", into_u8(_u8L("Iterations")).c_str());
                ImGui::SliderInt("##relaxation_iterations", &m_configuration.relaxation_iterations, 1, 10);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", into_u8(_u8L("Number of Lloyd iterations (3-5 typical, diminishing returns after 5)")).c_str());
                }
                ImGui::Unindent();
            }
            
            ImGui::Separator();
            
            // Weighted Voronoi (Power Diagram)
            ImGui::Text("%s:", into_u8(_u8L("Cell Sizing")).c_str());
            bool weighted_changed = ImGui::Checkbox(into_u8(_u8L("Weighted (Variable Density)")).c_str(), &m_configuration.use_weighted_cells);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", into_u8(_u8L("Enable weighted Voronoi (power diagram) for variable cell sizes\n"
                                                     "✓ Standard: Equal-sized cells\n"
                                                     "✓ Weighted: Cell size varies by weight\n"
                                                     "Use for gradient density or stress-optimized structures")).c_str());
            }
            
            // Show mode indicator
            if (m_configuration.use_weighted_cells) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 1.0f, 1.0f));
                ImGui::TextWrapped("Mode: Power Diagram (Variable Density)");
                ImGui::PopStyleColor();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.4f, 1.0f));
                ImGui::TextWrapped("Mode: Standard Voronoi (Uniform)");
                ImGui::PopStyleColor();
            }
            
            if (m_configuration.use_weighted_cells) {
                ImGui::Indent();
                
                // Density center position
                ImGui::Text("%s:", into_u8(_u8L("Density Center")).c_str());
                float density_center[3] = {
                    static_cast<float>(m_configuration.density_center.x()),
                    static_cast<float>(m_configuration.density_center.y()),
                    static_cast<float>(m_configuration.density_center.z())
                };
                if (ImGui::InputFloat3("##density_center", density_center)) {
                    m_configuration.density_center = Vec3d(density_center[0], density_center[1], density_center[2]);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", into_u8(_u8L("Point of highest density (smallest cells)\n"
                                                         "Cells grow larger with distance from this point")).c_str());
                }
                
                // Quick center button
                ImGui::SameLine();
                if (ImGui::SmallButton(into_u8(_u8L("Center")).c_str())) {
                    set_center_position();  // Use existing function to set to model center
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", into_u8(_u8L("Set to model center")).c_str());
                }
                
                // Density falloff
                ImGui::Text("%s:", into_u8(_u8L("Density Falloff")).c_str());
                ImGui::SliderFloat("##density_falloff", &m_configuration.density_falloff, 0.1f, 10.0f, "%.2f");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", into_u8(_u8L("How quickly density decreases with distance\n"
                                                         "Low (0.1-1.0): Gradual transition\n"
                                                         "Medium (1.0-3.0): Moderate gradient\n"
                                                         "High (3.0-10.0): Sharp transition\n"
                                                         "Formula: weight = (1 + dist × falloff)²")).c_str());
                }
                
                // Info about weights
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
                ImGui::TextWrapped("Weights auto-generated from density center");
                ImGui::PopStyleColor();
                
                ImGui::Unindent();
            }
            
            ImGui::Separator();
            
            // Multi-Scale Hierarchical
            ImGui::Text("%s:", into_u8(_u8L("Complexity")).c_str());
            if (ImGui::Checkbox(tr_multi_scale.c_str(), &m_configuration.multi_scale)) {
                // Config changed
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", into_u8(_u8L("Generate hierarchical structure with multiple density levels\nCoarse core → Fine surface detail\nCost: 2-4× generation time")).c_str());
            }
            
            if (m_configuration.multi_scale) {
                ImGui::Indent();
                ImGui::Text("%s:", tr_scale_levels.c_str());
                int num_levels = static_cast<int>(m_configuration.scale_seed_counts.size());
                if (ImGui::SliderInt("##scale_levels", &num_levels, 2, 4)) {
                    m_configuration.scale_seed_counts.resize(num_levels);
                    m_configuration.scale_thicknesses.resize(num_levels);
                    
                    // Auto-calculate reasonable defaults
                    for (int i = 0; i < num_levels; ++i) {
                        float ratio = std::pow(4.0f, static_cast<float>(i));  // 4× increase per level
                        m_configuration.scale_seed_counts[i] = static_cast<int>(m_configuration.num_seeds * ratio / 4.0f);
                        m_configuration.scale_thicknesses[i] = m_configuration.edge_thickness * std::pow(0.5f, static_cast<float>(i));
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", into_u8(_u8L("Number of hierarchy levels (2-4 typical)")).c_str());
                }
                
                // Show calculated seed counts
                std::string scale_info = "Seeds per level: ";
                for (size_t i = 0; i < m_configuration.scale_seed_counts.size(); ++i) {
                    scale_info += std::to_string(m_configuration.scale_seed_counts[i]);
                    if (i < m_configuration.scale_seed_counts.size() - 1)
                        scale_info += " → ";
                }
                ImGui::TextWrapped("%s", scale_info.c_str());
                ImGui::Unindent();
            }
            
            ImGui::Separator();
            
            // Anisotropic Transformation
            ImGui::Text("%s:", into_u8(_u8L("Directional Strength")).c_str());
            if (ImGui::Checkbox(tr_anisotropic.c_str(), &m_configuration.anisotropic)) {
                // Config changed
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", into_u8(_u8L("Elongate cells along load direction for directional strength\nIdeal for vertical towers or horizontal beams\nCost: <5ms (minimal)")).c_str());
            }
            
            if (m_configuration.anisotropic) {
                ImGui::Indent();
                
                // Direction presets
                ImGui::Text("%s:", tr_anisotropy_direction.c_str());
                if (ImGui::Button("Vertical (Z)")) {
                    m_configuration.anisotropy_direction = Vec3d(0, 0, 1);
                }
                ImGui::SameLine();
                if (ImGui::Button("Horizontal (X)")) {
                    m_configuration.anisotropy_direction = Vec3d(1, 0, 0);
                }
                ImGui::SameLine();
                if (ImGui::Button("Horizontal (Y)")) {
                    m_configuration.anisotropy_direction = Vec3d(0, 1, 0);
                }
                
                // Manual direction input
                float dir[3] = { 
                    static_cast<float>(m_configuration.anisotropy_direction.x()),
                    static_cast<float>(m_configuration.anisotropy_direction.y()),
                    static_cast<float>(m_configuration.anisotropy_direction.z())
                };
                if (ImGui::InputFloat3("##aniso_dir", dir)) {
                    m_configuration.anisotropy_direction = Vec3d(dir[0], dir[1], dir[2]);
                    // Normalize
                    double len = m_configuration.anisotropy_direction.norm();
                    if (len > 1e-6) {
                        m_configuration.anisotropy_direction /= len;
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", into_u8(_u8L("Manual direction vector (will be normalized)")).c_str());
                }
                
                // Stretch ratio
                ImGui::Text("%s:", tr_anisotropy_ratio.c_str());
                ImGui::SliderFloat("##aniso_ratio", &m_configuration.anisotropy_ratio, 1.0f, 5.0f);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", into_u8(_u8L("1.0 = isotropic (no stretch)\n1.5-2.5 = moderate (typical)\n3.0+ = extreme elongation")).c_str());
                }
                
                ImGui::Unindent();
            }
            
            ImGui::Separator();
            
            // Printability Options
            ImGui::Text("%s:", tr_printability.c_str());
            ImGui::Checkbox(into_u8(_u8L("Enforce watertight")).c_str(), &m_configuration.enforce_watertight);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", into_u8(_u8L("Fail generation if result is not watertight (has holes)")).c_str());
            }
            
            ImGui::Checkbox(into_u8(_u8L("Auto-repair")).c_str(), &m_configuration.auto_repair);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", into_u8(_u8L("Automatically attempt to repair holes and manifold issues")).c_str());
            }
            
            ImGui::Text("%s:", into_u8(_u8L("Min wall thickness (mm)")).c_str());
            ImGui::SliderFloat("##min_wall", &m_configuration.min_wall_thickness, 0.2f, 2.0f);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", into_u8(_u8L("Minimum printable wall thickness (typically 0.4mm for FDM)")).c_str());
            }
            
            ImGui::Text("%s:", into_u8(_u8L("Min feature size (mm)")).c_str());
            ImGui::SliderFloat("##min_feature", &m_configuration.min_feature_size, 0.1f, 1.0f);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", into_u8(_u8L("Minimum printable feature size")).c_str());
            }
            
            ImGui::Separator();
            
            // Weighted Voronoi (Variable Density)
            ImGui::Text("%s:", into_u8(_u8L("Variable Density")).c_str());
            if (ImGui::Checkbox(into_u8(_u8L("Use weighted cells")).c_str(), &m_configuration.use_weighted_cells)) {
                // Config changed
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", into_u8(_u8L("Create variable density infill (smaller cells = higher density)\nUseful for gradient infill and stress concentration")).c_str());
            }
            
            if (m_configuration.use_weighted_cells) {
                ImGui::Indent();
                
                ImGui::Text("%s:", into_u8(_u8L("Density center")).c_str());
                float density_center[3] = {
                    static_cast<float>(m_configuration.density_center.x()),
                    static_cast<float>(m_configuration.density_center.y()),
                    static_cast<float>(m_configuration.density_center.z())
                };
                if (ImGui::InputFloat3("##density_center", density_center)) {
                    m_configuration.density_center = Vec3d(density_center[0], density_center[1], density_center[2]);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", into_u8(_u8L("Point where density is highest (cells smallest)\nCells grow larger away from this point")).c_str());
                }
                
                if (ImGui::Button(into_u8(_u8L("Use Object Center")).c_str())) {
                    // Calculate object center from mesh bounds
                    if (m_volume) {
                        BoundingBoxf3 bbox = m_volume->mesh().bounding_box();
                        m_configuration.density_center = (bbox.min + bbox.max) * 0.5;
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", into_u8(_u8L("Set density center to object's geometric center")).c_str());
                }
                
                ImGui::Text("%s:", into_u8(_u8L("Falloff")).c_str());
                ImGui::SliderFloat("##density_falloff", &m_configuration.density_falloff, 0.5f, 5.0f);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", into_u8(_u8L("How quickly density decreases with distance\nLower = gradual change, Higher = sharp transition")).c_str());
                }
                
                ImGui::Unindent();
            }
            
            ImGui::Separator();
            
            // Load Optimization
            ImGui::Text("%s:", into_u8(_u8L("Load Optimization")).c_str());
            if (ImGui::Checkbox(into_u8(_u8L("Optimize for load")).c_str(), &m_configuration.optimize_for_load)) {
                // Config changed
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", into_u8(_u8L("Stretch cells along load direction for improved strength\nRecommended for parts with known load direction")).c_str());
            }
            
            if (m_configuration.optimize_for_load) {
                ImGui::Indent();
                
                ImGui::Text("%s:", into_u8(_u8L("Load direction")).c_str());
                if (ImGui::Button("Gravity (Down)")) {
                    m_configuration.load_direction = Vec3d(0, 0, -1);
                }
                ImGui::SameLine();
                if (ImGui::Button("Up")) {
                    m_configuration.load_direction = Vec3d(0, 0, 1);
                }
                
                float load_dir[3] = {
                    static_cast<float>(m_configuration.load_direction.x()),
                    static_cast<float>(m_configuration.load_direction.y()),
                    static_cast<float>(m_configuration.load_direction.z())
                };
                if (ImGui::InputFloat3("##load_dir", load_dir)) {
                    m_configuration.load_direction = Vec3d(load_dir[0], load_dir[1], load_dir[2]);
                    double len = m_configuration.load_direction.norm();
                    if (len > 1e-6) {
                        m_configuration.load_direction /= len;
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", into_u8(_u8L("Direction of primary load (will be normalized)")).c_str());
                }
                
                ImGui::Text("%s:", into_u8(_u8L("Stretch factor")).c_str());
                ImGui::SliderFloat("##load_stretch", &m_configuration.load_stretch_factor, 1.0f, 2.0f);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", into_u8(_u8L("1.0 = no stretch\n1.2 = 20% stretch (typical)\n1.5+ = aggressive")).c_str());
                }
                
                ImGui::Unindent();
            }
            
            ImGui::Separator();
            
            // Printability Validation
            ImGui::Text("%s:", into_u8(_u8L("Pre-Validation")).c_str());
            if (ImGui::Checkbox(into_u8(_u8L("Validate before generation")).c_str(), &m_configuration.validate_printability)) {
                // Config changed
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", into_u8(_u8L("Check if settings will produce printable features\nWarns if cells are too small for selected nozzle")).c_str());
            }
        }

        ImGui::Separator();

        // Triangle painting exclusion
        if (!m_configuration.enable_triangle_painting) {
            // Show Painting button when not in painting mode with proper styling
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, m_is_dark_mode ? ImVec4(38 / 255.0f, 46 / 255.0f, 48 / 255.0f, 1.0f) : ImVec4(0.70f, 0.70f, 0.70f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_is_dark_mode ? ImVec4(50 / 255.0f, 58 / 255.0f, 61 / 255.0f, 1.0f) : ImVec4(0.80f, 0.80f, 0.80f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, m_is_dark_mode ? ImVec4(60 / 255.0f, 68 / 255.0f, 71 / 255.0f, 1.0f) : ImVec4(0.60f, 0.60f, 0.60f, 1.0f));

            if (ImGui::Button(into_u8(_u8L("Painting")).c_str())) {
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: Painting button clicked, m_volume: " << (m_volume ? "VALID" : "NULL");

                // Only enable painting if we have a valid volume
                if (m_volume) {
                    m_configuration.enable_triangle_painting = true;
                    // Initialize painting system
                    update_from_model_object(true);
                    // Enable clipping plane for painting
                    if (m_c && m_c->object_clipper()) {
                        m_c->object_clipper()->set_position(0.5, false);
                        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: Clipping plane enabled at position 0.5";
                    }
                    request_rerender();
                } else {
                    BOOST_LOG_TRIVIAL(warning) << "GLGizmoVoronoi: Cannot enable painting - no volume available";
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", into_u8(_u8L("Paint areas to exclude from Voronoi generation")).c_str());
            }

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(1);
        }
        else {
            // Show painting controls and Apply button when in painting mode
            ImGui::Text("%s:", into_u8(_u8L("Brush radius")).c_str());
            ImGui::SliderFloat("##cursor_radius", &m_cursor_radius,
                get_cursor_radius_min(), get_cursor_radius_max(), "%.1f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", into_u8(_u8L("Size of the paint brush")).c_str());
            }

            ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.2f, 1.0f),
                "%s", into_u8(_u8L("Paint surfaces red to exclude")).c_str());
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                "%s", into_u8(_u8L("Click & drag to paint")).c_str());

            // Apply button - only visible when painting is active with proper styling
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, m_is_dark_mode ? ImVec4(43 / 255.0f, 64 / 255.0f, 54 / 255.0f, 1.0f) : ImVec4(0.86f, 0.99f, 0.91f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_is_dark_mode ? ImVec4(50 / 255.0f, 74 / 255.0f, 64 / 255.0f, 1.0f) : ImVec4(0.76f, 0.94f, 0.86f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, m_is_dark_mode ? ImVec4(35 / 255.0f, 56 / 255.0f, 46 / 255.0f, 1.0f) : ImVec4(0.81f, 0.97f, 0.88f, 1.0f));

            if (ImGui::Button(into_u8(_u8L("Apply")).c_str())) {
                // Apply the painting changes
                update_model_object();
                m_configuration.enable_triangle_painting = false;
                // Disable clipping plane when exiting painting mode
                if (m_c && m_c->object_clipper()) {
                    m_c->object_clipper()->set_position(-1., false);
                }
                request_rerender();
            }

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(1);

            ImGui::SameLine();

            // Cancel button with warning styling
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, m_is_dark_mode ? ImVec4(64 / 255.0f, 43 / 255.0f, 43 / 255.0f, 1.0f) : ImVec4(0.99f, 0.86f, 0.86f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_is_dark_mode ? ImVec4(74 / 255.0f, 50 / 255.0f, 50 / 255.0f, 1.0f) : ImVec4(0.94f, 0.76f, 0.76f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, m_is_dark_mode ? ImVec4(56 / 255.0f, 35 / 255.0f, 35 / 255.0f, 1.0f) : ImVec4(0.97f, 0.81f, 0.81f, 1.0f));

            if (ImGui::Button(into_u8(_u8L("Cancel")).c_str())) {
                // Cancel painting mode without applying changes
                m_configuration.enable_triangle_painting = false;
                // Disable clipping plane when exiting painting mode
                if (m_c && m_c->object_clipper()) {
                    m_c->object_clipper()->set_position(-1., false);
                }
                // Reset any pending changes if needed
                update_from_model_object(true);
                request_rerender();
            }

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(1);
        }

        ImGui::Separator();

        // Apply button
        {
            // Check status and volume, but release lock before calling apply_voronoi()
            bool is_idle;
            bool has_volume;
            {
                std::lock_guard<std::mutex> lock(m_state_mutex);
                is_idle = (m_state.status == State::idle);
                has_volume = (m_volume != nullptr);
            }

            if (is_idle) {
                // Generate button with primary styling
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);

                // Disable button if no volume
                if (!has_volume) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, m_is_dark_mode ? ImVec4(0 / 255.0f, 174 / 255.0f, 66 / 255.0f, 1.0f) : ImVec4(0 / 255.0f, 174 / 255.0f, 66 / 255.0f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_is_dark_mode ? ImVec4(26 / 255.0f, 190 / 255.0f, 92 / 255.0f, 1.0f) : ImVec4(26 / 255.0f, 190 / 255.0f, 92 / 255.0f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, m_is_dark_mode ? ImVec4(0 / 255.0f, 158 / 255.0f, 54 / 255.0f, 1.0f) : ImVec4(0 / 255.0f, 158 / 255.0f, 54 / 255.0f, 1.0f));
                }

                // Button is always rendered, but only clickable if has_volume
                bool button_clicked = ImGui::Button(into_u8(_u8L("Generate Voronoi")).c_str());
                if (button_clicked && has_volume) {
                    BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: Generate button clicked, has_volume: true";
                    // Just call apply_voronoi - it will handle the mutex and check if already running
                    apply_voronoi();
                } else if (button_clicked && !has_volume) {
                    BOOST_LOG_TRIVIAL(warning) << "GLGizmoVoronoi: Cannot generate - m_volume is NULL!";
                }

                if (!has_volume) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "(Select a model first)");
                }

                ImGui::PopStyleColor(3);
                ImGui::PopStyleVar(1);
            }
            else if (m_state.status == State::running) {
                ImGui::Text("%s %d%%", into_u8(_u8L("Processing...")).c_str(), m_state.progress);

                // Cancel button with warning styling
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, m_is_dark_mode ? ImVec4(64 / 255.0f, 43 / 255.0f, 43 / 255.0f, 1.0f) : ImVec4(0.99f, 0.86f, 0.86f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_is_dark_mode ? ImVec4(74 / 255.0f, 50 / 255.0f, 50 / 255.0f, 1.0f) : ImVec4(0.94f, 0.76f, 0.76f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, m_is_dark_mode ? ImVec4(56 / 255.0f, 35 / 255.0f, 35 / 255.0f, 1.0f) : ImVec4(0.97f, 0.81f, 0.81f, 1.0f));

                if (ImGui::Button(into_u8(_u8L("Cancel")).c_str())) {
                    stop_worker_thread_request();
                }

                ImGui::PopStyleColor(3);
                ImGui::PopStyleVar(1);
            }
        }

        // Close button with secondary styling
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, m_is_dark_mode ? ImVec4(60 / 255.0f, 60 / 255.0f, 60 / 255.0f, 1.0f) : ImVec4(0.75f, 0.75f, 0.75f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_is_dark_mode ? ImVec4(70 / 255.0f, 70 / 255.0f, 70 / 255.0f, 1.0f) : ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, m_is_dark_mode ? ImVec4(50 / 255.0f, 50 / 255.0f, 50 / 255.0f, 1.0f) : ImVec4(0.65f, 0.65f, 0.65f, 1.0f));

        if (ImGui::Button(into_u8(_u8L("Close")).c_str())) {
            close();
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(1);
    }

    void GLGizmoVoronoi::on_set_state()
    {
        fprintf(stderr, "GLGizmoVoronoi: on_set_state() ENTRY\n");
        fflush(stderr);
        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() START - state: " << (int)get_state();

        try {
            // CRITICAL: Grab volume BEFORE calling base class, because base class may affect selection
            if (get_state() == GLGizmoBase::EState::On) {
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - activating gizmo, capturing volume BEFORE base class";

                // Ensure painting mode is disabled when first activating
                m_configuration.enable_triangle_painting = false;
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - painting mode explicitly disabled";

                // Check if plater is available
                Plater* plater = wxGetApp().plater();
                if (plater) {
                    const Selection& selection = m_parent.get_selection();
                    BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - selection obtained BEFORE base class";

                    Model& model = plater->model();
                    BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - model obtained BEFORE base class";

                    m_volume = get_model_volume(selection, model);
                    BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - volume captured BEFORE base class: " << (m_volume ? "VALID" : "NULL");

                    // Store the ORIGINAL mesh for repeated generation
                    // CRITICAL: Only capture ONCE - never overwrite to preserve true input mesh
                    if (m_volume && m_original_mesh.vertices.empty()) {
                        m_original_mesh = m_volume->mesh().its;
                        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - original mesh captured, vertices: " << m_original_mesh.vertices.size();
                    } else if (!m_original_mesh.vertices.empty()) {
                        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - preserving original mesh (vertices: " << m_original_mesh.vertices.size() << "), current volume has: " << (m_volume ? std::to_string(m_volume->mesh().its.vertices.size()) : "NULL");
                    }
                } else {
                    BOOST_LOG_TRIVIAL(warning) << "GLGizmoVoronoi: on_set_state() - plater is NULL, can't capture volume";
                }
            }

            fprintf(stderr, "GLGizmoVoronoi: on_set_state() calling BASE CLASS\n");
            fflush(stderr);
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - calling base class";

            GLGizmoPainterBase::on_set_state();

            fprintf(stderr, "GLGizmoVoronoi: on_set_state() BASE CLASS returned\n");
            fflush(stderr);
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - base class returned, enable_triangle_painting: " << m_configuration.enable_triangle_painting;

            // Keep clipping plane state as set by base class for proper mesh rendering
            // The base class enables it at position 0, which allows the mesh to be visible
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - keeping clipping plane state from base class";

            if (get_state() == GLGizmoBase::EState::On) {
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - post-base-class activation";

                m_move_to_center = true;

                // Schedule an update to initialize triangle selectors on first render
                // The base class will call update_from_model_object() during rendering
                // Note: m_schedule_update is private in base class, so we trigger update via data_changed
                request_rerender();

                // Make sure model is visible - ensure instances hider shows everything
                if (m_c) {
                    BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - m_c is valid, checking subsystems";

                    if (m_c->instances_hider()) {
                        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - instances_hider is available";
                        // The instances hider should show all instances by default
                    } else {
                        BOOST_LOG_TRIVIAL(warning) << "GLGizmoVoronoi: on_set_state() - instances_hider is NULL!";
                    }

                    if (m_c->selection_info()) {
                        const ModelObject* mo = m_c->selection_info()->model_object();
                        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - model_object: " << (mo ? "VALID" : "NULL");
                        if (mo) {
                            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - model has " << mo->volumes.size() << " volumes";
                            
                            // CRITICAL FIX: Initialize triangle selectors NOW to fix gray screen
                            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - initializing triangle selectors";
                            update_from_model_object(true);
                            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - triangle selectors initialized, count: " << m_triangle_selectors.size();
                        }
                    } else {
                        BOOST_LOG_TRIVIAL(warning) << "GLGizmoVoronoi: on_set_state() - selection_info is NULL, will retry during render";
                        // Triangle selectors will be initialized during first render cycle
                    }
                } else {
                    BOOST_LOG_TRIVIAL(error) << "GLGizmoVoronoi: on_set_state() - m_c is NULL! Model may not be visible!";
                }

                // Initialize seed preview if enabled
                if (m_configuration.show_seed_preview) {
                    BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - updating seed preview";
                    update_seed_preview();
                    BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - preview updates complete";
                }

                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - activation COMPLETE";
            }
            else {
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - deactivating gizmo";
                m_volume = nullptr;
                m_glmodel.reset();
                m_seed_preview_points.clear();
                m_seed_preview_points_exact.clear();
                m_seed_preview_colors.clear();
                clear_voronoi_preview();
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - deactivation COMPLETE";
            }
        }
        catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "GLGizmoVoronoi: on_set_state() EXCEPTION: " << e.what();
            throw;
        }
        catch (...) {
            BOOST_LOG_TRIVIAL(error) << "GLGizmoVoronoi: on_set_state() UNKNOWN EXCEPTION";
            throw;
        }
    }

    void GLGizmoVoronoi::data_changed(bool is_serializing)
    {
        set_painter_gizmo_data(m_parent.get_selection());
    }

    void GLGizmoVoronoi::render_painter_gizmo() const
    {
        if (!m_configuration.enable_triangle_painting || m_triangle_selectors.empty() || m_c == nullptr)
            return;

        glsafe(::glEnable(GL_BLEND));
        glsafe(::glEnable(GL_DEPTH_TEST));

        render_triangles(m_parent.get_selection());

        if (m_c->object_clipper())
            m_c->object_clipper()->render_cut();
        if (m_c->instances_hider())
            m_c->instances_hider()->render_cut();

        render_cursor();

        glsafe(::glDisable(GL_BLEND));
    }

    void GLGizmoVoronoi::render_triangles(const Selection& selection) const
    {
        GLGizmoPainterBase::render_triangles(selection);
    }

    void GLGizmoVoronoi::update_model_object()
    {
        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::update_model_object()";

        if (!m_volume) {
            m_excluded_facet_mask.clear();
            return;
        }

        if (!m_c || !m_c->selection_info())
            return;

        const ModelObject* mo = m_c->selection_info()->model_object();
        if (!mo)
            return;

        int selector_idx = -1;
        TriangleSelectorPatch* selector_ptr = nullptr;
        for (const ModelVolume* mv : mo->volumes) {
            if (!mv->is_model_part())
                continue;
            ++selector_idx;
            if (mv == m_volume && selector_idx < int(m_triangle_selectors.size())) {
                selector_ptr = dynamic_cast<TriangleSelectorPatch*>(m_triangle_selectors[selector_idx].get());
                break;
            }
        }

        if (!selector_ptr) {
            BOOST_LOG_TRIVIAL(warning) << "GLGizmoVoronoi::update_model_object() - selector not found for current volume";
            m_excluded_facet_mask.clear();
            return;
        }

        const TriangleMesh& mesh = m_volume->mesh();
        size_t facet_count = static_cast<size_t>(mesh.facets_count());
        std::vector<uint8_t> mask(facet_count, 0);

        const auto& triangles = selector_ptr->get_triangles();
        for (const auto& triangle : triangles) {
            if (!triangle.valid() || triangle.is_split())
                continue;
            int source = triangle.source_triangle;
            if (source < 0 || static_cast<size_t>(source) >= mask.size())
                continue;
            if (triangle.get_state() == EnforcerBlockerType::BLOCKER)
                mask[static_cast<size_t>(source)] = 1;
        }

        m_excluded_facet_mask = std::move(mask);
        selector_ptr->request_update_render_data(true);
        request_rerender();
    }

    void GLGizmoVoronoi::update_from_model_object(bool first_update)
    {
        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::update_from_model_object(first_update=" << first_update << ")";

        m_triangle_selectors.clear();

        if (!m_c || !m_c->selection_info()) {
            m_volume = nullptr;
            m_excluded_facet_mask.clear();
            return;
        }

        ModelObject* mo = m_c->selection_info()->model_object();
        if (!mo)
            return;

        Plater* plater = wxGetApp().plater();
        if (!plater)
            return;

        Model& model = plater->model();
        ModelVolume* new_volume = get_model_volume(m_parent.get_selection(), model);
        bool volume_changed = (new_volume != m_volume);
        m_volume = new_volume;

        if (!m_volume) {
            m_excluded_facet_mask.clear();
            return;
        }

        if (volume_changed) {
            m_original_mesh = m_volume->mesh().its;
            m_excluded_facet_mask.assign(static_cast<size_t>(m_volume->mesh().facets_count()), 0);
        } else if (m_excluded_facet_mask.size() != static_cast<size_t>(m_volume->mesh().facets_count())) {
            m_excluded_facet_mask.assign(static_cast<size_t>(m_volume->mesh().facets_count()), 0);
        }

        std::vector<std::array<float, 4>> colors;
        colors.push_back(GLVolume::NEUTRAL_COLOR);
        colors.push_back(TriangleSelectorGUI::enforcers_color);
        colors.push_back(TriangleSelectorGUI::blockers_color);

        TriangleSelectorPatch* target_selector = nullptr;

        for (const ModelVolume* mv : mo->volumes) {
            if (!mv->is_model_part())
                continue;

            auto selector = std::make_unique<TriangleSelectorPatch>(mv->mesh(), colors);
            selector->set_wireframe_needed(true);
            selector->request_update_render_data(true);

            if (mv == m_volume)
                target_selector = selector.get();

            m_triangle_selectors.emplace_back(std::move(selector));
        }

        if (target_selector && !m_excluded_facet_mask.empty()) {
            auto& triangles = target_selector->get_triangles();
            const size_t mask_size = m_excluded_facet_mask.size();
            for (auto& triangle : triangles) {
                if (!triangle.valid() || triangle.is_split())
                    continue;
                int source = triangle.source_triangle;
                if (source < 0 || static_cast<size_t>(source) >= mask_size)
                    continue;
                bool blocked = m_excluded_facet_mask[static_cast<size_t>(source)] != 0;
                if (blocked && triangle.get_state() != EnforcerBlockerType::BLOCKER)
                    triangle.set_state(EnforcerBlockerType::BLOCKER);
                else if (!blocked && triangle.get_state() == EnforcerBlockerType::BLOCKER)
                    triangle.set_state(EnforcerBlockerType::NONE);
            }
            target_selector->request_update_render_data(true);
        }

        request_rerender();
    }

    void GLGizmoVoronoi::on_opening()
    {
        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::on_opening()";
        m_move_to_center = true;
        m_configuration.enable_triangle_painting = false;
    }

    void GLGizmoVoronoi::on_shutdown()
    {
        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::on_shutdown()";

        stop_worker_thread_request();
        if (m_worker.joinable())
            m_worker.join();

        {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            m_state.status = State::idle;
            m_state.result.reset();
            m_state.mv = nullptr;
            m_state.mesh_copy.vertices.clear();
            m_state.mesh_copy.indices.clear();
        }

        m_configuration.enable_triangle_painting = false;
        m_triangle_selectors.clear();
        m_volume = nullptr;
        m_glmodel.reset();
        m_seed_preview_points.clear();
        m_seed_preview_points_exact.clear();
        m_seed_preview_colors.clear();
        clear_voronoi_preview();
        m_excluded_facet_mask.clear();
    }

    wxString GLGizmoVoronoi::handle_snapshot_action_name(bool shift_down, Button button_down) const
    {
        if (shift_down)
            return wxString("Unselect all");
        if (button_down == Button::Left)
            return wxString("Exclude area");
        if (button_down == Button::Right)
            return wxString("Restore area");
        return wxString();
    }

    void GLGizmoVoronoi::on_render()
    {
        static bool logged_once = false;
        if (!logged_once) {
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::on_render() - First call";
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::on_render() - triangle_selectors.size(): " << m_triangle_selectors.size();
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::on_render() - enable_triangle_painting: " << m_configuration.enable_triangle_painting;
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::on_render() - show_seed_preview: " << m_configuration.show_seed_preview;
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::on_render() - m_c: " << (m_c ? "VALID" : "NULL");

            if (m_c) {
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::on_render() - object_clipper: " << (m_c->object_clipper() ? "VALID" : "NULL");
                if (m_c->object_clipper()) {
                    const auto* clipping_plane = m_c->object_clipper()->get_clipping_plane();
                    BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::on_render() - clipping_plane: " << (clipping_plane ? "EXISTS" : "NULL");
                    if (clipping_plane) {
                        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::on_render() - clipping_plane active: " << clipping_plane->is_active();
                    }
                }
            }
            logged_once = true;
        }

        // Note: render_painter_gizmo() is called by GLCanvas3D, not here
        // on_render() is for rendering overlays like seed preview

        if (m_configuration.show_seed_preview)
            render_voronoi_preview();
    }

    CommonGizmosDataID GLGizmoVoronoi::on_get_requirements() const
    {
        return CommonGizmosDataID(
            int(CommonGizmosDataID::SelectionInfo)
          | int(CommonGizmosDataID::InstancesHider)
          | int(CommonGizmosDataID::Raycaster)
          | int(CommonGizmosDataID::ObjectClipper)
        );
    }

    void GLGizmoVoronoi::apply_voronoi()
    {
        BOOST_LOG_TRIVIAL(info) << "=== GLGizmoVoronoi::apply_voronoi() - START ===";
        BOOST_LOG_TRIVIAL(info) << "apply_voronoi() - Thread ID: " << std::this_thread::get_id();

        if (!m_volume) {
            BOOST_LOG_TRIVIAL(error) << "apply_voronoi() - m_volume is NULL!";
            return;
        }

        BOOST_LOG_TRIVIAL(info) << "apply_voronoi() - m_volume is valid";

        // Copy mesh data immediately before starting thread to avoid dangling pointer
        // Use m_original_mesh to prevent layering on repeated generations
        indexed_triangle_set mesh_copy;
        try {
            // If original mesh is not stored yet, capture it now
            // IMPORTANT: Only capture if it looks like an actual model (not a previous Voronoi result)
            if (m_original_mesh.vertices.empty() && m_volume) {
                BOOST_LOG_TRIVIAL(info) << "apply_voronoi() - original mesh not stored, capturing now";
                m_original_mesh = m_volume->mesh().its;
                BOOST_LOG_TRIVIAL(info) << "apply_voronoi() - original mesh captured, vertices: " << m_original_mesh.vertices.size();
            } else if (!m_original_mesh.vertices.empty()) {
                BOOST_LOG_TRIVIAL(info) << "apply_voronoi() - using previously stored original mesh, vertices: " << m_original_mesh.vertices.size();
            }

            BOOST_LOG_TRIVIAL(info) << "apply_voronoi() - copying ORIGINAL mesh";
            mesh_copy = m_original_mesh;
            BOOST_LOG_TRIVIAL(info) << "apply_voronoi() - original mesh copied, vertices: " << mesh_copy.vertices.size() << ", faces: " << mesh_copy.indices.size();

            if (!m_excluded_facet_mask.empty()) {
                if (m_excluded_facet_mask.size() == mesh_copy.indices.size()) {
                    BOOST_LOG_TRIVIAL(info) << "apply_voronoi() - applying facet exclusion mask (" << m_excluded_facet_mask.size() << " entries)";
                    std::vector<stl_triangle_vertex_indices> filtered_indices;
                    filtered_indices.reserve(mesh_copy.indices.size());
                    for (size_t i = 0; i < mesh_copy.indices.size(); ++i) {
                        if (m_excluded_facet_mask[i] == 0)
                            filtered_indices.push_back(mesh_copy.indices[i]);
                    }
                    mesh_copy.indices.swap(filtered_indices);
                    BOOST_LOG_TRIVIAL(info) << "apply_voronoi() - mesh after exclusion: faces=" << mesh_copy.indices.size();
                } else {
                    BOOST_LOG_TRIVIAL(warning) << "apply_voronoi() - exclusion mask size (" << m_excluded_facet_mask.size()
                                               << ") does not match mesh face count (" << mesh_copy.indices.size() << ")";
                }
            }
        }
        catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "Failed to copy mesh for Voronoi generation: " << e.what();
            return;
        }
        catch (...) {
            BOOST_LOG_TRIVIAL(error) << "Failed to copy mesh for Voronoi generation (unknown exception)";
            return;
        }

        // CRITICAL: Join previous worker BEFORE acquiring mutex to avoid deadlock
        BOOST_LOG_TRIVIAL(info) << "apply_voronoi() - checking if worker is joinable";
        if (m_worker.joinable()) {
            BOOST_LOG_TRIVIAL(info) << "apply_voronoi() - previous worker IS joinable, joining now (this may take time)...";
            auto start_time = std::chrono::steady_clock::now();
            m_worker.join();
            auto end_time = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
            BOOST_LOG_TRIVIAL(info) << "apply_voronoi() - previous worker joined successfully (took " << duration << "ms)";
        } else {
            BOOST_LOG_TRIVIAL(info) << "apply_voronoi() - no previous worker to join";
        }

        BOOST_LOG_TRIVIAL(info) << "apply_voronoi() - about to acquire mutex";

        // Start worker thread
        try {
            BOOST_LOG_TRIVIAL(info) << "apply_voronoi() - attempting lock_guard on m_state_mutex...";
            std::lock_guard<std::mutex> lock(m_state_mutex);
            BOOST_LOG_TRIVIAL(info) << "apply_voronoi() - SUCCESS! Mutex acquired, current status: " << (int)m_state.status;

            // Check if somehow another thread started running (shouldn't happen since we joined)
            if (m_state.status == State::running) {
                BOOST_LOG_TRIVIAL(warning) << "GLGizmoVoronoi::apply_voronoi() - already running (unexpected!), returning";
                return;
            }

            m_state.status = State::running;
            m_state.progress = 0;
            m_state.config = m_configuration;
            m_state.mv = m_volume;  // Store pointer only for identity check in worker_finished
            m_state.mesh_copy = std::move(mesh_copy);  // Store mesh copy
            m_state.result.reset();
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::apply_voronoi() - state set to running";
        }
        catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "Exception in mutex section: " << e.what();
            return;
        }
        catch (...) {
            BOOST_LOG_TRIVIAL(error) << "Unknown exception in mutex section";
            return;
        }

        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::apply_voronoi() - Starting worker thread";
        m_worker = std::thread([this]() { process(); });
        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::apply_voronoi() - Worker thread started";
    }

    void GLGizmoVoronoi::close()
    {
        stop_worker_thread_request();
        if (m_worker.joinable())
            m_worker.join();

        // Post event to reset/close the gizmo (like BrimEars does)
        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_RESETGIZMOS));
    }

    void GLGizmoVoronoi::process()
    {
        BOOST_LOG_TRIVIAL(info) << "=== process() - Worker thread STARTED ===";
        BOOST_LOG_TRIVIAL(info) << "process() - Thread ID: " << std::this_thread::get_id();

        try {
            std::unique_ptr<indexed_triangle_set> result;

            // Get the input mesh from safe copy (not from pointer!)
            indexed_triangle_set input_mesh_copy;
            VoronoiMesh::Config voronoi_config;

            BOOST_LOG_TRIVIAL(info) << "process() - About to acquire mutex to read config...";
            {
                std::lock_guard<std::mutex> lock(m_state_mutex);
                BOOST_LOG_TRIVIAL(info) << "process() - Mutex acquired, status: " << (int)m_state.status;
                if (m_state.status != State::running) {
                    BOOST_LOG_TRIVIAL(warning) << "process() - Status is NOT running, exiting early";
                    return;
                }

                // Use the mesh copy, NOT the ModelVolume pointer!
                BOOST_LOG_TRIVIAL(info) << "process() - Copying mesh from state";
                input_mesh_copy = m_state.mesh_copy;
                BOOST_LOG_TRIVIAL(info) << "process() - Mesh copied: vertices=" << input_mesh_copy.vertices.size() << ", faces=" << input_mesh_copy.indices.size();

                // Convert configuration
                BOOST_LOG_TRIVIAL(info) << "process() - Converting configuration";
                voronoi_config = build_voronoi_config(m_state.config);

                // Set progress callback
                // Use try_lock to avoid deadlock - if we can't get the lock, just skip the update
                BOOST_LOG_TRIVIAL(info) << "process() - Setting up progress callback";
                voronoi_config.progress_callback = [this](int progress) -> bool {
                    std::unique_lock<std::mutex> lock(m_state_mutex, std::try_to_lock);
                    if (lock.owns_lock()) {
                        m_state.progress = progress;
                        return m_state.status == State::running;
                    }
                    // If we couldn't get the lock, assume we should continue
                    return true;
                    };
                BOOST_LOG_TRIVIAL(info) << "process() - Config setup complete, releasing mutex";
            }
            BOOST_LOG_TRIVIAL(info) << "process() - Mutex released, config ready";

            // Validate input mesh
            if (input_mesh_copy.vertices.empty() || input_mesh_copy.indices.empty()) {
                BOOST_LOG_TRIVIAL(error) << "Invalid input mesh for Voronoi generation";
                std::lock_guard<std::mutex> lock(m_state_mutex);
                m_state.status = State::idle;
                return;
            }

            // Generate Voronoi mesh
            BOOST_LOG_TRIVIAL(info) << "process() - *** Calling VoronoiMesh::generate() ***";
            auto gen_start = std::chrono::steady_clock::now();
            result = VoronoiMesh::generate(input_mesh_copy, voronoi_config);
            auto gen_end = std::chrono::steady_clock::now();
            auto gen_duration = std::chrono::duration_cast<std::chrono::milliseconds>(gen_end - gen_start).count();
            BOOST_LOG_TRIVIAL(info) << "process() - *** VoronoiMesh::generate() RETURNED (took " << gen_duration << "ms) ***";
            BOOST_LOG_TRIVIAL(info) << "process() - Result is " << (result ? "VALID" : "NULL");

            if (result && !result->vertices.empty() && !result->indices.empty()) {
                // Validate result before storing
                bool valid = true;
                for (const auto& face : result->indices) {
                    if (face[0] < 0 || face[0] >= result->vertices.size() ||
                        face[1] < 0 || face[1] >= result->vertices.size() ||
                        face[2] < 0 || face[2] >= result->vertices.size()) {
                        valid = false;
                        break;
                    }
                }

                if (valid) {
                    BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::process() - Result is valid, vertices: " << result->vertices.size() << ", faces: " << result->indices.size();
                    std::lock_guard<std::mutex> lock(m_state_mutex);
                    m_state.result = std::move(result);
                    m_state.progress = 100;

                    BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::process() - Calling worker_finished()";
                    call_after_if_active(this, [](GLGizmoVoronoi& gizmo) {
                        gizmo.worker_finished();
                        });
                }
                else {
                    BOOST_LOG_TRIVIAL(error) << "Invalid Voronoi mesh generated";
                    std::lock_guard<std::mutex> lock(m_state_mutex);
                    m_state.status = State::idle;
                }
            }
            else {
                BOOST_LOG_TRIVIAL(warning) << "Voronoi generation produced empty result";
                std::lock_guard<std::mutex> lock(m_state_mutex);
                m_state.status = State::idle;
            }

        }
        catch (const VoronoiCanceledException&) {
            // Cancelled by user
            std::lock_guard<std::mutex> lock(m_state_mutex);
            m_state.status = State::idle;
        }
        catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "Exception in Voronoi processing: " << e.what();
            std::lock_guard<std::mutex> lock(m_state_mutex);
            m_state.status = State::idle;
        }
        catch (...) {
            BOOST_LOG_TRIVIAL(error) << "Unknown error in Voronoi processing";
            std::lock_guard<std::mutex> lock(m_state_mutex);
            m_state.status = State::idle;
        }
    }

    void GLGizmoVoronoi::stop_worker_thread_request()
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        if (m_state.status == State::running)
            m_state.status = State::cancelling;
    }

    void GLGizmoVoronoi::worker_finished()
    {
        BOOST_LOG_TRIVIAL(info) << "=== worker_finished() - START ===";
        BOOST_LOG_TRIVIAL(info) << "worker_finished() - Thread ID: " << std::this_thread::get_id();

        std::unique_ptr<indexed_triangle_set> result_its;
        const ModelVolume* mv = nullptr;

        BOOST_LOG_TRIVIAL(info) << "worker_finished() - Acquiring mutex to get result...";
        {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            BOOST_LOG_TRIVIAL(info) << "worker_finished() - Mutex acquired";

            if (m_state.result && m_state.status == State::running) {
                BOOST_LOG_TRIVIAL(info) << "worker_finished() - Result available! Moving to result_its";
                BOOST_LOG_TRIVIAL(info) << "worker_finished() - Result mesh: vertices=" << m_state.result->vertices.size() << ", faces=" << m_state.result->indices.size();
                result_its = std::move(m_state.result);
                mv = m_state.mv;
                m_state.status = State::idle;
                BOOST_LOG_TRIVIAL(info) << "worker_finished() - Status set to idle";
            }
            else {
                BOOST_LOG_TRIVIAL(warning) << "worker_finished() - No result or not running! result=" << (m_state.result ? "exists" : "NULL") << ", status=" << (int)m_state.status;
                m_state.status = State::idle;
                return;
            }
        }
        BOOST_LOG_TRIVIAL(info) << "worker_finished() - Mutex released";

        // Apply the result to the model (outside of lock)
        if (result_its && mv && !result_its->vertices.empty()) {
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::worker_finished() - Applying result to model, vertices: " << result_its->vertices.size();

            // Get the model and update the volume's mesh
            Plater* plater = wxGetApp().plater();
            if (!plater) {
                BOOST_LOG_TRIVIAL(error) << "GLGizmoVoronoi::worker_finished() - Plater is NULL!";
                return;
            }

            Model& model = plater->model();
            const Selection& selection = m_parent.get_selection();
            const Selection::IndicesList& idxs = selection.get_volume_idxs();

            if (idxs.size() != 1)
                return;

            const GLVolume* selected_volume = selection.get_volume(*idxs.begin());
            if (!selected_volume)
                return;

            const GLVolume::CompositeID& cid = selected_volume->composite_id;
            if (cid.object_id < 0 || cid.volume_id < 0)
                return;

            ModelObject* obj = model.objects[cid.object_id];
            if (!obj || cid.volume_id >= obj->volumes.size())
                return;

            ModelVolume* volume = obj->volumes[cid.volume_id];
            if (volume != mv)
                return;

            // Preserve the object's position before replacing mesh
            Vec3d instance_offset = obj->instances.empty() ? Vec3d::Zero() : obj->instances[0]->get_offset();
            Vec3d volume_offset = volume->get_offset();

            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::worker_finished() - Preserving position: instance_offset=("
                << instance_offset.x() << "," << instance_offset.y() << "," << instance_offset.z()
                << "), volume_offset=(" << volume_offset.x() << "," << volume_offset.y() << "," << volume_offset.z() << ")";

            // Replace the mesh
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::worker_finished() - Replacing mesh in volume";
            TriangleMesh new_mesh(*result_its);
            volume->set_mesh(std::move(new_mesh));
            
            // Restore position BEFORE updating (so updates see correct position)
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::worker_finished() - Restoring original position";
            volume->set_offset(volume_offset);
            if (!obj->instances.empty()) {
                obj->instances[0]->set_offset(instance_offset);
            }
            
            // Recalculate geometry after position is set
            volume->calculate_convex_hull();
            volume->invalidate_convex_hull_2d();
            obj->invalidate_bounding_box();

            // CRITICAL: Proper update sequence
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::worker_finished() - Initiating model update";
            
            // Step 1: Notify plater that object changed (updates model internals)
            plater->changed_object(*obj);
            
            // Step 2: Reload the 3D scene to show new geometry  
            GLCanvas3D* canvas = plater->canvas3D();
            if (canvas) {
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::worker_finished() - Reloading 3D scene";
                canvas->reload_scene(true, true); // force=true, refresh_immediately=true
            }
            
            // Step 3: Update slicing/preview
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::worker_finished() - Scheduling background re-slice";
            plater->schedule_background_process();

            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::worker_finished() - COMPLETE, model replaced!";
            request_rerender();
        }
        else {
            BOOST_LOG_TRIVIAL(warning) << "GLGizmoVoronoi::worker_finished() - Cannot apply: result_its="
                << (result_its ? "valid" : "NULL") << ", mv=" << (mv ? "valid" : "NULL");
        }
    }

    void GLGizmoVoronoi::create_gui_cfg()
    {
        if (m_gui_cfg.has_value())
            return;

        m_gui_cfg = GuiCfg();
        m_gui_cfg->top_left_width = 300;      // Increased to prevent scrollbar from cutting off buttons
        m_gui_cfg->bottom_left_width = 320;   // Increased to prevent scrollbar from cutting off buttons
        m_gui_cfg->input_width = 160;         // Slightly wider for better usability
        m_gui_cfg->window_offset_x = 0;
        m_gui_cfg->window_offset_y = 650;
        m_gui_cfg->window_padding = 15;
    }

    void GLGizmoVoronoi::request_rerender()
    {
        // Guard against null plater/canvas during shutdown or initialization
        Plater* plater = wxGetApp().plater();
        if (!plater)
            return;

        GLCanvas3D* canvas = plater->canvas3D();
        if (canvas) {
            canvas->set_as_dirty();
            canvas->request_extra_frame();
        }
    }

    void GLGizmoVoronoi::set_center_position()
    {
        if (m_move_to_center && m_volume) {
            m_move_to_center = false;
            const Vec3d center = m_volume->mesh().center();
            m_configuration.density_center = center;
        }
    }

    void GLGizmoVoronoi::randomize_seed()
    {
        // Generate truly random seed using random_device and mt19937
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dis(0, 99999);
        m_configuration.random_seed = dis(gen);

        // Update preview if it's enabled
        if (m_configuration.show_seed_preview) {
            update_seed_preview();
        }
    }

    void GLGizmoVoronoi::update_seed_preview()
    {
        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() START";

        if (!m_volume) {
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() - no volume, returning";
            clear_voronoi_preview();
            return;
        }

        try {
            m_seed_preview_points.clear();
            m_seed_preview_points_exact.clear();
            m_seed_preview_colors.clear();
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() - cleared previous data";

            const indexed_triangle_set& mesh = !m_original_mesh.vertices.empty() ? m_original_mesh : m_volume->mesh().its;
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() - source mesh vertices: " << mesh.vertices.size();

            if (mesh.vertices.empty()) {
                BOOST_LOG_TRIVIAL(warning) << "GLGizmoVoronoi: update_seed_preview() - Empty mesh for seed preview";
                clear_voronoi_preview();
                return;
            }

            VoronoiMesh::Config voronoi_config = build_voronoi_config(m_configuration);

            std::vector<Vec3d> seeds;

            if (voronoi_config.multi_scale && !voronoi_config.scale_seed_counts.empty()) {
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() - multi-scale preview with "
                                        << voronoi_config.scale_seed_counts.size() << " levels";
                for (size_t level = 0; level < voronoi_config.scale_seed_counts.size(); ++level) {
                    VoronoiMesh::Config level_cfg = voronoi_config;
                    level_cfg.multi_scale = false;
                    level_cfg.num_seeds = voronoi_config.scale_seed_counts[level];
                    if (level < voronoi_config.scale_thicknesses.size())
                        level_cfg.edge_thickness = voronoi_config.scale_thicknesses[level];

                    auto level_seeds = VoronoiMesh::prepare_seed_points(mesh, level_cfg);
                    seeds.insert(seeds.end(), level_seeds.begin(), level_seeds.end());
                }
            } else {
                seeds = VoronoiMesh::prepare_seed_points(mesh, voronoi_config);
            }

            if (seeds.empty()) {
                BOOST_LOG_TRIVIAL(warning) << "GLGizmoVoronoi: update_seed_preview() - No seeds generated by VoronoiMesh::prepare_seed_points";
                clear_voronoi_preview();
                return;
            }

            m_seed_preview_points_exact = seeds;
            m_seed_preview_points.reserve(seeds.size());
            m_seed_preview_colors.reserve(seeds.size());
            for (size_t i = 0; i < seeds.size(); ++i) {
                m_seed_preview_points.push_back(seeds[i].cast<float>());
                m_seed_preview_colors.push_back(color_from_index(i));
            }

            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() - Final preview seed count: " << m_seed_preview_points.size();

            if (m_configuration.show_seed_preview) {
                update_voronoi_preview(m_seed_preview_points_exact);
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() - updating 2D voronoi preview";
                update_2d_voronoi_preview();
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() - 2D preview updated";
            } else {
                clear_voronoi_preview();
            }

            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() COMPLETE";
        }
        catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "GLGizmoVoronoi: update_seed_preview() EXCEPTION: " << e.what();
            m_seed_preview_points.clear();
            m_seed_preview_points_exact.clear();
            m_seed_preview_colors.clear();
            clear_voronoi_preview();
        }
        catch (...) {
            BOOST_LOG_TRIVIAL(error) << "GLGizmoVoronoi: update_seed_preview() UNKNOWN EXCEPTION";
            m_seed_preview_points.clear();
            m_seed_preview_points_exact.clear();
            m_seed_preview_colors.clear();
            clear_voronoi_preview();
        }
    }

    void GLGizmoVoronoi::render_2d_voronoi_preview()
    {
        if (m_2d_voronoi_cells.empty()) {
            return;
        }

        // Get ImGui draw list for drawing shapes
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        if (!draw_list) {
            return;
        }

        // Get the available region for drawing
        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        ImVec2 canvas_size = ImGui::GetContentRegionAvail();
        
        // Ensure minimum size
        if (canvas_size.x < 50.0f) canvas_size.x = 200.0f;
        if (canvas_size.y < 50.0f) canvas_size.y = 200.0f;
        
        // Limit maximum size for performance
        if (canvas_size.x > 400.0f) canvas_size.x = 400.0f;
        if (canvas_size.y > 400.0f) canvas_size.y = 400.0f;

        // Draw background
        ImU32 bg_color = m_is_dark_mode ? 
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.15f, 0.15f, 0.15f, 1.0f)) :
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.95f, 0.95f, 0.95f, 1.0f));
        draw_list->AddRectFilled(canvas_pos, 
            ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), 
            bg_color);

        // Draw Voronoi cells (filled polygons)
        for (const auto& cell : m_2d_voronoi_cells) {
            if (cell.vertices.size() < 3) continue;

            // Convert normalized coordinates [0,1] to screen coordinates
            std::vector<ImVec2> screen_vertices;
            screen_vertices.reserve(cell.vertices.size());
            
            for (const auto& vert : cell.vertices) {
                ImVec2 screen_pos;
                screen_pos.x = canvas_pos.x + vert.x() * canvas_size.x;
                screen_pos.y = canvas_pos.y + vert.y() * canvas_size.y;
                screen_vertices.push_back(screen_pos);
            }

            // Draw filled polygon
            if (screen_vertices.size() >= 3) {
                draw_list->AddConvexPolyFilled(screen_vertices.data(), 
                    static_cast<int>(screen_vertices.size()), 
                    cell.color);
            }
        }

        // Draw Voronoi edges (cell boundaries)
        ImU32 edge_color = m_is_dark_mode ?
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.7f, 0.7f, 0.7f, 1.0f)) :
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        
        for (const auto& cell : m_2d_voronoi_cells) {
            if (cell.vertices.size() < 2) continue;

            for (size_t i = 0; i < cell.vertices.size(); ++i) {
                const Vec2f& v0 = cell.vertices[i];
                const Vec2f& v1 = cell.vertices[(i + 1) % cell.vertices.size()];
                
                ImVec2 p0(canvas_pos.x + v0.x() * canvas_size.x, 
                         canvas_pos.y + v0.y() * canvas_size.y);
                ImVec2 p1(canvas_pos.x + v1.x() * canvas_size.x, 
                         canvas_pos.y + v1.y() * canvas_size.y);
                
                draw_list->AddLine(p0, p1, edge_color, 1.0f);
            }
        }

        // Draw Delaunay triangulation edges (if any)
        if (!m_2d_delaunay_edges.empty()) {
            ImU32 delaunay_color = ImGui::ColorConvertFloat4ToU32(ImVec4(0.3f, 0.3f, 0.8f, 0.5f));
            
            for (const auto& edge : m_2d_delaunay_edges) {
                ImVec2 p0(canvas_pos.x + edge.a.x() * canvas_size.x,
                         canvas_pos.y + edge.a.y() * canvas_size.y);
                ImVec2 p1(canvas_pos.x + edge.b.x() * canvas_size.x,
                         canvas_pos.y + edge.b.y() * canvas_size.y);
                
                draw_list->AddLine(p0, p1, delaunay_color, 1.0f);
            }
        }

        // Draw seed points (variable size for weighted mode)
        ImU32 seed_color = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImU32 seed_outline = ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.8f));
        
        bool is_weighted = m_configuration.use_weighted_cells && 
                          !m_configuration.cell_weights.empty() &&
                          m_configuration.cell_weights.size() == m_seed_preview_points_exact.size();
        
        // Compute weight range for sizing if weighted
        double min_weight = 1.0, max_weight = 1.0, weight_range = 1.0;
        if (is_weighted) {
            min_weight = m_configuration.cell_weights[0];
            max_weight = m_configuration.cell_weights[0];
            for (double w : m_configuration.cell_weights) {
                min_weight = std::min(min_weight, w);
                max_weight = std::max(max_weight, w);
            }
            weight_range = max_weight - min_weight;
            if (weight_range < 1e-9) weight_range = 1.0;
        }
        
        for (size_t i = 0; i < m_2d_voronoi_cells.size(); ++i) {
            const auto& cell = m_2d_voronoi_cells[i];
            ImVec2 seed_pos(canvas_pos.x + cell.seed_point.x() * canvas_size.x,
                           canvas_pos.y + cell.seed_point.y() * canvas_size.y);
            
            // Variable radius based on weight
            float radius = 2.0f;
            if (is_weighted && i < m_configuration.cell_weights.size()) {
                double normalized_weight = (m_configuration.cell_weights[i] - min_weight) / weight_range;
                radius = 1.5f + static_cast<float>(normalized_weight) * 3.5f; // 1.5 to 5.0
            }
            
            // Draw with outline for better visibility
            draw_list->AddCircleFilled(seed_pos, radius + 1.0f, seed_outline);
            draw_list->AddCircleFilled(seed_pos, radius, seed_color);
        }
        
        // Draw legend for weighted mode
        if (is_weighted) {
            ImVec2 legend_pos(canvas_pos.x + 5.0f, canvas_pos.y + canvas_size.y - 40.0f);
            ImU32 text_color = m_is_dark_mode ? 
                ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)) :
                ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
            
            // Legend background
            ImVec2 legend_size(80.0f, 30.0f);
            ImU32 legend_bg = m_is_dark_mode ?
                ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f, 0.2f, 0.2f, 0.8f)) :
                ImGui::ColorConvertFloat4ToU32(ImVec4(0.9f, 0.9f, 0.9f, 0.8f));
            draw_list->AddRectFilled(legend_pos, 
                ImVec2(legend_pos.x + legend_size.x, legend_pos.y + legend_size.y), 
                legend_bg, 3.0f);
            
            // Text
            draw_list->AddText(ImVec2(legend_pos.x + 5.0f, legend_pos.y + 3.0f), 
                              text_color, "Weighted");
            
            // Gradient bar (blue to red)
            for (int i = 0; i < 60; ++i) {
                float t = static_cast<float>(i) / 59.0f;
                ImU32 bar_color = ImGui::ColorConvertFloat4ToU32(
                    ImVec4(t, 0.3f, 1.0f - t, 1.0f));
                draw_list->AddRectFilled(
                    ImVec2(legend_pos.x + 5.0f + i, legend_pos.y + 18.0f),
                    ImVec2(legend_pos.x + 6.0f + i, legend_pos.y + 25.0f),
                    bar_color);
            }
        }
        
        // Draw mode indicator
        ImVec2 mode_pos(canvas_pos.x + canvas_size.x - 80.0f, canvas_pos.y + 5.0f);
        ImU32 mode_text_color = m_is_dark_mode ? 
            ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.8f)) :
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.8f));
        
        const char* mode_text = is_weighted ? "Power" : "Standard";
        draw_list->AddText(mode_pos, mode_text_color, mode_text);
        
        // Anisotropic indicator
        if (m_configuration.anisotropic) {
            ImVec2 aniso_pos(canvas_pos.x + 5.0f, canvas_pos.y + 5.0f);
            draw_list->AddText(aniso_pos, mode_text_color, "Anisotropic");
        }
        
        // Multi-scale indicator
        if (m_configuration.multi_scale) {
            ImVec2 ms_pos(canvas_pos.x + 5.0f, canvas_pos.y + 20.0f);
            draw_list->AddText(ms_pos, mode_text_color, "Multi-Scale");
        }

        // Reserve space in ImGui layout for the canvas
        ImGui::Dummy(canvas_size);
    }

    void GLGizmoVoronoi::update_2d_voronoi_preview()
    {
        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_2d_voronoi_preview() START";
        
        m_2d_voronoi_cells.clear();
        m_2d_delaunay_edges.clear();

        if (m_seed_preview_points_exact.empty()) {
            BOOST_LOG_TRIVIAL(warning) << "GLGizmoVoronoi: update_2d_voronoi_preview() - No seeds available";
            return;
        }

        try {
            // Project seeds onto XY plane and normalize to [0,1]
            std::vector<Vec3d> seeds_3d = m_seed_preview_points_exact;
            
            // Compute bounding box
            Vec3d min_pt = seeds_3d[0];
            Vec3d max_pt = seeds_3d[0];
            for (const auto& seed : seeds_3d) {
                min_pt = min_pt.cwiseMin(seed);
                max_pt = max_pt.cwiseMax(seed);
            }
            
            // Add margin
            Vec3d range = max_pt - min_pt;
            double margin = 0.1 * range.norm();
            min_pt -= Vec3d(margin, margin, margin);
            max_pt += Vec3d(margin, margin, margin);
            range = max_pt - min_pt;
            
            // Prevent division by zero
            if (range.x() < 1e-6) range.x() = 1.0;
            if (range.y() < 1e-6) range.y() = 1.0;

            // Apply anisotropic aspect ratio to preview if enabled
            double aspect_x = 1.0;
            double aspect_y = 1.0;
            if (m_configuration.anisotropic && m_configuration.anisotropy_ratio > 0.0f) {
                // Determine which axis to stretch based on anisotropy_direction
                Vec3d dir = m_configuration.anisotropy_direction.normalized();
                double abs_x = std::abs(dir.x());
                double abs_y = std::abs(dir.y());
                
                if (abs_x > abs_y) {
                    // Stretch in X direction
                    aspect_x = m_configuration.anisotropy_ratio;
                } else {
                    // Stretch in Y direction  
                    aspect_y = m_configuration.anisotropy_ratio;
                }
                
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: 2D preview - Applying anisotropic aspect: " 
                                         << aspect_x << " x " << aspect_y;
            }

            // Check if weighted mode is enabled
            bool use_weighted = m_configuration.use_weighted_cells;
            
            if (use_weighted && !m_configuration.cell_weights.empty() && 
                m_configuration.cell_weights.size() == seeds_3d.size()) {
                
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: 2D preview - Using WEIGHTED Voronoi (power diagram)";
                
                // Build weighted 2D Voronoi (power diagram)
                RT2 rt;
                std::vector<Point_2> seed_points_2d;
                std::vector<double> seed_weights;
                
                // Compute weight statistics for visualization
                double min_weight = m_configuration.cell_weights[0];
                double max_weight = m_configuration.cell_weights[0];
                for (double w : m_configuration.cell_weights) {
                    min_weight = std::min(min_weight, w);
                    max_weight = std::max(max_weight, w);
                }
                double weight_range = max_weight - min_weight;
                if (weight_range < 1e-9) weight_range = 1.0;
                
                for (size_t i = 0; i < seeds_3d.size(); ++i) {
                    const Vec3d& seed = seeds_3d[i];
                    double weight = m_configuration.cell_weights[i];
                    seed_weights.push_back(weight);
                    
                    // Normalize to [0,1] with anisotropic scaling
                    double x = (seed.x() - min_pt.x()) / range.x() / aspect_x;
                    double y = (seed.y() - min_pt.y()) / range.y() / aspect_y;
                    
                    // Clamp to valid range
                    x = std::max(0.0, std::min(1.0, x));
                    y = std::max(0.0, std::min(1.0, y));
                    
                    seed_points_2d.push_back(Point_2(x, y));
                    
                    // Scale weight to normalized space
                    double scaled_weight = weight / (range.x() * range.x());
                    
                    Weighted_point_2 wp(Point_2(x, y), scaled_weight);
                    rt.insert(wp);
                }
                
                // Build Voronoi diagram adaptor
                VD2_RT vd(rt);
                
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: 2D preview - Built weighted Voronoi with " 
                                         << vd.number_of_faces() << " faces";
                
                // Extract Voronoi cells
                int face_idx = 0;
                for (auto fit = vd.faces_begin(); fit != vd.faces_end(); ++fit, ++face_idx) {
                    VoronoiCell2D cell;
                    
                    // Get seed point for this face
                    auto vh = fit->dual();
                    if (rt.is_infinite(vh))
                        continue;
                    
                    Point_2 seed_pt = rt.geom_traits().construct_point_2_object()(vh->point());
                    cell.seed_point = Vec2f(static_cast<float>(seed_pt.x()), 
                                           static_cast<float>(seed_pt.y()));
                    
                    // Find which seed this corresponds to for weight-based coloring
                    size_t seed_idx = face_idx % seeds_3d.size();
                    for (size_t i = 0; i < seed_points_2d.size(); ++i) {
                        double dx = seed_pt.x() - seed_points_2d[i].x();
                        double dy = seed_pt.y() - seed_points_2d[i].y();
                        if (dx*dx + dy*dy < 1e-9) {
                            seed_idx = i;
                            break;
                        }
                    }
                    
                    // Color based on weight (gradient from blue=low to red=high)
                    if (seed_idx < seed_weights.size()) {
                        double normalized_weight = (seed_weights[seed_idx] - min_weight) / weight_range;
                        
                        // Blue to red gradient
                        float r = static_cast<float>(normalized_weight);
                        float g = 0.3f;
                        float b = static_cast<float>(1.0 - normalized_weight);
                        
                        cell.color = ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 0.6f));
                    } else {
                        cell.color = color_from_index_imgui(face_idx);
                    }
                    
                    // Extract cell vertices (bounded by [0,1] box)
                    auto ccb = fit->ccb();
                    auto he = ccb;
                    bool bounded = true;
                    
                    do {
                        if (he->has_source()) {
                            Point_2 p = he->source()->point();
                            
                            // Clamp to [0,1] bounding box
                            double x = std::max(0.0, std::min(1.0, p.x()));
                            double y = std::max(0.0, std::min(1.0, p.y()));
                            
                            cell.vertices.push_back(Vec2f(static_cast<float>(x), 
                                                         static_cast<float>(y)));
                        } else {
                            bounded = false;
                            break;
                        }
                    } while (++he != ccb);
                    
                    if (bounded && cell.vertices.size() >= 3) {
                        m_2d_voronoi_cells.push_back(cell);
                    }
                }
                
            } else {
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: 2D preview - Using STANDARD Voronoi (unweighted)";
                
                // Build standard 2D Voronoi
                DT2 dt;
                std::vector<Point_2> seed_points_2d;
                
                // Check for multi-scale mode
                std::vector<int> scale_levels;
                if (m_configuration.multi_scale && !m_configuration.scale_seed_counts.empty()) {
                    // Mark which scale level each seed belongs to
                    int cumulative = 0;
                    for (size_t level = 0; level < m_configuration.scale_seed_counts.size(); ++level) {
                        int count = m_configuration.scale_seed_counts[level];
                        for (int i = 0; i < count && cumulative + i < static_cast<int>(seeds_3d.size()); ++i) {
                            scale_levels.push_back(static_cast<int>(level));
                        }
                        cumulative += count;
                    }
                    // Fill remaining with last level
                    while (scale_levels.size() < seeds_3d.size()) {
                        scale_levels.push_back(static_cast<int>(m_configuration.scale_seed_counts.size() - 1));
                    }
                }
                
                for (const auto& seed : seeds_3d) {
                    // Normalize to [0,1] with anisotropic scaling
                    double x = (seed.x() - min_pt.x()) / range.x() / aspect_x;
                    double y = (seed.y() - min_pt.y()) / range.y() / aspect_y;
                    
                    // Clamp to valid range
                    x = std::max(0.0, std::min(1.0, x));
                    y = std::max(0.0, std::min(1.0, y));
                    
                    Point_2 p(x, y);
                    seed_points_2d.push_back(p);
                    dt.insert(p);
                }
                
                // Build Voronoi diagram adaptor
                VD2 vd(dt);
                
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: 2D preview - Built standard Voronoi with " 
                                         << vd.number_of_faces() << " faces";
                
                // Extract Voronoi cells
                int face_idx = 0;
                for (auto fit = vd.faces_begin(); fit != vd.faces_end(); ++fit, ++face_idx) {
                    VoronoiCell2D cell;
                    
                    // Get seed point for this face
                    Point_2 seed_pt = fit->dual()->point();
                    cell.seed_point = Vec2f(static_cast<float>(seed_pt.x()), 
                                           static_cast<float>(seed_pt.y()));
                    
                    // Color based on scale level if multi-scale
                    if (!scale_levels.empty() && face_idx < static_cast<int>(scale_levels.size())) {
                        int level = scale_levels[face_idx];
                        // Different hue per level
                        float hue = static_cast<float>(level) / static_cast<float>(m_configuration.scale_seed_counts.size());
                        float h_rad = hue * 6.28318f;
                        float r = 0.5f + 0.5f * std::cos(h_rad);
                        float g = 0.5f + 0.5f * std::cos(h_rad + 2.0944f);
                        float b = 0.5f + 0.5f * std::cos(h_rad + 4.1888f);
                        cell.color = ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 0.6f));
                    } else {
                        // Standard golden ratio coloring
                        cell.color = color_from_index_imgui(face_idx);
                    }
                    
                    // Extract cell vertices (bounded by [0,1] box)
                    auto ccb = fit->ccb();
                    auto he = ccb;
                    bool bounded = true;
                    
                    do {
                        if (he->has_source()) {
                            Point_2 p = he->source()->point();
                            
                            // Clamp to [0,1] bounding box
                            double x = std::max(0.0, std::min(1.0, p.x()));
                            double y = std::max(0.0, std::min(1.0, p.y()));
                            
                            cell.vertices.push_back(Vec2f(static_cast<float>(x), 
                                                         static_cast<float>(y)));
                        } else {
                            bounded = false;
                            break;
                        }
                    } while (++he != ccb);
                    
                    if (bounded && cell.vertices.size() >= 3) {
                        m_2d_voronoi_cells.push_back(cell);
                    }
                }
            }
            
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_2d_voronoi_preview() - Generated " 
                                     << m_2d_voronoi_cells.size() << " cells";
        }
        catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "GLGizmoVoronoi: update_2d_voronoi_preview() EXCEPTION: " << e.what();
            m_2d_voronoi_cells.clear();
        }
        catch (...) {
            BOOST_LOG_TRIVIAL(error) << "GLGizmoVoronoi: update_2d_voronoi_preview() UNKNOWN EXCEPTION";
            m_2d_voronoi_cells.clear();
        }
    }

} // namespace Slic3r::GUI
