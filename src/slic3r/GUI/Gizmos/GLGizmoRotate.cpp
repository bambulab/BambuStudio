// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoRotate.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"

#include <GL/glew.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/OpenGLManager.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "slic3r/GUI/Jobs/RotoptimizeJob.hpp"

namespace Slic3r {
namespace GUI {


const float GLGizmoRotate::Offset = 5.0;
const unsigned int GLGizmoRotate::CircleResolution = 64;
const unsigned int GLGizmoRotate::AngleResolution = 64;
const unsigned int GLGizmoRotate::ScaleStepsCount = 72;
const float GLGizmoRotate::ScaleStepRad = 2.0f * (float)PI / GLGizmoRotate::ScaleStepsCount;
const unsigned int GLGizmoRotate::ScaleLongEvery = 2;
const float GLGizmoRotate::ScaleLongTooth = 0.1f; // in percent of radius
const unsigned int GLGizmoRotate::SnapRegionsCount = 8;
const float GLGizmoRotate::GrabberOffset = 0.15f; // in percent of radius


GLGizmoRotate::GLGizmoRotate(GLCanvas3D& parent, GLGizmoRotate::Axis axis)
    : GLGizmoBase(parent, "", -1)
    , m_axis(axis)
    , m_angle(0.0)
    , m_center(0.0, 0.0, 0.0)
    , m_radius(0.0f)
    , m_snap_coarse_in_radius(0.0f)
    , m_snap_coarse_out_radius(0.0f)
    , m_snap_fine_in_radius(0.0f)
    , m_snap_fine_out_radius(0.0f)
{
}

GLGizmoRotate::GLGizmoRotate(const GLGizmoRotate& other)
    : GLGizmoBase(other.m_parent, other.m_icon_filename, other.m_sprite_id)
    , m_axis(other.m_axis)
    , m_angle(other.m_angle)
    , m_center(other.m_center)
    , m_radius(other.m_radius)
    , m_snap_coarse_in_radius(other.m_snap_coarse_in_radius)
    , m_snap_coarse_out_radius(other.m_snap_coarse_out_radius)
    , m_snap_fine_in_radius(other.m_snap_fine_in_radius)
    , m_snap_fine_out_radius(other.m_snap_fine_out_radius)
{
}


void GLGizmoRotate::set_angle(double angle)
{
    if (std::abs(angle - 2.0 * (double)PI) < EPSILON)
        angle = 0.0;

    m_angle = angle;
}

std::string GLGizmoRotate::get_tooltip() const
{
    std::string axis;
    switch (m_axis)
    {
    case X: { axis = "X"; break; }
    case Y: { axis = "Y"; break; }
    case Z: { axis = "Z"; break; }
    }
    return (m_hover_id == 0 || m_grabbers[0].dragging) ? axis + ": " + format((float)Geometry::rad2deg(m_angle), 2) : "";
}

bool GLGizmoRotate::on_init()
{
    m_grabbers.push_back(Grabber());
    return true;
}

void GLGizmoRotate::on_start_dragging()
{
    init_data_from_selection(m_parent.get_selection());
}

void GLGizmoRotate::on_update(const UpdateData& data)
{
    Vec2d mouse_pos = to_2d(mouse_position_in_local_plane(data.mouse_ray, m_parent.get_selection()));

    Vec2d orig_dir = Vec2d::UnitX();
    Vec2d new_dir = mouse_pos.normalized();

    double theta = ::acos(std::clamp(new_dir.dot(orig_dir), -1.0, 1.0));
    if (cross2(orig_dir, new_dir) < 0.0)
        theta = 2.0 * (double)PI - theta;

    double len = mouse_pos.norm();

    auto radius = m_radius;
    modify_radius(radius);

    const auto& scale_factor = Geometry::Transformation(m_base_model_matrix).get_scaling_factor();
    radius = scale_factor.maxCoeff()* radius;
    // snap to coarse snap region
    if ((m_snap_coarse_in_radius * radius <= len) && (len <= m_snap_coarse_out_radius * radius))
    {
        double step = 2.0 * (double)PI / (double)SnapRegionsCount;
        theta = step * (double)std::round(theta / step);
    }
    else
    {
        // snap to fine snap region (scale)
        if ((m_snap_fine_in_radius * radius <= len) && (len <= m_snap_fine_out_radius * radius))
        {
            double step = 2.0 * (double)PI / (double)ScaleStepsCount;
            theta = step * (double)std::round(theta / step);
        }
    }

    if (theta == 2.0 * (double)PI)
        theta = 0.0;

    m_angle = theta;
}

void GLGizmoRotate::on_render()
{
    if (!m_grabbers[0].enabled)
        return;

    const Selection& selection = m_parent.get_selection();
    if (m_hover_id != 0 && !m_grabbers[0].dragging) {
        init_data_from_selection(selection);
    }
    auto radius = m_radius;
    modify_radius(radius);
    const double grabber_radius = (double)radius * (1.0 + (double)GrabberOffset);
    m_grabbers.front().center = Vec3d(::cos(m_angle) * grabber_radius, ::sin(m_angle) * grabber_radius, 0.0);
    m_grabbers.front().color = AXES_COLOR[m_axis];
    m_grabbers.front().hover_color = AXES_HOVER_COLOR[m_axis];

    m_base_model_matrix = transform_to_local(selection);

    const auto t_angle = Vec3d(0.0f, 0.0f, m_angle);
    const auto t_fullsize = get_grabber_size();
    m_grabbers.front().m_matrix = m_base_model_matrix * Geometry::assemble_transform(m_grabbers.front().center, t_angle, t_fullsize * Vec3d::Ones());

    glsafe(::glEnable(GL_DEPTH_TEST));
    const auto& p_ogl_manager = wxGetApp().get_opengl_manager();
    if (p_ogl_manager) {
        p_ogl_manager->set_line_width((m_hover_id != -1) ? 2.0f : 1.5f);
    }
    ColorRGBA color((m_hover_id != -1) ? m_drag_color : m_highlight_color);
    const auto& shader = wxGetApp().get_shader("flat");
    if (shader) {
        wxGetApp().bind_shader(shader);

        const Camera& camera = wxGetApp().plater()->get_camera();
        Transform3d redius_scale_matrix;
        Geometry::scale_transform(redius_scale_matrix, { radius, radius, radius });
        Transform3d view_model_matrix = camera.get_view_matrix() * m_base_model_matrix * redius_scale_matrix;

        shader->set_uniform("view_model_matrix", view_model_matrix);
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());

        render_circle(color);

        if (m_hover_id != -1) {
            render_scale(color);
            render_snap_radii(color);
            render_reference_radius(color);
            render_angle(m_highlight_color);
        }
        Transform3d grabber_connection_model_matrix = Geometry::scale_transform({ ::cos(m_angle), ::sin(m_angle), 1.0f });
        shader->set_uniform("view_model_matrix", view_model_matrix * grabber_connection_model_matrix);
        render_grabber_connection(color);

        wxGetApp().unbind_shader();
    }

    render_grabber(m_bounding_box);
    render_grabber_extension(m_bounding_box, false);
}

void GLGizmoRotate::on_render_for_picking()
{
    const Selection& selection = m_parent.get_selection();

    glsafe(::glDisable(GL_DEPTH_TEST));

    const BoundingBoxf3& box = selection.get_bounding_box();
    render_grabbers_for_picking(box);
    render_grabber_extension(box, true);
}

//BBS: add input window for move
void GLGizmoRotate3D::on_render_input_window(float x, float y, float bottom_limit)
{
    //if (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptSLA)
    //    return;
    if (m_object_manipulation)
        m_object_manipulation->do_render_rotate_window(m_imgui, "Rotate", x, y, bottom_limit);
    //RotoptimzeWindow popup{m_imgui, m_rotoptimizewin_state, {x, y, bottom_limit}};
}

void GLGizmoRotate3D::load_rotoptimize_state()
{
    std::string accuracy_str =
        wxGetApp().app_config->get("sla_auto_rotate", "accuracy");

    std::string method_str =
        wxGetApp().app_config->get("sla_auto_rotate", "method_id");

    if (!accuracy_str.empty()) {
        float accuracy = std::stof(accuracy_str);
        accuracy = std::max(0.f, std::min(accuracy, 1.f));

        m_rotoptimizewin_state.accuracy = accuracy;
    }

    if (!method_str.empty()) {
        int method_id = std::stoi(method_str);
        if (method_id < int(RotoptimizeJob::get_methods_count()))
            m_rotoptimizewin_state.method_id = method_id;
    }
}

void GLGizmoRotate::render_circle(const ColorRGBA& color) const
{
    if (!m_circle.is_initialized()) {
        m_circle.reset();

        GLModel::Geometry init_data;
        init_data.format = { GLModel::PrimitiveType::LineLoop, GLModel::Geometry::EVertexLayout::P3 };
        init_data.reserve_vertices(ScaleStepsCount);
        init_data.reserve_indices(ScaleStepsCount);

        // vertices + indices
        for (unsigned short i = 0; i < ScaleStepsCount; ++i) {
            const float angle = float(i * ScaleStepRad);
            init_data.add_vertex(Vec3f(::cos(angle), ::sin(angle), 0.0f));
            init_data.add_index(i);
        }

        m_circle.init_from(std::move(init_data));
    }

    m_circle.set_color(color);
    m_circle.render_geometry();
}

void GLGizmoRotate::render_scale(const ColorRGBA& color) const
{
    const float out_radius_long = m_snap_fine_out_radius;
    const float out_radius_short = 1.0f * (1.0f + 0.5f * ScaleLongTooth);

    if (!m_scale.is_initialized()) {
        m_scale.reset();

        GLModel::Geometry init_data;
        init_data.format = { GLModel::PrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
        init_data.reserve_vertices(2 * ScaleStepsCount);
        init_data.reserve_indices(2 * ScaleStepsCount);

        // vertices + indices
        for (unsigned short i = 0; i < ScaleStepsCount; ++i) {
            const float angle = float(i * ScaleStepRad);
            const float cosa = ::cos(angle);
            const float sina = ::sin(angle);
            const float in_x = cosa;
            const float in_y = sina;
            const float out_x = (i % ScaleLongEvery == 0) ? cosa * out_radius_long : cosa * out_radius_short;
            const float out_y = (i % ScaleLongEvery == 0) ? sina * out_radius_long : sina * out_radius_short;

            init_data.add_vertex(Vec3f(in_x, in_y, 0.0f));
            init_data.add_vertex(Vec3f(out_x, out_y, 0.0f));
            init_data.add_index(i * 2);
            init_data.add_index(i * 2 + 1);
        }

        m_scale.init_from(std::move(init_data));
    }

    m_scale.set_color(color);
    m_scale.render_geometry();
}

void GLGizmoRotate::render_snap_radii(const ColorRGBA& color) const
{
    const float step = 2.0f * float(PI) / float(SnapRegionsCount);
    const float in_radius = 1.0f / 3.0f;
    const float out_radius = 2.0f * in_radius;

    if (!m_snap_radii.is_initialized()) {
        m_snap_radii.reset();

        GLModel::Geometry init_data;
        init_data.format = { GLModel::PrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
        init_data.reserve_vertices(2 * ScaleStepsCount);
        init_data.reserve_indices(2 * ScaleStepsCount);

        // vertices + indices
        for (unsigned short i = 0; i < ScaleStepsCount; ++i) {
            const float angle = float(i * step);
            const float cosa = ::cos(angle);
            const float sina = ::sin(angle);
            const float in_x = cosa * in_radius;
            const float in_y = sina * in_radius;
            const float out_x = cosa * out_radius;
            const float out_y = sina * out_radius;

            init_data.add_vertex(Vec3f(in_x, in_y, 0.0f));
            init_data.add_vertex(Vec3f(out_x, out_y, 0.0f));
            init_data.add_index(i * 2);
            init_data.add_index(i * 2 + 1);
        }

        m_snap_radii.init_from(std::move(init_data));
    }

    m_snap_radii.set_color(color);
    m_snap_radii.render_geometry();
}

void GLGizmoRotate::render_reference_radius(const ColorRGBA& color) const
{
    if (!m_reference_radius.is_initialized()) {
        m_reference_radius.reset();

        GLModel::Geometry init_data;
        init_data.format = { GLModel::PrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
        init_data.reserve_vertices(2);
        init_data.reserve_indices(2);

        // vertices
        init_data.add_vertex(Vec3f(0.0f, 0.0f, 0.0f));
        init_data.add_vertex(Vec3f(1.0f * (1.0f + GrabberOffset), 0.0f, 0.0f));

        // indices
        init_data.add_line(0, 1);

        m_reference_radius.init_from(std::move(init_data));
    }

    m_reference_radius.set_color(color);
    m_reference_radius.render_geometry();
}

void GLGizmoRotate::render_angle(const ColorRGBA& color) const
{
    const float step_angle = float(m_angle) / float(AngleResolution);
    const float ex_radius = 1.0f * (1.0f + GrabberOffset);

    const bool angle_changed = std::abs(m_old_angle - m_angle) > EPSILON;
    m_old_angle = m_angle;

    if (!m_angle_arc.is_initialized() || angle_changed) {
        m_angle_arc.reset();
        if (m_angle > 0.0f) {
            GLModel::Geometry init_data;
            init_data.format = { GLModel::PrimitiveType::LineStrip, GLModel::Geometry::EVertexLayout::P3 };
            init_data.reserve_vertices(1 + AngleResolution);
            init_data.reserve_indices(1 + AngleResolution);

            // vertices + indices
            for (unsigned short i = 0; i <= AngleResolution; ++i) {
                const float angle = float(i) * step_angle;
                init_data.add_vertex(Vec3f(::cos(angle) * ex_radius, ::sin(angle) * ex_radius, 0.0f));
                init_data.add_index(i);
            }

            m_angle_arc.init_from(std::move(init_data));
        }
    }

    m_angle_arc.set_color(color);
    m_angle_arc.render_geometry();
}

void GLGizmoRotate::render_grabber_connection(const ColorRGBA& color)
{
    if (!m_grabber_connection.model.is_initialized()) {
        m_grabber_connection.model.reset();

        GLModel::Geometry init_data;
        init_data.format = { GLModel::PrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
        init_data.reserve_vertices(2);
        init_data.reserve_indices(2);

        // vertices
        init_data.add_vertex(Vec3f(0.0f, 0.0f, 0.0f));
        init_data.add_vertex(Vec3f(1.0 + (double)GrabberOffset, 1.0 + (double)GrabberOffset, 0.0f));

        // indices
        init_data.add_line(0, 1);

        m_grabber_connection.model.init_from(std::move(init_data));
    }

    m_grabber_connection.model.set_color(color);
    m_grabber_connection.model.render_geometry();
}

void GLGizmoRotate::render_grabber(const BoundingBoxf3& box) const
{
    m_grabbers.front().color = m_highlight_color;
    render_grabbers(box);
}

void GLGizmoRotate::render_grabber_extension(const BoundingBoxf3& box, bool picking) const
{
    double size = get_grabber_size() * 0.75;//0.75 for arrow show
    std::array<float, 4> color = m_grabbers[0].color;
    if (!picking && m_hover_id != -1) {
        color = m_grabbers[0].hover_color;
    }

    const auto& shader = wxGetApp().get_shader(picking ? "flat" : "gouraud_light");
    if (shader == nullptr)
        return;

    const_cast<GLModel*>(&m_cone)->set_color(-1, color);

    wxGetApp().bind_shader(shader);

    if (!picking) {
        shader->set_uniform("emission_factor", 0.1f);
    }

    const Vec3d& center = m_grabbers.front().center;

    const Camera& camera = picking ? wxGetApp().plater()->get_picking_camera() : wxGetApp().plater()->get_camera();
    const Transform3d& view_matrix = camera.get_view_matrix();
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());

    Transform3d view_model_matrix = view_matrix * m_base_model_matrix *
        Geometry::assemble_transform(center, Vec3d(0.5 * PI, 0.0, m_angle)) *
        Geometry::assemble_transform(1.5 * size * Vec3d::UnitZ(), Vec3d::Zero(), Vec3d(0.75 * size, 0.75 * size, 3.0 * size));

    shader->set_uniform("view_model_matrix", view_model_matrix);
    shader->set_uniform("normal_matrix", (Matrix3d)view_model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose());
    m_cone.render_geometry();

    view_model_matrix = view_matrix * m_base_model_matrix *
        Geometry::assemble_transform(center, Vec3d(-0.5 * PI, 0.0, m_angle)) *
        Geometry::assemble_transform(1.5 * size * Vec3d::UnitZ(), Vec3d::Zero(), Vec3d(0.75 * size, 0.75 * size, 3.0 * size));

    shader->set_uniform("view_model_matrix", view_model_matrix);
    shader->set_uniform("normal_matrix", (Matrix3d)view_model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose());
    m_cone.render_geometry();

    wxGetApp().unbind_shader();
}

Transform3d GLGizmoRotate::calculate_circle_model_matrix() const
{
    auto radius = m_radius;
    modify_radius(radius);
    Transform3d redius_scale_matrix;
    Geometry::scale_transform(redius_scale_matrix, { radius, radius, radius });
    const Selection& selection = m_parent.get_selection();
    const Transform3d model_matrix = transform_to_local(selection);
    return model_matrix * redius_scale_matrix;
}

Transform3d  GLGizmoRotate::transform_to_local(const Selection &selection) const
{
    Transform3d ret;
    switch (m_axis)
    {
    case X:
    {
        ret = Geometry::assemble_transform(Vec3d::Zero(), 0.5 * PI * Vec3d::UnitY()) * Geometry::assemble_transform(Vec3d::Zero(), -0.5 * PI * Vec3d::UnitZ());
        break;
    }
    case Y:
    {
        ret = Geometry::assemble_transform(Vec3d::Zero(), -0.5 * PI * Vec3d::UnitZ()) * Geometry::assemble_transform(Vec3d::Zero(), -0.5 * PI * Vec3d::UnitY());
        break;
    }
    default:
    case Z:
    {
        ret = Transform3d::Identity();
        break;
    }
    }

    return m_orient_matrix * ret;

}

Vec3d GLGizmoRotate::mouse_position_in_local_plane(const Linef3& mouse_ray, const Selection& selection) const
{
    const double half_pi = 0.5 * double(PI);
    Transform3d m = Transform3d::Identity();
    switch (m_axis) {
    case X: {
        m.rotate(Eigen::AngleAxisd(half_pi, Vec3d::UnitZ()));
        m.rotate(Eigen::AngleAxisd(-half_pi, Vec3d::UnitY()));
        break;
    }
    case Y: {
        m.rotate(Eigen::AngleAxisd(half_pi, Vec3d::UnitY()));
        m.rotate(Eigen::AngleAxisd(half_pi, Vec3d::UnitZ()));
        break;
    }
    default:
    case Z: {
        // no rotation applied
        break;
    }
    }
    m = m * Geometry::Transformation(m_orient_matrix).get_matrix_no_offset().inverse();
    m.translate(-m_center);
    const Linef3 local_mouse_ray = transform(mouse_ray, m);
    if (std::abs(local_mouse_ray.vector().dot(Vec3d::UnitZ())) < EPSILON) {
        // if the ray is parallel to the plane containing the circle
        if (std::abs(local_mouse_ray.vector().dot(Vec3d::UnitY())) > 1.0 - EPSILON)
            // if the ray is parallel to grabber direction
            return Vec3d::UnitX();
        else {
            const Vec3d world_pos = (local_mouse_ray.a.x() >= 0.0) ? mouse_ray.a - m_center : mouse_ray.b - m_center;
            m.translate(m_center);
            return m * world_pos;
        }
    } else
        return local_mouse_ray.intersect_plane(0.0);
}

void GLGizmoRotate::init_data_from_selection(const Selection &selection) {
    const auto [box, box_trafo]           = m_force_local_coordinate ? selection.get_bounding_box_in_reference_system(ECoordinatesType::Local) :
                                                                       selection.get_bounding_box_in_current_reference_system();
    m_bounding_box                        = box;
    const std::pair<Vec3d, double> sphere = selection.get_bounding_sphere();
    if (m_custom_tran.has_value()) {
        Geometry::Transformation tran(m_custom_tran.value());
        m_center = tran.get_offset();
        m_orient_matrix = tran.get_matrix();
    } else {
        m_center = sphere.first;
        m_orient_matrix = box_trafo;
    }
    m_radius                              = Offset + sphere.second;

    m_orient_matrix.translation()         = m_center;
    m_snap_coarse_in_radius               = 1.0f / 3.0f;
    m_snap_coarse_out_radius              = 2.0f * m_snap_coarse_in_radius;
    m_snap_fine_in_radius                 = 1.0f;
    m_snap_fine_out_radius                = m_snap_fine_in_radius + 1.0f * ScaleLongTooth;

}

void GLGizmoRotate::set_custom_tran(const Transform3d &tran) {
    m_custom_tran = tran;
}

BoundingBoxf3 GLGizmoRotate::get_bounding_box() const
{
    BoundingBoxf3 t_aabb;
    t_aabb.reset();

    // m_circle aabb
    Transform3d t_circle_model_matrix = calculate_circle_model_matrix();
    if (m_circle.is_initialized()) {
        BoundingBoxf3 t_circle_aabb = m_circle.get_bounding_box();
        t_circle_aabb.defined = true;
        t_circle_aabb = t_circle_aabb.transformed(t_circle_model_matrix);
        t_circle_aabb.defined = true;
        t_aabb.merge(t_circle_aabb);
        t_aabb.defined = true;
    }
    // end m_circle aabb

    // m_grabber_connection aabb
    if (m_grabber_connection.model.is_initialized()) {
        BoundingBoxf3 t_grabber_connection_aabb = m_grabber_connection.model.get_bounding_box();
        t_grabber_connection_aabb = t_grabber_connection_aabb.transformed(t_circle_model_matrix);
        t_grabber_connection_aabb.defined = true;
        t_aabb.merge(t_grabber_connection_aabb);
        t_aabb.defined = true;
    }


    // m_grabbers aabb
    if (m_grabbers.front().get_cube().is_initialized()) {
        auto t_grabbers_aabb = m_grabbers.front().get_cube().get_bounding_box();
        t_grabbers_aabb = t_grabbers_aabb.transformed(m_grabbers.front().m_matrix);
        t_grabbers_aabb.defined = true;
        t_aabb.merge(t_grabbers_aabb);
        t_aabb.defined = true;
    }
    // end m_grabbers aabb

    // m_cone aabb
    if (m_cone.is_initialized()) {
        auto t_cone_aabb = m_cone.get_bounding_box();
        t_cone_aabb = t_cone_aabb.transformed(m_grabbers.front().m_matrix);
        t_cone_aabb.defined = true;
        t_aabb.merge(t_cone_aabb);
        t_aabb.defined = true;
    }
    return t_aabb;
}

//BBS: GUI refactor: add obj manipulation
GLGizmoRotate3D::GLGizmoRotate3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id, GizmoObjectManipulation* obj_manipulation)
    : GLGizmoBase(parent, icon_filename, sprite_id)
    //BBS: GUI refactor: add obj manipulation
    , m_object_manipulation(obj_manipulation)
{
    m_gizmos.emplace_back(parent, GLGizmoRotate::X);
    m_gizmos.emplace_back(parent, GLGizmoRotate::Y);
    m_gizmos.emplace_back(parent, GLGizmoRotate::Z);

    for (unsigned int i = 0; i < 3; ++i) {
        m_gizmos[i].set_group_id(i);
    }

    load_rotoptimize_state();
}

BoundingBoxf3 GLGizmoRotate3D::get_bounding_box() const
{
    BoundingBoxf3 t_aabb;
    t_aabb.reset();

    if (m_hover_id == -1 || m_hover_id == 0)
    {
        const auto t_x_aabb = m_gizmos[X].get_bounding_box();
        t_aabb.merge(t_x_aabb);
        t_aabb.defined = true;
    }

    if (m_hover_id == -1 || m_hover_id == 1)
    {
        const auto t_y_aabb = m_gizmos[Y].get_bounding_box();
        t_aabb.merge(t_y_aabb);
        t_aabb.defined = true;
    }

    if (m_hover_id == -1 || m_hover_id == 2)
    {
        const auto t_z_aabb = m_gizmos[Z].get_bounding_box();
        t_aabb.merge(t_z_aabb);
        t_aabb.defined = true;
    }
    return t_aabb;
}

bool GLGizmoRotate3D::on_init()
{
    for (GLGizmoRotate& g : m_gizmos) {
        if (!g.init())
            return false;
    }

    for (unsigned int i = 0; i < 3; ++i) {
        m_gizmos[i].set_highlight_color(AXES_COLOR[i]);
    }

    m_shortcut_key = WXK_CONTROL_R;

    return true;
}

std::string GLGizmoRotate3D::on_get_name() const
{
    if (!on_is_activable() && m_state == EState::Off) {
        return _u8L("Rotate") + ":\n" + _u8L("Please select at least one object.");
    } else {
        return _u8L("Rotate");
    }
}

void GLGizmoRotate3D::on_set_state()
{
    for (GLGizmoRotate &g : m_gizmos)
        g.set_state(m_state);
    if (get_state() == On && m_object_manipulation) {
        m_object_manipulation->set_coordinates_type(ECoordinatesType::World);
        m_last_volume = nullptr;
    }
    GLGizmoBase::on_set_state();
}

void GLGizmoRotate3D::data_changed(bool is_serializing) {
    const Selection &selection = m_parent.get_selection();
    const GLVolume * volume    = selection.get_first_volume();
    if (volume == nullptr) {
        m_last_volume = nullptr;
        return;
    }
    if (m_last_volume != volume) {
        m_last_volume = volume;
        Geometry::Transformation tran;
        if (selection.is_single_full_instance()) {
            tran = volume->get_instance_transformation();
        } else {
            tran = volume->get_volume_transformation();
        }
        m_object_manipulation->set_init_rotation(tran);
    }
    for (GLGizmoRotate &g : m_gizmos)
        g.init_data_from_selection(m_parent.get_selection());
}

bool GLGizmoRotate3D::on_is_activable() const
{
    const Selection &selection = m_parent.get_selection();
    return !selection.is_empty() && !selection.is_wipe_tower() // BBS: don't support rotate wipe tower
        &&!selection.is_any_cut_volume() && !selection.is_any_connector();
}

void GLGizmoRotate3D::on_start_dragging()
{
    if ((0 <= m_hover_id) && (m_hover_id < 3))
        m_gizmos[m_hover_id].start_dragging();
}

void GLGizmoRotate3D::on_stop_dragging()
{
    if ((0 <= m_hover_id) && (m_hover_id < 3))
        m_gizmos[m_hover_id].stop_dragging();
}

void GLGizmoRotate3D::on_render()
{
    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));

    if (m_hover_id == -1 || m_hover_id == 0)
        m_gizmos[X].render();

    if (m_hover_id == -1 || m_hover_id == 1)
        m_gizmos[Y].render();

    if (m_hover_id == -1 || m_hover_id == 2)
        m_gizmos[Z].render();
}

GLGizmoRotate3D::RotoptimzeWindow::RotoptimzeWindow(ImGuiWrapper *   imgui,
                                                    State &          state,
                                                    const Alignment &alignment)
    : m_imgui{imgui}
{
    imgui->begin(_L("Optimize orientation"), ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_AlwaysAutoResize |
                                     ImGuiWindowFlags_NoCollapse);

    // adjust window position to avoid overlap the view toolbar
    float win_h = ImGui::GetWindowHeight();
    float x = alignment.x, y = alignment.y;
    y = std::min(y, alignment.bottom_limit - win_h);
    ImGui::SetWindowPos(ImVec2(x, y), ImGuiCond_Always);

    float max_text_w = 0.;
    auto padding = ImGui::GetStyle().FramePadding;
    padding.x *= 2.f;
    padding.y *= 2.f;

    for (size_t i = 0; i < RotoptimizeJob::get_methods_count(); ++i) {
        float w =
            ImGui::CalcTextSize(RotoptimizeJob::get_method_name(i).c_str()).x +
            padding.x + ImGui::GetFrameHeight();
        max_text_w = std::max(w, max_text_w);
    }

    ImGui::PushItemWidth(max_text_w);

    if (ImGui::BeginCombo("", RotoptimizeJob::get_method_name(state.method_id).c_str())) {
        for (size_t i = 0; i < RotoptimizeJob::get_methods_count(); ++i) {
            if (ImGui::Selectable(RotoptimizeJob::get_method_name(i).c_str())) {
                state.method_id = i;
#ifdef SUPPORT_SLA_AUTO_ROTATE
                wxGetApp().app_config->set("sla_auto_rotate",
                                           "method_id",
                                           std::to_string(state.method_id));
#endif SUPPORT_SLA_AUTO_ROTATE
            }

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", RotoptimizeJob::get_method_description(i).c_str());
        }

        ImGui::EndCombo();
    }

    ImVec2 sz = ImGui::GetItemRectSize();

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", RotoptimizeJob::get_method_description(state.method_id).c_str());

    ImGui::Separator();

    auto btn_txt = _L("Apply");
    auto btn_txt_sz = ImGui::CalcTextSize(btn_txt.c_str());
    ImVec2 button_sz = {btn_txt_sz.x + padding.x, btn_txt_sz.y + padding.y};
    ImGui::SetCursorPosX(padding.x + sz.x - button_sz.x);

    if (wxGetApp().plater()->is_any_job_running())
        imgui->disabled_begin(true);

    if ( imgui->button(btn_txt) ) {
        wxGetApp().plater()->optimize_rotation();
    }

    imgui->disabled_end();
}

GLGizmoRotate3D::RotoptimzeWindow::~RotoptimzeWindow()
{
    m_imgui->end();
}

} // namespace GUI
} // namespace Slic3r
