#ifndef slic3r_GLGizmoBallEraser_hpp_
#define slic3r_GLGizmoBallEraser_hpp_

#include "GLGizmoBase.hpp"

#include "ag/ballerase/BallEraserSession.hpp"
#include "libslic3r/Format/OBJ.hpp"
#include "slic3r/GUI/GLModel.hpp"

namespace Slic3r {
namespace GUI {

class GLGizmoBallEraser : public GLGizmoBase
{
public:
    GLGizmoBallEraser(GLCanvas3D& parent, unsigned int sprite_id);

    std::string get_icon_filename(bool b_dark_mode) const override;
    void data_changed(bool is_serializing) override;
    bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down) override;

protected:
    bool on_init() override;
    std::string on_get_name() const override;
    std::string on_get_name_str() override { return "BallEraser"; }
    bool on_is_activable() const override;
    void on_set_state() override;
    void on_render() override;
    void on_render_for_picking() override {}
    void on_render_input_window(float x, float y, float bottom_limit) override;
    CommonGizmosDataID on_get_requirements() const override;

private:
    void close();
    void reset_session_from_selection();
    void apply_preview_state();
    void restore_preview_state();
    void refresh_preview_state();
    void apply_pending_strokes();
    void mark_preview_dirty();
    void rebuild_preview_models();
    void request_preview_refresh();
    void add_pending_stroke();
    void start_stroke_capture();
    void append_in_progress_stroke_sample(const Vec3d &center_instance_local);
    void update_in_progress_stroke_sampling();
    void commit_in_progress_stroke();
    void clear_in_progress_stroke();
    std::vector<Vec3d> simplify_stroke_samples(const std::vector<Vec3d> &samples) const;
    BallEraser::Stroke build_stroke_from_samples(const std::vector<Vec3d> &samples, BallEraser::Stroke::GestureType gesture) const;
    double current_drag_sample_spacing() const;
    void undo_pending_stroke();
    void clear_pending_strokes();
    bool update_hover_state(const Vec2d &mouse_position, bool update_active_center);
    bool hovered_target_supports_capture() const;
    bool prompt_for_exit_if_needed();
    BallEraser::HoverTarget classify_hover_target(const Vec3d &instance_local_point) const;
    bool hit_test_selected_object(const Vec2d &mouse_position, Vec3d &hit_instance_local) const;
    int selected_instance_index() const;
    Vec3d current_stroke_center_instance_local() const;
    Vec3d instance_local_to_world(const Vec3d &point) const;
    const BallEraser::Dimensions &active_dimensions() const;
    Vec3d active_rotation_degrees() const;
    TriangleColor register_cut_surface_color_binding() const;

private:
    BallEraser::Session m_session;
    bool m_single_cut_surface_color{false};
    ColorRGBA m_cut_surface_color{0.90f, 0.20f, 0.17f, 1.0f};
    BallEraser::Dimensions m_sphere_dimensions;
    BallEraser::Dimensions m_cube_dimensions;
    Vec3d m_cube_rotation_degrees{Vec3d::Zero()};
    GLModel m_sphere_preview;
    GLModel m_cube_preview;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoBallEraser_hpp_
