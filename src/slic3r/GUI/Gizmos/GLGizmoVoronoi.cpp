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

// 2D Voronoi library for preview
#define JC_VORONOI_IMPLEMENTATION
#include "jc_voronoi.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Slic3r::GUI {

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
        m_seed_preview_model.reset();
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
            }
            else {
                m_seed_preview_model.reset();
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

            if (ImGui::Button(into_u8(_u8L("Update Preview")).c_str())) {
                update_seed_preview();
                update_2d_voronoi_preview();
            }

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
                        }
                    } else {
                        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - selection_info is NULL (will be set up by gizmo manager)";
                        // selection_info will be set up after on_set_state() returns
                        // Triangle selectors will be initialized automatically during first render cycle
                    }
                } else {
                    BOOST_LOG_TRIVIAL(error) << "GLGizmoVoronoi: on_set_state() - m_c is NULL! Model may not be visible!";
                }

                // Initialize seed preview if enabled
                if (m_configuration.show_seed_preview) {
                    BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - updating seed preview";
                    update_seed_preview();
                    BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - updating 2D voronoi preview";
                    update_2d_voronoi_preview();
                    BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - preview updates complete";
                }

                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - activation COMPLETE";
            }
            else {
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_set_state() - deactivating gizmo";
                m_volume = nullptr;
                m_glmodel.reset();
                m_seed_preview_model.reset();
                m_seed_preview_points.clear();
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

        // Render seed preview points if enabled
        if (m_configuration.show_seed_preview) {
            render_seed_preview();
        }
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
                voronoi_config.seed_type = static_cast<VoronoiMesh::SeedType>(m_state.config.seed_type);
                voronoi_config.num_seeds = m_state.config.num_seeds;
                voronoi_config.wall_thickness = m_state.config.wall_thickness;
                voronoi_config.edge_thickness = m_state.config.edge_thickness;
                voronoi_config.edge_shape = static_cast<VoronoiMesh::EdgeShape>(m_state.config.edge_shape);
                voronoi_config.cell_style = static_cast<VoronoiMesh::CellStyle>(m_state.config.cell_style);
                voronoi_config.edge_segments = m_state.config.edge_segments;
                voronoi_config.edge_curvature = m_state.config.edge_curvature;
                voronoi_config.edge_subdivisions = m_state.config.edge_subdivisions;
                voronoi_config.hollow_cells = m_state.config.hollow_cells;
                voronoi_config.random_seed = m_state.config.random_seed;

                // Advanced features
                voronoi_config.relax_seeds = m_state.config.relax_seeds;
                voronoi_config.relaxation_iterations = m_state.config.relaxation_iterations;
                
                voronoi_config.multi_scale = m_state.config.multi_scale;
                voronoi_config.scale_seed_counts = m_state.config.scale_seed_counts;
                voronoi_config.scale_thicknesses = m_state.config.scale_thicknesses;
                
                voronoi_config.anisotropic = m_state.config.anisotropic;
                voronoi_config.anisotropy_direction = m_state.config.anisotropy_direction;
                voronoi_config.anisotropy_ratio = m_state.config.anisotropy_ratio;
                
                voronoi_config.enforce_watertight = m_state.config.enforce_watertight;
                voronoi_config.auto_repair = m_state.config.auto_repair;
                voronoi_config.min_wall_thickness = m_state.config.min_wall_thickness;
                voronoi_config.min_feature_size = m_state.config.min_feature_size;

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
            volume->calculate_convex_hull();

            // Invalidate caches that depend on mesh geometry
            volume->invalidate_convex_hull_2d();
            obj->invalidate_bounding_box();

            // CRITICAL: Force complete scene update
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::worker_finished() - Forcing complete update";
            
            // Method 1: Update the model object (this calls ensure_on_bed)
            plater->changed_object(*obj);
            
            // Method 2: Force GL scene reload to show the new mesh
            GLCanvas3D* canvas = plater->canvas3D();
            if (canvas) {
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::worker_finished() - Reloading GL scene";
                canvas->reload_scene(true);
            }

            // Method 3: Schedule background process to update slicing
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::worker_finished() - Scheduling background process for re-slice";
            plater->schedule_background_process();
            
            // Method 4: Force canvas refresh
            m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));

            // Restore position AFTER all updates
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::worker_finished() - Restoring original position";
            volume->set_offset(volume_offset);
            if (!obj->instances.empty()) {
                obj->instances[0]->set_offset(instance_offset);
            }

            // Final reload to show restored position
            if (canvas) {
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::worker_finished() - Final scene reload with restored position";
                canvas->reload_scene(true);
            }

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
            // Position window near the selected object
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
            update_2d_voronoi_preview();
        }
    }

    void GLGizmoVoronoi::update_seed_preview()
    {
        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() START";
        
        // Safety checks
        if (!m_volume) {
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() - no volume, returning";
            return;
        }

        try {
            m_seed_preview_points.clear();
            m_seed_preview_model.reset();
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() - cleared previous data";

            const indexed_triangle_set& mesh = m_volume->mesh().its;
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() - got mesh, vertices: " << mesh.vertices.size();

            // Validate mesh
            if (mesh.vertices.empty()) {
                BOOST_LOG_TRIVIAL(warning) << "GLGizmoVoronoi: update_seed_preview() - Empty mesh for seed preview";
                return;
            }

            // Compute bounding box for generated points
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() - computing bounding box";
            BoundingBoxf3 bbox;
            for (const auto& v : mesh.vertices) {
                bbox.merge(v.cast<double>());
            }
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() - bbox computed";

            // Ensure valid bounding box
            if (!bbox.defined || bbox.size().minCoeff() <= 0) {
                BOOST_LOG_TRIVIAL(warning) << "GLGizmoVoronoi: update_seed_preview() - Invalid bounding box for seed preview";
                return;
            }

            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() - generating seeds, type: " << (int)m_configuration.seed_type << ", num: " << m_configuration.num_seeds;

            if (m_configuration.seed_type == Configuration::SEED_GRID) {
                // Grid seeds - ensure proper distribution
                int seeds_per_axis = static_cast<int>(std::ceil(std::cbrt(m_configuration.num_seeds)));
                seeds_per_axis = std::max(2, seeds_per_axis); // At least 2x2x2 grid

                Vec3d step = bbox.size() / double(seeds_per_axis);
                Vec3d offset = step * 0.5; // Center points in cells

                for (int x = 0; x < seeds_per_axis; ++x) {
                    for (int y = 0; y < seeds_per_axis; ++y) {
                        for (int z = 0; z < seeds_per_axis; ++z) {
                            Vec3d pt = bbox.min + Vec3d(
                                offset.x() + x * step.x(),
                                offset.y() + y * step.y(),
                                offset.z() + z * step.z()
                            );

                            // Only add if within actual mesh bounds
                            if (pt.x() >= bbox.min.x() && pt.x() <= bbox.max.x() &&
                                pt.y() >= bbox.min.y() && pt.y() <= bbox.max.y() &&
                                pt.z() >= bbox.min.z() && pt.z() <= bbox.max.z()) {
                                m_seed_preview_points.push_back(pt.cast<float>());

                                if (m_seed_preview_points.size() >= static_cast<size_t>(m_configuration.num_seeds))
                                    break;
                            }
                        }
                        if (m_seed_preview_points.size() >= static_cast<size_t>(m_configuration.num_seeds))
                            break;
                    }
                    if (m_seed_preview_points.size() >= static_cast<size_t>(m_configuration.num_seeds))
                        break;
                }
            }
            else if (m_configuration.seed_type == Configuration::SEED_RANDOM) {
                // Random seeds with better distribution using Poisson disk sampling approximation
                std::mt19937 rng(m_configuration.random_seed);

                // Calculate minimum distance between points for better distribution
                float volume = bbox.size().x() * bbox.size().y() * bbox.size().z();
                float cell_volume = volume / m_configuration.num_seeds;
                float min_distance = std::cbrt(cell_volume) * 0.5f; // Half the average cell size

                std::uniform_real_distribution<double> dist_x(bbox.min.x(), bbox.max.x());
                std::uniform_real_distribution<double> dist_y(bbox.min.y(), bbox.max.y());
                std::uniform_real_distribution<double> dist_z(bbox.min.z(), bbox.max.z());

                int max_attempts = m_configuration.num_seeds * 50;
                int attempts = 0;

                while (static_cast<int>(m_seed_preview_points.size()) < m_configuration.num_seeds && attempts < max_attempts) {
                    Vec3d pt(dist_x(rng), dist_y(rng), dist_z(rng));

                    // Check minimum distance from existing points
                    bool too_close = false;
                    for (const auto& existing : m_seed_preview_points) {
                        if ((existing.cast<double>() - pt).norm() < min_distance) {
                            too_close = true;
                            break;
                        }
                    }

                    if (!too_close) {
                        m_seed_preview_points.push_back(pt.cast<float>());
                    }

                    attempts++;
                }

                // Fill remaining if we couldn't maintain minimum distance
                while (static_cast<int>(m_seed_preview_points.size()) < m_configuration.num_seeds) {
                    Vec3d pt(dist_x(rng), dist_y(rng), dist_z(rng));
                    m_seed_preview_points.push_back(pt.cast<float>());
                }
            }
            else {
                // Vertex seeds - use farthest point sampling for better distribution
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() - Using VERTEX seeds, mesh has " << mesh.vertices.size() << " vertices";

                if (static_cast<int>(mesh.vertices.size()) <= m_configuration.num_seeds) {
                    // Use all vertices if we have fewer than requested
                    BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() - Using ALL vertices (mesh.vertices.size() <= num_seeds)";
                    for (const auto& v : mesh.vertices) {
                        m_seed_preview_points.push_back(v);
                    }
                    BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() - Added " << m_seed_preview_points.size() << " vertex seeds";
                }
                else {
                    // Farthest point sampling
                    BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() - Using farthest point sampling for " << m_configuration.num_seeds << " seeds from " << mesh.vertices.size() << " vertices";

                    std::vector<bool> selected(mesh.vertices.size(), false);
                    std::vector<float> min_distances(mesh.vertices.size(), std::numeric_limits<float>::max());

                    // Start with a random vertex
                    std::mt19937 rng(m_configuration.random_seed);
                    std::uniform_int_distribution<size_t> dist(0, mesh.vertices.size() - 1);
                    size_t first_idx = dist(rng);
                    selected[first_idx] = true;
                    m_seed_preview_points.push_back(mesh.vertices[first_idx]);
                    BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() - Selected first vertex at index " << first_idx;

                    // Update distances from first point
                    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
                        float d = (mesh.vertices[i] - mesh.vertices[first_idx]).norm();
                        min_distances[i] = std::min(min_distances[i], d);
                    }

                    // Select remaining points
                    for (int k = 1; k < m_configuration.num_seeds; ++k) {
                        // Find vertex with maximum minimum distance
                        size_t best_idx = 0;
                        float max_min_dist = -1.0f;

                        for (size_t i = 0; i < mesh.vertices.size(); ++i) {
                            if (!selected[i] && min_distances[i] > max_min_dist) {
                                max_min_dist = min_distances[i];
                                best_idx = i;
                            }
                        }

                        // Add the selected vertex
                        selected[best_idx] = true;
                        m_seed_preview_points.push_back(mesh.vertices[best_idx]);

                        // Update distances
                        for (size_t i = 0; i < mesh.vertices.size(); ++i) {
                            if (!selected[i]) {
                                float d = (mesh.vertices[i] - mesh.vertices[best_idx]).norm();
                                min_distances[i] = std::min(min_distances[i], d);
                            }
                        }
                    }
                    BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() - Farthest point sampling complete, selected " << m_seed_preview_points.size() << " vertices";
                }
            }

            // Create OpenGL model for rendering seed points
            if (!m_seed_preview_points.empty()) {
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() - creating OpenGL model for " << m_seed_preview_points.size() << " points";
                GLModel::Geometry init_data;
                init_data.format = { GLModel::PrimitiveType::Points, GLModel::Geometry::EVertexLayout::P3 };

                for (const Vec3f& pt : m_seed_preview_points) {
                    init_data.add_vertex(pt);
                }

                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() - initializing model";
                m_seed_preview_model.init_from(std::move(init_data));
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() - requesting rerender";
                request_rerender();

                // Also update 2D preview
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() - updating 2D voronoi preview";
                update_2d_voronoi_preview();
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() - 2D preview updated";
            }
            else {
                BOOST_LOG_TRIVIAL(warning) << "GLGizmoVoronoi: update_seed_preview() - no seed points generated!";
            }
            
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_seed_preview() COMPLETE";
        }
        catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "GLGizmoVoronoi: update_seed_preview() EXCEPTION: " << e.what();
            m_seed_preview_points.clear();
        }
        catch (...) {
            BOOST_LOG_TRIVIAL(error) << "GLGizmoVoronoi: update_seed_preview() UNKNOWN EXCEPTION";
            m_seed_preview_points.clear();
        }
    }

    void GLGizmoVoronoi::render_seed_preview()
    {
        if (!m_seed_preview_model.is_initialized() || m_seed_preview_points.empty())
            return;

        glsafe(::glEnable(GL_BLEND));
        glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

        // Set point size for seed visualization
        glsafe(::glPointSize(8.0f));

        const Camera& camera = wxGetApp().plater()->get_camera();
        Transform3d view_model_matrix = camera.get_view_matrix();

        // Get model transform
        const Selection& selection = m_parent.get_selection();
        if (!selection.is_empty()) {
            const GLVolume* vol = selection.get_volume(*selection.get_volume_idxs().begin());
            if (vol) {
                view_model_matrix = camera.get_view_matrix() * vol->world_matrix();
            }
        }

        // Render seed points in green
        std::array<float, 4> green_color = { 0.0f, 0.7f, 0.0f, 0.9f };

        const auto& shader = wxGetApp().get_shader("gouraud_light");
        if (shader) {
            wxGetApp().bind_shader(shader);
            shader->set_uniform("view_model_matrix", view_model_matrix);
            shader->set_uniform("projection_matrix", camera.get_projection_matrix());
            shader->set_uniform("emission_factor", 0.5f);

            m_seed_preview_model.set_color(-1, green_color);
            m_seed_preview_model.render_geometry();

            wxGetApp().unbind_shader();
        }

        glsafe(::glPointSize(1.0f));
        glsafe(::glDisable(GL_BLEND));
    }

    void GLGizmoVoronoi::render_painter_gizmo() const
    {
        static bool logged_first_call = false;
        if (!logged_first_call) {
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::render_painter_gizmo() - FIRST CALL";
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi::render_painter_gizmo() - triangle_selectors.size(): " << m_triangle_selectors.size();
            logged_first_call = true;
        }

        const Selection& selection = m_parent.get_selection();

        glsafe(::glEnable(GL_BLEND));
        glsafe(::glEnable(GL_DEPTH_TEST));

        render_triangles(selection);

        // Always render clipping plane, instances hider, and cursor (like other painter gizmos)
        m_c->object_clipper()->render_cut();
        m_c->instances_hider()->render_cut();

        // Only render cursor when painting mode is active
        if (m_configuration.enable_triangle_painting) {
            render_cursor();
        }

        glsafe(::glDisable(GL_BLEND));
    }

    void GLGizmoVoronoi::render_triangles(const Selection& selection) const
    {
        // Safety check: only render if triangle selectors are initialized
        if (m_triangle_selectors.empty()) {
            BOOST_LOG_TRIVIAL(warning) << "GLGizmoVoronoi::render_triangles() - triangle_selectors is empty, skipping render";
            return;
        }

        // Call the base class implementation which handles all the shader setup
        // and proper rendering of the model triangles
        GLGizmoPainterBase::render_triangles(selection);
    }

    void GLGizmoVoronoi::update_model_object()
    {
        // Safety check for m_c pointer
        if (!m_c)
            return;

        // Save painted triangle data to model volume
        const Selection& selection = m_parent.get_selection();
        const ModelObject* mo = m_c->selection_info()->model_object();

        if (!mo || !selection.is_from_single_instance())
            return;

        for (const ModelVolume* mv : mo->volumes) {
            if (mv->is_model_part()) {
                auto it = std::find(mo->volumes.begin(), mo->volumes.end(), mv);
                int mesh_id = std::distance(mo->volumes.begin(), it);
                if (mesh_id < (int)m_triangle_selectors.size() && m_triangle_selectors[mesh_id]) {
                    m_parent.request_extra_frame();
                }
            }
        }
    }

    void GLGizmoVoronoi::update_from_model_object(bool first_update)
    {
        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_from_model_object() START, first_update=" << first_update;

        // Safety check for m_c pointer
        if (!m_c) {
            BOOST_LOG_TRIVIAL(warning) << "GLGizmoVoronoi: update_from_model_object() - m_c is NULL";
            return;
        }

        if (!m_c->selection_info()) {
            BOOST_LOG_TRIVIAL(warning) << "GLGizmoVoronoi: update_from_model_object() - selection_info is NULL";
            return;
        }

        const ModelObject* mo = m_c->selection_info()->model_object();
        if (!mo) {
            BOOST_LOG_TRIVIAL(warning) << "GLGizmoVoronoi: update_from_model_object() - model_object is NULL";
            return;
        }

        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_from_model_object() - model_object has " << mo->volumes.size() << " volumes";

        // Initialize triangle selectors if needed
        if (first_update || m_triangle_selectors.empty()) {
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_from_model_object() - clearing triangle selectors";
            m_triangle_selectors.clear();

            for (const ModelVolume* mv : mo->volumes) {
                if (mv->is_model_part()) {
                    BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_from_model_object() - creating triangle selector for model part";
                    try {
                        const TriangleMesh& mesh = mv->mesh();
                        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_from_model_object() - got mesh, creating TriangleSelectorGUI";
                        m_triangle_selectors.emplace_back(std::make_unique<TriangleSelectorGUI>(mesh));
                        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_from_model_object() - TriangleSelectorGUI created successfully";
                    }
                    catch (const std::exception& e) {
                        BOOST_LOG_TRIVIAL(error) << "GLGizmoVoronoi: update_from_model_object() - Exception creating TriangleSelectorGUI: " << e.what();
                        throw;
                    }
                }
            }
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_from_model_object() - created " << m_triangle_selectors.size() << " triangle selectors";
        }

        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_from_model_object() COMPLETE";
    }

    bool GLGizmoVoronoi::on_init()
    {
        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_init() START";
        
        try {
            // Initialize shortcut key and descriptions (similar to other painter gizmos)
            m_shortcut_key = WXK_CONTROL_V;
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_init() - shortcut key set";
            
            // Get shortkey prefixes
            const wxString ctrl = GUI::shortkey_ctrl_prefix();
            const wxString alt = GUI::shortkey_alt_prefix();
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_init() - shortkey prefixes obtained";
            
            // Set up tool descriptions
            m_desc["clipping_of_view_caption"] = alt + _L("Mouse wheel");
            m_desc["clipping_of_view"] = _L("Section view");
            m_desc["cursor_size_caption"] = ctrl + _L("Mouse wheel");
            m_desc["cursor_size"] = _L("Pen size");
            m_desc["remove_caption"] = _L("Shift + Left mouse button");
            m_desc["remove"] = _L("Erase");
            m_desc["remove_all"] = _L("Erase all painting");
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_init() - descriptions set";

            // Initialize painting system
            m_cursor_radius = 2.0f;
            m_is_dark_mode = false;

            // Initialize volume pointer to null for safety
            m_volume = nullptr;
            
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_init() COMPLETE - returning true";
            return true;
        }
        catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "GLGizmoVoronoi: on_init() EXCEPTION: " << e.what();
            return false;
        }
        catch (...) {
            BOOST_LOG_TRIVIAL(error) << "GLGizmoVoronoi: on_init() UNKNOWN EXCEPTION";
            return false;
        }
    }

    void GLGizmoVoronoi::on_opening()
    {
        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_opening() START";

        try {
            // Ensure model is visible when gizmo opens
            m_parent.toggle_model_objects_visibility(true);
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_opening() - toggled model visibility to true";

            // Try to get volume from selection if m_volume isn't set yet
            if (!m_volume && m_c && m_c->selection_info()) {
                const ModelObject* mo = m_c->selection_info()->model_object();
                if (mo && !mo->volumes.empty()) {
                    for (const ModelVolume* mv : mo->volumes) {
                        if (mv->is_model_part()) {
                            m_volume = mv;
                            break;
                        }
                    }
                }
            }

            // NOTE: Don't call update_from_model_object() here!
            // selection_info is NULL at this point, so triangle selectors can't be initialized
            // The base class will call update_from_model_object() later when selection_info is ready
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_opening() - skipping triangle selector init (will be done by base class)";

            // Keep clipping plane enabled for proper rendering
            // Don't disable it immediately - let base class manage it
            if (m_c && m_c->object_clipper()) {
                // Keep whatever position the base class set
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_opening() - keeping clipping plane state from base class";
            }

            // Always try to update previews when opening, not just when show_seed_preview is true
            if (m_volume) {
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_opening() - updating previews";
                try {
                    update_seed_preview();
                    BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_opening() - seed preview updated";
                }
                catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(error) << "GLGizmoVoronoi: on_opening() - Exception in preview update: " << e.what();
                }
                catch (...) {
                    BOOST_LOG_TRIVIAL(error) << "GLGizmoVoronoi: on_opening() - Unknown exception in preview update";
                }
            }
            else {
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_opening() - skipping preview (volume: NULL)";
            }

            // Safe 2D preview update
            try {
                update_2d_voronoi_preview();
                BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_opening() - 2D preview updated";
            }
            catch (...) {
                BOOST_LOG_TRIVIAL(error) << "GLGizmoVoronoi: on_opening() - Exception in 2D preview update";
            }

            request_rerender();
            BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: on_opening() COMPLETE";
        }
        catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "GLGizmoVoronoi: on_opening() EXCEPTION: " << e.what();
            throw;
        }
        catch (...) {
            BOOST_LOG_TRIVIAL(error) << "GLGizmoVoronoi: on_opening() UNKNOWN EXCEPTION";
            throw;
        }
    }

    void GLGizmoVoronoi::on_shutdown()
    {
        try {
            stop_worker_thread_request();
            if (m_worker.joinable()) {
                m_worker.join();
            }

            // Thread-safe state cleanup
            {
                std::lock_guard<std::mutex> lock(m_state_mutex);
                m_state.status = State::idle;
                m_state.progress = 0;
                m_state.result.reset();
                m_state.mv = nullptr;
            }

            // Safe cleanup of OpenGL resources
            if (m_glmodel.is_initialized()) {
                m_glmodel.reset();
            }
            if (m_seed_preview_model.is_initialized()) {
                m_seed_preview_model.reset();
            }

            // Clear all containers
            m_seed_preview_points.clear();
            m_2d_voronoi_cells.clear();
            m_2d_delaunay_edges.clear();

            // Clear original mesh so it can be recaptured for different objects
            m_original_mesh.clear();

            // Reset volume pointer
            m_volume = nullptr;

            // Ensure model is visible when shutting down (like other painter gizmos)
            m_parent.toggle_model_objects_visibility(true);

        }
        catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "Error in on_shutdown: " << e.what();
        }
        catch (...) {
            BOOST_LOG_TRIVIAL(error) << "Unknown error in on_shutdown";
        }
    }

    wxString GLGizmoVoronoi::handle_snapshot_action_name(bool shift_down, GLGizmoPainterBase::Button button_down) const
    {
        if (shift_down)
            return wxString("Reset Voronoi painting");

        return (button_down == Button::Left)
            ? wxString("Add Voronoi region")
            : wxString("Remove Voronoi region");
    }

    void GLGizmoVoronoi::update_2d_voronoi_preview()
    {
        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_2d_voronoi_preview() START - seed_preview_points.size() = " << m_seed_preview_points.size();

        m_2d_voronoi_cells.clear();
        m_2d_delaunay_edges.clear();

        if (m_seed_preview_points.empty()) {
            // Generate fallback preview instead of returning empty
            BOOST_LOG_TRIVIAL(warning) << "GLGizmoVoronoi: update_2d_voronoi_preview() - seed_preview_points is EMPTY, falling back to hexagonal preview";
            try {
                generate_fallback_hexagonal_preview();
            }
            catch (...) {
                BOOST_LOG_TRIVIAL(error) << "Exception in fallback preview generation";
            }
            return;
        }

        BOOST_LOG_TRIVIAL(info) << "GLGizmoVoronoi: update_2d_voronoi_preview() - seed_preview_points is NOT empty, generating actual Voronoi diagram";

        // Convert 3D seed points to 2D (project to XY plane)
        std::vector<jcv_point> points_2d;
        points_2d.reserve(m_seed_preview_points.size());

        // Find bounding box of projected points
        float min_x = FLT_MAX, max_x = -FLT_MAX;
        float min_y = FLT_MAX, max_y = -FLT_MAX;

        for (const auto& pt3d : m_seed_preview_points) {
            min_x = std::min(min_x, pt3d.x());
            max_x = std::max(max_x, pt3d.x());
            min_y = std::min(min_y, pt3d.y());
            max_y = std::max(max_y, pt3d.y());
        }

        // Add some padding to avoid edge cases
        float padding = 0.1f;
        min_x -= padding;
        max_x += padding;
        min_y -= padding;
        max_y += padding;

        // Normalize points to [0, 1] range for the 2D preview
        float scale_x = (max_x - min_x) > 0 ? 1.0f / (max_x - min_x) : 1.0f;
        float scale_y = (max_y - min_y) > 0 ? 1.0f / (max_y - min_y) : 1.0f;

        for (const auto& pt3d : m_seed_preview_points) {
            jcv_point pt2d;
            pt2d.x = (pt3d.x() - min_x) * scale_x;
            pt2d.y = (pt3d.y() - min_y) * scale_y;
            points_2d.push_back(pt2d);
        }

        // Set up bounding rectangle
        jcv_rect rect;
        rect.min.x = 0.0f;
        rect.min.y = 0.0f;
        rect.max.x = 1.0f;
        rect.max.y = 1.0f;

        // Generate Voronoi diagram
        jcv_diagram diagram;
        memset(&diagram, 0, sizeof(jcv_diagram));

        try {
            jcv_diagram_generate((int)points_2d.size(), points_2d.data(), &rect, nullptr, &diagram);

            // Extract cells from the diagram
            const jcv_site* sites = jcv_diagram_get_sites(&diagram);

            if (sites) {
                for (int i = 0; i < diagram.numsites; ++i) {
                    const jcv_site* site = &sites[i];
                    VoronoiCell2D cell;
                    cell.seed_point = Vec2f(site->p.x, site->p.y);

                    // Collect vertices from the edges
                    jcv_graphedge* edge = site->edges;
                    std::vector<Vec2f> vertices;

                    while (edge) {
                        vertices.push_back(Vec2f(edge->pos[0].x, edge->pos[0].y));
                        edge = edge->next;
                    }

                    // Sort vertices in counter-clockwise order
                    if (vertices.size() >= 3) {
                        Vec2f center = cell.seed_point;
                        std::sort(vertices.begin(), vertices.end(), [&center](const Vec2f& a, const Vec2f& b) {
                            float angle_a = atan2f(a.y() - center.y(), a.x() - center.x());
                            float angle_b = atan2f(b.y() - center.y(), b.x() - center.x());
                            return angle_a < angle_b;
                            });

                        cell.vertices = vertices;

                        // Generate a color for this cell based on the seed index
                        float hue = (float(i) / float(diagram.numsites)) * 360.0f;
                        float r, g, b;
                        ImGui::ColorConvertHSVtoRGB(hue / 360.0f, 0.6f, 0.8f, r, g, b);
                        cell.color = ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 0.7f));

                        m_2d_voronoi_cells.push_back(cell);
                    }
                }
            }

            // Capture Delaunay edges for the preview overlay
            jcv_delauney_iter delaunay_iter;
            jcv_delauney_begin(&diagram, &delaunay_iter);
            jcv_delauney_edge delaunay_edge;
            while (jcv_delauney_next(&delaunay_iter, &delaunay_edge)) {
                Vec2f start(delaunay_edge.pos[0].x, delaunay_edge.pos[0].y);
                Vec2f end(delaunay_edge.pos[1].x, delaunay_edge.pos[1].y);

                if ((start - end).squaredNorm() < std::numeric_limits<float>::epsilon())
                    continue;

                m_2d_delaunay_edges.push_back({ start, end });
            }

            // Safely free the diagram
            jcv_diagram_free(&diagram);

        }
        catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "Error in update_2d_voronoi_preview: " << e.what();
            m_2d_voronoi_cells.clear();
            m_2d_delaunay_edges.clear();
            generate_fallback_hexagonal_preview();
        }
        catch (...) {
            BOOST_LOG_TRIVIAL(error) << "Unknown error in update_2d_voronoi_preview";
            m_2d_voronoi_cells.clear();
            m_2d_delaunay_edges.clear();
            generate_fallback_hexagonal_preview();
        }
    }

    void GLGizmoVoronoi::generate_fallback_hexagonal_preview()
    {
        if (m_seed_preview_points.empty()) {
            return;
        }

        try {
            // Generate simple hexagonal cells as fallback
            int grid_size = std::max(2, static_cast<int>(std::sqrt(m_seed_preview_points.size())));
            float cell_size = 1.0f / grid_size;

            for (int i = 0; i < grid_size; ++i) {
                for (int j = 0; j < grid_size; ++j) {
                    VoronoiCell2D cell;
                    cell.seed_point = Vec2f((i + 0.5f) * cell_size, (j + 0.5f) * cell_size);

                    // Generate hexagon vertices
                    float radius = cell_size * 0.4f;
                    for (int k = 0; k < 6; ++k) {
                        float angle = (k * 2.0f * M_PI) / 6.0f;
                        Vec2f vertex;
                        vertex.x() = cell.seed_point.x() + radius * cosf(angle);
                        vertex.y() = cell.seed_point.y() + radius * sinf(angle);
                        cell.vertices.push_back(vertex);
                    }

                    // Generate color
                    float hue = (float(i * grid_size + j) / float(grid_size * grid_size)) * 360.0f;
                    float r, g, b;
                    ImGui::ColorConvertHSVtoRGB(hue / 360.0f, 0.6f, 0.8f, r, g, b);
                    cell.color = ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 0.7f));

                    m_2d_voronoi_cells.push_back(cell);
                }
            }
        }
        catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "Error in generate_fallback_hexagonal_preview: " << e.what();
        }
        catch (...) {
            BOOST_LOG_TRIVIAL(error) << "Unknown error in generate_fallback_hexagonal_preview";
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

        // Draw seed points
        ImU32 seed_color = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        for (const auto& cell : m_2d_voronoi_cells) {
            ImVec2 seed_pos(canvas_pos.x + cell.seed_point.x() * canvas_size.x,
                           canvas_pos.y + cell.seed_point.y() * canvas_size.y);
            draw_list->AddCircleFilled(seed_pos, 2.0f, seed_color);
        }

        // Reserve space in ImGui layout for the canvas
        ImGui::Dummy(canvas_size);
    }

} // namespace Slic3r::GUI