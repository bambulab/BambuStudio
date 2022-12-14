// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoPainterBase.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"

#include <GL/glew.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/Utils/UndoRedo.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <memory>
#include <optional>

namespace Slic3r::GUI {


GLGizmoPainterBase::GLGizmoPainterBase(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
{
    // Make sphere and save it into a vertex buffer.
    m_vbo_sphere.load_its_flat_shading(its_make_sphere(1., (2*M_PI)/24.));
    m_vbo_sphere.finalize_geometry(true);
}

void GLGizmoPainterBase::set_painter_gizmo_data(const Selection& selection)
{
    if (m_state != On)
        return;

    const ModelObject* mo = m_c->selection_info() ? m_c->selection_info()->model_object() : nullptr;

    if (mo && selection.is_from_single_instance()
     && (m_schedule_update || mo->id() != m_old_mo_id || mo->volumes.size() != m_old_volumes_size))
    {
        //BBS: add logic to distinguish the first_time_update and later_update
        update_from_model_object(!m_schedule_update);

        m_old_mo_id = mo->id();
        m_old_volumes_size = mo->volumes.size();
        m_schedule_update = false;
    }
}

GLGizmoPainterBase::ClippingPlaneDataWrapper GLGizmoPainterBase::get_clipping_plane_data() const
{
    ClippingPlaneDataWrapper clp_data_out{{0.f, 0.f, 1.f, FLT_MAX}, {-FLT_MAX, FLT_MAX}};
    // Take care of the clipping plane. The normal of the clipping plane is
    // saved with opposite sign than we need to pass to OpenGL (FIXME)
    if (bool clipping_plane_active = m_c->object_clipper()->get_position() != 0.; clipping_plane_active) {
        const ClippingPlane *clp = m_c->object_clipper()->get_clipping_plane();
        for (size_t i = 0; i < 3; ++i)
            clp_data_out.clp_dataf[i] = -1.f * float(clp->get_data()[i]);
        clp_data_out.clp_dataf[3] = float(clp->get_data()[3]);
    }

    // z_range is calculated in the same way as in GLCanvas3D::_render_objects(GLVolumeCollection::ERenderType type)
    if (m_c->get_canvas()->get_use_clipping_planes()) {
        const std::array<ClippingPlane, 2> &clps = m_c->get_canvas()->get_clipping_planes();
        clp_data_out.z_range                     = {float(-clps[0].get_data()[3]), float(clps[1].get_data()[3])};
    }

    return clp_data_out;
}

void GLGizmoPainterBase::render_triangles(const Selection& selection) const
{
    auto* shader = wxGetApp().get_shader("gouraud");
    if (! shader)
        return;
    shader->start_using();
    shader->set_uniform("slope.actived", false);
    shader->set_uniform("print_volume.type", 0);
    shader->set_uniform("clipping_plane", this->get_clipping_plane_data().clp_dataf);
    ScopeGuard guard([shader]() { if (shader) shader->stop_using(); });

    const ModelObject *mo      = m_c->selection_info()->model_object();
    int                mesh_id = -1;
    for (const ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;

        ++mesh_id;

        const Transform3d trafo_matrix =
            mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() *
            mv->get_matrix();

        bool is_left_handed = trafo_matrix.matrix().determinant() < 0.;
        if (is_left_handed)
            glsafe(::glFrontFace(GL_CW));

        glsafe(::glPushMatrix());
        glsafe(::glMultMatrixd(trafo_matrix.data()));

        // For printers with multiple extruders, it is necessary to pass trafo_matrix
        // to the shader input variable print_box.volume_world_matrix before
        // rendering the painted triangles. When this matrix is not set, the
        // wrong transformation matrix is used for "Clipping of view".
        shader->set_uniform("volume_world_matrix", trafo_matrix);

        m_triangle_selectors[mesh_id]->render(m_imgui);

        glsafe(::glPopMatrix());
        if (is_left_handed)
            glsafe(::glFrontFace(GL_CCW));
    }
}


void GLGizmoPainterBase::render_cursor() const
{
    // First check that the mouse pointer is on an object.
    const ModelObject* mo = m_c->selection_info()->model_object();
    const Selection& selection = m_parent.get_selection();
    const ModelInstance* mi = mo->instances[selection.get_instance_idx()];
    const Camera& camera = wxGetApp().plater()->get_camera();

    // Precalculate transformations of individual meshes.
    std::vector<Transform3d> trafo_matrices;
    for (const ModelVolume* mv : mo->volumes) {
        if (mv->is_model_part())
            trafo_matrices.emplace_back(mi->get_transformation().get_matrix() * mv->get_matrix());
    }
    // Raycast and return if there's no hit.
    update_raycast_cache(m_parent.get_local_mouse_position(), camera, trafo_matrices);
    if (m_rr.mesh_id == -1)
        return;

    if (m_tool_type == ToolType::BRUSH) {
        if (m_cursor_type == TriangleSelector::SPHERE)
            render_cursor_sphere(trafo_matrices[m_rr.mesh_id]);
        else if (m_cursor_type == TriangleSelector::CIRCLE)
            render_cursor_circle();
        else if (m_cursor_type == TriangleSelector::HEIGHT_RANGE)
            render_cursor_height_range(trafo_matrices[m_rr.mesh_id]);
    }
}



void GLGizmoPainterBase::render_cursor_circle() const
{
    const Camera &camera   = wxGetApp().plater()->get_camera();
    auto          zoom     = (float) camera.get_zoom();
    float         inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;

    Size  cnv_size        = m_parent.get_canvas_size();
    float cnv_half_width  = 0.5f * (float) cnv_size.get_width();
    float cnv_half_height = 0.5f * (float) cnv_size.get_height();
    if ((cnv_half_width == 0.0f) || (cnv_half_height == 0.0f))
        return;
    Vec2d mouse_pos(m_parent.get_local_mouse_position()(0), m_parent.get_local_mouse_position()(1));
    Vec2d center(mouse_pos(0) - cnv_half_width, cnv_half_height - mouse_pos(1));
    center = center * inv_zoom;

    glsafe(::glLineWidth(1.5f));

    // BBS
    std::array<float, 4> render_color = this->get_cursor_hover_color();
    if (m_button_down == Button::Left)
        render_color = this->get_cursor_sphere_left_button_color();
    else if (m_button_down == Button::Right)
        render_color = this->get_cursor_sphere_right_button_color();
    glsafe(::glColor4fv(render_color.data()));

    glsafe(::glDisable(GL_DEPTH_TEST));

    glsafe(::glPushMatrix());
    glsafe(::glLoadIdentity());
    // ensure that the circle is renderered inside the frustrum
    glsafe(::glTranslated(0.0, 0.0, -(camera.get_near_z() + 0.5)));
    // ensure that the overlay fits the frustrum near z plane
    double gui_scale = camera.get_gui_scale();
    glsafe(::glScaled(gui_scale, gui_scale, 1.0));

    glsafe(::glPushAttrib(GL_ENABLE_BIT));
    glsafe(::glLineStipple(4, 0xAAAA));
    glsafe(::glEnable(GL_LINE_STIPPLE));

    ::glBegin(GL_LINE_LOOP);
    for (double angle=0; angle<2*M_PI; angle+=M_PI/20.)
        ::glVertex2f(GLfloat(center.x()+m_cursor_radius*cos(angle)), GLfloat(center.y()+m_cursor_radius*sin(angle)));
    glsafe(::glEnd());

    glsafe(::glPopAttrib());
    glsafe(::glPopMatrix());
    glsafe(::glEnable(GL_DEPTH_TEST));
}


void GLGizmoPainterBase::render_cursor_sphere(const Transform3d& trafo) const
{
    const Transform3d complete_scaling_matrix_inverse = Geometry::Transformation(trafo).get_matrix(true, true, false, true).inverse();
    const bool is_left_handed = Geometry::Transformation(trafo).is_left_handed();

    glsafe(::glPushMatrix());
    glsafe(::glMultMatrixd(trafo.data()));
    // Inverse matrix of the instance scaling is applied so that the mark does not scale with the object.
    glsafe(::glTranslatef(m_rr.hit(0), m_rr.hit(1), m_rr.hit(2)));
    glsafe(::glMultMatrixd(complete_scaling_matrix_inverse.data()));
    glsafe(::glScaled(m_cursor_radius, m_cursor_radius, m_cursor_radius));

    if (is_left_handed)
        glFrontFace(GL_CW);

    // BBS
    std::array<float, 4> render_color = this->get_cursor_hover_color();
    if (m_button_down == Button::Left)
        render_color = this->get_cursor_sphere_left_button_color();
    else if (m_button_down == Button::Right)
        render_color = this->get_cursor_sphere_right_button_color();
    glsafe(::glColor4fv(render_color.data()));

    m_vbo_sphere.render();

    if (is_left_handed)
        glFrontFace(GL_CCW);

    glsafe(::glPopMatrix());
}

// BBS
void GLGizmoPainterBase::render_cursor_height_range(const Transform3d& trafo) const
{
    const BoundingBoxf3 box = bounding_box();
    Vec3d hit_world = trafo * Vec3d(m_rr.hit(0), m_rr.hit(1), m_rr.hit(2));
    float max_z = (float)box.max.z();

    float cursor_z = std::clamp((float)hit_world.z(), 0.f, max_z);
    std::array<float, 2> zs = { cursor_z, std::clamp(cursor_z + m_cursor_height, 0.f, max_z) };
    for (int i = 0; i < zs.size(); i++) {
        update_contours(zs[i], max_z);

        glsafe(::glPushMatrix());
        glsafe(::glTranslated(m_cut_contours.shift.x(), m_cut_contours.shift.y(), m_cut_contours.shift.z()));
        glsafe(::glLineWidth(2.0f));
        m_cut_contours.contours.render();
        glsafe(::glPopMatrix());
    }
}

BoundingBoxf3 GLGizmoPainterBase::bounding_box() const
{
    BoundingBoxf3 ret;
    const Selection& selection = m_parent.get_selection();
    const Selection::IndicesList& idxs = selection.get_volume_idxs();
    for (unsigned int i : idxs) {
        const GLVolume* volume = selection.get_volume(i);
        if (!volume->is_modifier)
            ret.merge(volume->transformed_convex_hull_bounding_box());
    }
    return ret;
}

void GLGizmoPainterBase::update_contours(float cursor_z, float max_z) const
{
    const Selection& selection = m_parent.get_selection();
    const GLVolume* first_glvolume = selection.get_volume(*selection.get_volume_idxs().begin());
    const BoundingBoxf3& box = first_glvolume->transformed_convex_hull_bounding_box();

    const ModelObject* model_object = wxGetApp().model().objects[selection.get_object_idx()];
    const int instance_idx = selection.get_instance_idx();

    if (0.0 < cursor_z && cursor_z < max_z) {
        if (m_cut_contours.cut_z != cursor_z || m_cut_contours.object_id != model_object->id() || m_cut_contours.instance_idx != instance_idx) {
            m_cut_contours.cut_z = cursor_z;

            if (m_cut_contours.object_id != model_object->id())
                m_cut_contours.mesh = model_object->raw_mesh();

            m_cut_contours.position = box.center();
            m_cut_contours.shift = Vec3d::Zero();
            m_cut_contours.object_id = model_object->id();
            m_cut_contours.instance_idx = instance_idx;
            m_cut_contours.contours.reset();

            MeshSlicingParams slicing_params;
            slicing_params.trafo = first_glvolume->get_instance_transformation().get_matrix();
            const Polygons polys = slice_mesh(m_cut_contours.mesh.its, cursor_z, slicing_params);
            if (!polys.empty()) {
                m_cut_contours.contours.init_from(polys, static_cast<float>(cursor_z));
                m_cut_contours.contours.set_color(-1, { 1.0f, 1.0f, 1.0f, 1.0f });
            }
        }
        else if (box.center() != m_cut_contours.position) {
            m_cut_contours.shift = box.center() - m_cut_contours.position;
        }
    }
    else
        m_cut_contours.contours.reset();
}

bool GLGizmoPainterBase::is_mesh_point_clipped(const Vec3d& point, const Transform3d& trafo) const
{
    if (m_c->object_clipper()->get_position() == 0.)
        return false;

    auto sel_info = m_c->selection_info();
    Vec3d transformed_point =  trafo * point;
    transformed_point(2) += sel_info->get_sla_shift();
    return m_c->object_clipper()->get_clipping_plane()->is_point_clipped(transformed_point);
}

// Interpolate points between the previous and current mouse positions, which are then projected onto the object.
// Returned projected mouse positions are grouped by mesh_idx. It may contain multiple std::vector<GLGizmoPainterBase::ProjectedMousePosition>
// with the same mesh_idx, but all items in std::vector<GLGizmoPainterBase::ProjectedMousePosition> always have the same mesh_idx.
std::vector<std::vector<GLGizmoPainterBase::ProjectedMousePosition>> GLGizmoPainterBase::get_projected_mouse_positions(const Vec2d &mouse_position, const double resolution, const std::vector<Transform3d> &trafo_matrices) const
{
    // List of mouse positions that will be used as seeds for painting.
    std::vector<Vec2d> mouse_positions{mouse_position};
    if (m_last_mouse_click != Vec2d::Zero()) {
        // In case current mouse position is far from the last one,
        // add several positions from between into the list, so there
        // are no gaps in the painted region.
        if (size_t patches_in_between = size_t((mouse_position - m_last_mouse_click).norm() / resolution); patches_in_between > 0) {
            const Vec2d diff = (m_last_mouse_click - mouse_position) / (patches_in_between + 1);
            for (size_t patch_idx = 1; patch_idx <= patches_in_between; ++patch_idx)
                mouse_positions.emplace_back(mouse_position + patch_idx * diff);
            mouse_positions.emplace_back(m_last_mouse_click);
        }
    }

    const Camera                       &camera = wxGetApp().plater()->get_camera();
    std::vector<ProjectedMousePosition> mesh_hit_points;
    mesh_hit_points.reserve(mouse_positions.size());

    // In mesh_hit_points only the last item could have mesh_id == -1, any other items mustn't.
    for (const Vec2d &mp : mouse_positions) {
        update_raycast_cache(mp, camera, trafo_matrices);
        mesh_hit_points.push_back({m_rr.hit, m_rr.mesh_id, m_rr.facet});
        if (m_rr.mesh_id == -1)
            break;
    }

    // Divide mesh_hit_points into groups with the same mesh_idx. It may contain multiple groups with the same mesh_idx.
    std::vector<std::vector<ProjectedMousePosition>> mesh_hit_points_by_mesh;
    for (size_t prev_mesh_hit_point = 0, curr_mesh_hit_point = 0; curr_mesh_hit_point < mesh_hit_points.size(); ++curr_mesh_hit_point) {
        size_t next_mesh_hit_point = curr_mesh_hit_point + 1;
        if (next_mesh_hit_point >= mesh_hit_points.size() || mesh_hit_points[curr_mesh_hit_point].mesh_idx != mesh_hit_points[next_mesh_hit_point].mesh_idx) {
            mesh_hit_points_by_mesh.emplace_back();
            mesh_hit_points_by_mesh.back().insert(mesh_hit_points_by_mesh.back().end(), mesh_hit_points.begin() + int(prev_mesh_hit_point), mesh_hit_points.begin() + int(next_mesh_hit_point));
            prev_mesh_hit_point = next_mesh_hit_point;
        }
    }

    auto on_same_facet = [](std::vector<ProjectedMousePosition> &hit_points) -> bool {
        for (const ProjectedMousePosition &mesh_hit_point : hit_points)
            if (mesh_hit_point.facet_idx != hit_points.front().facet_idx)
                return false;
        return true;
    };

    struct Plane
    {
        Vec3d origin;
        Vec3d first_axis;
        Vec3d second_axis;
    };
    auto find_plane = [](std::vector<ProjectedMousePosition> &hit_points) -> std::optional<Plane> {
        assert(hit_points.size() >= 3);
        for (size_t third_idx = 2; third_idx < hit_points.size(); ++third_idx) {
            const Vec3d &first_point  = hit_points[third_idx - 2].mesh_hit.cast<double>();
            const Vec3d &second_point = hit_points[third_idx - 1].mesh_hit.cast<double>();
            const Vec3d &third_point  = hit_points[third_idx].mesh_hit.cast<double>();

            const Vec3d  first_vec    = first_point - second_point;
            const Vec3d  second_vec   = third_point - second_point;

            // If three points aren't collinear, then there exists only one plane going through all points.
            if (first_vec.cross(second_vec).squaredNorm() > sqr(EPSILON)) {
                const Vec3d first_axis_vec_n = first_vec.normalized();
                // Make second_vec perpendicular to first_axis_vec_n using Gram–Schmidt orthogonalization process
                const Vec3d second_axis_vec_n = (second_vec - (first_vec.dot(second_vec) / first_vec.dot(first_vec)) * first_vec).normalized();
                return Plane{second_point, first_axis_vec_n, second_axis_vec_n};
            }
        }

        return std::nullopt;
    };

    for(std::vector<ProjectedMousePosition> &hit_points : mesh_hit_points_by_mesh) {
        assert(!hit_points.empty());
        if (hit_points.back().mesh_idx == -1)
            break;

        if (hit_points.size() <= 2)
            continue;

        if (on_same_facet(hit_points)) {
            hit_points = {hit_points.front(), hit_points.back()};
        } else if (std::optional<Plane> plane = find_plane(hit_points); plane) {
            Polyline polyline;
            polyline.points.reserve(hit_points.size());
            // Project hit_points into its plane to simplified them in the next step.
            for (auto &hit_point : hit_points) {
                const Vec3d &point  = hit_point.mesh_hit.cast<double>();
                const double x_cord = plane->first_axis.dot(point - plane->origin);
                const double y_cord = plane->second_axis.dot(point - plane->origin);
                polyline.points.emplace_back(scale_(x_cord), scale_(y_cord));
            }

            polyline.simplify(scale_(m_cursor_radius) / 10.);

            const int                           mesh_idx = hit_points.front().mesh_idx;
            std::vector<ProjectedMousePosition> new_hit_points;
            new_hit_points.reserve(polyline.points.size());
            // Project 2D simplified hit_points beck to 3D.
            for (const Point &point : polyline.points) {
                const double x_cord        = unscale<double>(point.x());
                const double y_cord        = unscale<double>(point.y());
                const Vec3d  new_hit_point = plane->origin + x_cord * plane->first_axis + y_cord * plane->second_axis;
                const int    facet_idx     = m_c->raycaster()->raycasters()[mesh_idx]->get_closest_facet(new_hit_point.cast<float>());
                new_hit_points.push_back({new_hit_point.cast<float>(), mesh_idx, size_t(facet_idx)});
            }

            hit_points = new_hit_points;
        } else {
            hit_points = {hit_points.front(), hit_points.back()};
        }
    }

    return mesh_hit_points_by_mesh;
}

// BBS
std::vector<GLGizmoPainterBase::ProjectedHeightRange> GLGizmoPainterBase::get_projected_height_range(
    const Vec2d& mouse_position,
    double resolution,
    const std::vector<const ModelVolume*>& part_volumes,
    const std::vector<Transform3d>& trafo_matrices) const
{
    std::vector<GLGizmoPainterBase::ProjectedHeightRange> hit_triangles_by_mesh;

    const Camera& camera = wxGetApp().plater()->get_camera();

    // In mesh_hit_points only the last item could have mesh_id == -1, any other items mustn't.
    update_raycast_cache(mouse_position, camera, trafo_matrices);
    if (m_rr.mesh_id == -1)
        return hit_triangles_by_mesh;

    ProjectedMousePosition mesh_hit_point = { m_rr.hit, m_rr.mesh_id, m_rr.facet };
    float z_bot_world= (trafo_matrices[m_rr.mesh_id] * Vec3d(m_rr.hit(0), m_rr.hit(1), m_rr.hit(2))).z();
    float z_top_world = z_bot_world+ m_cursor_height;
    hit_triangles_by_mesh.push_back({ z_bot_world, m_rr.mesh_id, size_t(m_rr.facet) });

    const Selection& selection = m_parent.get_selection();
    const ModelObject* mo = m_c->selection_info()->model_object();
    const ModelInstance* mi = mo->instances[selection.get_instance_idx()];
    const Transform3d   instance_trafo = mi->get_transformation().get_matrix();
    const Transform3d   instance_trafo_not_translate = mi->get_transformation().get_matrix(true);

    for (int mesh_idx = 0; mesh_idx < part_volumes.size(); mesh_idx++) {
        if (mesh_idx == m_rr.mesh_id)
            continue;

        const Transform3d& trafo = trafo_matrices[mesh_idx];
        const indexed_triangle_set& its = part_volumes[mesh_idx]->mesh().its;

        int first_hit_facet_idx = -1;
        for (int facet_idx = 0; facet_idx < its.indices.size(); facet_idx++) {
            stl_vertex v0 = its.vertices[its.indices[facet_idx].x()];
            stl_vertex v1 = its.vertices[its.indices[facet_idx].y()];
            stl_vertex v2 = its.vertices[its.indices[facet_idx].z()];

            float v0_z = (trafo * Vec3d(v0(0), v0(1), v0(2))).z();
            float v1_z = (trafo * Vec3d(v1(0), v1(1), v1(2))).z();
            float v2_z = (trafo * Vec3d(v2(0), v2(1), v2(2))).z();
            bool outside_range = (v0_z < z_bot_world&& v1_z < z_bot_world&& v2_z < z_bot_world) ||
                                 (v0_z > z_top_world && v1_z > z_top_world && v2_z > z_top_world);
            if (!outside_range) {
                first_hit_facet_idx = facet_idx;
                break;
            }
        }

        if (first_hit_facet_idx != -1) {
            hit_triangles_by_mesh.push_back({ z_bot_world, mesh_idx, (size_t)first_hit_facet_idx });
        }
    }

    return hit_triangles_by_mesh;
}

// Following function is called from GLCanvas3D to inform the gizmo about a mouse/keyboard event.
// The gizmo has an opportunity to react - if it does, it should return true so that the Canvas3D is
// aware that the event was reacted to and stops trying to make different sense of it. If the gizmo
// concludes that the event was not intended for it, it should return false.
bool GLGizmoPainterBase::gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down)
{
    if (action == SLAGizmoEventType::MouseWheelUp
     || action == SLAGizmoEventType::MouseWheelDown) {
        if (control_down) {
            //BBS
            if (m_tool_type == ToolType::BRUSH && m_cursor_type == TriangleSelector::CursorType::HEIGHT_RANGE) {
                m_cursor_height = action == SLAGizmoEventType::MouseWheelDown ? std::max(m_cursor_height - this->get_cursor_height_step(), this->get_cursor_height_min()) :
                                                                                std::min(m_cursor_height + this->get_cursor_height_step(), this->get_cursor_height_max());
                m_parent.set_as_dirty();
                return true;
            }

            if (m_tool_type == ToolType::BRUSH && (m_cursor_type == TriangleSelector::CursorType::SPHERE || m_cursor_type == TriangleSelector::CursorType::CIRCLE)) {
                m_cursor_radius = action == SLAGizmoEventType::MouseWheelDown ? std::max(m_cursor_radius - this->get_cursor_radius_step(), this->get_cursor_radius_min()) :
                                                                                std::min(m_cursor_radius + this->get_cursor_radius_step(), this->get_cursor_radius_max());
                m_parent.set_as_dirty();
                return true;
            }

            if (m_tool_type == ToolType::BUCKET_FILL || m_tool_type == ToolType::SMART_FILL) {
                m_smart_fill_angle = action == SLAGizmoEventType::MouseWheelDown ? std::max(m_smart_fill_angle - SmartFillAngleStep, SmartFillAngleMin)
                                                                                : std::min(m_smart_fill_angle + SmartFillAngleStep, SmartFillAngleMax);
                m_parent.set_as_dirty();
                if (m_rr.mesh_id != -1) {
                    const Selection     &selection                 = m_parent.get_selection();
                    const ModelObject   *mo                        = m_c->selection_info()->model_object();
                    const ModelInstance *mi                        = mo->instances[selection.get_instance_idx()];
                    const Transform3d   trafo_matrix_not_translate = mi->get_transformation().get_matrix(true) * mo->volumes[m_rr.mesh_id]->get_matrix(true);
                    const Transform3d   trafo_matrix = mi->get_transformation().get_matrix() * mo->volumes[m_rr.mesh_id]->get_matrix();
                    m_triangle_selectors[m_rr.mesh_id]->seed_fill_select_triangles(m_rr.hit, int(m_rr.facet), trafo_matrix_not_translate, this->get_clipping_plane_in_volume_coordinates(trafo_matrix), m_smart_fill_angle,
                                                                                   m_paint_on_overhangs_only ? m_highlight_by_angle_threshold_deg : 0.f, true);
                    m_triangle_selectors[m_rr.mesh_id]->request_update_render_data();
                    m_seed_fill_last_mesh_id = m_rr.mesh_id;
                }
                return true;
            }

            if (m_tool_type == ToolType::GAP_FILL) {
                TriangleSelectorPatch::gap_area = action == SLAGizmoEventType::MouseWheelDown ?
                    std::max(TriangleSelectorPatch::gap_area - TriangleSelectorPatch::GapAreaStep, TriangleSelectorPatch::GapAreaMin) :
                    std::min(TriangleSelectorPatch::gap_area + TriangleSelectorPatch::GapAreaStep, TriangleSelectorPatch::GapAreaMax);
                m_parent.set_as_dirty();
                return true;
            }
        }
        else if (alt_down) {
            // BBS
            double pos = m_c->object_clipper()->get_position();
            pos = action == SLAGizmoEventType::MouseWheelDown
                      ? std::max(0., pos - 0.01)
                      : std::min(1., pos + 0.01);
            m_c->object_clipper()->set_position(pos, true);
            return true;
        }
    }

    if (action == SLAGizmoEventType::ResetClippingPlane) {
        m_c->object_clipper()->set_position(-1., false);
        return true;
    }

    if (action == SLAGizmoEventType::LeftDown
     || action == SLAGizmoEventType::RightDown
    || (action == SLAGizmoEventType::Dragging && m_button_down != Button::None)) {

        if (m_triangle_selectors.empty())
            return false;

        EnforcerBlockerType new_state = EnforcerBlockerType::NONE;
        // BBS
        if (action == SLAGizmoEventType::Dragging) {
            if (m_button_down == Button::Right && this->get_right_button_state_type() == EnforcerBlockerType(-1))
                return false;
        }
        else {
            if (action == SLAGizmoEventType::RightDown && this->get_right_button_state_type() == EnforcerBlockerType(-1))
                return false;
        }

        if (! shift_down) {
            if (action == SLAGizmoEventType::Dragging)
                new_state = m_button_down == Button::Left ? this->get_left_button_state_type() : this->get_right_button_state_type();
            else
                new_state = action == SLAGizmoEventType::LeftDown ? this->get_left_button_state_type() : this->get_right_button_state_type();
        }

        const Camera        &camera                      = wxGetApp().plater()->get_camera();
        const Selection     &selection                   = m_parent.get_selection();
        const ModelObject   *mo                          = m_c->selection_info()->model_object();
        const ModelInstance *mi                          = mo->instances[selection.get_instance_idx()];
        const Transform3d   instance_trafo               = mi->get_transformation().get_matrix();
        const Transform3d   instance_trafo_not_translate = mi->get_transformation().get_matrix(true);
        std::vector<const ModelVolume*> part_volumes;

        // Precalculate transformations of individual meshes.
        std::vector<Transform3d> trafo_matrices;
        std::vector<Transform3d> trafo_matrices_not_translate;
        for (const ModelVolume *mv : mo->volumes)
            if (mv->is_model_part()) {
                trafo_matrices.emplace_back(instance_trafo * mv->get_matrix());
                trafo_matrices_not_translate.emplace_back(instance_trafo_not_translate * mv->get_matrix(true));
                part_volumes.push_back(mv);
            }

        // BBS
        if (m_tool_type == ToolType::BRUSH && m_cursor_type == TriangleSelector::CursorType::HEIGHT_RANGE)
        {
            std::vector<ProjectedHeightRange> projected_height_range_by_mesh = get_projected_height_range(mouse_position, 1., part_volumes, trafo_matrices);
            m_last_mouse_click = Vec2d::Zero();

            for (int i = 0; i < projected_height_range_by_mesh.size(); i++) {
                const ProjectedHeightRange& phr = projected_height_range_by_mesh[i];
                int mesh_idx = phr.mesh_idx;

                // The mouse button click detection is enabled when there is a valid hit.
                // Missing the object entirely
                // shall not capture the mouse.
                const bool dragging_while_painting = (action == SLAGizmoEventType::Dragging && m_button_down != Button::None);
                if (mesh_idx != -1 && m_button_down == Button::None)
                    m_button_down = ((action == SLAGizmoEventType::LeftDown) ? Button::Left : Button::Right);

                const Transform3d& trafo_matrix = trafo_matrices[mesh_idx];
                const Transform3d& trafo_matrix_not_translate = trafo_matrices_not_translate[mesh_idx];

                // Calculate direction from camera to the hit (in mesh coords):
                Vec3f camera_pos = (trafo_matrix.inverse() * camera.get_position()).cast<float>();
                const TriangleSelector::ClippingPlane& clp = this->get_clipping_plane_in_volume_coordinates(trafo_matrix);

                std::unique_ptr<TriangleSelector::Cursor> cursor = TriangleSelector::SinglePointCursor::cursor_factory(phr.z_world,
                    camera_pos, m_cursor_height, trafo_matrix, clp);
                m_triangle_selectors[mesh_idx]->select_patch(int(phr.first_facet_idx), std::move(cursor), new_state, trafo_matrix_not_translate,
                    m_triangle_splitting_enabled, m_paint_on_overhangs_only ? m_highlight_by_angle_threshold_deg : 0.f);

                m_triangle_selectors[mesh_idx]->request_update_render_data(true);
                m_last_mouse_click = mouse_position;
            }

            return true;
        }

        std::vector<std::vector<ProjectedMousePosition>> projected_mouse_positions_by_mesh = get_projected_mouse_positions(mouse_position, 1., trafo_matrices);
        m_last_mouse_click = Vec2d::Zero(); // only actual hits should be saved

        for (const std::vector<ProjectedMousePosition> &projected_mouse_positions : projected_mouse_positions_by_mesh) {
            assert(!projected_mouse_positions.empty());
            const int  mesh_idx                = projected_mouse_positions.front().mesh_idx;
            const bool dragging_while_painting = (action == SLAGizmoEventType::Dragging && m_button_down != Button::None);

            // The mouse button click detection is enabled when there is a valid hit.
            // Missing the object entirely
            // shall not capture the mouse.
            if (mesh_idx != -1)
                if (m_button_down == Button::None)
                    m_button_down = ((action == SLAGizmoEventType::LeftDown) ? Button::Left : Button::Right);

            // In case we have no valid hit, we can return. The event will be stopped when
            // dragging while painting (to prevent scene rotations and moving the object)
            if (mesh_idx == -1)
                return dragging_while_painting;

            const Transform3d &trafo_matrix               = trafo_matrices[mesh_idx];
            const Transform3d &trafo_matrix_not_translate = trafo_matrices_not_translate[mesh_idx];

            // Calculate direction from camera to the hit (in mesh coords):
            Vec3f camera_pos = (trafo_matrix.inverse() * camera.get_position()).cast<float>();

            assert(mesh_idx < int(m_triangle_selectors.size()));
            const TriangleSelector::ClippingPlane &clp = this->get_clipping_plane_in_volume_coordinates(trafo_matrix);
            if (m_tool_type == ToolType::SMART_FILL || m_tool_type == ToolType::BUCKET_FILL || (m_tool_type == ToolType::BRUSH && m_cursor_type == TriangleSelector::CursorType::POINTER)) {
                for(const ProjectedMousePosition &projected_mouse_position : projected_mouse_positions) {
                    assert(projected_mouse_position.mesh_idx == mesh_idx);
                    const Vec3f mesh_hit = projected_mouse_position.mesh_hit;
                    const int facet_idx = int(projected_mouse_position.facet_idx);
                    m_triangle_selectors[mesh_idx]->seed_fill_apply_on_triangles(new_state);
                    if (m_tool_type == ToolType::SMART_FILL)
                        m_triangle_selectors[mesh_idx]->seed_fill_select_triangles(mesh_hit, facet_idx, trafo_matrix_not_translate, clp, m_smart_fill_angle,
                                                                                       m_paint_on_overhangs_only ? m_highlight_by_angle_threshold_deg : 0.f, true);
                    else if (m_tool_type == ToolType::BRUSH && m_cursor_type == TriangleSelector::CursorType::POINTER)
                        // BBS: add infill_angle parameter
                        m_triangle_selectors[mesh_idx]->bucket_fill_select_triangles(mesh_hit, facet_idx, clp, -1.f, false, true);
                    else if (m_tool_type == ToolType::BUCKET_FILL)
                        // BBS: add infill_angle parameter
                        m_triangle_selectors[mesh_idx]->bucket_fill_select_triangles(mesh_hit, facet_idx, clp, m_smart_fill_angle, true, true);

                    m_seed_fill_last_mesh_id = -1;
                }
            } else if (m_tool_type == ToolType::BRUSH) {
                assert(m_cursor_type == TriangleSelector::CursorType::CIRCLE || m_cursor_type == TriangleSelector::CursorType::SPHERE);

                if (projected_mouse_positions.size() == 1) {
                    const ProjectedMousePosition& first_position = projected_mouse_positions.front();
                    std::unique_ptr<TriangleSelector::Cursor> cursor = TriangleSelector::SinglePointCursor::cursor_factory(first_position.mesh_hit,
                        camera_pos, m_cursor_radius,
                        m_cursor_type, trafo_matrix, clp);
                    m_triangle_selectors[mesh_idx]->select_patch(int(first_position.facet_idx), std::move(cursor), new_state, trafo_matrix_not_translate,
                        m_triangle_splitting_enabled, m_paint_on_overhangs_only ? m_highlight_by_angle_threshold_deg : 0.f);
                }
                else {
                    for (auto first_position_it = projected_mouse_positions.cbegin(); first_position_it != projected_mouse_positions.cend() - 1; ++first_position_it) {
                        auto second_position_it = first_position_it + 1;
                        std::unique_ptr<TriangleSelector::Cursor> cursor = TriangleSelector::DoublePointCursor::cursor_factory(first_position_it->mesh_hit, second_position_it->mesh_hit, camera_pos, m_cursor_radius, m_cursor_type, trafo_matrix, clp);
                        m_triangle_selectors[mesh_idx]->select_patch(int(first_position_it->facet_idx), std::move(cursor), new_state, trafo_matrix_not_translate, m_triangle_splitting_enabled, m_paint_on_overhangs_only ? m_highlight_by_angle_threshold_deg : 0.f);
                    }
                }
            }

            m_triangle_selectors[mesh_idx]->request_update_render_data(true);

            m_last_mouse_click = mouse_position;
        }

        return true;
    }

    if (action == SLAGizmoEventType::Moving && (m_tool_type == ToolType::SMART_FILL || m_tool_type == ToolType::BUCKET_FILL || (m_tool_type == ToolType::BRUSH && m_cursor_type == TriangleSelector::CursorType::POINTER))) {
        if (m_triangle_selectors.empty())
            return false;

        const Camera        &camera                       = wxGetApp().plater()->get_camera();
        const Selection     &selection                    = m_parent.get_selection();
        const ModelObject   *mo                           = m_c->selection_info()->model_object();
        const ModelInstance *mi                           = mo->instances[selection.get_instance_idx()];
        const Transform3d    instance_trafo               = mi->get_transformation().get_matrix();
        const Transform3d    instance_trafo_not_translate = mi->get_transformation().get_matrix(true);

        // Precalculate transformations of individual meshes.
        std::vector<Transform3d> trafo_matrices;
        std::vector<Transform3d> trafo_matrices_not_translate;
        for (const ModelVolume *mv : mo->volumes)
            if (mv->is_model_part()) {
                trafo_matrices.emplace_back(instance_trafo * mv->get_matrix());
                trafo_matrices_not_translate.emplace_back(instance_trafo_not_translate * mv->get_matrix(true));
            }

        // Now "click" into all the prepared points and spill paint around them.
        update_raycast_cache(mouse_position, camera, trafo_matrices);

        auto seed_fill_unselect_all = [this]() {
            for (auto &triangle_selector : m_triangle_selectors) {
                triangle_selector->seed_fill_unselect_all_triangles();
                triangle_selector->request_update_render_data();
            }
        };

        if (m_rr.mesh_id == -1) {
            // Clean selected by seed fill for all triangles in all meshes when a mouse isn't pointing on any mesh.
            seed_fill_unselect_all();
            m_seed_fill_last_mesh_id = -1;

            // In case we have no valid hit, we can return.
            return false;
        }

        // The mouse moved from one object's volume to another one. So it is needed to unselect all triangles selected by seed fill.
        if(m_rr.mesh_id != m_seed_fill_last_mesh_id)
            seed_fill_unselect_all();

        const Transform3d &trafo_matrix = trafo_matrices[m_rr.mesh_id];
        const Transform3d &trafo_matrix_not_translate = trafo_matrices_not_translate[m_rr.mesh_id];

        assert(m_rr.mesh_id < int(m_triangle_selectors.size()));
        const TriangleSelector::ClippingPlane &clp = this->get_clipping_plane_in_volume_coordinates(trafo_matrix);
        if (m_tool_type == ToolType::SMART_FILL)
            m_triangle_selectors[m_rr.mesh_id]->seed_fill_select_triangles(m_rr.hit, int(m_rr.facet), trafo_matrix_not_translate, clp, m_smart_fill_angle,
                                                                           m_paint_on_overhangs_only ? m_highlight_by_angle_threshold_deg : 0.f);
        else if (m_tool_type == ToolType::BRUSH && m_cursor_type == TriangleSelector::CursorType::POINTER)
            // BBS: add infill_angle parameter
            m_triangle_selectors[m_rr.mesh_id]->bucket_fill_select_triangles(m_rr.hit, int(m_rr.facet), clp, -1.f, false);
        else if (m_tool_type == ToolType::BUCKET_FILL)
            // BBS: add infill_angle parameter
            m_triangle_selectors[m_rr.mesh_id]->bucket_fill_select_triangles(m_rr.hit, int(m_rr.facet), clp, m_smart_fill_angle, true);
        m_triangle_selectors[m_rr.mesh_id]->request_update_render_data();
        m_seed_fill_last_mesh_id = m_rr.mesh_id;
        return true;
    }

    if ((action == SLAGizmoEventType::LeftUp || action == SLAGizmoEventType::RightUp)
      && m_button_down != Button::None) {
        // Take snapshot and update ModelVolume data.
        wxString action_name = this->handle_snapshot_action_name(shift_down, m_button_down);
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), std::string(action_name.ToUTF8().data()), UndoRedo::SnapshotType::GizmoAction);
        update_model_object();

        m_button_down = Button::None;
        m_last_mouse_click = Vec2d::Zero();
        return true;
    }

    return false;
}



void GLGizmoPainterBase::update_raycast_cache(const Vec2d& mouse_position,
                                              const Camera& camera,
                                              const std::vector<Transform3d>& trafo_matrices) const
{
    if (m_rr.mouse_position == mouse_position) {
        // Same query as last time - the answer is already in the cache.
        return;
    }

    Vec3f normal =  Vec3f::Zero();
    Vec3f hit = Vec3f::Zero();
    size_t facet = 0;
    Vec3f closest_hit = Vec3f::Zero();
    double closest_hit_squared_distance = std::numeric_limits<double>::max();
    size_t closest_facet = 0;
    int closest_hit_mesh_id = -1;

    // Cast a ray on all meshes, pick the closest hit and save it for the respective mesh
    for (int mesh_id = 0; mesh_id < int(trafo_matrices.size()); ++mesh_id) {

        if (m_c->raycaster()->raycasters()[mesh_id]->unproject_on_mesh(
                   mouse_position,
                   trafo_matrices[mesh_id],
                   camera,
                   hit,
                   normal,
                   m_c->object_clipper()->get_clipping_plane(),
                   &facet))
        {
            // In case this hit is clipped, skip it.
            if (is_mesh_point_clipped(hit.cast<double>(), trafo_matrices[mesh_id]))
                continue;

            // Is this hit the closest to the camera so far?
            double hit_squared_distance = (camera.get_position()-trafo_matrices[mesh_id]*hit.cast<double>()).squaredNorm();
            if (hit_squared_distance < closest_hit_squared_distance) {
                closest_hit_squared_distance = hit_squared_distance;
                closest_facet = facet;
                closest_hit_mesh_id = mesh_id;
                closest_hit = hit;
            }
        }
    }

    m_rr = {mouse_position, closest_hit_mesh_id, closest_hit, closest_facet};
}

bool GLGizmoPainterBase::on_is_activable() const
{
    const Selection& selection = m_parent.get_selection();

    if (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptFFF
        || !selection.is_single_full_instance()/* || wxGetApp().get_mode() == comSimple*/)
        return false;

    // BBS
#if 0
    // Check that none of the selected volumes is outside. Only SLA auxiliaries (supports) are allowed outside.
    const Selection::IndicesList& list = selection.get_volume_idxs();
    return std::all_of(list.cbegin(), list.cend(), [&selection](unsigned int idx) { return !selection.get_volume(idx)->is_outside; });
#else
    return true;
#endif
}

bool GLGizmoPainterBase::on_is_selectable() const
{
    //BBS
    /*return (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptFFF
         && wxGetApp().get_mode() != comSimple );*/
    return (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptFFF);
}


CommonGizmosDataID GLGizmoPainterBase::on_get_requirements() const
{
    return CommonGizmosDataID(
                int(CommonGizmosDataID::SelectionInfo)
              | int(CommonGizmosDataID::InstancesHider)
              | int(CommonGizmosDataID::Raycaster)
              | int(CommonGizmosDataID::ObjectClipper));
}


void GLGizmoPainterBase::on_set_state()
{
    if (m_state == m_old_state)
        return;

    if (m_state == On && m_old_state != On) { // the gizmo was just turned on
        on_opening();

        const Selection& selection = m_parent.get_selection();
        //Camera& camera = wxGetApp().plater()->get_camera();
        //Vec3d rotate_target = selection.get_bounding_box().center();
        //rotate_target(2) = 0.f;
        //Vec3d position = camera.get_position();
        //camera.set_target(rotate_target);
        //camera.look_at(position, rotate_target, Vec3d::UnitZ());
    }
    if (m_state == Off && m_old_state != Off) { // the gizmo was just turned Off
        // we are actually shutting down
        on_shutdown();
        m_old_mo_id = -1;
        //m_iva.release_geometry();
        m_triangle_selectors.clear();

        //Camera& camera = wxGetApp().plater()->get_camera();
        //camera.look_at(camera.get_position(), m_previous_target, Vec3d::UnitZ());
        //camera.set_target(m_previous_target);
        //camera.recover_from_free_camera();
    }
    m_old_state = m_state;
}



void GLGizmoPainterBase::on_load(cereal::BinaryInputArchive&)
{
    // We should update the gizmo from current ModelObject, but it is not
    // possible at this point. That would require having updated selection and
    // common gizmos data, which is not done at this point. Instead, save
    // a flag to do the update in set_painter_gizmo_data, which will be called
    // soon after.
    m_schedule_update = true;
}

TriangleSelector::ClippingPlane GLGizmoPainterBase::get_clipping_plane_in_volume_coordinates(const Transform3d &trafo) const {
    const ::Slic3r::GUI::ClippingPlane *const clipping_plane = m_c->object_clipper()->get_clipping_plane();
    if (clipping_plane == nullptr || !clipping_plane->is_active())
        return {};

    const Vec3d  clp_normal = clipping_plane->get_normal();
    const double clp_offset = clipping_plane->get_offset();

    const Transform3d trafo_normal = Transform3d(trafo.linear().transpose());
    const Transform3d trafo_inv    = trafo.inverse();

    Vec3d point_on_plane             = clp_normal * clp_offset;
    Vec3d point_on_plane_transformed = trafo_inv * point_on_plane;
    Vec3d normal_transformed         = trafo_normal * clp_normal;
    auto offset_transformed          = float(point_on_plane_transformed.dot(normal_transformed));

    return TriangleSelector::ClippingPlane({float(normal_transformed.x()), float(normal_transformed.y()), float(normal_transformed.z()), offset_transformed});
}

std::array<float, 4> TriangleSelectorGUI::get_seed_fill_color(const std::array<float, 4> &base_color)
{
    // BBS
    return {
        base_color[0] * 1.25f < 1.f ? base_color[0] * 1.25f : 1.f,
        base_color[1] * 1.25f < 1.f ? base_color[1] * 1.25f : 1.f,
        base_color[2] * 1.25f < 1.f ? base_color[2] * 1.25f : 1.f,
        1.f};
}

void TriangleSelectorGUI::render(ImGuiWrapper* imgui)
{
    if (m_update_render_data) {
        update_render_data();
        m_update_render_data = false;
    }

    auto* shader = wxGetApp().get_current_shader();
    if (! shader)
        return;
    assert(shader->get_name() == "gouraud");
    ScopeGuard guard([shader]() { if (shader) shader->set_uniform("offset_depth_buffer", false);});
    shader->set_uniform("offset_depth_buffer", true);
    for (auto iva : {std::make_pair(&m_iva_enforcers, enforcers_color),
                     std::make_pair(&m_iva_blockers, blockers_color)}) {
        if (iva.first->has_VBOs()) {
            shader->set_uniform("uniform_color", iva.second);
            iva.first->render();
        }
    }

    for (auto &iva : m_iva_seed_fills)
        if (iva.has_VBOs()) {
            size_t                      color_idx = &iva - &m_iva_seed_fills.front();
            const std::array<float, 4> &color     = TriangleSelectorGUI::get_seed_fill_color(color_idx == 1 ? enforcers_color :
                                                                                             color_idx == 2 ? blockers_color :
                                                                                                              GLVolume::NEUTRAL_COLOR);
            shader->set_uniform("uniform_color", color);
            iva.render();
        }

    if (m_paint_contour.has_VBO()) {
        ScopeGuard guard_gouraud([shader]() { shader->start_using(); });
        shader->stop_using();

        auto *contour_shader = wxGetApp().get_shader("mm_contour");
        contour_shader->start_using();

        glsafe(::glDepthFunc(GL_LEQUAL));
        m_paint_contour.render();
        glsafe(::glDepthFunc(GL_LESS));

        contour_shader->stop_using();
    }

#ifdef PRUSASLICER_TRIANGLE_SELECTOR_DEBUG
    if (imgui)
        render_debug(imgui);
    else
        assert(false); // If you want debug output, pass ptr to ImGuiWrapper.
#endif
}

void TriangleSelectorGUI::update_render_data()
{
    int              enf_cnt = 0;
    int              blc_cnt = 0;
    std::vector<int> seed_fill_cnt(m_iva_seed_fills.size(), 0);

    for (auto *iva : {&m_iva_enforcers, &m_iva_blockers})
        iva->release_geometry();

    for (auto &iva : m_iva_seed_fills)
        iva.release_geometry();

    for (const Triangle &tr : m_triangles) {
        bool is_valid = tr.valid();
        bool is_split = tr.is_split();
        EnforcerBlockerType type = tr.get_state();
        bool is_select_by_seed_fill = tr.is_selected_by_seed_fill();
        if (!tr.valid() || tr.is_split() || (tr.get_state() == EnforcerBlockerType::NONE && !tr.is_selected_by_seed_fill()))
            continue;

        int tr_state = int(tr.get_state());
        GLIndexedVertexArray &iva = tr.is_selected_by_seed_fill()                   ? m_iva_seed_fills[tr_state] :
                                    tr.get_state() == EnforcerBlockerType::ENFORCER ? m_iva_enforcers :
                                                                                      m_iva_blockers;
        int                  &cnt = tr.is_selected_by_seed_fill()                   ? seed_fill_cnt[tr_state] :
                                    tr.get_state() == EnforcerBlockerType::ENFORCER ? enf_cnt :
                                                                                      blc_cnt;
        const Vec3f          &v0  = m_vertices[tr.verts_idxs[0]].v;
        const Vec3f          &v1  = m_vertices[tr.verts_idxs[1]].v;
        const Vec3f          &v2  = m_vertices[tr.verts_idxs[2]].v;
        //FIXME the normal may likely be pulled from m_triangle_selectors, but it may not be worth the effort
        // or the current implementation may be more cache friendly.
        const Vec3f           n   = (v1 - v0).cross(v2 - v1).normalized();
        iva.push_geometry(v0, n);
        iva.push_geometry(v1, n);
        iva.push_geometry(v2, n);
        iva.push_triangle(cnt, cnt + 1, cnt + 2);
        cnt += 3;
    }

    for (auto *iva : {&m_iva_enforcers, &m_iva_blockers})
        iva->finalize_geometry(true);

    for (auto &iva : m_iva_seed_fills)
        iva.finalize_geometry(true);

    m_paint_contour.release_geometry();
    std::vector<Vec2i> contour_edges = this->get_seed_fill_contour();
    m_paint_contour.contour_vertices.reserve(contour_edges.size() * 6);
    for (const Vec2i &edge : contour_edges) {
        m_paint_contour.contour_vertices.emplace_back(m_vertices[edge(0)].v.x());
        m_paint_contour.contour_vertices.emplace_back(m_vertices[edge(0)].v.y());
        m_paint_contour.contour_vertices.emplace_back(m_vertices[edge(0)].v.z());

        m_paint_contour.contour_vertices.emplace_back(m_vertices[edge(1)].v.x());
        m_paint_contour.contour_vertices.emplace_back(m_vertices[edge(1)].v.y());
        m_paint_contour.contour_vertices.emplace_back(m_vertices[edge(1)].v.z());
    }

    m_paint_contour.contour_indices.assign(m_paint_contour.contour_vertices.size() / 3, 0);
    std::iota(m_paint_contour.contour_indices.begin(), m_paint_contour.contour_indices.end(), 0);
    m_paint_contour.contour_indices_size = m_paint_contour.contour_indices.size();

    m_paint_contour.finalize_geometry();
}

// BBS
bool TrianglePatch::is_fragment() const
{
    return this->area < TriangleSelectorPatch::gap_area;
}

float TriangleSelectorPatch::gap_area = TriangleSelectorPatch::GapAreaMin;

void TriangleSelectorPatch::render(ImGuiWrapper* imgui)
{
    if (m_update_render_data)
        update_render_data();

    auto* shader = wxGetApp().get_current_shader();
    if (!shader)
        return;
    assert(shader->get_name() == "mm_gouraud");
    GLint position_id = -1;
    GLint barycentric_id = -1;
    if (wxGetApp().plater()->is_wireframe_enabled()) {
        position_id = shader->get_attrib_location("v_position");
        barycentric_id = shader->get_attrib_location("v_barycentric");
        if (m_need_wireframe && wxGetApp().plater()->is_show_wireframe()) {
            //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", show_wireframe on");
            shader->set_uniform("show_wireframe", true);
        }
        else {
            //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", show_wireframe off");
            shader->set_uniform("show_wireframe", false);
        }
    }

    for (size_t buffer_idx = 0; buffer_idx < m_triangle_patches.size(); ++buffer_idx) {
        if (this->has_VBOs(buffer_idx)) {
            const TrianglePatch& patch = m_triangle_patches[buffer_idx];
            std::array<float, 4> color;
            if (patch.is_fragment() && !patch.neighbor_types.empty()) {
                size_t color_idx = (size_t)*patch.neighbor_types.begin();
                color = m_ebt_colors[color_idx];
                color[3] = 0.85;
            }
            else {
                size_t color_idx = (size_t)patch.type;
                color = m_ebt_colors[color_idx];
            }
            //to make black not too hard too see
            std::array<float, 4> new_color = adjust_color_for_rendering(color);
            shader->set_uniform("uniform_color", new_color);
            //shader->set_uniform("uniform_color", color);
            //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", buffer_idx %1%: new_color[%2%, %3%, %4%, %5%]")%buffer_idx%new_color[0]%new_color[1]%new_color[2]%new_color[3];
            this->render(buffer_idx, (int)position_id, (int)barycentric_id);
        }
    }

    if (m_paint_contour.has_VBO())
    {
        ScopeGuard guard_mm_gouraud([shader]() { shader->start_using(); });
        shader->stop_using();

        auto* contour_shader = wxGetApp().get_shader("mm_contour");
        contour_shader->start_using();

        glsafe(::glDepthFunc(GL_LEQUAL));
        m_paint_contour.render();
        glsafe(::glDepthFunc(GL_LESS));

        contour_shader->stop_using();
    }

    m_update_render_data = false;
}

void TriangleSelectorPatch::update_triangles_per_type()
{
    //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", enter");
    m_triangle_patches.resize((int)EnforcerBlockerType::ExtruderMax + 1);
    for (int i = 0; i < m_triangle_patches.size(); i++) {
        auto& patch = m_triangle_patches[i];
        patch.type = (EnforcerBlockerType)i;
        patch.triangle_indices.reserve(m_triangles.size() / 3);
    }

    bool using_wireframe = (wxGetApp().plater()->is_wireframe_enabled())?true:false;

    for (auto& triangle : m_triangles) {
        if (!triangle.valid() || triangle.is_split())
            continue;

        int state = (int)triangle.get_state();
        auto& patch = m_triangle_patches[state];
        //patch.triangle_indices.insert(patch.triangle_indices.end(), triangle.verts_idxs.begin(), triangle.verts_idxs.end());
        for (int i = 0; i < 3; ++i) {
            int j = triangle.verts_idxs[i];
            int index = using_wireframe?int(patch.patch_vertices.size()/6) : int(patch.patch_vertices.size()/3);
            //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", Line %1%: i=%2%, j=%3%, index=%4%, v[%5%,%6%,%7%]")%__LINE__%i%j%index%m_vertices[j].v(0)%m_vertices[j].v(1)%m_vertices[j].v(2);
            patch.patch_vertices.emplace_back(m_vertices[j].v(0));
            patch.patch_vertices.emplace_back(m_vertices[j].v(1));
            patch.patch_vertices.emplace_back(m_vertices[j].v(2));
            if (using_wireframe) {
                if (i == 0) {
                    patch.patch_vertices.emplace_back(1.0);
                    patch.patch_vertices.emplace_back(0.0);
                    patch.patch_vertices.emplace_back(0.0);
                }
                else if (i == 1) {
                    patch.patch_vertices.emplace_back(0.0);
                    patch.patch_vertices.emplace_back(1.0);
                    patch.patch_vertices.emplace_back(0.0);
                }
                else {
                    patch.patch_vertices.emplace_back(0.0);
                    patch.patch_vertices.emplace_back(0.0);
                    patch.patch_vertices.emplace_back(1.0);
                }
            }
            patch.triangle_indices.emplace_back( index);
        }
        //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", Line %1%: state=%2%, vertice size=%3%, triangle size %4%")%__LINE__%state%patch.patch_vertices.size()%patch.triangle_indices.size();
    }
    //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format("exit");
}

void TriangleSelectorPatch::update_selector_triangles()
{
    for (TrianglePatch& patch : m_triangle_patches) {
        if (!patch.is_fragment() || patch.neighbor_types.empty())
            continue;

        EnforcerBlockerType type = *patch.neighbor_types.begin();
        for (int facet_idx : patch.facet_indices) {
            m_triangles[facet_idx].set_state(type);
        }
    }
}

void TriangleSelectorPatch::update_triangles_per_patch()
{
    auto [neighbors, neighbors_propagated] = this->precompute_all_neighbors();
    std::vector<bool>  visited(m_triangles.size(), false);

    bool using_wireframe = (wxGetApp().plater()->is_wireframe_enabled())?true:false;

    auto get_all_touching_triangles = [this](int facet_idx, const Vec3i& neighbors, const Vec3i& neighbors_propagated) -> std::vector<int> {
        assert(facet_idx != -1 && facet_idx < int(m_triangles.size()));
        assert(this->verify_triangle_neighbors(m_triangles[facet_idx], neighbors));
        std::vector<int> touching_triangles;
        Vec3i            vertices = { m_triangles[facet_idx].verts_idxs[0], m_triangles[facet_idx].verts_idxs[1], m_triangles[facet_idx].verts_idxs[2] };
        append_touching_subtriangles(neighbors(0), vertices(1), vertices(0), touching_triangles);
        append_touching_subtriangles(neighbors(1), vertices(2), vertices(1), touching_triangles);
        append_touching_subtriangles(neighbors(2), vertices(0), vertices(2), touching_triangles);

        for (int neighbor_idx : neighbors_propagated)
            if (neighbor_idx != -1 && !m_triangles[neighbor_idx].is_split())
                touching_triangles.emplace_back(neighbor_idx);

        return touching_triangles;
    };

    auto calc_fragment_area = [this](const TrianglePatch& patch, float max_limit_area) {
        double total_area = 0.f;
        const std::vector<int>& ti = patch.triangle_indices;
        for (int i = 0; i < ti.size() / 3; i++) {
            total_area += std::abs((m_vertices[ti[i]].v - m_vertices[ti[i + 1]].v)
                .cross(m_vertices[ti[i]].v - m_vertices[ti[i + 2]].v).norm()) / 2;
            if (total_area >= max_limit_area)
                break;
        }

        return total_area;
    };

    int start_facet_idx = 0;
    while (1) {
        for (; start_facet_idx < visited.size(); start_facet_idx++) {
            if (!visited[start_facet_idx] && m_triangles[start_facet_idx].valid() && !m_triangles[start_facet_idx].is_split())
                break;
        }

        if (start_facet_idx >= m_triangles.size())
            break;

        EnforcerBlockerType start_facet_state = m_triangles[start_facet_idx].get_state();
        TrianglePatch patch;
        std::queue<int> facet_queue;
        facet_queue.push(start_facet_idx);
        while (!facet_queue.empty()) {
            int current_facet = facet_queue.front();
            facet_queue.pop();
            assert(!m_triangles[current_facet].is_split());

            if (!visited[current_facet]) {
                Triangle& triangle = m_triangles[current_facet];
                for (int i = 0; i < 3; ++i) {
                    int j = triangle.verts_idxs[i];
                    int index = using_wireframe?int(patch.patch_vertices.size()/6) : int(patch.patch_vertices.size()/3);
                    patch.patch_vertices.emplace_back(m_vertices[j].v(0));
                    patch.patch_vertices.emplace_back(m_vertices[j].v(1));
                    patch.patch_vertices.emplace_back(m_vertices[j].v(2));
                    if (using_wireframe) {
                        if (i == 0) {
                            patch.patch_vertices.emplace_back(1.0);
                            patch.patch_vertices.emplace_back(0.0);
                            patch.patch_vertices.emplace_back(0.0);
                        }
                        else if (i == 1) {
                            patch.patch_vertices.emplace_back(0.0);
                            patch.patch_vertices.emplace_back(1.0);
                            patch.patch_vertices.emplace_back(0.0);
                        }
                        else {
                            patch.patch_vertices.emplace_back(0.0);
                            patch.patch_vertices.emplace_back(0.0);
                            patch.patch_vertices.emplace_back(1.0);
                        }
                    }
                    patch.triangle_indices.emplace_back( index);
                }
                //patch.triangle_indices.insert(patch.triangle_indices.end(), triangle.verts_idxs.begin(), triangle.verts_idxs.end());
                patch.facet_indices.push_back(current_facet);

                std::vector<int> touching_triangles = get_all_touching_triangles(current_facet, neighbors[current_facet], neighbors_propagated[current_facet]);
                for (const int tr_idx : touching_triangles) {
                    if (tr_idx < 0)
                        continue;

                    if (m_triangles[tr_idx].get_state() != start_facet_state) {
                        patch.neighbor_types.insert(m_triangles[tr_idx].get_state());
                        continue;
                    }

                    // should check visited state after color for neight types
                    if (visited[tr_idx])
                        continue;

                    assert(!m_triangles[tr_idx].is_split());
                    facet_queue.push(tr_idx);
                }
            }

            visited[current_facet] = true;
        }

        patch.area = calc_fragment_area(patch, GapAreaMax);
        patch.type = start_facet_state;
        m_triangle_patches.emplace_back(std::move(patch));
     }
}

void TriangleSelectorPatch::set_filter_state(bool is_filter_state)
{
    if (!m_filter_state && is_filter_state) {
        m_filter_state = is_filter_state;
        this->release_geometry();
        update_render_data();
    }

    m_filter_state = is_filter_state;
}

void TriangleSelectorPatch::update_render_data()
{
    //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", m_paint_changed=%1%, m_triangle_patches.size %2%")%m_paint_changed%m_triangle_patches.size();
    if (m_paint_changed || (m_triangle_patches.size() == 0)) {
        this->release_geometry();

        /*m_patch_vertices.reserve(m_vertices.size() * 3);
        for (const Vertex& vr : m_vertices) {
            m_patch_vertices.emplace_back(vr.v.x());
            m_patch_vertices.emplace_back(vr.v.y());
            m_patch_vertices.emplace_back(vr.v.z());
        }
        this->finalize_vertices();*/

        if (m_filter_state)
            update_triangles_per_patch();
        else
            update_triangles_per_type();
        this->finalize_triangle_indices();

        m_paint_changed = false;
    }

    //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", before paint_contour");

    m_paint_contour.release_geometry();
    std::vector<Vec2i> contour_edges = this->get_seed_fill_contour();
    m_paint_contour.contour_vertices.reserve(contour_edges.size() * 6);
    for (const Vec2i& edge : contour_edges) {
        m_paint_contour.contour_vertices.emplace_back(m_vertices[edge(0)].v.x());
        m_paint_contour.contour_vertices.emplace_back(m_vertices[edge(0)].v.y());
        m_paint_contour.contour_vertices.emplace_back(m_vertices[edge(0)].v.z());

        m_paint_contour.contour_vertices.emplace_back(m_vertices[edge(1)].v.x());
        m_paint_contour.contour_vertices.emplace_back(m_vertices[edge(1)].v.y());
        m_paint_contour.contour_vertices.emplace_back(m_vertices[edge(1)].v.z());
    }

    m_paint_contour.contour_indices.assign(m_paint_contour.contour_vertices.size() / 3, 0);
    std::iota(m_paint_contour.contour_indices.begin(), m_paint_contour.contour_indices.end(), 0);
    m_paint_contour.contour_indices_size = m_paint_contour.contour_indices.size();

    m_paint_contour.finalize_geometry();
    //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", exit");
}

void TriangleSelectorPatch::render(int triangle_indices_idx, int position_id, int barycentric_id)
{
    assert(triangle_indices_idx < this->m_triangle_indices_VBO_ids.size());
    assert(this->m_triangle_patches.size() == this->m_triangle_indices_VBO_ids.size());
    //assert(this->m_vertices_VBO_id != 0);
    assert(this->m_triangle_patches.size() == this->m_vertices_VBO_ids.size());
    assert(this->m_vertices_VBO_ids[triangle_indices_idx] != 0);
    assert(this->m_triangle_indices_VBO_ids[triangle_indices_idx] != 0);

    //glsafe(::glBindBuffer(GL_ARRAY_BUFFER, this->m_vertices_VBO_id));
    //glsafe(::glVertexPointer(3, GL_FLOAT, 3 * sizeof(float), (const void*)(0 * sizeof(float))));
    if (this->m_triangle_indices_sizes[triangle_indices_idx] > 0) {
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, this->m_vertices_VBO_ids[triangle_indices_idx]));
        if (position_id != -1) {
            glsafe(::glEnableVertexAttribArray((GLint)position_id));
            glsafe(::glVertexAttribPointer((GLint)position_id, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), nullptr));
        }
        else {
            glsafe(::glVertexPointer(3, GL_FLOAT, 3 * sizeof(float), nullptr));
        }

        if (barycentric_id != -1) {
            glsafe(::glEnableVertexAttribArray((GLint)barycentric_id));
            glsafe(::glVertexAttribPointer((GLint)barycentric_id, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (GLvoid*)(intptr_t)(3 * sizeof(float))));
        }
        //glsafe(::glVertexPointer(3, GL_FLOAT, 3 * sizeof(float), nullptr));
        //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", Line %1%: triangle_indices_idx %2%, bind vertex vbo, buffer id %3%")%__LINE__%triangle_indices_idx%this->m_vertices_VBO_ids[triangle_indices_idx];
    }

    glsafe(::glEnableClientState(GL_VERTEX_ARRAY));

    // Render using the Vertex Buffer Objects.
    if (this->m_triangle_indices_sizes[triangle_indices_idx] > 0) {
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->m_triangle_indices_VBO_ids[triangle_indices_idx]));
        glsafe(::glDrawElements(GL_TRIANGLES, GLsizei(this->m_triangle_indices_sizes[triangle_indices_idx]), GL_UNSIGNED_INT, nullptr));
        glsafe(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
        //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", Line %1%: triangle_indices_idx %2%, bind indices vbo, buffer id %3%")%__LINE__%triangle_indices_idx%this->m_triangle_indices_VBO_ids[triangle_indices_idx];
    }

    glsafe(::glDisableClientState(GL_VERTEX_ARRAY));
    if ((this->m_triangle_indices_sizes[triangle_indices_idx] > 0)&&(position_id != -1))
        glsafe(::glDisableVertexAttribArray(position_id));
    if ((this->m_triangle_indices_sizes[triangle_indices_idx] > 0)&&(barycentric_id != -1))
        glsafe(::glDisableVertexAttribArray((GLint)barycentric_id));

    if (this->m_triangle_indices_sizes[triangle_indices_idx] > 0)
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
}

void TriangleSelectorPatch::release_geometry()
{
    /*if (m_vertices_VBO_id) {
        glsafe(::glDeleteBuffers(1, &m_vertices_VBO_id));
        m_vertices_VBO_id = 0;
    }*/
    for (auto& vertice_VBO_id : m_vertices_VBO_ids) {
        glsafe(::glDeleteBuffers(1, &vertice_VBO_id));
        vertice_VBO_id = 0;
    }
    for (auto& triangle_indices_VBO_id : m_triangle_indices_VBO_ids) {
        glsafe(::glDeleteBuffers(1, &triangle_indices_VBO_id));
        triangle_indices_VBO_id = 0;
    }
    this->clear();

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", Line %1%: released geometry")%__LINE__;
}

void TriangleSelectorPatch::finalize_vertices()
{
    /*assert(m_vertices_VBO_id == 0);
    if (!this->m_patch_vertices.empty()) {
        glsafe(::glGenBuffers(1, &this->m_vertices_VBO_id));
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, this->m_vertices_VBO_id));
        glsafe(::glBufferData(GL_ARRAY_BUFFER, this->m_patch_vertices.size() * sizeof(float), this->m_patch_vertices.data(), GL_STATIC_DRAW));
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
        this->m_patch_vertices.clear();
    }*/
}

void TriangleSelectorPatch::finalize_triangle_indices()
{
    m_vertices_VBO_ids.resize(m_triangle_patches.size());
    m_triangle_indices_VBO_ids.resize(m_triangle_patches.size());
    m_triangle_indices_sizes.resize(m_triangle_patches.size());
    assert(std::all_of(m_triangle_indices_VBO_ids.cbegin(), m_triangle_indices_VBO_ids.cend(), [](const auto& ti_VBO_id) { return ti_VBO_id == 0; }));

    for (size_t buffer_idx = 0; buffer_idx < m_triangle_patches.size(); ++buffer_idx) {
        std::vector<float>& patch_vertices = m_triangle_patches[buffer_idx].patch_vertices;
        if (!patch_vertices.empty()) {
            glsafe(::glGenBuffers(1, &m_vertices_VBO_ids[buffer_idx]));
            glsafe(::glBindBuffer(GL_ARRAY_BUFFER, m_vertices_VBO_ids[buffer_idx]));
            glsafe(::glBufferData(GL_ARRAY_BUFFER, patch_vertices.size() * sizeof(float), patch_vertices.data(), GL_STATIC_DRAW));
            glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
            //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", Line %1%: buffer_idx %2%, vertices size %3%, buffer id %4%")%__LINE__%buffer_idx%patch_vertices.size()%m_vertices_VBO_ids[buffer_idx];
            patch_vertices.clear();
        }

        std::vector<int>& triangle_indices = m_triangle_patches[buffer_idx].triangle_indices;
        m_triangle_indices_sizes[buffer_idx] = triangle_indices.size();
        if (!triangle_indices.empty()) {
            glsafe(::glGenBuffers(1, &m_triangle_indices_VBO_ids[buffer_idx]));
            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_triangle_indices_VBO_ids[buffer_idx]));
            glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, triangle_indices.size() * sizeof(int), triangle_indices.data(), GL_STATIC_DRAW));
            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
            //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", Line %1%: buffer_idx %2%, vertices size %3%, buffer id %4%")%__LINE__%buffer_idx%triangle_indices.size()%m_triangle_indices_VBO_ids[buffer_idx];
            triangle_indices.clear();
        }
    }
}

void GLPaintContour::render() const
{
    assert(this->m_contour_VBO_id != 0);
    assert(this->m_contour_EBO_id != 0);

    glsafe(::glLineWidth(4.0f));

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, this->m_contour_VBO_id));
    glsafe(::glVertexPointer(3, GL_FLOAT, 3 * sizeof(float), nullptr));

    glsafe(::glEnableClientState(GL_VERTEX_ARRAY));

    if (this->contour_indices_size > 0) {
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->m_contour_EBO_id));
        glsafe(::glDrawElements(GL_LINES, GLsizei(this->contour_indices_size), GL_UNSIGNED_INT, nullptr));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
    }

    glsafe(::glDisableClientState(GL_VERTEX_ARRAY));

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
}

void GLPaintContour::finalize_geometry()
{
    assert(this->m_contour_VBO_id == 0);
    assert(this->m_contour_EBO_id == 0);

    if (!this->contour_vertices.empty()) {
        glsafe(::glGenBuffers(1, &this->m_contour_VBO_id));
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, this->m_contour_VBO_id));
        glsafe(::glBufferData(GL_ARRAY_BUFFER, this->contour_vertices.size() * sizeof(float), this->contour_vertices.data(), GL_STATIC_DRAW));
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
        this->contour_vertices.clear();
    }

    if (!this->contour_indices.empty()) {
        glsafe(::glGenBuffers(1, &this->m_contour_EBO_id));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->m_contour_EBO_id));
        glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, this->contour_indices.size() * sizeof(unsigned int), this->contour_indices.data(), GL_STATIC_DRAW));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
        this->contour_indices.clear();
    }
}

void GLPaintContour::release_geometry()
{
    if (this->m_contour_VBO_id) {
        glsafe(::glDeleteBuffers(1, &this->m_contour_VBO_id));
        this->m_contour_VBO_id = 0;
    }
    if (this->m_contour_EBO_id) {
        glsafe(::glDeleteBuffers(1, &this->m_contour_EBO_id));
        this->m_contour_EBO_id = 0;
    }
    this->clear();
}

#ifdef PRUSASLICER_TRIANGLE_SELECTOR_DEBUG
void TriangleSelectorGUI::render_debug(ImGuiWrapper* imgui)
{
    imgui->begin(std::string("TriangleSelector dialog (DEV ONLY)"),
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
    static float edge_limit = 1.f;
    imgui->text("Edge limit (mm): ");
    imgui->slider_float("", &edge_limit, 0.1f, 8.f);
    set_edge_limit(edge_limit);
    imgui->checkbox("Show split triangles: ", m_show_triangles);
    imgui->checkbox("Show invalid triangles: ", m_show_invalid);

    int valid_triangles = m_triangles.size() - m_invalid_triangles;
    imgui->text("Valid triangles: " + std::to_string(valid_triangles) +
                  "/" + std::to_string(m_triangles.size()));
    imgui->text("Vertices: " + std::to_string(m_vertices.size()));
    if (imgui->button("Force garbage collection"))
        garbage_collect();

    if (imgui->button("Serialize - deserialize")) {
        auto map = serialize();
        deserialize(map);
    }

    imgui->end();

    if (! m_show_triangles)
        return;

    enum vtype {
        ORIGINAL = 0,
        SPLIT,
        INVALID
    };

    for (auto& va : m_varrays)
        va.release_geometry();

    std::array<int, 3> cnts;

    ::glScalef(1.01f, 1.01f, 1.01f);

    for (int tr_id=0; tr_id<int(m_triangles.size()); ++tr_id) {
        const Triangle& tr = m_triangles[tr_id];
        GLIndexedVertexArray* va = nullptr;
        int* cnt = nullptr;
        if (tr_id < m_orig_size_indices) {
            va = &m_varrays[ORIGINAL];
            cnt = &cnts[ORIGINAL];
        }
        else if (tr.valid()) {
            va = &m_varrays[SPLIT];
            cnt = &cnts[SPLIT];
        }
        else {
            if (! m_show_invalid)
                continue;
            va = &m_varrays[INVALID];
            cnt = &cnts[INVALID];
        }

        for (int i=0; i<3; ++i)
            va->push_geometry(double(m_vertices[tr.verts_idxs[i]].v[0]),
                              double(m_vertices[tr.verts_idxs[i]].v[1]),
                              double(m_vertices[tr.verts_idxs[i]].v[2]),
                              0., 0., 1.);
        va->push_triangle(*cnt,
                          *cnt+1,
                          *cnt+2);
        *cnt += 3;
    }

    ::glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
    for (vtype i : {ORIGINAL, SPLIT, INVALID}) {
        GLIndexedVertexArray& va = m_varrays[i];
        va.finalize_geometry(true);
        if (va.has_VBOs()) {
            switch (i) {
            case ORIGINAL : ::glColor3f(0.f, 0.f, 1.f); break;
            case SPLIT    : ::glColor3f(1.f, 0.f, 0.f); break;
            case INVALID  : ::glColor3f(1.f, 1.f, 0.f); break;
            }
            va.render();
        }
    }
    ::glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
}
#endif



} // namespace Slic3r::GUI
