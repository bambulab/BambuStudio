// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoMove.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
//BBS: GUI refactor
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/OpenGLManager.hpp"
#include "libslic3r/AppConfig.hpp"


#include <GL/glew.h>

#include <wx/utils.h>

namespace Slic3r {
namespace GUI {

#if ENABLE_FIXED_GRABBER
const double GLGizmoMove3D::Offset = 50.0;
#else
const double GLGizmoMove3D::Offset = 10.0;
#endif

//BBS: GUI refactor: add obj manipulation
GLGizmoMove3D::GLGizmoMove3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id, GizmoObjectManipulation* obj_manipulation)
    : GLGizmoBase(parent, icon_filename, sprite_id)
    , m_displacement(Vec3d::Zero())
    , m_snap_step(1.0)
    , m_starting_drag_position(Vec3d::Zero())
    , m_starting_box_center(Vec3d::Zero())
    , m_starting_box_bottom_center(Vec3d::Zero())
    //BBS: GUI refactor: add obj manipulation
    , m_object_manipulation(obj_manipulation)
{
    m_vbo_cone.init_from(its_make_cone(1., 1., 2*PI/36));
    try {
        float value                             = std::stof(wxGetApp().app_config->get("grabber_size_factor"));
        GLGizmoBase::Grabber::GrabberSizeFactor = value;
    } catch (const std::invalid_argument &e) {
        BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << e.what();
        GLGizmoBase::Grabber::GrabberSizeFactor = 1.0f;
    }
}

std::string GLGizmoMove3D::get_tooltip() const
{
    const Selection& selection = m_parent.get_selection();
    bool show_position = selection.is_single_full_instance();
    const Vec3d& position = selection.get_bounding_box().center();

    if (m_hover_id == 0 || m_grabbers[0].dragging)
        return "X: " + format(show_position ? position(0) : m_displacement(0), 2);
    else if (m_hover_id == 1 || m_grabbers[1].dragging)
        return "Y: " + format(show_position ? position(1) : m_displacement(1), 2);
    else if (m_hover_id == 2 || m_grabbers[2].dragging)
        return "Z: " + format(show_position ? position(2) : m_displacement(2), 2);
    else
        return "";
}

void GLGizmoMove3D::data_changed(bool is_serializing)
{
    enable_grabber(2, !m_parent.get_selection().is_wipe_tower());
    change_cs_by_selection();
}

BoundingBoxf3 GLGizmoMove3D::get_bounding_box() const
{
    BoundingBoxf3 t_aabb;

    Selection& selection = m_parent.get_selection();
    // m_cone aabb
    if (m_cone.is_initialized()) {
        const auto& t_cone_aabb = m_cone.get_bounding_box();
        const auto& [box, box_trafo] = selection.get_bounding_box_in_current_reference_system();
        Transform3d model_matrix = box_trafo;

        double size = get_grabber_size() * 0.75;//0.75 for arrow show
        for (unsigned int i = 0; i < 3; ++i) {
            if (m_grabbers[i].enabled) {
                auto i_model_matrix = model_matrix * Geometry::assemble_transform(m_grabbers[i].center);

                if (i == X)
                    i_model_matrix = i_model_matrix * Geometry::assemble_transform(Vec3d::Zero(), 0.5 * PI * Vec3d::UnitY());
                else if (i == Y)
                    i_model_matrix = i_model_matrix * Geometry::assemble_transform(Vec3d::Zero(), -0.5 * PI * Vec3d::UnitX());
                i_model_matrix = i_model_matrix * Geometry::assemble_transform(Vec3d::Zero(), Vec3d::Zero(), Vec3d(0.75 * size, 0.75 * size, 2.0 * size));

                auto i_aabb = t_cone_aabb.transformed(i_model_matrix);
                i_aabb.defined = true;
                t_aabb.merge(i_aabb);
                t_aabb.defined = true;
            }
        }
    }
    // end m_cone aabb

    // m_cross_mark aabb
    if (m_object_manipulation->is_instance_coordinates()) {
        Geometry::Transformation cur_tran;
        if (auto mi = m_parent.get_selection().get_selected_single_intance()) {
            cur_tran = mi->get_transformation();
        }
        else {
            cur_tran = selection.get_first_volume()->get_instance_transformation();
        }

        auto t_cross_mask_aabb = get_cross_mask_aabb(cur_tran.get_matrix(), Vec3f::Zero());
        t_cross_mask_aabb.defined = true;
        t_aabb.merge(t_cross_mask_aabb);
        t_aabb.defined = true;
    }

    // end m_cross_mark aabb
    return t_aabb;
}

bool GLGizmoMove3D::on_init()
{
    for (int i = 0; i < 3; ++i) {
        m_grabbers.push_back(Grabber());
    }

    m_shortcut_key = WXK_CONTROL_M;

    return true;
}

std::string GLGizmoMove3D::on_get_name() const
{
    if (!on_is_activable() && m_state == EState::Off) {
        return _u8L("Move") + ":\n" + _u8L("Please select at least one object.");
    } else {
        return _u8L("Move");
    }
}

bool GLGizmoMove3D::on_is_activable() const
{
    const Selection &selection = m_parent.get_selection();
    return !selection.is_any_cut_volume() && !selection.is_any_connector() && !selection.is_empty();
}

void GLGizmoMove3D::on_set_state() {
    if (get_state() == On) {
        m_last_selected_obejct_idx = -1;
        m_last_selected_volume_idx = -1;
        change_cs_by_selection();
    }
    GLGizmoBase::on_set_state();
}

void GLGizmoMove3D::on_start_dragging()
{
    if (m_hover_id != -1) {
        m_displacement = Vec3d::Zero();
        const BoundingBoxf3& box = m_parent.get_selection().get_bounding_box();
        m_starting_drag_position = m_orient_matrix *m_grabbers[m_hover_id].center;
        m_starting_box_center = box.center();
        m_starting_box_bottom_center = box.center();
        m_starting_box_bottom_center(2) = box.min(2);
    }
}

void GLGizmoMove3D::on_stop_dragging()
{
    m_displacement = Vec3d::Zero();
}

void GLGizmoMove3D::on_update(const UpdateData& data)
{
    if (m_hover_id == 0)
        m_displacement.x() = calc_projection(data);
    else if (m_hover_id == 1)
        m_displacement.y() = calc_projection(data);
    else if (m_hover_id == 2)
        m_displacement.z() = calc_projection(data);
}

void GLGizmoMove3D::on_render()
{
    Selection& selection = m_parent.get_selection();

    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glEnable(GL_DEPTH_TEST));

    const auto &[box, box_trafo]    = selection.get_bounding_box_in_current_reference_system();
    m_bounding_box                  = box;
    m_center                        = box_trafo.translation();
    if (m_object_manipulation) {
        m_object_manipulation->cs_center = box_trafo.translation();
    }
    m_orient_matrix                 = box_trafo;

    const auto& p_ogl_manager = wxGetApp().get_opengl_manager();
    if (!p_ogl_manager) {
        return;
    }

    float space_size = 20.f * INV_ZOOM * GLGizmoBase::Grabber::GrabberSizeFactor;
    modify_radius(space_size);
#if ENABLE_FIXED_GRABBER
    // x axis
    m_grabbers[0].center = {space_size, 0, 0};
    // y axis
    m_grabbers[1].center = {0, space_size,0};
    // z axis
    m_grabbers[2].center = {0,0, space_size};
    if (!p_ogl_manager->is_gizmo_keep_screen_size_enabled()) {
        m_grabbers[0].center.x() += m_bounding_box.max.x();
        m_grabbers[1].center.y() += m_bounding_box.max.y();
        m_grabbers[2].center.z() += m_bounding_box.max.z();
    }

    for (int i = 0; i < 3; ++i) {
        m_grabbers[i].color       = AXES_COLOR[i];
        m_grabbers[i].hover_color = AXES_HOVER_COLOR[i];
    }
#else
    // x axis
    m_grabbers[0].center = { box.max.x() + Offset, center.y(), center.z() };
    m_grabbers[0].color = AXES_COLOR[0];

    // y axis
    m_grabbers[1].center = { center.x(), box.max.y() + Offset, center.z() };
    m_grabbers[1].color = AXES_COLOR[1];

    // z axis
    m_grabbers[2].center = { center.x(), center.y(), box.max.z() + Offset };
    m_grabbers[2].color = AXES_COLOR[2];
#endif

    p_ogl_manager->set_line_width((m_hover_id != -1) ? 2.0f : 1.5f);
    const auto& gl_info = p_ogl_manager->get_gl_info();
    const auto formated_gl_version = gl_info.get_formated_gl_version();
    // draw grabbers
    for (unsigned int i = 0; i < 3; ++i) {
        if (m_grabbers[i].enabled) render_grabber_extension((Axis) i, box, false);
    }

    // draw axes line
    // draw axes
    auto render_grabber_connection = [this, &formated_gl_version](unsigned int id) {
        if (m_grabbers[id].enabled) {
            m_grabber_connections[id].old_center = m_center;
            m_grabber_connections[id].model.reset();

            GLModel::Geometry init_data;
            init_data.format = { GLModel::PrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
            init_data.color = AXES_COLOR[id];
            init_data.reserve_vertices(2);
            init_data.reserve_indices(2);

            // vertices
            init_data.add_vertex((Vec3f)origin.cast<float>());
            init_data.add_vertex((Vec3f)m_grabbers[id].center.cast<float>());

            // indices
            init_data.add_line(0, 1);

            m_grabber_connections[id].model.init_from(std::move(init_data));
            //}

#ifdef __APPLE__
            if (formated_gl_version < 30)
#endif
            {
                glLineStipple(1, 0x0FFF);
                glEnable(GL_LINE_STIPPLE);
            }

            m_grabber_connections[id].model.render_geometry();

#ifdef __APPLE__
            if (formated_gl_version < 30)
#endif
            {
                glDisable(GL_LINE_STIPPLE);
            }
        }
    };

    const auto& shader = wxGetApp().get_shader("flat");
    if (shader) {
        wxGetApp().bind_shader(shader);
        const Camera& camera = wxGetApp().plater()->get_camera();
        shader->set_uniform("view_model_matrix", camera.get_view_matrix() * m_orient_matrix);
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
        for (unsigned int i = 0; i < 3; ++i) {
            render_grabber_connection(i);
        }
        wxGetApp().unbind_shader();
    }

    if (m_object_manipulation->is_instance_coordinates()) {
        Geometry::Transformation cur_tran;
        if (auto mi = m_parent.get_selection().get_selected_single_intance()) {
            cur_tran = mi->get_transformation();
        }
        else {
            cur_tran = selection.get_first_volume()->get_instance_transformation();
        }
        render_cross_mark(cur_tran.get_matrix(), Vec3f::Zero());
    }
}

void GLGizmoMove3D::on_render_for_picking()
{
    glsafe(::glDisable(GL_DEPTH_TEST));

    //BBS donot render base grabber for picking
    //render_grabbers_for_picking(box);

    //get picking colors only
    for (unsigned int i = 0; i < (unsigned int) m_grabbers.size(); ++i) {
        if (m_grabbers[i].enabled) {
            std::array<float, 4> color = picking_color_component(i);
            m_grabbers[i].color        = color;
        }
    }

    render_grabber_extension(X, m_bounding_box, true);
    render_grabber_extension(Y, m_bounding_box, true);
    render_grabber_extension(Z, m_bounding_box, true);
}

//BBS: add input window for move
void GLGizmoMove3D::on_render_input_window(float x, float y, float bottom_limit)
{
    if (m_object_manipulation)
        m_object_manipulation->do_render_move_window(m_imgui, "Move", x, y, bottom_limit);
}


double GLGizmoMove3D::calc_projection(const UpdateData& data) const
{
    double projection = 0.0;

    Vec3d starting_vec = m_starting_drag_position - m_starting_box_center;
    double len_starting_vec = starting_vec.norm();
    if (len_starting_vec != 0.0) {
        Vec3d mouse_dir = data.mouse_ray.unit_vector();
        // finds the intersection of the mouse ray with the plane parallel to the camera viewport and passing throught the starting position
        // use ray-plane intersection see i.e. https://en.wikipedia.org/wiki/Line%E2%80%93plane_intersection algebric form
        // in our case plane normal and ray direction are the same (orthogonal view)
        // when moving to perspective camera the negative z unit axis of the camera needs to be transformed in world space and used as plane normal
        Vec3d inters = data.mouse_ray.a + (m_starting_drag_position - data.mouse_ray.a).dot(mouse_dir) / mouse_dir.squaredNorm() * mouse_dir;
        // vector from the starting position to the found intersection
        Vec3d inters_vec = inters - m_starting_drag_position;

        projection = inters_vec.norm();
        const double sign = inters_vec.dot(starting_vec) > 1e-6f ? 1.0f : -1.0f;

        projection = projection * sign;
    }

    if (wxGetKeyState(WXK_SHIFT))
        projection = m_snap_step * (double)std::round(projection / m_snap_step);

    return projection;
}

void GLGizmoMove3D::render_grabber_extension(Axis axis, const BoundingBoxf3& box, bool picking) const
{
    double size = get_grabber_size() * 0.75;//0.75 for arrow show
    std::array<float, 4> color = m_grabbers[axis].color;
    if (!picking && m_hover_id != -1) {
        if (m_hover_id == axis) {
            color = m_grabbers[axis].hover_color;
        }
    }

    const auto& shader = wxGetApp().get_shader(picking ? "flat" : "gouraud_light");
    if (shader == nullptr)
        return;

    wxGetApp().bind_shader(shader);
    const_cast<GLModel*>(&m_vbo_cone)->set_color(-1, color);

    const Camera& camera = picking ? wxGetApp().plater()->get_picking_camera() : wxGetApp().plater()->get_camera();
    Transform3d view_model_matrix = camera.get_view_matrix() * m_orient_matrix * Geometry::assemble_transform(m_grabbers[axis].center);
    if (axis == X)
        view_model_matrix = view_model_matrix * Geometry::assemble_transform(Vec3d::Zero(), 0.5 * PI * Vec3d::UnitY());
    else if (axis == Y)
        view_model_matrix = view_model_matrix * Geometry::assemble_transform(Vec3d::Zero(), -0.5 * PI * Vec3d::UnitX());
    view_model_matrix = view_model_matrix * Geometry::assemble_transform(Vec3d::Zero(), Vec3d::Zero(), Vec3d(0.75 * size, 0.75 * size, 2.0 * size));

    shader->set_uniform("view_model_matrix", view_model_matrix);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    if (!picking) {
        shader->set_uniform("emission_factor", 0.1f);
        shader->set_uniform("normal_matrix", (Matrix3d)view_model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose());
    }

    m_vbo_cone.render_geometry();

    wxGetApp().unbind_shader();
}

void GLGizmoMove3D::change_cs_by_selection() {
    int          obejct_idx, volume_idx;
    ModelVolume *model_volume = m_parent.get_selection().get_selected_single_volume(obejct_idx, volume_idx);
    if (m_last_selected_obejct_idx == obejct_idx && m_last_selected_volume_idx == volume_idx) {
        return;
    }
    m_last_selected_obejct_idx = obejct_idx;
    m_last_selected_volume_idx = volume_idx;
    if (m_parent.get_selection().is_multiple_full_object()) {
        m_object_manipulation->set_use_object_cs(false);
    }
    else if (model_volume) {
         m_object_manipulation->set_use_object_cs(true);
    } else {
        m_object_manipulation->set_use_object_cs(false);
    }
    if (m_object_manipulation->get_use_object_cs()) {
        m_object_manipulation->set_coordinates_type(ECoordinatesType::Instance);
    } else {
        m_object_manipulation->set_coordinates_type(ECoordinatesType::World);
    }
}

} // namespace GUI
} // namespace Slic3r
