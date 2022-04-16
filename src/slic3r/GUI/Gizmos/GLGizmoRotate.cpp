// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoRotate.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"

#include <GL/glew.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Tesselate.hpp"

#include "slic3r/GUI/Jobs/RotoptimizeJob.hpp"

namespace Slic3r {
namespace GUI {


const unsigned int GLGizmoRotate::ScaleStepsCount = 72;
const float GLGizmoRotate::ScaleStepRad = 2.0f * (float)PI / GLGizmoRotate::ScaleStepsCount;
const unsigned int GLGizmoRotate::ScaleLongEvery = 9;
//const float GLGizmoRotate::ScaleLongTooth = 0.1f; // in percent of radius
const unsigned int GLGizmoRotate::SnapRegionsCount = 16;
const float GLGizmoRotate::GrabberOffset = 20;
const float GLGizmoRotate::CircleOffset = 20;

const float GLGizmoRotate::MaxGrabberRadius = 128.0f;
const float GLGizmoRotate::GrabberRange = 20.0f;
const float GLGizmoRotate::GrabberDepth = 2.5f;
const float GLGizmoRotate::ArrowDepth = 6.0f;
const float GLGizmoRotate::ArrowRange = 5.0f;
//const float GLGizmoRotate::ArrowLen = 5.0f;
const float GLGizmoRotate::ArrowDegree = PI / 6;
std::array<float, 4> GLGizmoRotate::CIRCLE_FILL_COLOR = { 1.0f, 0.4f, 0.4f, 0.75f };
std::array<float, 4> GLGizmoRotate::CIRCLE_BACK_COLOR = { 1.0f, 0.7f, 0.7f, 0.5f };
std::array<float, 4> GLGizmoRotate::LINE_COLOR = { 0.7f, 0.7f, 1.0f, 0.3f };
std::array<float, 4> GLGizmoRotate::LINE_HIGHLIGHTT_COLOR = { 0.4f, 0.4f, 1.0f, 0.5f };

GLGizmoRotate::GLGizmoRotate(GLCanvas3D& parent, GLGizmoRotate::Axis axis)
    : GLGizmoBase(parent, "", -1)
    , m_axis(axis)
    , m_angle(0.0)
    , m_center(0.0, 0.0, 0.0)
    , m_circle_radius(0.0f)
    , m_grabber_radius(0.0f)
    , m_snap_coarse_in_radius(0.0f)
    , m_snap_coarse_out_radius(0.0f)
    , m_snap_fine_in_radius(0.0f)
    , m_snap_fine_out_short_radius(0.0f)
    , m_snap_fine_out_long_radius(0.0f)
{
}

GLGizmoRotate::GLGizmoRotate(const GLGizmoRotate& other)
    : GLGizmoBase(other.m_parent, other.m_icon_filename, other.m_sprite_id)
    , m_axis(other.m_axis)
    , m_angle(other.m_angle)
    , m_center(other.m_center)
    , m_circle_radius(other.m_circle_radius)
    , m_grabber_radius(other.m_grabber_radius)
    , m_snap_coarse_in_radius(other.m_snap_coarse_in_radius)
    , m_snap_coarse_out_radius(other.m_snap_coarse_out_radius)
    , m_snap_fine_in_radius(other.m_snap_fine_in_radius)
    , m_snap_fine_out_short_radius(other.m_snap_fine_out_short_radius)
    , m_snap_fine_out_long_radius(other.m_snap_fine_out_long_radius)
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
    m_drag_color = { 0.8f, 0.8f, 0.8f, 0.8f };
    m_highlight_color = { 0.8f, 0.8f, 0.8f, 0.8f };
    return true;
}

void GLGizmoRotate::update_positions(const BoundingBoxf3& box)
{
    m_center = box.center();
    /*switch (m_axis)
    {
        case X: { m_center(0) = box.max.x(); break; }
        case Y: { m_center(1) = box.max.y(); break; }
        case Z: { m_center(2) = 0.1f; break; }
    }*/
    //float zoom = (float)wxGetApp().plater()->get_camera().get_zoom();
    //const Transform3d& camera_view_matrix = wxGetApp().plater()->get_camera().get_view_matrix();
    //Geometry::Transformation camera_view_transform(camera_view_matrix);
    //double rotation_z = camera_view_transform.get_rotation(Slic3r::Z);
    //double rotation_x = camera_view_transform.get_rotation(Slic3r::X);
    //double rotation_y = camera_view_transform.get_rotation(Slic3r::Y);
    //Vec3d& dir_up = wxGetApp().plater()->get_camera().get_dir_up();
    //Vec3d& dir_right = wxGetApp().plater()->get_camera().get_dir_right();
    //Vec3d& dir_forward = wxGetApp().plater()->get_camera().get_dir_forward();
    Vec3d view_position = wxGetApp().plater()->get_camera().get_position();
    const Vec3d& view_target = wxGetApp().plater()->get_camera().get_target();
    Vec3d view_direction = (view_position - view_target).normalized();
    double angle_x = ::acos(view_direction.dot(Vec3d::UnitX()));
    if (view_direction.y() < 0)
        angle_x = 2*PI - angle_x;
    double angle_y = ::acos(view_direction.dot(Vec3d::UnitY()));
    double angle_z = ::acos(view_direction.dot(Vec3d::UnitZ()));

    //compute the z-axis firstly
    float rotate_z, size_z;
    //Vec3d direction;
    if (angle_x <= PI/2) {
        rotate_z = PI/2;
        size_z = box.max.y() - m_center(1);
        if (m_axis == X) {
            m_center(0) = box.min.x();
        }
        else if (m_axis == Y) {
            m_center(1) = box.min.y();
        }
    }
    else if (angle_x < PI) {
        rotate_z = PI;
        size_z = m_center(0) - box.min.x();
        if (m_axis == X) {
            m_center(0) = box.max.x();
        }
        else if (m_axis == Y) {
            m_center(1) = box.min.y();
        }
    }
    else if (angle_x < 3*PI/2) {
        rotate_z = 3*PI/2;
        size_z = m_center(1) - box.min.y();
        if (m_axis == X) {
            m_center(0) = box.max.x();
        }
        else if (m_axis == Y) {
            m_center(1) = box.max.y();
        }
    }
    else if (angle_x < 2*PI) {
        rotate_z = 0;
        size_z = box.max.x() - m_center(0);
        if (m_axis == X) {
            m_center(0) = box.min.x();
        }
        else if (m_axis == Y) {
            m_center(1) = box.max.y();
        }
    }

    //compute the normal_up
    Vec3d direction = (view_position - m_center).normalized();
    if (m_axis == X) {
        m_normal_up = (direction.x() >= 0)?true:false;
    }
    else if (m_axis == Y) {
        m_normal_up = (direction.y() >= 0)?true:false;
    }
    else if (m_axis == Z) {
        m_normal_up = (direction.z() >= 0)?true:false;
    }

    float max_z = box.max.z() - box.min.z();
    float max_y = box.max.y() - box.min.y();
    float max_x = box.max.x() - box.min.x();
    float max_width;
    if ((m_axis == X) || (m_axis == Y)) {
        m_grabber_radius = max_z/2 +  GrabberOffset * INV_ZOOM;
        m_rotate_angle = 0.f;
        if (m_axis == X) {
            max_width = (max_y > max_z)?max_y:max_z;
        }
        else {
            max_width = (max_x > max_z)?max_x:max_z;
        }
        m_circle_radius = max_width/2 +  CircleOffset * INV_ZOOM;
    }
    else {
        m_rotate_angle = rotate_z;
        max_width = sqrt(max_x*max_x + max_y*max_y);
        m_circle_radius = max_width/2 + CircleOffset * INV_ZOOM;
        m_grabber_radius = size_z +  GrabberOffset * INV_ZOOM;
        m_center(2) = 0.1f;
    }

    //m_radius = GLGizmoBase::Grabber::FixedRadiusSize;
    m_snap_coarse_in_radius = m_circle_radius*0.6;
    m_snap_coarse_out_radius = m_circle_radius - 2.0f;
    m_snap_fine_in_radius = m_circle_radius;
    m_snap_fine_out_short_radius = m_circle_radius*1.2;
    m_snap_fine_out_long_radius = m_circle_radius*1.4;
}

void GLGizmoRotate::on_start_dragging()
{
    const BoundingBoxf3& box = m_parent.get_selection().get_bounding_box();

#if 0
    m_center = box.center();

#if ENABLE_FIXED_GRABBER
    m_radius = GLGizmoBase::Grabber::FixedRadiusSize;
#else
    m_radius = Offset + box.radius();
#endif
    m_snap_coarse_in_radius = m_radius / 3.0f;
    m_snap_coarse_out_radius = 2.0f * m_snap_coarse_in_radius;
    m_snap_fine_in_radius = m_radius;
    m_snap_fine_out_radius = m_snap_fine_in_radius + m_radius * ScaleLongTooth;
#else
    update_positions(box);
#endif
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

#if 0
    // snap to coarse snap region
    if ((m_snap_coarse_in_radius <= len) && (len <= m_snap_coarse_out_radius))
    {
        double step = 2.0 * (double)PI / (double)SnapRegionsCount;
        theta = step * (double)std::round(theta / step);
    }
    else
    {
        // snap to fine snap region (scale)
        if ((m_snap_fine_in_radius <= len) && (len <= m_snap_fine_out_radius))
        {
            double step = 2.0 * (double)PI / (double)ScaleStepsCount;
            theta = step * (double)std::round(theta / step);
        }
    }
#else
    // snap to coarse snap region
    if (len < m_snap_fine_in_radius)
    {
        double step = 2.0 * (double)PI / (double)SnapRegionsCount;
        if (theta < PI)
            theta = step * (double)std::round(theta / step);
        else {
            theta = step * (double)std::round(theta / step);
            theta -= 2*PI;
        }
        m_fine_tuning = false;
    }
    else
    {
        // snap to fine snap region (scale)
        //double step = 2.0 * (double)PI / (double)ScaleStepsCount;
        //theta = step * (double)std::round(theta / step);
        m_fine_tuning = true;
        if (theta > PI)
            theta -= 2*PI;
    }
    m_mouse_radius = len;
    //BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ <<
    //    boost::format(": mouse_ray a {%1%,%2%,%3%} to b {%4%,%5%,%6%}, mouse_pos {%7%,%8%}, len %9%, theta %10%")
    //    %data.mouse_ray.a.x() %data.mouse_ray.a.y() %data.mouse_ray.a.z()
    //    %data.mouse_ray.b.x() %data.mouse_ray.b.y() %data.mouse_ray.b.z()
    //    %data.mouse_pos.x() %data.mouse_pos.y() %len %theta;
#endif

    if (theta == 2.0 * (double)PI)
        theta = 0.0;

    m_angle = theta;
}

void GLGizmoRotate::on_render()
{
    if (!m_grabbers[0].enabled)
        return;

    const Selection& selection = m_parent.get_selection();
    const BoundingBoxf3& box = selection.get_bounding_box();

    if (m_hover_id != 0 && !m_grabbers[0].dragging) {
#if 0
        m_center = box.center();
#if ENABLE_FIXED_GRABBER
        double grabber_radius = GLGizmoBase::Grabber::FixedRadiusSize * GLGizmoBase::INV_ZOOM;
#else
        m_radius = Offset + box.radius();
#endif
        m_snap_coarse_in_radius = m_radius / 3.0f;
        m_snap_coarse_out_radius = 2.0f * m_snap_coarse_in_radius;
        m_snap_fine_in_radius = m_radius;
        m_snap_fine_out_radius = m_radius * (1.0f + ScaleLongTooth);
#else
        update_positions(box);
#endif
    }

    glsafe(::glEnable(GL_DEPTH_TEST));

    glsafe(::glPushMatrix());
    transform_to_local(selection);

    glsafe(::glLineWidth((m_hover_id != -1) ? 2.0f : 1.5f));
    //glsafe(::glColor4fv((m_hover_id != -1) ? m_drag_color.data() : m_highlight_color.data()));


    //BBS do not render circle
    //render_circle();

    if (m_hover_id != -1) {
        render_scale();
        render_snap_radii();
        //render_reference_radius();
    }

    //glsafe(::glColor4fv(m_highlight_color.data()));

    if (m_hover_id != -1)
        render_angle();

    render_grabber(box);
    //render_grabber_extension(box, false);

    glsafe(::glPopMatrix());
}

void GLGizmoRotate::on_render_for_picking()
{
    const Selection& selection = m_parent.get_selection();
    const BoundingBoxf3 &box   = selection.get_bounding_box();

    glsafe(::glDisable(GL_DEPTH_TEST));

    glsafe(::glPushMatrix());

    transform_to_local(selection);

    // BBS get color for pick only
    //render_grabbers_for_picking(box);
    std::array<float, 4> color;
    for (unsigned int i = 0; i < (unsigned int) m_grabbers.size(); ++i) {
        if (m_grabbers[i].enabled) {
            color = picking_color_component(i);
        }
    }

    glsafe(::glColor4fv(color.data()));
    render_arrow();
    //render arrow
    /*double left_angle = m_angle - (PI * GrabberRange)/180;
    double right_angle = m_angle + (PI * GrabberRange)/180;
    //use rectangle to simulate the arc, so minus 2 for accuracy
    double arrow_in_radius = m_grabber_radius - (ArrowDepth - 2) * GLGizmoBase::INV_ZOOM, arrow_out_radius = m_grabber_radius + (ArrowDepth + 2) * GLGizmoBase::INV_ZOOM;
    Vec3d in_p1, in_p2, out_p1, out_p2;
    in_p1 = {::cos(left_angle) * arrow_in_radius, ::sin(left_angle) * arrow_in_radius, 0.0};
    in_p2 = {::cos(right_angle) * arrow_in_radius, ::sin(right_angle) * arrow_in_radius, 0.0};
    out_p1 = {::cos(left_angle) * arrow_out_radius, ::sin(left_angle) * arrow_out_radius, 0.0};
    out_p2 = {::cos(right_angle) * arrow_out_radius, ::sin(right_angle) * arrow_out_radius, 0.0};
    glsafe(::glColor4fv(color.data() ));
    ::glPolygonMode(GL_FRONT, GL_FILL);
    ::glPolygonMode(GL_BACK, GL_FILL);
    if (!m_normal_up)
        ::glFrontFace(GL_CW);
    ::glBegin(GL_POLYGON);
    ::glVertex3d(out_p1.x(), out_p1.y(), 0.0f);
    ::glVertex3d(out_p2.x(), out_p2.y(), 0.0f);
    ::glVertex3d(in_p2.x(), in_p2.y(), 0.0f);
    ::glVertex3d(in_p1.x(), in_p1.y(), 0.0f);
    glsafe(::glEnd());
    ::glFrontFace(GL_CCW);

    //render_grabber_extension(box, true);*/
    glsafe(::glPopMatrix());
}

//BBS: add input window for move
void GLGizmoRotate3D::on_render_input_window(float x, float y, float bottom_limit)
{
    //if (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptSLA)
    //    return;
    if (m_object_manipulation)
        m_object_manipulation->do_render_input_window(m_imgui, "Rotate", x, y, bottom_limit);
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

void GLGizmoRotate::render_circle() const
{
    /*::glBegin(GL_LINE_LOOP);
    for (unsigned int i = 0; i < ScaleStepsCount; ++i)
    {
        float angle = (float)i * ScaleStepRad;
        float x = ::cos(angle) * m_radius;
        float y = ::sin(angle) * m_radius;
        float z = 0.0f;
        ::glVertex3f((GLfloat)x, (GLfloat)y, (GLfloat)z);
    }
    glsafe(::glEnd());*/
}

void GLGizmoRotate::render_scale() const
{
    //float out_radius_long = m_snap_fine_out_long_radius;
    //float out_radius_short = m_snap_fine_out_short_radius;
    //float out_radius_short = m_radius * (1.0f + 0.5f * ScaleLongTooth);
    glsafe(::glColor4fv(LINE_COLOR.data()));
    ::glBegin(GL_LINES);
    for (unsigned int i = 0; i < ScaleStepsCount; ++i)
    {
        float angle = (float)i * ScaleStepRad;
        float cosa = ::cos(angle);
        float sina = ::sin(angle);
        float in_x = cosa * m_snap_fine_in_radius;
        float in_y = sina * m_snap_fine_in_radius;
        float in_z = 0.0f;
        float out_x = (i % ScaleLongEvery == 0) ? cosa * m_snap_fine_out_long_radius : cosa * m_snap_fine_out_short_radius;
        float out_y = (i % ScaleLongEvery == 0) ? sina * m_snap_fine_out_long_radius : sina * m_snap_fine_out_short_radius;
        float out_z = 0.0f;
        ::glVertex3f((GLfloat)in_x, (GLfloat)in_y, (GLfloat)in_z);
        ::glVertex3f((GLfloat)out_x, (GLfloat)out_y, (GLfloat)out_z);
    }
    glsafe(::glEnd());
}

void GLGizmoRotate::render_snap_radii() const
{
    float step = 2.0f * (float)PI / (float)SnapRegionsCount;

    //float in_radius = m_radius / 3.0f;
    //float out_radius = 2.0f * in_radius;
    float in_radius = m_snap_coarse_in_radius;
    float out_radius = m_snap_coarse_out_radius;

    ::glBegin(GL_LINES);
    for (unsigned int i = 0; i < SnapRegionsCount; ++i)
    {
        float angle = (float)i * step;
        float cosa = ::cos(angle);
        float sina = ::sin(angle);
        float in_x = cosa * in_radius;
        float in_y = sina * in_radius;
        float in_z = 0.0f;
        float out_x = cosa * out_radius;
        float out_y = sina * out_radius;
        float out_z = 0.0f;
        ::glVertex3f((GLfloat)in_x, (GLfloat)in_y, (GLfloat)in_z);
        ::glVertex3f((GLfloat)out_x, (GLfloat)out_y, (GLfloat)out_z);
    }
    glsafe(::glEnd());
}

void GLGizmoRotate::render_reference_radius() const
{
    /*::glBegin(GL_LINES);
    ::glVertex3f(0.0f, 0.0f, 0.0f);
    ::glVertex3f((GLfloat)(m_radius * (1.0f + GrabberOffset)), 0.0f, 0.0f);
    glsafe(::glEnd());*/
}

void GLGizmoRotate::render_angle() const
{
#if 0
    float step_angle = (float)m_angle / AngleResolution;
    float ex_radius = m_radius * (1.0f + GrabberOffset);

    ::glBegin(GL_LINE_STRIP);
    for (unsigned int i = 0; i <= AngleResolution; ++i)
    {
        float angle = (float)i * step_angle;
        float x = ::cos(angle) * ex_radius;
        float y = ::sin(angle) * ex_radius;
        float z = 0.0f;
        ::glVertex3f((GLfloat)x, (GLfloat)y, (GLfloat)z);
    }
    glsafe(::glEnd());
#else
    ExPolygon filled_circle, background_filled_circle_1, background_filled_circle_2;
    float in_radius, out_radius;
    unsigned int resolution;
    if (m_fine_tuning) {
        in_radius = m_snap_fine_in_radius;
        out_radius = m_snap_fine_in_radius * 1.1;
        resolution = ScaleStepsCount;
    }
    else {
        in_radius = m_snap_coarse_out_radius * 0.9;
        out_radius = m_snap_coarse_out_radius;
        resolution = SnapRegionsCount*4;
    }
    double step_angle = ((double)2*PI) / resolution;
    double current_angle = m_angle;
    if (current_angle < 0) {
        step_angle = -step_angle;
    }
    double angle = 0.f;
    float cosa, sina, back_cosa1, back_cosa2, back_sina1, back_sina2;
    unsigned int i;
    Points in_points;
    Points out_points;
    Points background_in_points_1;
    Points background_out_points_1;
    Points background_in_points_2;
    Points background_out_points_2;
    //BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ <<
    //    boost::format(": m_fine_tuning %1%, step_angle %2%, current_angle %3%, m_angle %4%, resolution %5%\n")
    //    %m_fine_tuning %step_angle %current_angle %m_angle %resolution;
    //compute the background points

    bool reach_angle = false, finished = false;
    float in_x, in_y, out_x, out_y;
    for (i = 0; i <= resolution; ++i)
    {
        angle = (double)i * step_angle;
        float back_angle = angle;

        if (!reach_angle) {
            double compare_angle = angle;
            if (!m_fine_tuning) {
                compare_angle = (current_angle >= 0)? compare_angle + 0.02f : compare_angle - 0.02f;
            }
            if ((current_angle >= 0) && (compare_angle >= current_angle)) {
                reach_angle = true;
                if (m_fine_tuning)
                    angle = current_angle;
            }
            else if ((current_angle < 0) && (compare_angle <= current_angle)) {
                reach_angle = true;
                if (m_fine_tuning)
                    angle = current_angle;
            }
        }

        cosa = ::cos(angle);
        sina = ::sin(angle);
        in_x = cosa * in_radius;
        in_y = sina * in_radius;
        out_x = cosa * out_radius;
        out_y = sina * out_radius;
        if ((current_angle >= 0)) {
            if (!finished) {
                //BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ <<
                //    boost::format(": point %1%, x1 %2%, y1 %3%, x2 %4%, y2 %5%\n")
                //    %i %in_x %in_y %out_x %out_y;
                in_points.emplace(in_points.begin(), scale_(in_x), scale_(in_y));
                out_points.emplace_back(scale_(out_x), scale_(out_y));
                if (reach_angle&&m_fine_tuning) {
                    cosa = ::cos(back_angle);
                    sina = ::sin(back_angle);
                    in_x = cosa * in_radius;
                    in_y = sina * in_radius;
                    out_x = cosa * out_radius;
                    out_y = sina * out_radius;
                }
            }
            background_in_points_1.emplace(background_in_points_1.begin(), scale_(in_x), scale_(in_y));
            background_out_points_1.emplace_back(scale_(out_x), scale_(out_y));
            background_in_points_2.emplace_back(scale_(in_x), scale_(-in_y));
            background_out_points_2.emplace(background_out_points_2.begin(), scale_(out_x), scale_(-out_y));
        }
        else {
            if (!finished) {
                //BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ <<
                //    boost::format(": point %1%, x1 %2%, y1 %3%, x2 %4%, y2 %5%\n")
                //    %i %in_x %in_y %out_x %out_y;
                in_points.emplace_back(scale_(in_x), scale_(in_y));
                out_points.emplace(out_points.begin(), scale_(out_x), scale_(out_y));
                if (reach_angle&&m_fine_tuning) {
                    cosa = ::cos(back_angle);
                    sina = ::sin(back_angle);
                    in_x = cosa * in_radius;
                    in_y = sina * in_radius;
                    out_x = cosa * out_radius;
                    out_y = sina * out_radius;
                }
            }
            background_in_points_1.emplace(background_in_points_1.begin(), scale_(in_x), scale_(-in_y));
            background_out_points_1.emplace_back(scale_(out_x), scale_(-out_y));
            background_in_points_2.emplace_back(scale_(in_x), scale_(in_y));
            background_out_points_2.emplace(background_out_points_2.begin(), scale_(out_x), scale_(out_y));
        }
        if (reach_angle)
            finished = true;
    }

    //process the background circle first
    background_filled_circle_1.contour.append(background_out_points_1);
    background_filled_circle_1.contour.append(background_in_points_1);
    background_filled_circle_2.contour.append(background_in_points_2);
    background_filled_circle_2.contour.append(background_out_points_2);
    m_background_circle_buffer_1.set_from_triangles(triangulate_expolygon_2f(background_filled_circle_1, NORMALS_UP), 0.f);
    m_background_circle_buffer_2.set_from_triangles(triangulate_expolygon_2f(background_filled_circle_2, NORMALS_DOWN), 0.f);
    {
        unsigned int triangles_vcount = m_background_circle_buffer_1.get_vertices_count();

        glsafe(::glDepthMask(GL_FALSE));
        glsafe(::glEnable(GL_BLEND));
        glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
        glsafe(::glEnableClientState(GL_VERTEX_ARRAY));
        glsafe(::glColor4fv(GLGizmoRotate::CIRCLE_BACK_COLOR.data()));
        //glsafe(::glNormal3d(0.0f, 0.0f, 1.0f));

        glsafe(::glVertexPointer(3, GL_FLOAT, m_background_circle_buffer_1.get_vertex_data_size(), (GLvoid*)m_background_circle_buffer_1.get_vertices_data()));
        glsafe(::glDrawArrays(GL_TRIANGLES, 0, (GLsizei)triangles_vcount));

        triangles_vcount = m_background_circle_buffer_2.get_vertices_count();
        glsafe(::glVertexPointer(3, GL_FLOAT, m_background_circle_buffer_2.get_vertex_data_size(), (GLvoid*)m_background_circle_buffer_2.get_vertices_data()));
        glsafe(::glDrawArrays(GL_TRIANGLES, 0, (GLsizei)triangles_vcount));
        glsafe(::glDisableClientState(GL_VERTEX_ARRAY));
        glsafe(::glDisable(GL_BLEND));
        glsafe(::glDepthMask(GL_TRUE));

        //shader->stop_using();
    }

    int points_count = in_points.size();
    if (points_count > 1) {
        if (current_angle >= 0) {
            filled_circle.contour.append(out_points);
            filled_circle.contour.append(in_points);
        }
        else {
            filled_circle.contour.append(in_points);
            filled_circle.contour.append(out_points);
        }
        //need to draw the filled circle
        if (!m_filled_circle_buffer.set_from_triangles(triangulate_expolygon_2f(filled_circle, !m_normal_up), 0.f))
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":Unable to create filled circle\n";
        else {
            unsigned int triangles_vcount = m_filled_circle_buffer.get_vertices_count();

            //shader = wxGetApp().get_shader("gouraud_light");
            //if (shader == nullptr)
            //    return;
            //shader->start_using();

            glsafe(::glDepthMask(GL_FALSE));
            glsafe(::glEnable(GL_BLEND));

            //shader->set_uniform("uniform_color", GLGizmoRotate::CIRCLE_FILL_COLOR);
            glsafe(::glColor4fv(GLGizmoRotate::CIRCLE_FILL_COLOR.data()));
            glsafe(::glNormal3d(0.0f, 0.0f, -1.0f));
            glsafe(::glEnableClientState(GL_VERTEX_ARRAY));
            glsafe(::glVertexPointer(3, GL_FLOAT, m_filled_circle_buffer.get_vertex_data_size(), (GLvoid*)m_filled_circle_buffer.get_vertices_data()));
            glsafe(::glDrawArrays(GL_TRIANGLES, 0, (GLsizei)triangles_vcount));
            glsafe(::glDisableClientState(GL_VERTEX_ARRAY));
            glsafe(::glDisable(GL_BLEND));
            glsafe(::glDepthMask(GL_TRUE));

            //shader->stop_using();
        }
    }
    return;
#endif
}

void GLGizmoRotate::render_arrow() const
{
    if (m_arrow_point_1.empty())
        return;

    ::glPolygonMode(GL_FRONT, GL_FILL);
    ::glPolygonMode(GL_BACK, GL_FILL);
    if (!m_normal_up)
        ::glFrontFace(GL_CW);
    //draw the left arrow
    ::glBegin(GL_POLYGON);
    for (auto it = m_arrow_point_1.begin() ; it != m_arrow_point_1.end(); ++it)
    {
        Vec2d& p = *it;
        ::glVertex3d(p.x(), p.y(), 0.0f);
    }
    glsafe(::glEnd());

    //draw the right arrow
    ::glBegin(GL_POLYGON);
    for (auto it = m_arrow_point_2.begin() ; it != m_arrow_point_2.end(); ++it)
    {
        Vec2d& p = *it;
        ::glVertex3d(p.x(), p.y(), 0.0f);
    }
    glsafe(::glEnd());

    //draw the circles
    int size = m_grabber_out_points.size();
    for (unsigned index = 0; index < size - 1; index ++ )
    {
        Vec2d& in_p1 = m_grabber_in_points[index], in_p2 = m_grabber_in_points[index + 1];
        Vec2d& out_p1 = m_grabber_out_points[index], out_p2 = m_grabber_out_points[index + 1];

        ::glBegin(GL_POLYGON);
        ::glVertex3d(out_p1.x(), out_p1.y(), 0.0f);
        ::glVertex3d(out_p2.x(), out_p2.y(), 0.0f);
        ::glVertex3d(in_p2.x(), in_p2.y(), 0.0f);
        ::glVertex3d(in_p1.x(), in_p1.y(), 0.0f);
        glsafe(::glEnd());
    }
    ::glFrontFace(GL_CCW);
}

void GLGizmoRotate::render_grabber(const BoundingBoxf3& box) const
{
    //double grabber_radius = GLGizmoBase::Grabber::FixedRadiusSize * GLGizmoBase::INV_ZOOM;

#if 0
    double grabber_radius = (double)m_radius * (1.0 + (double)GrabberOffset);
    m_grabbers[0].center = Vec3d(::cos(m_angle) * grabber_radius, ::sin(m_angle) * grabber_radius, 0.0);
    m_grabbers[0].angles(2) = m_angle;

    m_grabbers[0].color       = GRABBER_NORMAL_COL;
    m_grabbers[0].hover_color = GRABBER_HOVER_COL;

    glsafe(::glColor4fv((m_hover_id != -1) ? m_drag_color.data() : m_highlight_color.data()));
    ::glBegin(GL_LINES);
    ::glVertex3f(0.0f, 0.0f, 0.0f);
    ::glVertex3dv(m_grabbers[0].center.data());
    glsafe(::glEnd());

    m_grabbers[0].color = m_highlight_color;
    render_grabbers(box);
#else
    //m_grabbers[0].center = Vec3d(::cos(m_angle) * grabber_radius, ::sin(m_angle) * grabber_radius, 0.0);
    //m_grabbers[0].angles(2) = m_angle;

    //glsafe(::glColor4fv((m_hover_id != -1) ? m_drag_color.data() : m_highlight_color.data()));

    if (m_hover_id != -1) {
        float mouse_radius = m_circle_radius;
        if (m_grabbers[0].dragging) {
            mouse_radius = m_mouse_radius;
            if ((mouse_radius < m_snap_coarse_in_radius) && !m_fine_tuning)
                mouse_radius = m_snap_coarse_in_radius;
            else if ((mouse_radius < m_circle_radius) && m_fine_tuning)
                mouse_radius = m_circle_radius;
            else if ((mouse_radius > MaxGrabberRadius) && m_fine_tuning)
                mouse_radius = MaxGrabberRadius;
        }

        glsafe(::glColor4fv(LINE_HIGHLIGHTT_COLOR.data()));
        ::glBegin(GL_LINES);
        ::glVertex3f(0.0f, 0.0f, 0.0f);
        ::glVertex3f(::cos(m_angle) * mouse_radius, ::sin(m_angle) * mouse_radius, 0.0);
        glsafe(::glEnd());
    }

    //when dragging, don't draw the grabber arrows
    if (m_dragging)
        return;

    double arc_range = GrabberRange * GLGizmoBase::INV_ZOOM;
    double angle_range = arc_range/m_grabber_radius;
    double arrow_angle = ArrowRange * GLGizmoBase::INV_ZOOM / m_grabber_radius;

    double new_origin_x, new_origin_y, new_radius, new_angle_range, new_arrow_angle;
    if (angle_range >= ArrowDegree) {
        new_origin_x = 0.f;
        new_origin_y = 0.f;
        new_radius = m_grabber_radius;
        new_angle_range = angle_range;
        new_arrow_angle = arrow_angle;
    }
    else {
        new_radius = m_grabber_radius * angle_range / ArrowDegree;

        double  temp_radius = m_grabber_radius - new_radius;
        new_origin_x = ::cos(m_angle) * temp_radius;
        new_origin_y = ::sin(m_angle) * temp_radius;
        new_angle_range = ArrowDegree;
        new_arrow_angle = arrow_angle * m_grabber_radius/new_radius;
    }
    //render arrow
    double left_angle = m_angle - new_angle_range/2;
    double right_angle = m_angle + new_angle_range/2;
    int steps = 12;
    double step = (right_angle - left_angle)/steps;

    Vec3d in_p, out_p, p1, p2;
    double grabber_in_radius = new_radius - GrabberDepth * GLGizmoBase::INV_ZOOM, grabber_out_radius = new_radius + GrabberDepth * GLGizmoBase::INV_ZOOM;
    double arrow_in_radius = new_radius - ArrowDepth * GLGizmoBase::INV_ZOOM, arrow_out_radius = new_radius + ArrowDepth * GLGizmoBase::INV_ZOOM;

    m_grabber_in_points.clear();
    m_grabber_out_points.clear();
    m_arrow_point_1.clear();
    m_arrow_point_2.clear();
    p1 = {new_origin_x + ::cos(left_angle - new_arrow_angle) * new_radius, new_origin_y + ::sin(left_angle - new_arrow_angle) * new_radius, 0.0};
    m_arrow_point_1.emplace_back(p1.x(), p1.y());
    for (unsigned int index = 0; index <= steps; index ++)
    {
        double cur_angle = left_angle + step * index;

        if (index == 0) {
            in_p = {new_origin_x + ::cos(cur_angle) * grabber_in_radius, new_origin_y + ::sin(cur_angle) * grabber_in_radius, 0.0};
            out_p = {new_origin_x + ::cos(cur_angle) * grabber_out_radius, new_origin_y + ::sin(cur_angle) * grabber_out_radius, 0.0};

            p1 = {new_origin_x + ::cos(cur_angle) * arrow_in_radius, new_origin_y + ::sin(cur_angle) * arrow_in_radius, 0.0};
            p2 = {new_origin_x + ::cos(cur_angle) * arrow_out_radius, new_origin_y + ::sin(cur_angle) * arrow_out_radius, 0.0};

            m_grabber_in_points.emplace_back(in_p.x(), in_p.y());
            m_grabber_out_points.emplace_back(out_p.x(), out_p.y());
            m_arrow_point_1.emplace_back(p2.x(), p2.y());
            m_arrow_point_1.emplace_back(out_p.x(), out_p.y());
            m_arrow_point_1.emplace_back(in_p.x(), in_p.y());
            m_arrow_point_1.emplace_back(p1.x(), p1.y());
        }
        else if (index == steps) {
            in_p = {new_origin_x + ::cos(cur_angle) * grabber_in_radius, new_origin_y + ::sin(cur_angle) * grabber_in_radius, 0.0};
            out_p = {new_origin_x + ::cos(cur_angle) * grabber_out_radius, new_origin_y + ::sin(cur_angle) * grabber_out_radius, 0.0};

            p1 = {new_origin_x + ::cos(cur_angle) * arrow_in_radius, new_origin_y + ::sin(cur_angle) * arrow_in_radius, 0.0};
            p2 = {new_origin_x + ::cos(cur_angle) * arrow_out_radius, new_origin_y + ::sin(cur_angle) * arrow_out_radius, 0.0};

            m_grabber_in_points.emplace_back(in_p.x(), in_p.y());
            m_grabber_out_points.emplace_back(out_p.x(), out_p.y());
            m_arrow_point_2.emplace_back(p1.x(), p1.y());
            m_arrow_point_2.emplace_back(in_p.x(), in_p.y());
            m_arrow_point_2.emplace_back(out_p.x(), out_p.y());
            m_arrow_point_2.emplace_back(p2.x(), p2.y());
        }
        else if ((index >= 1) && (index <= (steps - 1))){
            in_p = {new_origin_x + ::cos(cur_angle) * grabber_in_radius, new_origin_y + ::sin(cur_angle) * grabber_in_radius, 0.0};
            out_p = {new_origin_x + ::cos(cur_angle) * grabber_out_radius, new_origin_y + ::sin(cur_angle) * grabber_out_radius, 0.0};
            m_grabber_in_points.emplace_back(in_p.x(), in_p.y());
            m_grabber_out_points.emplace_back(out_p.x(), out_p.y());
        }
    }
    p1 = {new_origin_x + ::cos(right_angle + new_arrow_angle) * new_radius, new_origin_y + ::sin(right_angle + new_arrow_angle) * new_radius, 0.0};
    m_arrow_point_2.emplace_back(p1.x(), p1.y());

    if (m_hover_id != -1)
        glsafe(::glColor4fv(AXES_HOVER_COLOR[m_axis].data()));
    else
        glsafe(::glColor4fv(AXES_COLOR[m_axis].data()));

    render_arrow();
#endif
}

/*void GLGizmoRotate::calc_left_arrow_points(const Vec3d& line_p1, const Vec3d& line_p2, Vec3d &arrow_p1, Vec3d &arrow_p2) const
{
    Vec3d direction = (line_p2 - line_p1).normalized();
    double angle_x = ::acos(direction.dot(Vec3d::UnitX()));
    if (direction.y() < 0)
        angle_x = 2*PI - angle_x;

    double angle_1 = angle_x + ArrowDegree * PI/180, angle_2 = angle_x - ArrowDegree * PI/180;
    double delta_x1, delta_y1, delta_x2, delta_y2;
    delta_x1 = ArrowLen * cos(angle_1);
    delta_y1 = ArrowLen * sin(angle_1);
    delta_x2 = ArrowLen * cos(angle_2);
    delta_y2 = ArrowLen * sin(angle_2);
    arrow_p1 = { line_p1.x() + delta_x1, line_p1.y() + delta_y1, 0.f };
    arrow_p2 = { line_p1.x() + delta_x2, line_p1.y() + delta_y2, 0.f };
}*/

/*void GLGizmoRotate::calc_right_arrow_points(const Vec3d& line_p1, const Vec3d& line_p2, Vec3d &arrow_p1, Vec3d &arrow_p2) const
{
    Vec3d direction = (line_p2 - line_p1).normalized();
    double angle_x = ::acos(direction.dot(Vec3d::UnitX()));
    if (direction.y() < 0)
        angle_x = 2*PI - angle_x;

    double angle_1 = angle_x + PI/6, angle_2 = angle_x - PI/6;
    double delta_x1, delta_y1, delta_x2, delta_y2;
    delta_x1 = ArrowLen * cos(angle_1);
    delta_y1 = ArrowLen * sin(angle_1);
    delta_x2 = ArrowLen * cos(angle_2);
    delta_y2 = ArrowLen * sin(angle_2);
    arrow_p1 = { line_p1.x() - delta_x1, line_p1.y() - delta_y1, 0.f };
    arrow_p2 = { line_p1.x() - delta_x2, line_p1.y() - delta_y2, 0.f };
}*/


void GLGizmoRotate::render_grabber_extension(const BoundingBoxf3& box, bool picking) const
{
    double size = 0.75 * GLGizmoBase::Grabber::FixedGrabberSize * GLGizmoBase::INV_ZOOM;

    std::array<float, 4> color = m_grabbers[0].color;
    if (!picking && m_hover_id != -1) {
        color = m_grabbers[0].hover_color;
    }

    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
    if (shader == nullptr)
        return;

    const_cast<GLModel*>(&m_cone)->set_color(-1, color);
    if (!picking) {
        shader->start_using();
        shader->set_uniform("emission_factor", 0.1f);
    }

    glsafe(::glPushMatrix());
    glsafe(::glTranslated(m_grabbers[0].center.x(), m_grabbers[0].center.y(), m_grabbers[0].center.z()));
    glsafe(::glRotated(Geometry::rad2deg(m_angle), 0.0, 0.0, 1.0));
    glsafe(::glRotated(90.0, 1.0, 0.0, 0.0));
    glsafe(::glTranslated(0.0, 0.0, 0.0));
    glsafe(::glScaled(0.75 * size, 0.75 * size, 2.0 * size));
    m_cone.render();
    glsafe(::glPopMatrix());
    glsafe(::glPushMatrix());
    glsafe(::glTranslated(m_grabbers[0].center.x(), m_grabbers[0].center.y(), m_grabbers[0].center.z()));
    glsafe(::glRotated(Geometry::rad2deg(m_angle), 0.0, 0.0, 1.0));
    glsafe(::glRotated(-90.0, 1.0, 0.0, 0.0));
    glsafe(::glTranslated(0.0, 0.0, 0.0));
    glsafe(::glScaled(0.75 * size, 0.75 * size, 2.0 * size));
    m_cone.render();
    glsafe(::glPopMatrix());

    if (! picking)
        shader->stop_using();
}

void GLGizmoRotate::transform_to_local(const Selection& selection) const
{
    glsafe(::glTranslated(m_center(0), m_center(1), m_center(2)));

    if (selection.is_single_volume() || selection.is_single_modifier() || selection.requires_local_axes()) {
        Transform3d orient_matrix = selection.get_volume(*selection.get_volume_idxs().begin())->get_instance_transformation().get_matrix(true, false, true, true);
        glsafe(::glMultMatrixd(orient_matrix.data()));
    }

    //BBS: this logic should be the the same with GLGizmoRotate::mouse_position_in_local_plane, in reversed order
    switch (m_axis)
    {
    case X:
    {
        glsafe(::glRotatef(90.0f, 0.0f, 1.0f, 0.0f));
        glsafe(::glRotatef(180.0f, 0.0f, 0.0f, 1.0f));
        break;
    }
    case Y:
    {
        glsafe(::glRotatef(-90.0f, 0.0f, 0.0f, 1.0f));
        glsafe(::glRotatef(-90.0f, 0.0f, 1.0f, 0.0f));
        break;
    }
    default:
    case Z:
    {
        // no rotation
        if (m_rotate_angle > 0)
            glsafe(::glRotatef(Geometry::rad2deg(m_rotate_angle), 0.0f, 0.0f, 1.0f));
        break;
    }
    }
}

Vec3d GLGizmoRotate::mouse_position_in_local_plane(const Linef3& mouse_ray, const Selection& selection) const
{
    double half_pi = 0.5 * (double)PI;

    Transform3d m = Transform3d::Identity();

    switch (m_axis)
    {
    case X:
    {
        m.rotate(Eigen::AngleAxisd(-PI, Vec3d::UnitZ()));
        m.rotate(Eigen::AngleAxisd(-half_pi, Vec3d::UnitY()));
        break;
    }
    case Y:
    {
        m.rotate(Eigen::AngleAxisd(half_pi, Vec3d::UnitY()));
        m.rotate(Eigen::AngleAxisd(half_pi, Vec3d::UnitZ()));
        break;
    }
    default:
    case Z:
    {
        // no rotation applied
        if (m_rotate_angle > 0)
            m.rotate(Eigen::AngleAxisd(-m_rotate_angle, Vec3d::UnitZ()));
        break;
    }
    }

    if (selection.is_single_volume() || selection.is_single_modifier() || selection.requires_local_axes())
        m = m * selection.get_volume(*selection.get_volume_idxs().begin())->get_instance_transformation().get_matrix(true, false, true, true).inverse();

    m.translate(-m_center);

    return transform(mouse_ray, m).intersect_plane(0.0);
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

bool GLGizmoRotate3D::on_init()
{
    for (GLGizmoRotate& g : m_gizmos) {
        if (!g.init())
            return false;
    }

    for (unsigned int i = 0; i < 3; ++i) {
        m_gizmos[i].set_highlight_color(AXES_COLOR[i]);
    }

    m_shortcut_key = WXK_NONE;

    return true;
}

std::string GLGizmoRotate3D::on_get_name() const
{
    return _u8L("Rotate");
}

bool GLGizmoRotate3D::on_is_activable() const
{
    // BBS: don't support rotate wipe tower
    const Selection& selection = m_parent.get_selection();
    return !m_parent.get_selection().is_empty() && !selection.is_wipe_tower();
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
