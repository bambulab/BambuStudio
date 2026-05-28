#ifndef slic3r_GLGizmoWarpCut_hpp_
#define slic3r_GLGizmoWarpCut_hpp_

#include "GLGizmoBase.hpp"

#include "slic3r/GUI/GLModel.hpp"

#include "ag/warpcut/WarpCutTypes.hpp"

#include <vector>

namespace Slic3r {
namespace GUI {

class GLGizmoWarpCut : public GLGizmoBase
{
public:
    GLGizmoWarpCut(GLCanvas3D& parent, unsigned int sprite_id);

    std::string get_icon_filename(bool b_dark_mode) const override;
    bool on_mouse(const wxMouseEvent& mouse_event) override;
    void data_changed(bool is_serializing) override;

protected:
    bool on_init() override;
    std::string on_get_name() const override;
    std::string on_get_name_str() override { return "WarpCut"; }
    bool on_is_activable() const override;
    void on_set_state() override;
    void on_render() override;
    void on_render_for_picking() override {}
    void on_render_input_window(float x, float y, float bottom_limit) override;

private:
    void reset_from_selection();
    void ensure_preview_state();
    void rebuild_preview_models();
    bool selection_box_changed() const;
    bool has_selected_point() const;
    float max_abs_offset() const;
    bool can_perform_cut() const;
    int selected_instance_index() const;
    void mark_preview_for_refresh();
    void apply_selected_point_delta(float delta);
    void smooth_offsets_around(size_t center_index);
    WarpCut::SurfaceDefinition build_warp_surface_definition() const;
    void perform_cut();
    float sampled_offset(double u, double v) const;
    Vec3d surface_position(double u, double v) const;
    Vec3d surface_normal(double u, double v) const;
    Vec3d control_point_position(size_t index) const;
    int pick_control_point(const wxMouseEvent& mouse_event) const;
    void apply_tilt_to_frame();

private:
    WarpCut::Frame m_frame;
    WarpCut::ControlGrid m_grid;
    GLModel m_preview_surface;
    GLModel m_preview_lines;
    GLModel m_control_point_sphere;
    Vec3d m_last_box_center{ Vec3d::Zero() };
    Vec3d m_last_box_size{ Vec3d::Zero() };
    int m_selected_point_index{ -1 };
    float m_edit_step{ 1.0f };
    float m_smoothing_strength{ 0.35f };
    int m_smoothing_radius{ 1 };
    float m_vertical_offset{ 0.0f };
    Vec3d m_tilt_angles{ Vec3d::Zero() };
    Vec3d m_initial_center{ Vec3d::Zero() };
    bool m_preview_mode{ false };
    size_t m_preview_surface_resolution{ 81 };
    bool m_preview_dirty{ true };
    bool m_has_preview_state{ false };
    bool m_single_cut_surface_color{ false };
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoWarpCut_hpp_
