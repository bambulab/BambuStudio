// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoScale.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/OpenGLManager.hpp"

#include <GL/glew.h>

#include <wx/utils.h>

namespace Slic3r {
namespace GUI {


const float GLGizmoScale3D::Offset = 5.0f;

// get intersection of ray and plane
Vec3d GetIntersectionOfRayAndPlane(Vec3d ray_position, Vec3d ray_dir, Vec3d plane_position, Vec3d plane_normal)
{
    double t = (plane_normal.dot(plane_position) - plane_normal.dot(ray_position)) / (plane_normal.dot(ray_dir));
    Vec3d  intersection = ray_position + t * ray_dir;
    return intersection;
}

//BBS: GUI refactor: add obj manipulation
GLGizmoScale3D::GLGizmoScale3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id, GizmoObjectManipulation* obj_manipulation)
    : GLGizmoBase(parent, icon_filename, sprite_id)
    , m_scale(Vec3d::Ones())
    , m_offset(Vec3d::Zero())
    , m_snap_step(0.05)
    //BBS: GUI refactor: add obj manipulation
    , m_object_manipulation(obj_manipulation)
{
    m_grabber_connections[0].grabber_indices = { 0, 1 };
    m_grabber_connections[1].grabber_indices = { 2, 3 };
    m_grabber_connections[2].grabber_indices = { 4, 5 };
    m_grabber_connections[3].grabber_indices = { 6, 7 };
    m_grabber_connections[4].grabber_indices = { 7, 8 };
    m_grabber_connections[5].grabber_indices = { 8, 9 };
    m_grabber_connections[6].grabber_indices = { 9, 6 };
}

const Vec3d &GLGizmoScale3D::get_scale()
{
    if (m_object_manipulation) {
        Vec3d cache_scale = m_object_manipulation->get_cache().scale.cwiseQuotient(Vec3d(100,100,100));
        Vec3d temp_scale  = cache_scale.cwiseProduct(m_scale);
        m_object_manipulation->limit_scaling_ratio(temp_scale);
        m_scale = temp_scale.cwiseQuotient(cache_scale);
    }
    return m_scale;
}

std::string GLGizmoScale3D::get_tooltip() const
{
    const Selection& selection = m_parent.get_selection();

    bool single_instance = selection.is_single_full_instance();
    bool single_volume = selection.is_single_modifier() || selection.is_single_volume();

    Vec3f scale = 100.0f * Vec3f::Ones();
    if (single_instance)
        scale = 100.0f * selection.get_volume(*selection.get_volume_idxs().begin())->get_instance_scaling_factor().cast<float>();
    else if (single_volume)
        scale = 100.0f * selection.get_volume(*selection.get_volume_idxs().begin())->get_volume_scaling_factor().cast<float>();

    if (m_hover_id == 0 || m_hover_id == 1 || m_grabbers[0].dragging || m_grabbers[1].dragging)
        return "X: " + format(scale(0), 2) + "%";
    else if (m_hover_id == 2 || m_hover_id == 3 || m_grabbers[2].dragging || m_grabbers[3].dragging)
        return "Y: " + format(scale(1), 2) + "%";
    else if (m_hover_id == 4 || m_hover_id == 5 || m_grabbers[4].dragging || m_grabbers[5].dragging)
        return "Z: " + format(scale(2), 2) + "%";
    else if (m_hover_id == 6 || m_hover_id == 7 || m_hover_id == 8 || m_hover_id == 9 ||
        m_grabbers[6].dragging || m_grabbers[7].dragging || m_grabbers[8].dragging || m_grabbers[9].dragging)
    {
        std::string tooltip = "X: " + format(scale(0), 2) + "%\n";
        tooltip += "Y: " + format(scale(1), 2) + "%\n";
        tooltip += "Z: " + format(scale(2), 2) + "%";
        return tooltip;
    }
    else
        return "";
}

void GLGizmoScale3D::data_changed(bool is_serializing)
{
    set_scale(Vec3d::Ones());

    change_cs_by_selection();
}

void GLGizmoScale3D::enable_ununiversal_scale(bool enable)
{
    for (unsigned int i = 0; i < 6; ++i)
        m_grabbers[i].enabled = enable;
}

BoundingBoxf3 GLGizmoScale3D::get_bounding_box() const
{
    BoundingBoxf3 t_aabb;
    t_aabb.reset();

    for (unsigned int i = 0; i < m_grabbers.size(); ++i) {
        if (!m_grabbers[i].enabled) {
            continue;
        }
        const auto& t_grabber_model = m_grabbers[i].get_cube();
        if (!t_grabber_model.is_initialized()) {
            continue;
        }
        auto t_grabber_aabb = t_grabber_model.get_bounding_box();
        const auto& t_grabber_model_matrix = m_grabbers[i].m_matrix;
        t_grabber_aabb = t_grabber_aabb.transformed(t_grabber_model_matrix);
        t_grabber_aabb.defined = true;

        t_aabb.merge(t_grabber_aabb);
        t_aabb.defined = true;
    }

    return t_aabb;
}

bool GLGizmoScale3D::on_key(const wxKeyEvent& key_event)
{
    bool b_processed = false;
    if (key_event.GetEventType() == wxEVT_KEY_DOWN) {
        if (key_event.GetKeyCode() == WXK_CONTROL) {
            if (!is_scalling_mode_locked()) {
                set_asymmetric_scalling_enable(!is_asymmetric_scalling_enabled());
                lock_scalling_mode(true);
                b_processed = true;
            }
        }
    }
    else if (key_event.GetEventType() == wxEVT_KEY_UP) {
        if (key_event.GetKeyCode() == WXK_CONTROL) {
            lock_scalling_mode(false);
            b_processed = true;
        }
    }
    return b_processed;
}

bool GLGizmoScale3D::on_init()
{
    for (int i = 0; i < 10; ++i)
    {
        m_grabbers.push_back(Grabber());
    }

    double half_pi = 0.5 * (double)PI;


    // BBS
    m_grabbers[4].enabled = false;
    m_shortcut_key = WXK_CONTROL_S;

    return true;
}

std::string GLGizmoScale3D::on_get_name() const
{
    if (!on_is_activable() && m_state == EState::Off) {
        return _u8L("Scale") + ":\n" + _u8L("Please select at least one object.");
    } else {
        return _u8L("Scale");
    }
}

bool GLGizmoScale3D::on_is_activable() const
{
    const Selection &selection = m_parent.get_selection();
    return !selection.is_empty() && !selection.is_wipe_tower() && !selection.is_any_cut_volume() && !selection.is_any_connector();
}

void GLGizmoScale3D::on_set_state() {
    if (get_state() == On) {
        m_last_selected_obejct_idx = -1;
        m_last_selected_volume_idx = -1;
        change_cs_by_selection();
    }
    GLGizmoBase::on_set_state();
}

static int constraint_id(int grabber_id)
{
    static const std::vector<int> id_map = {1, 0, 3, 2, 5, 4, 8, 9, 6, 7};
    return (0 <= grabber_id && grabber_id < (int) id_map.size()) ? id_map[grabber_id] : -1;
}

void GLGizmoScale3D::on_start_dragging()
{
    if (m_hover_id != -1) {
        auto grabbers_transform  = m_grabbers_tran.get_matrix();
        m_starting.drag_position = grabbers_transform * m_grabbers[m_hover_id].center;
        m_starting.plane_center  = grabbers_transform * m_grabbers[4].center; // plane_center = bottom center
        m_starting.plane_nromal  = (grabbers_transform * m_grabbers[5].center - grabbers_transform * m_grabbers[4].center).normalized();
        m_starting.box           = m_bounding_box;

        m_starting.center              = m_center;
        m_starting.instance_center     = m_instance_center;

        const Vec3d  box_half_size = 0.5 * m_bounding_box.size();

        m_starting.local_pivots[0] = Vec3d(box_half_size.x(), 0.0, -box_half_size.z());
        m_starting.local_pivots[1] = Vec3d(-box_half_size.x(), 0.0, -box_half_size.z());
        m_starting.local_pivots[2] = Vec3d(0.0, box_half_size.y(), -box_half_size.z());
        m_starting.local_pivots[3] = Vec3d(0.0, -box_half_size.y(), -box_half_size.z());
        m_starting.local_pivots[4] = Vec3d(0.0, 0.0, box_half_size.z());
        m_starting.local_pivots[5] = Vec3d(0.0, 0.0, -box_half_size.z());
        for (size_t i = 0; i < 6; i++) {
            m_starting.pivots[i] = grabbers_transform * m_starting.local_pivots[i]; // todo delete
        }
        m_starting.constraint_position = grabbers_transform * m_grabbers[constraint_id(m_hover_id)].center;
        m_scale = m_starting.scale = Vec3d::Ones() ;
        m_offset                   = Vec3d::Zero();
    }
}

void GLGizmoScale3D::on_stop_dragging()
{
}

void GLGizmoScale3D::on_update(const UpdateData& data)
{
    if ((m_hover_id == 0) || (m_hover_id == 1))
        do_scale_along_axis(X, data);
    else if ((m_hover_id == 2) || (m_hover_id == 3))
        do_scale_along_axis(Y, data);
    else if ((m_hover_id == 4) || (m_hover_id == 5))
        do_scale_along_axis(Z, data);
    else if (m_hover_id >= 6)
        do_scale_uniform(data);
}

void GLGizmoScale3D::update_grabbers_data()
{
    const Selection &selection = m_parent.get_selection();
    const auto &[box, box_trafo] = selection.get_bounding_box_in_current_reference_system();
    m_bounding_box               = box;
    m_center                     = box_trafo.translation();
    m_grabbers_tran.set_matrix(box_trafo);
    m_instance_center = (selection.is_single_full_instance() || selection.is_single_volume_or_modifier()) ? selection.get_first_volume()->get_instance_offset() : m_center;

    const Vec3d box_half_size   = 0.5 * m_bounding_box.size();
    bool b_asymmetric_scalling = is_asymmetric_scalling_enabled();


    bool single_instance = selection.is_single_full_instance();
    bool single_volume   = selection.is_single_modifier() || selection.is_single_volume();

    // x axis
    m_grabbers[0].center = Vec3d(-(box_half_size.x()), 0.0, -box_half_size.z());
    m_grabbers[0].color  = (b_asymmetric_scalling && m_hover_id == 1) ? CONSTRAINED_COLOR : AXES_COLOR[0];
    m_grabbers[1].center = Vec3d(box_half_size.x(), 0.0, -box_half_size.z());
    m_grabbers[1].color  = (b_asymmetric_scalling && m_hover_id == 0) ? CONSTRAINED_COLOR : AXES_COLOR[0];
    // y axis
    m_grabbers[2].center = Vec3d(0.0, -(box_half_size.y()), -box_half_size.z());
    m_grabbers[2].color  = (b_asymmetric_scalling && m_hover_id == 3) ? CONSTRAINED_COLOR : AXES_COLOR[1];
    m_grabbers[3].center = Vec3d(0.0, box_half_size.y(), -box_half_size.z());
    m_grabbers[3].color  = (b_asymmetric_scalling && m_hover_id == 2) ? CONSTRAINED_COLOR : AXES_COLOR[1];
    // z axis do not show 4
    m_grabbers[4].center  = Vec3d(0.0, 0.0, -(box_half_size.z()));
    m_grabbers[4].enabled = false;

    m_grabbers[5].center = Vec3d(0.0, 0.0, box_half_size.z());
    m_grabbers[5].color  = (b_asymmetric_scalling && m_hover_id == 4) ? CONSTRAINED_COLOR : AXES_COLOR[2];
    // uniform
    m_grabbers[6].center = Vec3d(-box_half_size.x(), -box_half_size.y(), -box_half_size.z());
    m_grabbers[6].color  = (b_asymmetric_scalling && m_hover_id == 8) ? CONSTRAINED_COLOR : GRABBER_UNIFORM_COL;
    m_grabbers[7].center = Vec3d(box_half_size.x(), -box_half_size.y(), -box_half_size.z());
    m_grabbers[7].color  = (b_asymmetric_scalling && m_hover_id == 9) ? CONSTRAINED_COLOR : GRABBER_UNIFORM_COL;
    m_grabbers[8].center = Vec3d(box_half_size.x(), box_half_size.y(), -box_half_size.z());
    m_grabbers[8].color  = (b_asymmetric_scalling && m_hover_id == 6) ? CONSTRAINED_COLOR : GRABBER_UNIFORM_COL;
    m_grabbers[9].center = Vec3d(-box_half_size.x(), box_half_size.y(), -box_half_size.z());
    m_grabbers[9].color  = (b_asymmetric_scalling && m_hover_id == 7) ? CONSTRAINED_COLOR : GRABBER_UNIFORM_COL;

    Transform3d t_model_matrix{ Transform3d::Identity() };
    const auto t_fullsize = get_grabber_size();
    for (int i = 0; i < m_grabbers.size(); ++i) {
        if (i < 6) {
            m_grabbers[i].hover_color = AXES_HOVER_COLOR[i / 2];
        }
        else {
            m_grabbers[i].hover_color = GRABBER_UNIFORM_HOVER_COL;
        }
        t_model_matrix = m_grabbers_tran.get_matrix() * Geometry::assemble_transform(m_grabbers[i].center, Vec3d(0.0f, 0.0f, 0.0f), t_fullsize * Vec3d::Ones());
        m_grabbers[i].set_model_matrix(t_model_matrix);
    }
}


void GLGizmoScale3D::change_cs_by_selection() {
    int          obejct_idx, volume_idx;
    ModelVolume *model_volume = m_parent.get_selection().get_selected_single_volume(obejct_idx, volume_idx);
    if (m_last_selected_obejct_idx == obejct_idx && m_last_selected_volume_idx == volume_idx) { return; }
    m_last_selected_obejct_idx = obejct_idx;
    m_last_selected_volume_idx = volume_idx;
    if (m_parent.get_selection().is_multiple_full_object()) {
        m_object_manipulation->set_coordinates_type(ECoordinatesType::World);
    } else if (model_volume) {
        m_object_manipulation->set_coordinates_type(ECoordinatesType::Local);
    }
}

void GLGizmoScale3D::set_asymmetric_scalling_enable(bool is_enabled)
{
    m_b_asymmetric_scalling = is_enabled;
}

bool GLGizmoScale3D::is_asymmetric_scalling_enabled() const
{
    return m_b_asymmetric_scalling;
}

void GLGizmoScale3D::lock_scalling_mode(bool is_locked)
{
    m_b_scalling_mode_locked = is_locked;
}

bool GLGizmoScale3D::is_scalling_mode_locked() const
{
    return m_b_scalling_mode_locked;
}

void GLGizmoScale3D::on_render()
{
    const Selection& selection = m_parent.get_selection();

    bool single_instance = selection.is_single_full_instance();
    bool single_volume = selection.is_single_modifier() || selection.is_single_volume();

    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glEnable(GL_DEPTH_TEST));

    if (m_hover_id == -1) {
        lock_scalling_mode(false);
        set_asymmetric_scalling_enable(false);
    }

    update_grabbers_data();

    const auto& p_ogl_manager = wxGetApp().get_opengl_manager();
    p_ogl_manager->set_line_width((m_hover_id != -1) ? 2.0f : 1.5f);

    //draw connections
    const auto& shader = wxGetApp().get_shader("flat");
    if (shader != nullptr) {
        wxGetApp().bind_shader(shader);
        // BBS: when select multiple objects, uniform scale can be deselected, display the connection(4,5)
        //if (single_instance || single_volume) {
        const Camera& camera = wxGetApp().plater()->get_camera();
        shader->set_uniform("view_model_matrix", camera.get_view_matrix() * m_grabbers_tran.get_matrix());
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
        if (m_grabbers[4].enabled && m_grabbers[5].enabled)
            render_grabbers_connection(4, 5, m_grabbers[4].color);
        render_grabbers_connection(6, 7, m_grabbers[2].color);
        render_grabbers_connection(7, 8, m_grabbers[0].color);
        render_grabbers_connection(8, 9, m_grabbers[2].color);
        render_grabbers_connection(9, 6, m_grabbers[0].color);
        wxGetApp().unbind_shader();
    }
    // draw grabbers
    render_grabbers();
}

void GLGizmoScale3D::on_render_for_picking()
{
    glsafe(::glDisable(GL_DEPTH_TEST));
    render_grabbers_for_picking(m_parent.get_selection().get_bounding_box());
}

void GLGizmoScale3D::render_grabbers_connection(unsigned int id_1, unsigned int id_2, const ColorRGBA& color) const
{
    auto grabber_connection = [this](unsigned int id_1, unsigned int id_2) {
        for (int i = 0; i < int(m_grabber_connections.size()); ++i) {
            if (m_grabber_connections[i].grabber_indices.first == id_1 && m_grabber_connections[i].grabber_indices.second == id_2)
                return i;
        }
        return -1;
        };

    const int id = grabber_connection(id_1, id_2);
    if (id == -1)
        return;

    if (!m_grabber_connections[id].model.is_initialized() ||
        !m_grabber_connections[id].old_v1.isApprox(m_grabbers[id_1].center) ||
        !m_grabber_connections[id].old_v2.isApprox(m_grabbers[id_2].center)) {
        m_grabber_connections[id].old_v1 = m_grabbers[id_1].center;
        m_grabber_connections[id].old_v2 = m_grabbers[id_2].center;
        m_grabber_connections[id].model.reset();

        GLModel::Geometry init_data;
        init_data.format = { GLModel::PrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
        init_data.reserve_vertices(2);
        init_data.reserve_indices(2);

        // vertices
        init_data.add_vertex((Vec3f)m_grabbers[id_1].center.cast<float>());
        init_data.add_vertex((Vec3f)m_grabbers[id_2].center.cast<float>());

        // indices
        init_data.add_line(0, 1);

        m_grabber_connections[id].model.init_from(std::move(init_data));
    }

    m_grabber_connections[id].model.set_color(color);

#ifdef __APPLE__
    const auto& p_ogl_manager = wxGetApp().get_opengl_manager();
    const auto& gl_info = p_ogl_manager->get_gl_info();
    const auto formated_gl_version = gl_info.get_formated_gl_version();
    if (formated_gl_version < 30)
#endif
    {
        glsafe(::glLineStipple(1, 0x0FFF));
        glsafe(::glEnable(GL_LINE_STIPPLE));
    }

    m_grabber_connections[id].model.render_geometry();

#ifdef __APPLE__
    if (formated_gl_version < 30)
#endif
    {
        glsafe(::glDisable(GL_LINE_STIPPLE));
    }
}

//BBS: add input window for move
void GLGizmoScale3D::on_render_input_window(float x, float y, float bottom_limit)
{
    if (m_object_manipulation)
        m_object_manipulation->do_render_scale_input_window(m_imgui, "Scale", x, y, bottom_limit);
}

void GLGizmoScale3D::do_scale_along_axis(Axis axis, const UpdateData& data)
{
    double ratio = calc_ratio(data);
    if (ratio > 0.0)
    {
        m_scale(axis) = m_starting.scale(axis) * ratio;
        if (is_asymmetric_scalling_enabled() && abs(ratio - 1.0f)>0.001) {
            double local_offset = 0.5 * (m_scale(axis) - m_starting.scale(axis)) * m_starting.box.size()(axis);
            if (m_hover_id == 2 * axis) {
                local_offset *= -1.0;
            }
            Vec3d local_offset_vec;
            switch (axis)
            {
            case X: { local_offset_vec = local_offset * Vec3d::UnitX(); break; }
            case Y: { local_offset_vec = local_offset * Vec3d::UnitY(); break;}
            case Z: { local_offset_vec = local_offset * Vec3d::UnitZ(); break;
            }
            default: break;
            }
            if (m_object_manipulation->is_world_coordinates()) {
                m_offset = local_offset_vec;
            } else {//if (m_object_manipulation->is_instance_coordinates())
                m_offset = m_grabbers_tran.get_matrix_no_offset() *  local_offset_vec;
            }
        }
        else
            m_offset = Vec3d::Zero();
    }
}

void GLGizmoScale3D::do_scale_uniform(const UpdateData& data)
{
    double ratio = calc_ratio(data);
    if (ratio > 0.0)
    {
        m_scale = m_starting.scale * ratio;
        m_offset = Vec3d::Zero();
    }
}

double GLGizmoScale3D::calc_ratio(const UpdateData& data) const
{
    double ratio = 0.0;

    Vec3d  pivot = (is_asymmetric_scalling_enabled() && (m_hover_id < 6)) ? m_starting.constraint_position : m_starting.plane_center; // plane_center = bottom center
    Vec3d starting_vec = m_starting.drag_position - pivot;
    double len_starting_vec = starting_vec.norm();
    if (len_starting_vec != 0.0)
    {
        Vec3d mouse_dir = data.mouse_ray.unit_vector();
        Vec3d inters = data.mouse_ray.a + (m_starting.drag_position - data.mouse_ray.a).dot(mouse_dir) / mouse_dir.squaredNorm() * mouse_dir;
        // vector from the starting position to the found intersection
        Vec3d inters_vec = inters - m_starting.drag_position;

        double proj = inters_vec.norm();
        const double sign = inters_vec.dot(starting_vec) > 1e-6f ? 1.0f : -1.0f;

        ratio = (len_starting_vec + proj * sign) / len_starting_vec;
    }

    if (wxGetKeyState(WXK_SHIFT))
        ratio = m_snap_step * (double)std::round(ratio / m_snap_step);

    return ratio;
}

} // namespace GUI
} // namespace Slic3r
