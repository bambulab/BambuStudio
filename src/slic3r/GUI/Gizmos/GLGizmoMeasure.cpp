#include "GLGizmoMeasure.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Gizmos/GizmoObjectManipulation.hpp"


#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/MeasureUtils.hpp"

#include <imgui/imgui_internal.h>

#include <numeric>

#include <GL/glew.h>

#include <tbb/parallel_for.h>
#include <future>
#include <wx/clipbrd.h>

namespace Slic3r {
namespace GUI {

static const Slic3r::ColorRGBA SELECTED_1ST_COLOR = { 0.25f, 0.75f, 0.75f, 1.0f };
static const Slic3r::ColorRGBA SELECTED_2ND_COLOR = { 0.75f, 0.25f, 0.75f, 1.0f };
static const Slic3r::ColorRGBA NEUTRAL_COLOR      = {0.5f, 0.5f, 0.5f, 1.0f};
static const Slic3r::ColorRGBA HOVER_COLOR        = {0.0f, 1.0f, 0.0f, 1.0f};//Green

static const int POINT_ID         = 100;
static const int EDGE_ID          = 200;
static const int CIRCLE_ID        = 300;
static const int PLANE_ID         = 400;
static const int SEL_SPHERE_1_ID  = 501;
static const int SEL_SPHERE_2_ID  = 502;

static const float TRIANGLE_BASE = 10.0f;
static const float TRIANGLE_HEIGHT = TRIANGLE_BASE * 1.618033f;

static const std::string CTRL_STR =
#ifdef __APPLE__
"⌘"
#else
"Ctrl"
#endif //__APPLE__
;

static std::string format_double(double value)
{
    char buf[1024];
    sprintf(buf, "%.3f", value);
    return std::string(buf);
}

static std::string format_vec3(const Vec3d& v)
{
    char buf[1024];
    sprintf(buf, "X: %.3f, Y: %.3f, Z: %.3f", v.x(), v.y(), v.z());
    return std::string(buf);
}

static std::string surface_feature_type_as_string(Measure::SurfaceFeatureType type)
{
    switch (type)
    {
    default:
    case Measure::SurfaceFeatureType::Undef:  { return ("No feature"); }
    case Measure::SurfaceFeatureType::Point:  { return _u8L("Vertex"); }
    case Measure::SurfaceFeatureType::Edge:   { return _u8L("Edge"); }
    case Measure::SurfaceFeatureType::Circle: { return _u8L("Circle"); }
    case Measure::SurfaceFeatureType::Plane:  { return _u8L("Plane"); }
    }
}

static std::string point_on_feature_type_as_string(Measure::SurfaceFeatureType type, int hover_id)
{
    std::string ret;
    switch (type) {
    case Measure::SurfaceFeatureType::Point:  { ret = _u8L("Vertex"); break; }
    case Measure::SurfaceFeatureType::Edge:   { ret = _u8L("Point on edge"); break; }
    case Measure::SurfaceFeatureType::Circle: { ret = _u8L("Point on circle"); break; }
    case Measure::SurfaceFeatureType::Plane:  { ret = _u8L("Point on plane"); break; }
    default:                                  { assert(false); break; }
    }
    return ret;
}

static std::string center_on_feature_type_as_string(Measure::SurfaceFeatureType type)
{
    std::string ret;
    switch (type) {
    case Measure::SurfaceFeatureType::Edge:   { ret = _u8L("Center of edge"); break; }
    case Measure::SurfaceFeatureType::Circle: { ret = _u8L("Center of circle"); break; }
    default: { assert(false); break; }
    }
    return ret;
}

static std::shared_ptr<GLModel> init_plane_data(const indexed_triangle_set &its, const std::vector<int> &triangle_indices)
{
    GLModel::Geometry init_data;
    init_data.format = {GUI::GLModel::PrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3};
    init_data.reserve_indices(3 * triangle_indices.size());
    init_data.reserve_vertices(3 * triangle_indices.size());
    unsigned int i = 0;
    for (int idx : triangle_indices) {
        const Vec3f &v0 = its.vertices[its.indices[idx][0]];
        const Vec3f &v1 = its.vertices[its.indices[idx][1]];
        const Vec3f &v2 = its.vertices[its.indices[idx][2]];

        const Vec3f n = (v1 - v0).cross(v2 - v0).normalized();
        init_data.add_vertex(v0, n);
        init_data.add_vertex(v1, n);
        init_data.add_vertex(v2, n);
        init_data.add_triangle(i, i + 1, i + 2);
        i += 3;
    }
    std::shared_ptr<GLModel> gl_data = std::make_shared<GLModel>();
    gl_data->init_from(std::move(init_data),true);
    return gl_data;
}


static std::shared_ptr<GLModel> init_torus_data(unsigned int       primary_resolution,
                                                unsigned int       secondary_resolution,
                                                const Vec3f &      center,
                                                float radius,
                                                float thickness,
                                                const Vec3f& model_axis,
                                                const Transform3f& world_trafo)
{
    const unsigned int torus_sector_count = std::max<unsigned int>(4, primary_resolution);
    const unsigned int section_sector_count = std::max<unsigned int>(4, secondary_resolution);
    const float torus_sector_step = 2.0f * float(M_PI) / float(torus_sector_count);
    const float section_sector_step = 2.0f * float(M_PI) / float(section_sector_count);

    GLModel::Geometry data;
    data.format = {GLModel::PrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3};
    data.reserve_vertices(torus_sector_count * section_sector_count);
    data.reserve_indices(torus_sector_count * section_sector_count * 2 * 3);

    // vertices
    const Transform3f local_to_world_matrix = world_trafo * Geometry::translation_transform(center.cast<double>()).cast<float>() *
                                              Eigen::Quaternion<float>::FromTwoVectors(Vec3f::UnitZ(), model_axis);
    for (unsigned int i = 0; i < torus_sector_count; ++i) {
        const float section_angle = torus_sector_step * i;
        const Vec3f radius_dir(std::cos(section_angle), std::sin(section_angle), 0.0f);
        const Vec3f local_section_center = radius * radius_dir;
        const Vec3f world_section_center = local_to_world_matrix * local_section_center;
        const Vec3f local_section_normal = local_section_center.normalized().cross(Vec3f::UnitZ()).normalized();
        const Vec3f world_section_normal = (Vec3f) (local_to_world_matrix.matrix().block(0, 0, 3, 3) * local_section_normal).normalized();
        const Vec3f base_v               = thickness * radius_dir;
        for (unsigned int j = 0; j < section_sector_count; ++j) {
            const Vec3f v = Eigen::AngleAxisf(section_sector_step * j, world_section_normal) * base_v;
            data.add_vertex(world_section_center + v, (Vec3f) v.normalized());
        }
    }

    // triangles
    for (unsigned int i = 0; i < torus_sector_count; ++i) {
        const unsigned int ii      = i * section_sector_count;
        const unsigned int ii_next = ((i + 1) % torus_sector_count) * section_sector_count;
        for (unsigned int j = 0; j < section_sector_count; ++j) {
            const unsigned int j_next = (j + 1) % section_sector_count;
            const unsigned int i0     = ii + j;
            const unsigned int i1     = ii_next + j;
            const unsigned int i2     = ii_next + j_next;
            const unsigned int i3     = ii + j_next;
            data.add_triangle(i0, i1, i2);
            data.add_triangle(i0, i2, i3);
        }
    }
    std::shared_ptr<GLModel> gl_data = std::make_shared<GLModel>();
    gl_data->init_from(std::move(data), true);
    return gl_data;
}

static bool is_feature_with_center(const Measure::SurfaceFeature& feature)
{
    const Measure::SurfaceFeatureType type = feature.get_type();
    return (type == Measure::SurfaceFeatureType::Circle || (type == Measure::SurfaceFeatureType::Edge && feature.get_extra_point().has_value()));
}

static Vec3d get_feature_offset(const Measure::SurfaceFeature& feature)
{
    Vec3d ret;
    switch (feature.get_type())
    {
    case Measure::SurfaceFeatureType::Circle:
    {
        const auto [center, radius, normal] = feature.get_circle();
        ret = center;
        break;
    }
    case Measure::SurfaceFeatureType::Edge:
    {
        std::optional<Vec3d> p = feature.get_extra_point();
        assert(p.has_value());
        ret = *p;
        break;
    }
    case Measure::SurfaceFeatureType::Point:
    {
        ret = feature.get_point();
        break;
    }
    default: { assert(false); }
    }
    return ret;
}

class TransformHelper
{
    struct Cache
    {
        std::array<int, 4> viewport;
        Matrix4d ndc_to_ss_matrix;
        Transform3d ndc_to_ss_matrix_inverse;
    };

    static Cache s_cache;

public:
    static Vec3d model_to_world(const Vec3d& model, const Transform3d& world_matrix) {
        return world_matrix * model;
    }

    static Vec4d world_to_clip(const Vec3d& world, const Matrix4d& projection_view_matrix) {
        return projection_view_matrix * Vec4d(world.x(), world.y(), world.z(), 1.0);
    }

    static Vec3d clip_to_ndc(const Vec4d& clip) {
        return Vec3d(clip.x(), clip.y(), clip.z()) / clip.w();
    }

    static Vec2d ndc_to_ss(const Vec3d& ndc, const std::array<int, 4>& viewport) {
        const double half_w = 0.5 * double(viewport[2]);
        const double half_h = 0.5 * double(viewport[3]);
        return { half_w * ndc.x() + double(viewport[0]) + half_w, half_h * ndc.y() + double(viewport[1]) + half_h };
    };

    static Vec4d model_to_clip(const Vec3d& model, const Transform3d& world_matrix, const Matrix4d& projection_view_matrix) {
        return world_to_clip(model_to_world(model, world_matrix), projection_view_matrix);
    }

    static Vec3d model_to_ndc(const Vec3d& model, const Transform3d& world_matrix, const Matrix4d& projection_view_matrix) {
        return clip_to_ndc(world_to_clip(model_to_world(model, world_matrix), projection_view_matrix));
    }

    static Vec2d model_to_ss(const Vec3d& model, const Transform3d& world_matrix, const Matrix4d& projection_view_matrix, const std::array<int, 4>& viewport) {
        return ndc_to_ss(clip_to_ndc(world_to_clip(model_to_world(model, world_matrix), projection_view_matrix)), viewport);
    }

    static Vec2d world_to_ss(const Vec3d& world, const Matrix4d& projection_view_matrix, const std::array<int, 4>& viewport) {
        return ndc_to_ss(clip_to_ndc(world_to_clip(world, projection_view_matrix)), viewport);
    }

    static const Matrix4d& ndc_to_ss_matrix(const std::array<int, 4>& viewport) {
        update(viewport);
        return s_cache.ndc_to_ss_matrix;
    }

    static const Transform3d ndc_to_ss_matrix_inverse(const std::array<int, 4>& viewport) {
        update(viewport);
        return s_cache.ndc_to_ss_matrix_inverse;
    }

private:
    static void update(const std::array<int, 4>& viewport) {
        if (s_cache.viewport == viewport)
            return;

        const double half_w = 0.5 * double(viewport[2]);
        const double half_h = 0.5 * double(viewport[3]);
        s_cache.ndc_to_ss_matrix << half_w, 0.0, 0.0, double(viewport[0]) + half_w,
            0.0, half_h, 0.0, double(viewport[1]) + half_h,
            0.0, 0.0, 1.0, 0.0,
            0.0, 0.0, 0.0, 1.0;

        s_cache.ndc_to_ss_matrix_inverse = s_cache.ndc_to_ss_matrix.inverse();
        s_cache.viewport = viewport;
    }
};

TransformHelper::Cache TransformHelper::s_cache = { { 0, 0, 0, 0 }, Matrix4d::Identity(), Transform3d::Identity() };

GLGizmoMeasure::GLGizmoMeasure(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
: GLGizmoBase(parent, icon_filename, sprite_id)
{
    auto sphere_geometry = smooth_sphere(16, 7.5f);
    m_sphere             = std::make_shared<GLModel>();
    m_sphere->init_from(std::move(sphere_geometry),true);
    m_gripper_id_raycast_map[GripperType::POINT] = std::make_shared<PickRaycaster>(m_sphere->mesh, POINT_ID);

    auto cylinder_geometry = smooth_cylinder(16, 5.0f, 1.0f);

    m_cylinder = std::make_shared<GLModel>();
    m_cylinder->init_from(std::move(cylinder_geometry), true);
    m_gripper_id_raycast_map[GripperType::EDGE] = std::make_shared<PickRaycaster>(m_cylinder->mesh,EDGE_ID);
}

bool GLGizmoMeasure::on_mouse(const wxMouseEvent &mouse_event)
{
    if (mouse_event.Moving()) {
        // only for sure
        m_mouse_left_down = false;
        return false;
    }
    else if (mouse_event.Dragging()) {
        // Enable/Disable panning/rotating the 3D scene
        // Ctrl is pressed or the mouse is not hovering a selected volume
        bool unlock_dragging = mouse_event.CmdDown() || (m_hover_id == -1 && !m_parent.get_selection().contains_volume(m_parent.get_first_hover_volume_idx()));
        // mode is not center selection or mouse is not hovering a center
        unlock_dragging &= !mouse_event.ShiftDown() || (m_hover_id != SEL_SPHERE_1_ID && m_hover_id != SEL_SPHERE_2_ID && m_hover_id != POINT_ID);
        return !unlock_dragging;
    }
    else if (mouse_event.LeftDown()) {
        // let the event pass through to allow panning/rotating the 3D scene
        if (mouse_event.CmdDown())
            return false;

        if (m_hover_id != -1) {
            m_mouse_left_down = true;
            m_mouse_left_down_mesh_deal = true;
            auto detect_current_item = [this]() {
                SelectedFeatures::Item item;
                if (m_hover_id == SEL_SPHERE_1_ID) {
                    if (m_selected_features.first.is_center)
                        // mouse is hovering over a selected center
                        item = { true, m_selected_features.first.source, { Measure::SurfaceFeature(get_feature_offset(*m_selected_features.first.source)) } };
                    else if (is_feature_with_center(*m_selected_features.first.feature))
                        // mouse is hovering over a unselected center
                        item = { true, m_selected_features.first.feature, { Measure::SurfaceFeature(get_feature_offset(*m_selected_features.first.feature)) } };
                    else
                        // mouse is hovering over a point
                        item = m_selected_features.first;
                }
                else if (m_hover_id == SEL_SPHERE_2_ID) {
                    if (m_selected_features.second.is_center)
                        // mouse is hovering over a selected center
                        item = { true, m_selected_features.second.source, { Measure::SurfaceFeature(get_feature_offset(*m_selected_features.second.source)) } };
                    else if (is_feature_with_center(*m_selected_features.second.feature))
                        // mouse is hovering over a center
                        item = { true, m_selected_features.second.feature, { Measure::SurfaceFeature(get_feature_offset(*m_selected_features.second.feature)) } };
                    else
                        // mouse is hovering over a point
                        item = m_selected_features.second;
                }
                else {
                    switch (m_mode)
                    {
                    case EMode::FeatureSelection: { item = { false, m_curr_feature, m_curr_feature }; break; }
                    case EMode::PointSelection:   { item = { false, m_curr_feature, Measure::SurfaceFeature(*m_curr_point_on_feature_position) }; break; }
                    }
                }
                return item;
            };

            auto requires_sphere_raycaster_for_picking = [this](const SelectedFeatures::Item& item) {
                if (m_mode == EMode::PointSelection || item.feature->get_type() == Measure::SurfaceFeatureType::Point)
                    return true;
                else if (m_mode == EMode::FeatureSelection) {
                    if (is_feature_with_center(*item.feature))
                        return true;
                }
                return false;
            };

            if (m_selected_features.first.feature.has_value()) {
                const SelectedFeatures::Item item = detect_current_item();
                if (m_selected_features.first != item) {
                    bool processed = false;
                    if (item.is_center) {
                        if (item.source == m_selected_features.first.feature) {
                            // switch 1st selection from feature to its center
                            m_selected_features.first = item;
                            processed = true;
                        }
                        else if (item.source == m_selected_features.second.feature) {
                            // switch 2nd selection from feature to its center
                            m_selected_features.second = item;
                            processed = true;
                        }
                    }
                    else if (is_feature_with_center(*item.feature)) {
                      if (m_selected_features.first.is_center && m_selected_features.first.source == item.feature) {
                          // switch 1st selection from center to its feature
                          m_selected_features.first = item;
                          processed = true;
                      }
                      else if (m_selected_features.second.is_center && m_selected_features.second.source == item.feature) {
                          // switch 2nd selection from center to its feature
                          m_selected_features.second = item;
                          processed = true;
                      }
                    }

                    if (!processed) {
                        remove_selected_sphere_raycaster(SEL_SPHERE_2_ID);
                        if (m_selected_features.second == item) {
                            // 2nd feature deselection
                            // m_selected_features.second.reset();
                            reset_feature2();
                        }
                        else {
                            // 2nd feature selection
                            m_selected_features.second = item;
                            if (requires_sphere_raycaster_for_picking(item)) {
                                auto pick = std::make_shared<PickRaycaster>(m_sphere->mesh, SEL_SPHERE_2_ID);
                                m_gripper_id_raycast_map[GripperType::SPHERE_2] = pick;
                            }
                        }
                    }
                }
                else {
                    remove_selected_sphere_raycaster(SEL_SPHERE_1_ID);
                    if (m_selected_features.second.feature.has_value()) {
                        // promote 2nd feature to 1st feature
                        reset_feature1();
                        if (requires_sphere_raycaster_for_picking(m_selected_features.first)) {
                            auto pick = std::make_shared<PickRaycaster>(m_sphere->mesh, SEL_SPHERE_1_ID);
                            m_gripper_id_raycast_map[GripperType::SPHERE_1] = pick;
                        }
                    } else {
                        // 1st feature deselection
                        reset_feature1();
                    }
                }
            }
            else {
                // 1st feature selection
                const SelectedFeatures::Item item = detect_current_item();
                m_selected_features.first = item;
                if (requires_sphere_raycaster_for_picking(item)) {
                    auto pick = std::make_shared<PickRaycaster>(m_sphere->mesh, SEL_SPHERE_1_ID);
                    m_gripper_id_raycast_map[GripperType::SPHERE_1] = pick;
                }
            }

            update_measurement_result();

            m_imgui->set_requires_extra_frame();

            const Measure::MeasurementResult &measure = m_measurement_result;
            m_distance  = Vec3d::Zero();
            if (measure.distance_xyz.has_value() && measure.distance_xyz->norm() > EPSILON) {
                m_distance = *measure.distance_xyz;
            }
            else if (measure.distance_infinite.has_value()) {
                m_distance = measure.distance_infinite->to - measure.distance_infinite->from;
            } else if (measure.distance_strict.has_value()) {
                m_distance = measure.distance_strict->to - measure.distance_strict->from;
            }
            if (wxGetApp().app_config->get("use_inches") == "1")
                m_distance = GizmoObjectManipulation::mm_to_in * m_distance;
            m_buffered_distance = m_distance;
            return true;
        }
        else
            // if the mouse pointer is on any volume, filter out the event to prevent the user to move it
            // equivalent tp: return (m_parent.get_first_hover_volume_idx() != -1);
            return m_curr_feature.has_value();

        // fix: prevent restart gizmo when reselect object
        // take responsibility for left up
        if (m_parent.get_first_hover_volume_idx() >= 0)
            m_mouse_left_down = true;
    }
    else if (mouse_event.LeftUp()) {
        if (m_mouse_left_down) {
            // responsible for mouse left up after selecting plane
            m_mouse_left_down = false;
            return true;
        }
        if (m_hover_id == -1 && !m_parent.is_mouse_dragging())
            // avoid closing the gizmo if the user clicks outside of any volume
            return true;
    }
    else if (mouse_event.RightDown()) {
        // let the event pass through to allow panning/rotating the 3D scene
        if (mouse_event.CmdDown())
            return false;
    }
    else if (mouse_event.Leaving())
        m_mouse_left_down = false;

    return false;
}
void func() {}

void GLGizmoMeasure::data_changed(bool is_serializing)
{
    wxBusyCursor wait;

    if (m_pending_scale) {
        update_if_needed();
        register_single_mesh_pick();
        update_measurement_result();
        m_pending_scale = false;
    }
    else {
        m_parent.toggle_selected_volume_visibility(true);
        reset_all_pick();
        update_if_needed();
        register_single_mesh_pick();
        reset_all_feature();
    }
    m_last_inv_zoom  = 0.0f;
    m_last_plane_idx = -1;
    m_editing_distance                = false;
    m_is_editing_distance_first_frame = true;
}

bool GLGizmoMeasure::gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down)
{
    if (action == SLAGizmoEventType::ShiftDown) {
        if (m_shift_kar_filter.is_first()) {
            m_mode = EMode::PointSelection;
            disable_scene_raycasters();
        }
        m_shift_kar_filter.increase_count();
    }
    else if (action == SLAGizmoEventType::ShiftUp) {
        m_shift_kar_filter.reset_count();
        m_mode = EMode::FeatureSelection;
        restore_scene_raycasters_state();
    }
    else if (action == SLAGizmoEventType::Delete) {
        reset_all_feature();
        m_parent.request_extra_frame();
    }
    else if (action == SLAGizmoEventType::Escape) {
        if (!m_selected_features.first.feature.has_value()) {
            update_measurement_result();
            return false;
        }
        else {
            if (m_selected_features.second.feature.has_value()) {
                reset_feature2();
            }
            else {
                reset_feature1();
            }
        }
    }
    return true;
}

bool GLGizmoMeasure::on_init()
{
    m_shortcut_key = WXK_CONTROL_U;

    m_desc["feature_selection"]         = _L("Select feature");
    m_desc["point_selection_caption"]   = _L("Shift + Left mouse button");
    m_desc["point_selection"]           = _L("Select point");
    m_desc["reset_caption"]             = _L("Delete");
    m_desc["reset"]                     = _L("Restart selection");
    m_desc["unselect_caption"]          = _L("Esc");
    m_desc["unselect"]                  = _L("Cancel a feature until exit");

    return true;
}

void GLGizmoMeasure::on_set_state()
{
    if (m_state == Off) {
        m_parent.toggle_selected_volume_visibility(false);
        m_shift_kar_filter.reset_count();
        m_curr_feature.reset();
        m_curr_point_on_feature_position.reset();
        restore_scene_raycasters_state();
        m_editing_distance = false;
        m_is_editing_distance_first_frame = true;
        std::map<GLVolume *, std::shared_ptr<Measure::Measuring>>().swap(m_mesh_measure_map);
    }
    else {
        m_mode = EMode::FeatureSelection;
        m_hover_id = -1;
        m_show_reset_first_tip = false;
        m_distance             = Vec3d::Zero();
    }
}

std::string GLGizmoMeasure::on_get_name() const
{
    if (!on_is_activable() && m_state == EState::Off) {
        return _u8L("Measure") + ":\n" + _u8L("Please select at least one object.");
    } else {
        return _u8L("Measure");
    }
}

bool GLGizmoMeasure::on_is_activable() const
{
    const Selection& selection = m_parent.get_selection();
    return selection.volumes_count()>0;
}

void GLGizmoMeasure::init_circle_glmodel(GripperType gripper_type, const Measure::SurfaceFeature &feature, CircleGLModel &circle_gl_model,float inv_zoom)
{
    const auto [center, radius, normal] = feature.get_circle();
    auto cur_feature =const_cast<Measure::SurfaceFeature *>(&feature);
    if (circle_gl_model.inv_zoom != inv_zoom) {
        if (circle_gl_model.last_circle_feature) {
            const auto [last_center, last_radius, last_normal] = circle_gl_model.last_circle_feature->get_circle();
            float eps    = 1e-2;
            if ((last_center - center).norm() < eps && (last_normal - normal).norm() < eps && abs(last_radius - radius) < eps){
                return;
            }
        }
        reset_gripper_pick(gripper_type);
        circle_gl_model.circle              = init_torus_data(64, 16, center.cast<float>(), float(radius), 5.0f * inv_zoom, normal.cast<float>(), Transform3f::Identity());
        if (circle_gl_model.circle) {
            if (auto mesh = circle_gl_model.circle->mesh) {
                m_gripper_id_raycast_map[gripper_type] = std::make_shared<PickRaycaster>(m_curr_circle.circle->mesh, CIRCLE_ID);
            }
        }
        circle_gl_model.last_circle_feature = cur_feature;
        circle_gl_model.inv_zoom            = inv_zoom;
    }
}

void GLGizmoMeasure::init_plane_glmodel(GripperType gripper_type, const Measure::SurfaceFeature &feature, PlaneGLModel &plane_gl_model)
{
    const auto &[idx, normal, pt] = feature.get_plane();
    if (plane_gl_model.plane_idx != idx) {
        plane_gl_model.plane.reset();
    }
    if (!plane_gl_model.plane) {
        plane_gl_model.plane_idx    = idx;
        reset_gripper_pick(gripper_type);
        plane_gl_model.plane     = init_plane_data(*feature.mesh, *feature.plane_indices);
        if (plane_gl_model.plane) {
            if (auto mesh = plane_gl_model.plane->mesh) {
                m_gripper_id_raycast_map[gripper_type] = std::make_shared<PickRaycaster>(mesh, PLANE_ID);
                m_gripper_id_raycast_map[gripper_type]->set_transform(feature.world_tran);
            }
        }
    }
}

void GLGizmoMeasure::on_render()
{
#if ENABLE_MEASURE_GIZMO_DEBUG
    render_debug_dialog();
#endif // ENABLE_MEASURE_GIZMO_DEBUG

    Vec2d mouse_position = m_parent.get_local_mouse_position();
    update_if_needed();

    const Camera& camera = wxGetApp().plater()->get_camera();
    const float inv_zoom = (float)camera.get_inv_zoom();
    bool mouse_on_gripper = false;
    bool mouse_on_object =false;
    {
        if (!m_editing_distance) {
            Vec3f  hit                          = Vec3f::Zero();
            double closest_hit_squared_distance = std::numeric_limits<double>::max();
            for (auto item : m_gripper_id_raycast_map) {

                auto  world_tran = item.second->world_tran;
                Vec3f normal_on_gripper;
                if (item.second->mesh_raycaster->closest_hit(mouse_position, item.second->world_tran.get_matrix(), camera, hit, normal_on_gripper)) {
                    double hit_squared_distance = (camera.get_position() - world_tran.get_matrix() * hit.cast<double>()).squaredNorm();
                    if (hit_squared_distance < closest_hit_squared_distance) {
                        closest_hit_squared_distance = hit_squared_distance;
                        if (item.second->get_id() > 0) {
                            m_hover_id       = item.second->get_id();
                            mouse_on_gripper = true;
                        }
                    }
                }
            }
        }
    }
    Vec3d  position_on_model;
    Vec3d  direction_on_model;
    size_t model_facet_idx = -1;
    double closest_hit_distance = std::numeric_limits<double>::max();
    {
        if (!m_editing_distance) {
            for (auto item : m_mesh_raycaster_map) {
                auto   raycaster                    = item.second->mesh_raycaster;
                auto   world_tran                   = item.second->world_tran;
                Vec3f  normal                       = Vec3f::Zero();
                Vec3f  hit                          = Vec3f::Zero();
                size_t facet                        = 0;
                if (raycaster->unproject_on_mesh(mouse_position, world_tran.get_matrix(), camera, hit, normal, nullptr, &facet)) {
                    // Is this hit the closest to the camera so far?
                    double hit_squared_distance = (camera.get_position() - world_tran.get_matrix() * hit.cast<double>()).norm();
                    if (hit_squared_distance < closest_hit_distance) {
                        closest_hit_distance = hit_squared_distance;
                        mouse_on_object   = true;
                        model_facet_idx   = facet;
                        position_on_model = hit.cast<double>();
                        m_last_hit_volume = item.first;
                        m_curr_measuring  = m_mesh_measure_map[m_last_hit_volume];
                    }
                }
            }
        }

        if (!(mouse_on_gripper || mouse_on_object)) {
            m_hover_id = -1;
            m_last_plane_idx = -1;
            reset_gripper_pick(GripperType::PLANE);
            reset_gripper_pick(GripperType::CIRCLE);
        }
        if (m_mouse_left_down_mesh_deal && m_hover_id >= 0) {
            m_mouse_left_down_mesh_deal = false;
            if (m_selected_features.second.feature.has_value()) {
                if (m_hit_order_volumes.size() == 1) {
                    m_hit_order_volumes.push_back(m_last_hit_volume);
                } else {
                    m_hit_order_volumes[1] = m_last_hit_volume;
                }
                // deal hit_different_volumes
                if (m_hit_different_volumes.size() >= 1) {
                    if (m_last_hit_volume == m_hit_different_volumes[0]) {
                        //do nothing
                    } else  {
                        if (m_hit_different_volumes.size() == 2) {
                            m_hit_different_volumes[1] = m_last_hit_volume;
                        } else {
                            m_hit_different_volumes.push_back(m_last_hit_volume);
                        }
                    }
                }
            }
            else if (m_selected_features.first.feature.has_value()) {
                if (m_hit_order_volumes.size() == 0) {
                    m_hit_order_volumes.push_back(m_last_hit_volume);
                } else {
                    m_hit_order_volumes[0] = m_last_hit_volume;
                }
                //deal hit_different_volumes
                if (m_hit_different_volumes.size() == 0) {
                    m_hit_different_volumes.push_back(m_last_hit_volume);
                }
            }
        }
    }
    //const bool mouse_on_object = m_raycaster->unproject_on_mesh(mouse_position, Transform3d::Identity(), camera, position_on_model, normal_on_model, nullptr, &model_facet_idx);
    const bool is_hovering_on_feature = m_mode == EMode::PointSelection && m_hover_id != -1;

    if (m_mode == EMode::FeatureSelection || m_mode == EMode::PointSelection) {
        if (m_hover_id == SEL_SPHERE_1_ID || m_hover_id == SEL_SPHERE_2_ID) {
            // Skip feature detection if hovering on a selected point/center
            //reset_gripper_pick(GripperType::UNDEFINE, true);
            m_curr_feature.reset();
            m_curr_point_on_feature_position.reset();
        }
        else {
            std::optional<Measure::SurfaceFeature> curr_feature = std::nullopt;
            if (m_curr_measuring) {
                curr_feature = wxGetMouseState().LeftIsDown() ? m_curr_feature :
                               mouse_on_object ? m_curr_measuring->get_feature(model_facet_idx, position_on_model, m_mesh_raycaster_map[m_last_hit_volume]->world_tran.get_matrix()) :
                                                              std::nullopt;
            }

            if (m_curr_feature != curr_feature ||
                (curr_feature.has_value() && curr_feature->get_type() == Measure::SurfaceFeatureType::Circle && (m_curr_feature != curr_feature || m_last_inv_zoom != inv_zoom))) {
                reset_gripper_pick(GripperType::UNDEFINE, true);

                m_curr_feature = curr_feature;
                auto mesh  = const_cast<TriangleMesh *>(m_last_hit_volume->ori_mesh);
                m_curr_feature->mesh = &mesh->its;
                m_curr_feature->world_tran = m_mesh_raycaster_map[m_last_hit_volume]->world_tran.get_matrix();
                if (!m_curr_feature.has_value())
                    return;

                switch (m_curr_feature->get_type()) {
                default: { assert(false); break; }
                case Measure::SurfaceFeatureType::Point:
                {
                    if (m_sphere) {
                        if (auto mesh = m_sphere->mesh) {
                            m_gripper_id_raycast_map[GripperType::POINT] = std::make_shared<PickRaycaster>(mesh, POINT_ID);
                        }
                    }
                    break;
                }
                case Measure::SurfaceFeatureType::Edge:
                {
                    if (m_cylinder) {
                        if (auto mesh = m_cylinder->mesh) {
                            m_gripper_id_raycast_map[GripperType::EDGE] = std::make_shared<PickRaycaster>(mesh, EDGE_ID);
                        }
                    }
                    break;
                }
                case Measure::SurfaceFeatureType::Circle: {
                    m_curr_circle.last_circle_feature = nullptr;
                    m_curr_circle.inv_zoom            = 0;
                    init_circle_glmodel(GripperType::CIRCLE, *m_curr_feature, m_curr_circle,inv_zoom);
                    break;
                }
                case Measure::SurfaceFeatureType::Plane: {
                    update_world_plane_features(m_curr_measuring.get(), *m_curr_feature);
                    m_curr_plane.plane_idx = -1;
                    init_plane_glmodel(GripperType::PLANE, *m_curr_feature, m_curr_plane);
                    break;
                }
                }
            }
        }
    }

    if (m_mode != EMode::PointSelection) {
        m_curr_point_on_feature_position.reset();
    }
    else if (is_hovering_on_feature) {
        auto position_on_feature = [this, &mouse_position](int feature_type_id, const Camera &camera, std::function<Vec3f(const Vec3f &)> callback = nullptr) -> Vec3d {
            auto it = m_gripper_id_raycast_map.find((GripperType) feature_type_id); // m_raycasters.find(feature_type_id);
            if (it != m_gripper_id_raycast_map.end() && it->second != nullptr) {
                Vec3f p;
                Vec3f n;
                const Transform3d& trafo = it->second->world_tran.get_matrix();
                bool               res   = it->second->mesh_raycaster->closest_hit(mouse_position, trafo, camera, p, n);
                if (res) {
                    if (callback)
                        p = callback(p);
                    return trafo * p.cast<double>();
                }
            }
            return Vec3d(DBL_MAX, DBL_MAX, DBL_MAX);
        };
        if (m_curr_feature.has_value()) {
            switch (m_curr_feature->get_type())
            {
            default: { assert(false); break; }
            case Measure::SurfaceFeatureType::Point:
            {
                m_curr_point_on_feature_position = m_curr_feature->get_point();
                break;
            }
            case Measure::SurfaceFeatureType::Edge:
            {
                const std::optional<Vec3d> extra = m_curr_feature->get_extra_point();
                if (extra.has_value() && m_hover_id == GripperType::POINT)
                    m_curr_point_on_feature_position = *extra;
                else {
                    const Vec3d pos = position_on_feature(GripperType::EDGE, camera, [](const Vec3f &v) { return Vec3f(0.0f, 0.0f, v.z()); });
                    if (!pos.isApprox(Vec3d(DBL_MAX, DBL_MAX, DBL_MAX)))
                        m_curr_point_on_feature_position = pos;
                }
                break;
            }
            case Measure::SurfaceFeatureType::Plane:
            {
                m_curr_point_on_feature_position = position_on_feature(GripperType::PLANE, camera);
                break;
            }
            case Measure::SurfaceFeatureType::Circle:
            {
                const auto [center, radius, normal] = m_curr_feature->get_circle();
                if (m_hover_id == POINT_ID)
                    m_curr_point_on_feature_position = center;
                else {
                    const Vec3d world_pof = position_on_feature(GripperType::CIRCLE, camera, [](const Vec3f &v) { return v; });
                    const Eigen::Hyperplane<double, 3> plane(normal, center);
                    const Transform3d local_to_model_matrix = Geometry::translation_transform(center) * Eigen::Quaternion<double>::FromTwoVectors(Vec3d::UnitZ(), normal);
                    const Vec3d local_proj = local_to_model_matrix.inverse() * plane.projection(world_pof);
                    double angle = std::atan2(local_proj.y(), local_proj.x());
                    if (angle < 0.0)
                        angle += 2.0 * double(M_PI);

                    const Vec3d local_pos = radius * Vec3d(std::cos(angle), std::sin(angle), 0.0);
                    m_curr_point_on_feature_position = local_to_model_matrix * local_pos;
                }
                break;
            }
            }
        }
    }
    else {
        m_curr_point_on_feature_position.reset();
    }

    if (!m_curr_feature.has_value() && !m_selected_features.first.feature.has_value()) {
        return;
    }

    const bool old_cullface = ::glIsEnabled(GL_CULL_FACE);

    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_CULL_FACE));

    auto render_feature = [this](const Measure::SurfaceFeature& feature, const std::vector<ColorRGBA>& colors,
        float inv_zoom, bool hover, bool update_raycasters_transform,int featura_index = -1) {
            switch (feature.get_type())
            {
            default: { assert(false); break; }
            case Measure::SurfaceFeatureType::Point:
            {
                const Transform3d feature_matrix =  Geometry::translation_transform(feature.get_point()) * Geometry::scale_transform(inv_zoom);
                Geometry::Transformation tran(feature_matrix);
                render_glmodel(*m_sphere, colors.front().get_data(), tran.get_matrix(), false, hover ? 0.5f : 0.25f);
                if (update_raycasters_transform) {
                    auto it = m_gripper_id_raycast_map.find(GripperType::POINT);
                    if (it != m_gripper_id_raycast_map.end() && it->second != nullptr)
                        it->second->set_transform(feature_matrix);
                }
                break;
            }
            case Measure::SurfaceFeatureType::Circle:
            {
                const auto& [center, radius, normal] = feature.get_circle();
                // render circle
                const Transform3d circle_matrix =  Transform3d::Identity();
                //set_matrix_uniforms(circle_matrix);
                Geometry::Transformation tran(circle_matrix);
                if (featura_index == -1) {
                    init_circle_glmodel(GripperType::CIRCLE, feature, m_curr_circle, inv_zoom);
                    render_glmodel(*m_curr_circle.circle, colors.front().get_data(), tran.get_matrix(), false, hover ? 0.5f : 0.25f);
                }
                // render plane feature1 or feature2
                if (featura_index == 0) { // feature1
                    init_circle_glmodel(GripperType::CIRCLE_1, feature, m_feature_circle_first, inv_zoom);
                    render_glmodel(*m_feature_circle_first.circle, colors.front().get_data(), tran.get_matrix(), false, hover ? 0.5f : 0.25f);
                } else if (featura_index == 1) { // feature2
                    init_circle_glmodel(GripperType::CIRCLE_2, feature, m_feature_circle_second, inv_zoom);
                    render_glmodel(*m_feature_circle_second.circle, colors.front().get_data(), tran.get_matrix(), false, hover ? 0.5f : 0.25f);
                }
                // render center
                if (colors.size() > 1) {
                    const Transform3d center_matrix = Geometry::translation_transform(center) * Geometry::scale_transform(inv_zoom);
                    render_glmodel(*m_sphere, colors.back().get_data(), center_matrix, false, hover ? 0.5f : 0.25f);

                    Geometry::Transformation tran(center_matrix);
                    auto it = m_gripper_id_raycast_map.find(GripperType::POINT);
                    if (it != m_gripper_id_raycast_map.end() && it->second != nullptr)
                        it->second->set_transform(center_matrix);
                }
                break;
            }
            case Measure::SurfaceFeatureType::Edge:
            {
                const auto& [from, to] = feature.get_edge();
                // render edge
                const Transform3d edge_matrix =  Geometry::translation_transform(from) *
                    Eigen::Quaternion<double>::FromTwoVectors(Vec3d::UnitZ(), to - from) *
                    Geometry::scale_transform({ (double)inv_zoom, (double)inv_zoom, (to - from).norm() });

                Geometry::Transformation tran(edge_matrix);
                render_glmodel(*m_cylinder, colors.front().get_data(), tran.get_matrix(), false, hover ? 0.5f : 0.25f);
                if (update_raycasters_transform) {
                    auto it = m_gripper_id_raycast_map.find(GripperType::EDGE);
                    if (it != m_gripper_id_raycast_map.end() && it->second != nullptr){
                        it->second->set_transform(edge_matrix);
                    }
                }
                // render extra point
                if (colors.size() > 1) {
                    const std::optional<Vec3d> extra = feature.get_extra_point();
                    if (extra.has_value()) {
                        const Transform3d point_matrix = Geometry::translation_transform(*extra) * Geometry::scale_transform(inv_zoom);
                        Geometry::Transformation tran(point_matrix);
                        render_glmodel(*m_sphere, colors.back().get_data(), tran.get_matrix(), false, hover ? 0.5f : 0.25f);

                        auto it = m_gripper_id_raycast_map.find(GripperType::POINT);
                        if (it != m_gripper_id_raycast_map.end() && it->second != nullptr) {
                            it->second->set_transform(point_matrix);
                        }
                    }
                }
                break;
            }
            case Measure::SurfaceFeatureType::Plane: {
                if (featura_index == -1) {
                    render_glmodel(*m_curr_plane.plane, colors.back().get_data(), feature.world_tran, false, hover ? 0.5f : 0.25f);
                    break;
                }
                //render plane feature1 or feature2
                if (featura_index == 0) {//feature1
                    init_plane_glmodel(GripperType::PLANE_1, feature, m_feature_plane_first);
                    render_glmodel(*m_feature_plane_first.plane, colors.back().get_data(), feature.world_tran, false, hover ? 0.5f : 0.25f);
                } else if (featura_index == 1) {//feature2
                    init_plane_glmodel(GripperType::PLANE_2, feature, m_feature_plane_second);
                    render_glmodel(*m_feature_plane_second.plane, colors.back().get_data(), feature.world_tran, false, hover ? 0.5f : 0.25f);
                }
                break;
            }
            }
    };

    auto hover_selection_color = [this]() {
      return ((m_mode == EMode::PointSelection && !m_selected_features.first.feature.has_value()) ||
              (m_mode != EMode::PointSelection && (!m_selected_features.first.feature.has_value() || *m_curr_feature == *m_selected_features.first.feature))) ?
          SELECTED_1ST_COLOR : SELECTED_2ND_COLOR;
    };

    auto hovering_color = [this, hover_selection_color]() {
        return (m_mode == EMode::PointSelection) ? HOVER_COLOR : hover_selection_color();
    };

    if (m_curr_feature.has_value()) {
        // render hovered feature
        std::vector<ColorRGBA> colors;
        if (m_selected_features.first.feature.has_value() && *m_curr_feature == *m_selected_features.first.feature) {
            // hovering over the 1st selected feature
            if (m_selected_features.first.is_center)
                // hovering over a center
                colors = { NEUTRAL_COLOR, hovering_color() };
            else if (is_feature_with_center(*m_selected_features.first.feature))
                // hovering over a feature with center
                colors = { hovering_color(), NEUTRAL_COLOR };
            else
                colors = { hovering_color() };
        }
        else if (m_selected_features.second.feature.has_value() && *m_curr_feature == *m_selected_features.second.feature) {
            // hovering over the 2nd selected feature
            if (m_selected_features.second.is_center)
                // hovering over a center
                colors = { NEUTRAL_COLOR, hovering_color() };
            else if (is_feature_with_center(*m_selected_features.second.feature))
                // hovering over a feature with center
                colors = { hovering_color(), NEUTRAL_COLOR };
            else
                colors = { hovering_color() };
        }
        else {
            switch (m_curr_feature->get_type())
            {
            default: { assert(false); break; }
            case Measure::SurfaceFeatureType::Point:
            {
                colors.emplace_back(hover_selection_color());
                break;
            }
            case Measure::SurfaceFeatureType::Edge:
            case Measure::SurfaceFeatureType::Circle:
            {
                if (m_selected_features.first.is_center && m_curr_feature == m_selected_features.first.source)
                    colors = { SELECTED_1ST_COLOR, NEUTRAL_COLOR };
                else if (m_selected_features.second.is_center && m_curr_feature == m_selected_features.second.source)
                    colors = { SELECTED_2ND_COLOR, NEUTRAL_COLOR };
                else
                    colors = { hovering_color(), hovering_color() };
                break;
            }
            case Measure::SurfaceFeatureType::Plane:
            {
                colors.emplace_back(hovering_color());
                break;
            }
            }
        }

        render_feature(*m_curr_feature, colors, inv_zoom, true, true);
    }

    if (m_selected_features.first.feature.has_value() && (!m_curr_feature.has_value() || *m_curr_feature != *m_selected_features.first.feature)) {
        // render 1st selected feature

        std::optional<Measure::SurfaceFeature> feature_to_render;
        std::vector<ColorRGBA> colors;
        bool requires_raycaster_update = false;
        if (m_hover_id == SEL_SPHERE_1_ID && (m_selected_features.first.is_center || is_feature_with_center(*m_selected_features.first.feature))) {
            // hovering over a center
            feature_to_render = m_selected_features.first.source;
            colors = { NEUTRAL_COLOR, SELECTED_1ST_COLOR };
            requires_raycaster_update = true;
        }
        else if (is_feature_with_center(*m_selected_features.first.feature)) {
            // hovering over a feature with center
            feature_to_render = m_selected_features.first.feature;
            colors = { SELECTED_1ST_COLOR, NEUTRAL_COLOR };
            requires_raycaster_update = true;
        }
        else {
            feature_to_render = m_selected_features.first.feature;
            colors = { SELECTED_1ST_COLOR };
            requires_raycaster_update = m_selected_features.first.feature->get_type() == Measure::SurfaceFeatureType::Point;
        }

        render_feature(*feature_to_render, colors, inv_zoom, m_hover_id == SEL_SPHERE_1_ID, false, 0);

        if (requires_raycaster_update) {
            if (m_gripper_id_raycast_map.find(GripperType::SPHERE_1) != m_gripper_id_raycast_map.end()) {
                m_gripper_id_raycast_map[GripperType::SPHERE_1]->set_transform(Geometry::translation_transform(get_feature_offset(*m_selected_features.first.feature)) *
                                                                               Geometry::scale_transform(inv_zoom));
            }
        }
    }

    if (m_selected_features.second.feature.has_value() && (!m_curr_feature.has_value() || *m_curr_feature != *m_selected_features.second.feature)) {
        // render 2nd selected feature

        std::optional<Measure::SurfaceFeature> feature_to_render;
        std::vector<ColorRGBA> colors;
        bool requires_raycaster_update = false;
        if (m_hover_id == SEL_SPHERE_2_ID && (m_selected_features.second.is_center || is_feature_with_center(*m_selected_features.second.feature))) {
            // hovering over a center
            feature_to_render = m_selected_features.second.source;
            colors = { NEUTRAL_COLOR, SELECTED_2ND_COLOR };
            requires_raycaster_update = true;
        }
        else if (is_feature_with_center(*m_selected_features.second.feature)) {
            // hovering over a feature with center
            feature_to_render = m_selected_features.second.feature;
            colors = { SELECTED_2ND_COLOR, NEUTRAL_COLOR };
            requires_raycaster_update = true;
        }
        else {
            feature_to_render = m_selected_features.second.feature;
            colors = { SELECTED_2ND_COLOR };
            requires_raycaster_update = m_selected_features.second.feature->get_type() == Measure::SurfaceFeatureType::Point;
        }

        render_feature(*feature_to_render, colors, inv_zoom, m_hover_id == SEL_SPHERE_2_ID, false, 1);

        if (requires_raycaster_update) {
            if (m_gripper_id_raycast_map.find(GripperType::SPHERE_2) != m_gripper_id_raycast_map.end()) {
                m_gripper_id_raycast_map[GripperType::SPHERE_2]->set_transform(Geometry::translation_transform(get_feature_offset(*m_selected_features.first.feature)) *
                                                                               Geometry::scale_transform(inv_zoom));
            }
        }
    }

    if (is_hovering_on_feature && m_curr_point_on_feature_position.has_value()) {
        if (m_hover_id != POINT_ID) {
            // render point on feature while SHIFT is pressed
            const Transform3d matrix = Geometry::translation_transform(*m_curr_point_on_feature_position) * Geometry::scale_transform(inv_zoom);
            const ColorRGBA color = hover_selection_color();

            Geometry::Transformation tran(matrix);
            render_glmodel(*m_sphere, color.get_data(), tran.get_matrix(), false, 0.5f);
        }
    }

    if (old_cullface)
        glsafe(::glEnable(GL_CULL_FACE));

    render_dimensioning();
}

void GLGizmoMeasure::update_if_needed()
{
    const Selection& selection = m_parent.get_selection();
    if (selection.is_empty())
        return;

    const Selection::IndicesList& idxs = selection.get_volume_idxs();
    std::vector<VolumeCacheItem> volumes_cache;
    volumes_cache.reserve(idxs.size());
    for (unsigned int idx : idxs) {
        const GLVolume* v = selection.get_volume(idx);
        const int volume_idx = v->volume_idx();
        if (volume_idx < 0)
            continue;

        const ModelObject* obj = selection.get_model()->objects[v->object_idx()];
        const ModelInstance* inst = obj->instances[v->instance_idx()];
        const ModelVolume* vol = obj->volumes[volume_idx];
        const VolumeCacheItem item = {
            obj, inst, vol,
            Geometry::translation_transform(selection.get_first_volume()->get_sla_shift_z() * Vec3d::UnitZ()) * inst->get_matrix() * vol->get_matrix()
        };
        volumes_cache.emplace_back(item);
    }

    if (m_state != On || volumes_cache.empty())
        return;

    if (m_volumes_cache != volumes_cache) {
        m_volumes_cache = volumes_cache;
    }
}

void GLGizmoMeasure::disable_scene_raycasters()
{
    for(auto iter:m_gripper_id_raycast_map)
    {
        iter.second->set_active(false);
    }
}

void GLGizmoMeasure::restore_scene_raycasters_state()
{
    for(auto iter:m_gripper_id_raycast_map)
    {
        iter.second->set_active(true);
    }
}


void GLGizmoMeasure::render_dimensioning()
{
    static SelectedFeatures last_selected_features;

    if (!m_selected_features.first.feature.has_value())
        return;

    if (!m_selected_features.second.feature.has_value() && m_selected_features.first.feature->get_type() != Measure::SurfaceFeatureType::Circle)
        return;

    GLShaderProgram* shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    auto point_point = [this, &shader](const Vec3d &v1, const Vec3d &v2, float distance, const std::array<float, 4> &color, bool show_label = true, bool show_first_tri = true) {
        if ((v2 - v1).squaredNorm() < 0.000001 || abs(distance) < 0.001f)
            return;

        const Camera& camera = wxGetApp().plater()->get_camera();
        const Matrix4d projection_view_matrix = camera.get_projection_matrix().matrix() * camera.get_view_matrix().matrix();
        const std::array<int, 4>& viewport = camera.get_viewport();

        // screen coordinates
        const Vec2d v1ss = TransformHelper::world_to_ss(v1, projection_view_matrix, viewport);
        const Vec2d v2ss = TransformHelper::world_to_ss(v2, projection_view_matrix, viewport);

        if (v1ss.isApprox(v2ss))
            return;

        const Vec2d v12ss = v2ss - v1ss;
        const double v12ss_len = v12ss.norm();

        const bool overlap = v12ss_len - 2.0 * TRIANGLE_HEIGHT < 0.0;

        const auto q12ss = Eigen::Quaternion<double>::FromTwoVectors(Vec3d::UnitX(), Vec3d(v12ss.x(), v12ss.y(), 0.0));
        const auto q21ss = Eigen::Quaternion<double>::FromTwoVectors(Vec3d::UnitX(), Vec3d(-v12ss.x(), -v12ss.y(), 0.0));

        shader->set_uniform("projection_matrix", Transform3d::Identity());

        const Vec3d v1ss_3 = { v1ss.x(), v1ss.y(), 0.0 };
        const Vec3d v2ss_3 = { v2ss.x(), v2ss.y(), 0.0 };

        const Transform3d ss_to_ndc_matrix = TransformHelper::ndc_to_ss_matrix_inverse(viewport);

#if ENABLE_GL_CORE_PROFILE
        if (OpenGLManager::get_gl_info().is_core_profile()) {
            shader->stop_using();

            shader = wxGetApp().get_shader("dashed_thick_lines");
            if (shader == nullptr)
                return;

            shader->start_using();
            shader->set_uniform("projection_matrix", Transform3d::Identity());
            const std::array<int, 4>& viewport = camera.get_viewport();
            shader->set_uniform("viewport_size", Vec2d(double(viewport[2]), double(viewport[3])));
            shader->set_uniform("width", 1.0f);
            shader->set_uniform("gap_size", 0.0f);
        }
        else
#endif // ENABLE_GL_CORE_PROFILE
            glsafe(::glLineWidth(2.0f));
        // stem
        shader->set_uniform("view_model_matrix", overlap ?
            ss_to_ndc_matrix * Geometry::translation_transform(v1ss_3) * q12ss * Geometry::translation_transform(-2.0 * TRIANGLE_HEIGHT * Vec3d::UnitX()) * Geometry::scale_transform({ v12ss_len + 4.0 * TRIANGLE_HEIGHT, 1.0f, 1.0f }) :
            ss_to_ndc_matrix * Geometry::translation_transform(v1ss_3) * q12ss * Geometry::scale_transform({ v12ss_len, 1.0f, 1.0f }));
        m_dimensioning.line.set_color(-1, color);
        m_dimensioning.line.render();

#if ENABLE_GL_CORE_PROFILE
        if (OpenGLManager::get_gl_info().is_core_profile()) {
            shader->stop_using();

            shader = wxGetApp().get_shader("flat");
            if (shader == nullptr)
                return;

            shader->start_using();
        }
        else
#endif // ENABLE_GL_CORE_PROFILE
            glsafe(::glLineWidth(1.0f));

        // arrow 1
        if (show_first_tri) {
            shader->set_uniform("view_model_matrix", overlap ?
                ss_to_ndc_matrix * Geometry::translation_transform(v1ss_3) * q12ss :
                ss_to_ndc_matrix * Geometry::translation_transform(v1ss_3) * q21ss);
            m_dimensioning.triangle.render();
        }
        // arrow 2
        shader->set_uniform("view_model_matrix", overlap ?
            ss_to_ndc_matrix * Geometry::translation_transform(v2ss_3) * q21ss :
            ss_to_ndc_matrix * Geometry::translation_transform(v2ss_3) * q12ss);
        m_dimensioning.triangle.render();

        const bool use_inches = wxGetApp().app_config->get("use_inches") == "1";
        const double curr_value = use_inches ? GizmoObjectManipulation::mm_to_in * distance : distance;
        const std::string curr_value_str = format_double(curr_value);
        const std::string units = use_inches ? _u8L("in") : _u8L("mm");
        const float value_str_width = 20.0f + ImGui::CalcTextSize(curr_value_str.c_str()).x;
        static double edit_value = 0.0;

        ImGuiWrapper::push_common_window_style(m_parent.get_scale());
        const Vec2d label_position = 0.5 * (v1ss + v2ss);
        m_imgui->set_next_window_pos(label_position.x(), viewport[3] - label_position.y(), ImGuiCond_Always, 0.0f, 1.0f);
        m_imgui->set_next_window_bg_alpha(0.0f);

        if (!m_editing_distance && show_label) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 1.0f, 1.0f });
            m_imgui->begin(std::string("distance"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration);
            ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
            ImGui::AlignTextToFramePadding();
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const std::string txt = curr_value_str + " " + units;
            ImVec2 txt_size = ImGui::CalcTextSize(txt.c_str());
            const ImGuiStyle& style = ImGui::GetStyle();
            draw_list->AddRectFilled({pos.x - style.FramePadding.x, pos.y + style.FramePadding.y},
                                     {pos.x + txt_size.x + 2.0f * style.FramePadding.x, pos.y + txt_size.y + 2.0f * style.FramePadding.y},
                                     ImGuiWrapper::to_ImU32({1.0f, 1.0f, 1.0f, 0.5f}));
            ImGui::SetCursorScreenPos({ pos.x + style.FramePadding.x, pos.y });
            m_imgui->text(txt);
            if (m_hit_different_volumes.size() < 2) {
                ImGui::SameLine();
                if (m_imgui->image_button(ImGui::SliderFloatEditBtnIcon, _L("Edit to scale"))) {
                    m_editing_distance = true;
                    edit_value         = curr_value;
                    m_imgui->requires_extra_frame();
                }
            }
            m_imgui->end();
            ImGui::PopStyleVar(3);
        }

        if (m_editing_distance && !ImGui::IsPopupOpen("distance_popup"))
            ImGui::OpenPopup("distance_popup");

        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 1.0f, 1.0f });
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 4.0f, 0.0f });
        if (ImGui::BeginPopupModal("distance_popup", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration)) {
            auto perform_scale = [this](double new_value, double old_value,bool scale_single_volume) {
                if (new_value == old_value || new_value <= 0.0)
                    return;

                const double ratio = new_value / old_value;
                wxGetApp().plater()->take_snapshot(_u8L("Scale"));
                // apply scale
                TransformationType type;
                type.set_world();
                type.set_relative();
                type.set_joint();

                // scale selection
                Selection& selection = m_parent.get_selection();
                selection.setup_cache();
                if (scale_single_volume && m_hit_different_volumes.size()==1) {
                    //todo//update_single_mesh_pick(m_hit_different_volumes[0])
                } else {
                    selection.scale(ratio * Vec3d::Ones(), type);
                    wxGetApp().plater()->canvas3D()->do_scale(""); // avoid storing another snapshot
                    register_single_mesh_pick();
                }
                wxGetApp().obj_manipul()->set_dirty();
                // scale dimensioning
                update_feature_by_tran(*m_selected_features.first.feature);
                if (m_selected_features.second.feature.has_value())
                    update_feature_by_tran(*m_selected_features.second.feature);
                // update measure on next call to data_changed()
                m_pending_scale = true;

            };
            auto action_exit = [this]() {
                m_editing_distance = false;
                m_is_editing_distance_first_frame = true;
                ImGui::CloseCurrentPopup();
            };
            auto action_scale = [perform_scale, action_exit](double new_value, double old_value, bool scale_single_volume) {
                perform_scale(new_value, old_value, scale_single_volume);
                action_exit();
            };

            m_imgui->disable_background_fadeout_animation();
            ImGui::PushItemWidth(value_str_width);
            if (ImGui::InputDouble("##distance", &edit_value, 0.0f, 0.0f, "%.3f")) {
            }

            // trick to auto-select text in the input widgets on 1st frame
            if (m_is_editing_distance_first_frame) {
                ImGui::SetKeyboardFocusHere(0);
                m_is_editing_distance_first_frame = false;
                m_imgui->set_requires_extra_frame();
            }

            // handle keys input
            if (ImGui::IsKeyPressedMap(ImGuiKey_Enter) || ImGui::IsKeyPressedMap(ImGuiKey_KeyPadEnter))
                action_scale(edit_value, curr_value, false);
            else if (ImGui::IsKeyPressedMap(ImGuiKey_Escape))
                action_exit();

            ImGui::SameLine();
            ImGuiWrapper::push_confirm_button_style();
            //if (m_imgui->button(_CTX(L_CONTEXT("Scale part", "Verb"), "Verb")))
                //action_scale(edit_value, curr_value, true);
            //ImGui::SameLine();
            if (m_imgui->button(_CTX(L_CONTEXT("Scale all", "Verb"), "Verb")))
                action_scale(edit_value, curr_value, false);
            ImGuiWrapper::pop_confirm_button_style();
            ImGui::SameLine();
            ImGuiWrapper::push_cancel_button_style();
            if (m_imgui->button(_L("Cancel")))
                action_exit();
            ImGuiWrapper::pop_cancel_button_style();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(4);
        ImGuiWrapper::pop_common_window_style();
    };

    auto point_edge = [this, shader](const Measure::SurfaceFeature& f1, const Measure::SurfaceFeature& f2) {
        assert(f1.get_type() == Measure::SurfaceFeatureType::Point && f2.get_type() == Measure::SurfaceFeatureType::Edge);
        std::pair<Vec3d, Vec3d> e = f2.get_edge();
        const Vec3d v_proj = m_measurement_result.distance_infinite->to;
        const Vec3d e1e2 = e.second - e.first;
        const Vec3d v_proje1 = v_proj - e.first;
        const bool on_e1_side = v_proje1.dot(e1e2) < -EPSILON;
        const bool on_e2_side = !on_e1_side && v_proje1.norm() > e1e2.norm();
        if (on_e1_side || on_e2_side) {
            const Camera& camera = wxGetApp().plater()->get_camera();
            const Matrix4d projection_view_matrix = camera.get_projection_matrix().matrix() * camera.get_view_matrix().matrix();
            const std::array<int, 4>& viewport = camera.get_viewport();
            const Transform3d ss_to_ndc_matrix = TransformHelper::ndc_to_ss_matrix_inverse(viewport);

            const Vec2d v_projss = TransformHelper::world_to_ss(v_proj, projection_view_matrix, viewport);
            auto render_extension = [this, &v_projss, &projection_view_matrix, &viewport, &ss_to_ndc_matrix, shader](const Vec3d& p) {
                const Vec2d pss = TransformHelper::world_to_ss(p, projection_view_matrix, viewport);
                if (!pss.isApprox(v_projss)) {
                    const Vec2d pv_projss = v_projss - pss;
                    const double pv_projss_len = pv_projss.norm();

                    const auto q = Eigen::Quaternion<double>::FromTwoVectors(Vec3d::UnitX(), Vec3d(pv_projss.x(), pv_projss.y(), 0.0));

                    shader->set_uniform("projection_matrix", Transform3d::Identity());
                    shader->set_uniform("view_model_matrix", ss_to_ndc_matrix * Geometry::translation_transform({ pss.x(), pss.y(), 0.0 }) * q *
                        Geometry::scale_transform({ pv_projss_len, 1.0f, 1.0f }));
                    m_dimensioning.line.set_color(-1, ColorRGBA::LIGHT_GRAY().get_data());
                    m_dimensioning.line.render();
                }
            };

            render_extension(on_e1_side ? e.first : e.second);
        }
    };

    auto arc_edge_edge = [this, &shader](const Measure::SurfaceFeature& f1, const Measure::SurfaceFeature& f2, double radius = 0.0) {
        assert(f1.get_type() == Measure::SurfaceFeatureType::Edge && f2.get_type() == Measure::SurfaceFeatureType::Edge);
        if (!m_measurement_result.angle.has_value())
            return;

        const double angle = m_measurement_result.angle->angle;
        const Vec3d  center = m_measurement_result.angle->center;
        const std::pair<Vec3d, Vec3d> e1 = m_measurement_result.angle->e1;
        const std::pair<Vec3d, Vec3d> e2 = m_measurement_result.angle->e2;
        const double calc_radius = m_measurement_result.angle->radius;
        const bool   coplanar = m_measurement_result.angle->coplanar;

        if (std::abs(angle) < EPSILON || std::abs(calc_radius) < EPSILON)
            return;

        const double draw_radius = (radius > 0.0) ? radius : calc_radius;

        const Vec3d e1_unit = Measure::edge_direction(e1);
        const Vec3d e2_unit = Measure::edge_direction(e2);

        const unsigned int resolution = std::max<unsigned int>(2, 64 * angle / double(PI));
        const double step = angle / double(resolution);
        const Vec3d normal = e1_unit.cross(e2_unit).normalized();

        if (!m_dimensioning.arc.is_initialized()) {
            GLModel::Geometry init_data;
            init_data.format = {GLModel::PrimitiveType::LineStrip, GLModel::Geometry::EVertexLayout::P3};
            init_data.color  = ColorRGBA::WHITE();
            init_data.reserve_vertices(resolution + 1);
            init_data.reserve_indices(resolution + 1);

            // vertices + indices
            for (unsigned int i = 0; i <= resolution; ++i) {
                const double a = step * double(i);
                const Vec3d  v = draw_radius * (Eigen::Quaternion<double>(Eigen::AngleAxisd(a, normal)) * e1_unit);
                init_data.add_vertex((Vec3f) v.cast<float>());
                init_data.add_index(i);
            }

            m_dimensioning.arc.init_from(std::move(init_data));
        }

        const Camera& camera = wxGetApp().plater()->get_camera();
#if ENABLE_GL_CORE_PROFILE
        if (OpenGLManager::get_gl_info().is_core_profile()) {
            shader->stop_using();

            shader = wxGetApp().get_shader("dashed_thick_lines");
            if (shader == nullptr)
                return;

            shader->start_using();
            shader->set_uniform("projection_matrix", Transform3d::Identity());
            const std::array<int, 4>& viewport = camera.get_viewport();
            shader->set_uniform("viewport_size", Vec2d(double(viewport[2]), double(viewport[3])));
            shader->set_uniform("width", 1.0f);
            shader->set_uniform("gap_size", 0.0f);
        }
        else
#endif // ENABLE_GL_CORE_PROFILE
          glsafe(::glLineWidth(2.0f));

        // arc
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
        shader->set_uniform("view_model_matrix", camera.get_view_matrix() * Geometry::translation_transform(center));
        m_dimensioning.arc.render();

#if ENABLE_GL_CORE_PROFILE
        if (OpenGLManager::get_gl_info().is_core_profile()) {
            shader->stop_using();

            shader = wxGetApp().get_shader("flat");
            if (shader == nullptr)
                return;

            shader->start_using();
        }
        else
#endif // ENABLE_GL_CORE_PROFILE
          glsafe(::glLineWidth(1.0f));

        // arrows
        auto render_arrow = [this, shader, &camera, &normal, &center, &e1_unit, draw_radius, step, resolution](unsigned int endpoint_id) {
            const double angle = (endpoint_id == 1) ? 0.0 : step * double(resolution);
            const Vec3d position_model = Geometry::translation_transform(center) * (draw_radius * (Eigen::Quaternion<double>(Eigen::AngleAxisd(angle, normal)) * e1_unit));
            const Vec3d direction_model = (endpoint_id == 1) ? -normal.cross(position_model - center).normalized() : normal.cross(position_model - center).normalized();
            const auto qz = Eigen::Quaternion<double>::FromTwoVectors(Vec3d::UnitZ(), (endpoint_id == 1) ? normal : -normal);
            const auto qx = Eigen::Quaternion<double>::FromTwoVectors(qz * Vec3d::UnitX(), direction_model);
            const Transform3d view_model_matrix = camera.get_view_matrix() * Geometry::translation_transform(position_model) *
                qx * qz * Geometry::scale_transform(camera.get_inv_zoom());
            shader->set_uniform("view_model_matrix", view_model_matrix);
            m_dimensioning.triangle.render();
        };

        glsafe(::glDisable(GL_CULL_FACE));
        render_arrow(1);
        render_arrow(2);
        glsafe(::glEnable(GL_CULL_FACE));

        // edge 1 extension
        const Vec3d e11e12 = e1.second - e1.first;
        const Vec3d e11center = center - e1.first;
        const double e11center_len = e11center.norm();

        if (e11center_len > EPSILON && e11center.dot(e11e12) < 0.0) {
            shader->set_uniform("view_model_matrix", camera.get_view_matrix() * Geometry::translation_transform(center) *
                Eigen::Quaternion<double>::FromTwoVectors(Vec3d::UnitX(), Measure::edge_direction(e1.first, e1.second)) *
                Geometry::scale_transform({ e11center_len, 1.0f, 1.0f }));
            m_dimensioning.line.set_color(-1, ColorRGBA::LIGHT_GRAY().get_data());
            m_dimensioning.line.render();
        }

        // edge 2 extension
        const Vec3d e21center = center - e2.first;
        const double e21center_len = e21center.norm();
        if (e21center_len > EPSILON) {
            shader->set_uniform("view_model_matrix", camera.get_view_matrix() * Geometry::translation_transform(center) *
                Eigen::Quaternion<double>::FromTwoVectors(Vec3d::UnitX(), Measure::edge_direction(e2.first, e2.second)) *
                Geometry::scale_transform({ (coplanar && radius > 0.0) ? e21center_len : draw_radius, 1.0f, 1.0f }));
            m_dimensioning.line.set_color(-1, ColorRGBA::LIGHT_GRAY().get_data());
            m_dimensioning.line.render();
        }

        // label
        // label world coordinates
        const Vec3d label_position_world = Geometry::translation_transform(center) * (draw_radius * (Eigen::Quaternion<double>(Eigen::AngleAxisd(step * 0.5 * double(resolution), normal)) * e1_unit));

        // label screen coordinates
        const std::array<int, 4>& viewport = camera.get_viewport();
        const Vec2d label_position_ss = TransformHelper::world_to_ss(label_position_world,
            camera.get_projection_matrix().matrix() * camera.get_view_matrix().matrix(), viewport);

        ImGuiWrapper::push_common_window_style(m_parent.get_scale());
        m_imgui->set_next_window_pos(label_position_ss.x(), viewport[3] - label_position_ss.y(), ImGuiCond_Always, 0.0f, 1.0f);
        m_imgui->set_next_window_bg_alpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        m_imgui->begin(wxString("##angle"), ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImGui::AlignTextToFramePadding();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const std::string txt = format_double(Geometry::rad2deg(angle)) + "°";
        ImVec2 txt_size = ImGui::CalcTextSize(txt.c_str());
        const ImGuiStyle& style = ImGui::GetStyle();
        ColorRGBA         color{1.0f, 1.0f, 1.0f, 0.5f};
        draw_list->AddRectFilled({ pos.x - style.FramePadding.x, pos.y + style.FramePadding.y }, { pos.x + txt_size.x + 2.0f * style.FramePadding.x,
            pos.y + txt_size.y + 2.0f * style.FramePadding.y}, ImGuiWrapper::to_ImU32(color));
        ImGui::SetCursorScreenPos({ pos.x + style.FramePadding.x, pos.y });
        m_imgui->text(txt);
        m_imgui->end();
        ImGui::PopStyleVar();
        ImGuiWrapper::pop_common_window_style();
    };

    auto arc_edge_plane = [this, arc_edge_edge](const Measure::SurfaceFeature& f1, const Measure::SurfaceFeature& f2) {
        assert(f1.get_type() == Measure::SurfaceFeatureType::Edge && f2.get_type() == Measure::SurfaceFeatureType::Plane);
        if (!m_measurement_result.angle.has_value())
            return;

        const std::pair<Vec3d, Vec3d> e1 = m_measurement_result.angle->e1;
        const std::pair<Vec3d, Vec3d> e2 = m_measurement_result.angle->e2;
        const double calc_radius = m_measurement_result.angle->radius;

        if (calc_radius == 0.0)
            return;

        arc_edge_edge(Measure::SurfaceFeature(Measure::SurfaceFeatureType::Edge, e1.first, e1.second),
            Measure::SurfaceFeature(Measure::SurfaceFeatureType::Edge, e2.first, e2.second), calc_radius);
    };

    auto arc_plane_plane = [this, arc_edge_edge](const Measure::SurfaceFeature& f1, const Measure::SurfaceFeature& f2) {
        assert(f1.get_type() == Measure::SurfaceFeatureType::Plane && f2.get_type() == Measure::SurfaceFeatureType::Plane);
        if (!m_measurement_result.angle.has_value())
            return;

        const std::pair<Vec3d, Vec3d> e1 = m_measurement_result.angle->e1;
        const std::pair<Vec3d, Vec3d> e2 = m_measurement_result.angle->e2;
        const double calc_radius = m_measurement_result.angle->radius;

        if (calc_radius == 0.0)
            return;

        arc_edge_edge(Measure::SurfaceFeature(Measure::SurfaceFeatureType::Edge, e1.first, e1.second),
            Measure::SurfaceFeature(Measure::SurfaceFeatureType::Edge, e2.first, e2.second), calc_radius);
    };

    shader->start_using();

    if (!m_dimensioning.line.is_initialized()) {
        GLModel::Geometry init_data;
        init_data.format = {GLModel::PrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3};
        init_data.color  = ColorRGBA::WHITE();
        init_data.reserve_vertices(2);
        init_data.reserve_indices(2);

        // vertices
        init_data.add_vertex(Vec3f(0.0f, 0.0f, 0.0f));
        init_data.add_vertex(Vec3f(1.0f, 0.0f, 0.0f));

        // indices
        init_data.add_line(0, 1);

        m_dimensioning.line.init_from(std::move(init_data));
    }

    if (!m_dimensioning.triangle.is_initialized()) {
        GLModel::Geometry init_data;
        init_data.format = {GLModel::PrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3};
        init_data.color  = ColorRGBA::WHITE();
        init_data.reserve_vertices(3);
        init_data.reserve_indices(3);

        // vertices
        init_data.add_vertex(Vec3f(0.0f, 0.0f, 0.0f));
        init_data.add_vertex(Vec3f(-TRIANGLE_HEIGHT, 0.5f * TRIANGLE_BASE, 0.0f));
        init_data.add_vertex(Vec3f(-TRIANGLE_HEIGHT, -0.5f * TRIANGLE_BASE, 0.0f));

        // indices
        init_data.add_triangle(0, 1, 2);

        m_dimensioning.triangle.init_from(std::move(init_data),true);
    }

    if (last_selected_features != m_selected_features)
        m_dimensioning.arc.reset();

    glsafe(::glDisable(GL_DEPTH_TEST));

    const bool has_distance = m_measurement_result.has_distance_data();

    const Measure::SurfaceFeature* f1 = &(*m_selected_features.first.feature);
    const Measure::SurfaceFeature* f2 = nullptr;
    std::unique_ptr<Measure::SurfaceFeature> temp_feature;
    if (m_selected_features.second.feature.has_value())
        f2 = &(*m_selected_features.second.feature);
    else {
        assert(m_selected_features.first.feature->get_type() == Measure::SurfaceFeatureType::Circle);
        temp_feature = std::make_unique<Measure::SurfaceFeature>(std::get<0>(m_selected_features.first.feature->get_circle()));
        f2 = temp_feature.get();
    }

    if (!m_selected_features.second.feature.has_value() && m_selected_features.first.feature->get_type() != Measure::SurfaceFeatureType::Circle)
        return;

    Measure::SurfaceFeatureType ft1 = f1->get_type();
    Measure::SurfaceFeatureType ft2 = f2->get_type();

    // Order features by type so following conditions are simple.
    if (ft1 > ft2) {
        std::swap(ft1, ft2);
        std::swap(f1, f2);
    }

    // If there is an angle to show, draw the arc:
    if (ft1 == Measure::SurfaceFeatureType::Edge && ft2 == Measure::SurfaceFeatureType::Edge)
        arc_edge_edge(*f1, *f2);
    else if (ft1 == Measure::SurfaceFeatureType::Edge && ft2 == Measure::SurfaceFeatureType::Plane)
        arc_edge_plane(*f1, *f2);
    else if (ft1 == Measure::SurfaceFeatureType::Plane && ft2 == Measure::SurfaceFeatureType::Plane)
        arc_plane_plane(*f1, *f2);

    if (has_distance){
        // Where needed, draw the extension of the edge to where the dist is measured:
        if (ft1 == Measure::SurfaceFeatureType::Point && ft2 == Measure::SurfaceFeatureType::Edge)
            point_edge(*f1, *f2);

        // Render the arrow between the points that the backend passed:
        const Measure::DistAndPoints& dap = m_measurement_result.distance_infinite.has_value()
            ? *m_measurement_result.distance_infinite
            : *m_measurement_result.distance_strict;
        if (m_selected_features.second.feature.has_value()) {
            auto x_to = dap.from;
            x_to[0]   = dap.to[0];
            point_point(dap.from, x_to, x_to[0] - dap.from[0], ColorRGBA::RED().get_data(), false, false);
            auto y_to = x_to;
            y_to[1]   = dap.to[1];
            point_point(x_to, y_to, y_to[1] - x_to[1], ColorRGBA::GREEN().get_data(), false, false);
            point_point(y_to, dap.to, dap.to[2] - y_to[2], ColorRGBA::BLUE().get_data(), false, false);
        }

        point_point(dap.from, dap.to, dap.dist, ColorRGBA::WHITE().get_data());
    }

    glsafe(::glEnable(GL_DEPTH_TEST));

    shader->stop_using();
}

static void add_row_to_table(std::function<void(void)> col_1 = nullptr, std::function<void(void)> col_2 = nullptr)
{
    assert(col_1 != nullptr && col_2 != nullptr);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    col_1();
    ImGui::TableSetColumnIndex(1);
    col_2();
}

static void add_strings_row_to_table(ImGuiWrapper& imgui, const std::string& col_1, const ImVec4& col_1_color, const std::string& col_2, const ImVec4& col_2_color)
{
    add_row_to_table([&]() { imgui.text_colored(col_1_color, col_1); }, [&]() { imgui.text_colored(col_2_color, col_2); });
};

#if ENABLE_MEASURE_GIZMO_DEBUG
void GLGizmoMeasure::render_debug_dialog()
{
    auto add_feature_data = [this](const SelectedFeatures::Item& item) {
        const std::string text = (item.source == item.feature) ? surface_feature_type_as_string(item.feature->get_type()) : point_on_feature_type_as_string(item.source->get_type(), m_hover_id);
        add_strings_row_to_table(*m_imgui, "Type", ImGuiWrapper::COL_BAMBU, text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
        switch (item.feature->get_type())
        {
        case Measure::SurfaceFeatureType::Point:
        {
            add_strings_row_to_table(*m_imgui, "m_pt1", ImGuiWrapper::COL_BAMBU, format_vec3(item.feature->get_point()), ImGui::GetStyleColorVec4(ImGuiCol_Text));
            break;
        }
        case Measure::SurfaceFeatureType::Edge:
        {
            auto [from, to] = item.feature->get_edge();
            add_strings_row_to_table(*m_imgui, "m_pt1", ImGuiWrapper::COL_BAMBU, format_vec3(from), ImGui::GetStyleColorVec4(ImGuiCol_Text));
            add_strings_row_to_table(*m_imgui, "m_pt2", ImGuiWrapper::COL_BAMBU, format_vec3(to), ImGui::GetStyleColorVec4(ImGuiCol_Text));
            break;
        }
        case Measure::SurfaceFeatureType::Plane:
        {
            auto [idx, normal, origin] = item.feature->get_plane();
            add_strings_row_to_table(*m_imgui, "m_pt1", ImGuiWrapper::COL_BAMBU, format_vec3(normal), ImGui::GetStyleColorVec4(ImGuiCol_Text));
            add_strings_row_to_table(*m_imgui, "m_pt2", ImGuiWrapper::COL_BAMBU, format_vec3(origin), ImGui::GetStyleColorVec4(ImGuiCol_Text));
            add_strings_row_to_table(*m_imgui, "m_value", ImGuiWrapper::COL_BAMBU, format_double(idx), ImGui::GetStyleColorVec4(ImGuiCol_Text));
            break;
        }
        case Measure::SurfaceFeatureType::Circle:
        {
            auto [center, radius, normal] = item.feature->get_circle();
            const Vec3d on_circle = center + radius * Measure::get_orthogonal(normal, true);
            radius = (on_circle - center).norm();
            add_strings_row_to_table(*m_imgui, "m_pt1", ImGuiWrapper::COL_BAMBU, format_vec3(center), ImGui::GetStyleColorVec4(ImGuiCol_Text));
            add_strings_row_to_table(*m_imgui, "m_pt2", ImGuiWrapper::COL_BAMBU, format_vec3(normal), ImGui::GetStyleColorVec4(ImGuiCol_Text));
            add_strings_row_to_table(*m_imgui, "m_value", ImGuiWrapper::COL_BAMBU, format_double(radius), ImGui::GetStyleColorVec4(ImGuiCol_Text));
            break;
        }
        }
        std::optional<Vec3d> extra_point = item.feature->get_extra_point();
        if (extra_point.has_value())
            add_strings_row_to_table(*m_imgui, "m_pt3", ImGuiWrapper::COL_BAMBU, format_vec3(*extra_point), ImGui::GetStyleColorVec4(ImGuiCol_Text));
    };

    m_imgui->begin("Measure tool debug", ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    if (ImGui::BeginTable("Mode", 2)) {
        std::string txt;
        switch (m_mode)
        {
        case EMode::FeatureSelection: { txt = "Feature selection"; break; }
        case EMode::PointSelection:   { txt = "Point selection"; break; }
        default:                      { assert(false); break; }
        }
        add_strings_row_to_table(*m_imgui, "Mode", ImGuiWrapper::COL_BAMBU, txt, ImGui::GetStyleColorVec4(ImGuiCol_Text));
        ImGui::EndTable();
    }

    ImGui::Separator();
    if (ImGui::BeginTable("Hover", 2)) {
        add_strings_row_to_table(*m_imgui, "Hover id", ImGuiWrapper::COL_BAMBU, std::to_string(m_hover_id), ImGui::GetStyleColorVec4(ImGuiCol_Text));
        const std::string txt = m_curr_feature.has_value() ? surface_feature_type_as_string(m_curr_feature->get_type()) : "None";
        add_strings_row_to_table(*m_imgui, "Current feature", ImGuiWrapper::COL_BAMBU, txt, ImGui::GetStyleColorVec4(ImGuiCol_Text));
        ImGui::EndTable();
    }

    ImGui::Separator();
    if (!m_selected_features.first.feature.has_value() && !m_selected_features.second.feature.has_value())
        m_imgui->text("Empty selection");
    else {
        const ImGuiTableFlags flags = ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersH;
        if (m_selected_features.first.feature.has_value()) {
            m_imgui->text_colored(ImGuiWrapper::COL_BAMBU, "Selection 1");
            if (ImGui::BeginTable("Selection 1", 2, flags)) {
                add_feature_data(m_selected_features.first);
                ImGui::EndTable();
            }
        }
        if (m_selected_features.second.feature.has_value()) {
            m_imgui->text_colored(ImGuiWrapper::COL_BAMBU, "Selection 2");
            if (ImGui::BeginTable("Selection 2", 2, flags)) {
                add_feature_data(m_selected_features.second);
                ImGui::EndTable();
            }
        }
    }
    m_imgui->end();
}
#endif // ENABLE_MEASURE_GIZMO_DEBUG
void GLGizmoMeasure::on_render_for_picking()
{

}

void GLGizmoMeasure::on_render_input_window(float x, float y, float bottom_limit)
{
    static std::optional<Measure::SurfaceFeature> last_feature;
    static EMode last_mode = EMode::FeatureSelection;
    static SelectedFeatures last_selected_features;

    static float last_y = 0.0f;
    static float last_h = 0.0f;

    if (m_editing_distance)
        return;
    unsigned int current_active_id = ImGui::GetActiveID();
    // adjust window position to avoid overlap the view toolbar
    const float win_h = ImGui::GetWindowHeight();
    y = std::min(y, bottom_limit - win_h);
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 0.0f, 0.0f);
    if (last_h != win_h || last_y != y) {
        // ask canvas for another frame to render the window in the correct position
        m_imgui->set_requires_extra_frame();
        if (last_h != win_h)
            last_h = win_h;
        if (last_y != y)
            last_y = y;
    }

    // Orca
    ImGuiWrapper::push_toolbar_style(m_parent.get_scale());

    GizmoImguiBegin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    float caption_max    = 0.f;
    float total_text_max = 0.f;
    for (const auto &t : std::array<std::string, 3>{ "point_selection", "reset", "unselect"}) {
        caption_max    = std::max(caption_max, m_imgui->calc_text_size(m_desc[t + "_caption"]).x);
        total_text_max = std::max(total_text_max, m_imgui->calc_text_size(m_desc[t]).x);
    }

    const bool        use_inches = wxGetApp().app_config->get("use_inches") == "1";
    const std::string units = use_inches ? " " + _u8L("in") : " " + _u8L("mm");

    // Show selection
    {
        auto format_item_text = [this, use_inches, &units](const SelectedFeatures::Item& item) {
            if (!item.feature.has_value())
                return _u8L("None");

            std::string text = (item.source == item.feature) ? surface_feature_type_as_string(item.feature->get_type()) :
                item.is_center ? center_on_feature_type_as_string(item.source->get_type()) : point_on_feature_type_as_string(item.source->get_type(), m_hover_id);
            if (item.feature.has_value() && item.feature->get_type() == Measure::SurfaceFeatureType::Circle) {
                auto [center, radius, normal] = item.feature->get_circle();
                const Vec3d on_circle = center + radius * Measure::get_orthogonal(normal, true);
                radius = (on_circle - center).norm();
                if (use_inches)
                    radius = GizmoObjectManipulation::mm_to_in * radius;
                text += " (" + _u8L("Diameter") + ": " + format_double(2.0 * radius) + units + ")";
            }
            else if (item.feature.has_value() && item.feature->get_type() == Measure::SurfaceFeatureType::Edge) {
                auto [start, end] = item.feature->get_edge();
                double length = (end - start).norm();
                if (use_inches)
                    length = GizmoObjectManipulation::mm_to_in * length;
                text += " (" + _u8L("Length") + ": " + format_double(length) + units + ")";
            }
            return text;
        };

        const float space_size      = ImGui::CalcTextSize("  ").x * 2;
        const float selection_cap_length = ImGui::CalcTextSize((_u8L("Selection") + " 1").c_str()).x * 1.2;
        auto feature_first_text= format_item_text(m_selected_features.first);
        const float feature_first_text_length = ImGui::CalcTextSize((_u8L(feature_first_text)).c_str()).x;
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGuiWrapper::to_ImVec4(SELECTED_1ST_COLOR));

        m_imgui->text(_u8L("Selection") + " 1");
        ImGui::SameLine(selection_cap_length + space_size);
        ImGui::PushItemWidth(feature_first_text_length);
        m_imgui->text(feature_first_text);
        if (m_selected_features.first.feature.has_value()) {
            ImGui::SameLine(selection_cap_length + feature_first_text_length + space_size * 2);
            ImGui::PushItemWidth(space_size * 2);
            ImGui::PushID("Reset1");//for image_button
            if (m_imgui->image_button(m_is_dark_mode ? ImGui::RevertBtn : ImGui::RevertBtn, _L("Reset"))) {
                reset_feature1();
            }
            ImGui::PopID();
        }
        ImGui::PopStyleColor();

        auto        feature_second_text        = format_item_text(m_selected_features.second);
        const float feature_second_text_length = ImGui::CalcTextSize((_u8L(feature_second_text)).c_str()).x;
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGuiWrapper::to_ImVec4(SELECTED_2ND_COLOR));
        m_imgui->text(_u8L("Selection") + " 2");
        ImGui::SameLine(selection_cap_length + space_size);
        ImGui::PushItemWidth(feature_second_text_length);
        m_imgui->text(feature_second_text);
        if (m_selected_features.first.feature.has_value() && m_selected_features.second.feature.has_value()) {
            m_show_reset_first_tip = false;
            ImGui::SameLine(selection_cap_length + feature_second_text_length + space_size * 2);
            ImGui::PushItemWidth(space_size * 2);
            ImGui::PushID("Reset2");
            if (m_imgui->image_button(m_is_dark_mode ? ImGui::RevertBtn : ImGui::RevertBtn, _L("Reset"))) {
                reset_feature2();
            }
            ImGui::PopID();
        }
        ImGui::PopStyleColor();
    }

    m_imgui->disabled_begin(!m_selected_features.first.feature.has_value());
        if (m_imgui->button(_L("Restart selection"))) {
            reset_all_feature();
            m_imgui->set_requires_extra_frame();
        }
    m_imgui->disabled_end();

    if (m_show_reset_first_tip) {
        m_imgui->text(_L("Feature 1 has been reset, \nfeature 2 has been feature 1"));
    }

    auto add_measure_row_to_table = [this](const std::string& col_1, const ImVec4& col_1_color, const std::string& col_2, const ImVec4& col_2_color) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        m_imgui->text_colored(col_1_color, col_1);
        ImGui::TableSetColumnIndex(1);
        m_imgui->text_colored(col_2_color, col_2);
        ImGui::TableSetColumnIndex(2);
        if (m_imgui->image_button(m_is_dark_mode ? ImGui::ClipboardBtnDarkIcon : ImGui::ClipboardBtnIcon, _L("Copy to clipboard"))) {
            wxTheClipboard->Open();
            wxTheClipboard->SetData(new wxTextDataObject(wxString((col_1 + ": " + col_2).c_str(), wxConvUTF8)));
            wxTheClipboard->Close();
        }
    };
    auto add_edit_distance_xyz_box = [this, &current_active_id](Vec3d& distance) {
        float buf_size_max = ImGui::CalcTextSize("-100.00").x * 1.2;
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        m_imgui->text_colored(ImGuiWrapper::COL_RED, "X:");
        ImGui::TableSetColumnIndex(1);
        ImGui::PushItemWidth(buf_size_max);
        ImGui::BBLInputDouble("##measure_distance_x", &m_buffered_distance[0], 0.0f, 0.0f, "%.2f");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        m_imgui->text_colored(ImGuiWrapper::COL_GREEN, "Y:");
        ImGui::TableSetColumnIndex(1);
        ImGui::BBLInputDouble("##measure_distance_y", &m_buffered_distance[1], 0.0f, 0.0f, "%.2f");

        bool same_model_object = is_two_volume_in_same_model_object();
        m_imgui->disabled_begin(!same_model_object);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        m_imgui->text_colored(ImGuiWrapper::COL_BLUE, "Z:");
        ImGui::TableSetColumnIndex(1);
        ImGui::BBLInputDouble("##measure_distance_z", &m_buffered_distance[2], 0.0f, 0.0f, "%.2f");
        m_imgui->disabled_end();

        /*ImGui::TableNextRow();
        if (m_imgui->button(_L("Adsorbed onto the surface"))) {
            std::cout << "todo";
        }*/
        m_imgui->disabled_end();
        if (m_last_active_item_imgui != current_active_id && m_hit_different_volumes.size() == 2) {
            auto selection         = const_cast<Selection*>(&m_parent.get_selection());
            selection->setup_cache();
            Vec3d displacement = Vec3d::Zero();
            auto  v                = m_hit_different_volumes[1];
            if (std::abs(m_buffered_distance[0] - distance[0]) > EPSILON) {
                wxGetApp().plater()->take_snapshot("modify x distance between objects");
                displacement[0] = m_buffered_distance[0] - distance[0];
                distance[0] = m_buffered_distance[0];
            } else if (std::abs(m_buffered_distance[1] - distance[1]) > EPSILON) {
                wxGetApp().plater()->take_snapshot("modify y distance between objects");
                displacement[1] = m_buffered_distance[1] - distance[1];
                distance[1] = m_buffered_distance[1];
            } else if (std::abs(m_buffered_distance[2] - distance[2]) > EPSILON) {
                wxGetApp().plater()->take_snapshot("modify z distance between objects");
                displacement[2] = m_buffered_distance[2] - distance[2];
                distance[2]     = m_buffered_distance[2];
            }
            if (displacement.norm() > 0.0f) {
                wxGetApp().plater()->take_snapshot(_u8L("MoveInMeasure"));// avoid storing another snapshot
                selection->set_mode(same_model_object ? Selection::Volume : Selection::Instance);
                auto llo = selection->get_mode();
                if (same_model_object == false) {
                    selection->translate(v->object_idx(), v->instance_idx(), displacement);
                } else {
                    selection->translate(v->object_idx(), v->instance_idx(), v->volume_idx(), displacement);
                }
                wxGetApp().plater()->canvas3D()->do_move("");
                update_single_mesh_pick(v);
                update_feature_by_tran(*m_selected_features.second.feature);
                m_pending_scale = true;
            }
        }
    };
    ImGui::Separator();
    m_imgui->text(_u8L("Measure"));

    const unsigned int max_measure_row_count = 2;
    unsigned int measure_row_count = 0;
    if (ImGui::BeginTable("Measure", 4)) {
        if (m_selected_features.second.feature.has_value()) {
            const Measure::MeasurementResult& measure = m_measurement_result;
            if (measure.angle.has_value()) {
                ImGui::PushID("ClipboardAngle");
                add_measure_row_to_table(_u8L("Angle"), ImGuiWrapper::COL_BAMBU, format_double(Geometry::rad2deg(measure.angle->angle)) + "°",
                    ImGui::GetStyleColorVec4(ImGuiCol_Text));
                ++measure_row_count;
                ImGui::PopID();
            }

            const bool show_strict = measure.distance_strict.has_value() &&
                (!measure.distance_infinite.has_value() || std::abs(measure.distance_strict->dist - measure.distance_infinite->dist) > EPSILON);

            if (measure.distance_infinite.has_value()) {
                double distance = measure.distance_infinite->dist;
                if (use_inches)
                    distance = GizmoObjectManipulation::mm_to_in * distance;
                ImGui::PushID("ClipboardDistanceInfinite");
                add_measure_row_to_table(show_strict ? _u8L("Perpendicular distance") : _u8L("Distance"), ImGuiWrapper::COL_BAMBU, format_double(distance) + units,
                    ImGui::GetStyleColorVec4(ImGuiCol_Text));
                ++measure_row_count;
                ImGui::PopID();
            }
            if (show_strict) {
                double distance = measure.distance_strict->dist;
                if (use_inches)
                    distance = GizmoObjectManipulation::mm_to_in * distance;
                ImGui::PushID("ClipboardDistanceStrict");
                add_measure_row_to_table(_u8L("Direct distance"), ImGuiWrapper::COL_BAMBU, format_double(distance) + units,
                    ImGui::GetStyleColorVec4(ImGuiCol_Text));
                ++measure_row_count;
                ImGui::PopID();
            }
            if (measure.distance_xyz.has_value()) {
                Vec3d distance = *measure.distance_xyz;
                if (use_inches)
                    distance = GizmoObjectManipulation::mm_to_in * distance;
                if (measure.distance_xyz->norm() > EPSILON) {
                    ImGui::PushID("ClipboardDistanceXYZ");
                    add_measure_row_to_table(_u8L("Distance XYZ"), ImGuiWrapper::COL_BAMBU, format_vec3(distance), ImGui::GetStyleColorVec4(ImGuiCol_Text));
                    ++measure_row_count;
                    ImGui::PopID();
                }
            }
            if (m_distance.norm() >0.01 && m_hit_different_volumes.size() == 2) {
                add_edit_distance_xyz_box(m_distance);
            }
        }
        // add dummy rows to keep dialog size fixed
        /*for (unsigned int i = measure_row_count; i < max_measure_row_count; ++i) {
            add_strings_row_to_table(*m_imgui, " ", ImGuiWrapper::COL_BAMBU, " ", ImGui::GetStyleColorVec4(ImGuiCol_Text));
        }*/
        ImGui::EndTable();
    }

    ImGui::Separator();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 10.0f));
    float get_cur_y = ImGui::GetContentRegionMax().y + ImGui::GetFrameHeight() + y;
    show_tooltip_information(caption_max, x, get_cur_y);

    float f_scale =m_parent.get_gizmos_manager().get_layout_scale();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f * f_scale));

    ImGui::PopStyleVar(2);

    if (last_feature != m_curr_feature || last_mode != m_mode || last_selected_features != m_selected_features) {
        // the dialog may have changed its size, ask for an extra frame to render it properly
        last_feature = m_curr_feature;
        last_mode = m_mode;
        last_selected_features = m_selected_features;
        m_imgui->set_requires_extra_frame();
    }
    m_last_active_item_imgui = current_active_id;
    GizmoImguiEnd();

    // Orca
    ImGuiWrapper::pop_toolbar_style();
}

void GLGizmoMeasure::remove_selected_sphere_raycaster(int id)
{
    reset_gripper_pick(id == SEL_SPHERE_1_ID ? GripperType::SPHERE_1 : GripperType::SPHERE_2);
}

void GLGizmoMeasure::update_measurement_result()
{
    if (!m_selected_features.first.feature.has_value())
        m_measurement_result = Measure::MeasurementResult();
    else if (m_selected_features.second.feature.has_value())
        m_measurement_result = Measure::get_measurement(*m_selected_features.first.feature, *m_selected_features.second.feature,true);
    else if (!m_selected_features.second.feature.has_value() && m_selected_features.first.feature->get_type() == Measure::SurfaceFeatureType::Circle)
        m_measurement_result = Measure::get_measurement(*m_selected_features.first.feature, Measure::SurfaceFeature(std::get<0>(m_selected_features.first.feature->get_circle())));
}

void GLGizmoMeasure::show_tooltip_information(float caption_max, float x, float y)
{
    ImTextureID normal_id = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP);
    ImTextureID hover_id  = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP_HOVER);

    caption_max += m_imgui->calc_text_size(": ").x + 35.f;

    float font_size = ImGui::GetFontSize();
    ImVec2 button_size = ImVec2(font_size * 1.8, font_size * 1.3);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 0, ImGui::GetStyle().FramePadding.y });
    ImGui::ImageButton3(normal_id, hover_id, button_size);

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip2(ImVec2(x, y));
        auto draw_text_with_caption = [this, &caption_max](const wxString &caption, const wxString &text) {
            m_imgui->text_colored(ImGuiWrapper::COL_ACTIVE, caption);
            ImGui::SameLine(caption_max);
            m_imgui->text_colored(ImGuiWrapper::COL_WINDOW_BG, text);
        };

        for (const auto &t : std::array<std::string, 3>{"point_selection", "reset", "unselect"})
            draw_text_with_caption(m_desc.at(t + "_caption") + ": ", m_desc.at(t));
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar(2);
}

void GLGizmoMeasure::reset_all_pick()
{
   std::map<GLVolume*, std::shared_ptr<PickRaycaster>>().swap(m_mesh_raycaster_map);
   reset_gripper_pick(GripperType::UNDEFINE,true);
}

void GLGizmoMeasure::reset_gripper_pick(GripperType id, bool is_all)
{
    if (m_gripper_id_raycast_map.find(id) != m_gripper_id_raycast_map.end()) {
        m_gripper_id_raycast_map[id].reset();
        m_gripper_id_raycast_map.erase(id);
    }
    if (id == GripperType ::UNDEFINE && is_all) {
        reset_gripper_pick(GripperType::POINT);
        reset_gripper_pick(GripperType::EDGE);
        reset_gripper_pick(GripperType::PLANE);
        reset_gripper_pick(GripperType::PLANE_1);
        reset_gripper_pick(GripperType::PLANE_2);
        reset_gripper_pick(GripperType::CIRCLE);
        reset_gripper_pick(GripperType::CIRCLE_1);
        reset_gripper_pick(GripperType::CIRCLE_2);
    }
}

void GLGizmoMeasure::register_single_mesh_pick()
{
    Selection&  selection = m_parent.get_selection();
    const Selection::IndicesList &idxs  = selection.get_volume_idxs();
    if (idxs.size() > 0) {
        for (unsigned int idx : idxs) {
            GLVolume *v                = const_cast<GLVolume *>(selection.get_volume(idx));
            auto world_tran            = v->get_instance_transformation() * v->get_volume_transformation();
            auto mesh                  = const_cast<TriangleMesh *>(v->ori_mesh);
            if (m_mesh_raycaster_map.find(v) != m_mesh_raycaster_map.end()) {
                m_mesh_raycaster_map[v]->world_tran.set_from_transform(world_tran.get_matrix());
            } else {
                m_mesh_raycaster_map[v] = std::make_shared<PickRaycaster>(mesh, -1);
                m_mesh_raycaster_map[v]->world_tran.set_from_transform(world_tran.get_matrix());
                m_mesh_measure_map[v] = std::make_shared<Measure::Measuring>(mesh->its);
            }
        }
    }
}

void GLGizmoMeasure::update_single_mesh_pick(GLVolume *v)
{
    if (m_mesh_raycaster_map.find(v) != m_mesh_raycaster_map.end()) {
        auto world_tran = v->get_instance_transformation() * v->get_volume_transformation();
        m_mesh_raycaster_map[v]->world_tran.set_from_transform(world_tran.get_matrix());
    }
}

 void GLGizmoMeasure::reset_all_feature() {
     reset_feature2();
     reset_feature1();
     m_show_reset_first_tip = false;
     m_hit_different_volumes.clear();
     m_hit_order_volumes.clear();
     m_last_hit_volume = nullptr;
 }

void GLGizmoMeasure::reset_feature1()
 {
     if (m_selected_features.second.feature.has_value()) {
         if (m_hit_different_volumes.size() == 2) {
             m_hit_different_volumes[0] = m_hit_different_volumes[1];
         }
         if (m_hit_order_volumes.size() == 2) {
             m_hit_order_volumes[0] = m_hit_order_volumes[1];
         }
         m_selected_features.first = m_selected_features.second;
         reset_feature2();
         m_show_reset_first_tip = true;
     } else {
         remove_selected_sphere_raycaster(SEL_SPHERE_1_ID);
         m_selected_features.first.feature.reset();
         m_show_reset_first_tip = false;
         if (m_hit_different_volumes.size() == 1) {
             m_hit_different_volumes.clear();
         }
         if (m_hit_order_volumes.size() == 1) {
             m_hit_order_volumes.clear();
         }
         if (m_feature_plane_first.plane) {
             m_feature_plane_first.plane->reset();
             m_feature_plane_first.plane_idx = -1;
         }
         if (m_feature_circle_first.circle) {
             m_feature_circle_first.circle->reset();
             m_feature_circle_first.last_circle_feature=nullptr;
             m_feature_circle_first.inv_zoom = 0;
         }
         reset_gripper_pick(GripperType::PLANE_1);
         reset_gripper_pick(GripperType::CIRCLE_1);
         reset_gripper_pick(GripperType::SPHERE_1);
     }
     update_measurement_result();
 }

void GLGizmoMeasure::reset_feature2()
{
     if (m_hit_different_volumes.size() == 2) {
         m_hit_different_volumes.erase(m_hit_different_volumes.begin() + 1);
     }
     if (m_hit_order_volumes.size() == 2) {
         m_hit_order_volumes.erase(m_hit_order_volumes.begin() + 1);
     }
     remove_selected_sphere_raycaster(SEL_SPHERE_2_ID);
     m_selected_features.second.reset();
     m_show_reset_first_tip = false;
     reset_gripper_pick(GripperType::PLANE_2);
     reset_gripper_pick(GripperType::CIRCLE_2);
     reset_gripper_pick(GripperType::SPHERE_2);
     if (m_feature_plane_second.plane) {
         m_feature_plane_second.plane->reset();
         m_feature_plane_second.plane_idx = -1;
     }
     if (m_feature_circle_second.circle) {
         m_feature_circle_second.circle->reset();
         m_feature_circle_second.last_circle_feature = nullptr;
         m_feature_circle_second.inv_zoom            = 0;
     }
     update_measurement_result();
}

bool Slic3r::GUI::GLGizmoMeasure::is_two_volume_in_same_model_object()
{
    if (m_hit_different_volumes.size() == 2) {
        if (m_hit_different_volumes[0]->composite_id.object_id == m_hit_different_volumes[1]->composite_id.object_id) {
            return true;
        }
    }
    return false;
}

Measure::Measuring *GLGizmoMeasure::get_measuring_of_mesh(indexed_triangle_set *mesh, Transform3d &tran)
{
    for (auto glvolume:m_hit_order_volumes) {
        auto ori_mesh = const_cast<TriangleMesh*>(glvolume->ori_mesh);
        auto ori_triangle_set = const_cast<indexed_triangle_set*>(&ori_mesh->its);
        if (ori_triangle_set == mesh) {
            tran = m_mesh_raycaster_map[glvolume]->world_tran.get_matrix();
            return m_mesh_measure_map[glvolume].get();
        }
    }
    return nullptr;
}

void GLGizmoMeasure::update_world_plane_features(Measure::Measuring *cur_measuring, Measure::SurfaceFeature& feautre)
{
    if (cur_measuring) {
        const auto &[idx, normal, pt] = feautre.get_plane();
        feautre.plane_indices       = const_cast<std::vector<int> *>(&cur_measuring->get_plane_triangle_indices(idx));
        auto cur_plane_features       = const_cast<std::vector<Measure::SurfaceFeature> *>(&cur_measuring->get_plane_features(idx));
        if (cur_plane_features) {
            if (!feautre.world_plane_features) {
                feautre.world_plane_features = std::make_shared<std::vector<Measure::SurfaceFeature>>();
            }
            feautre.world_plane_features->clear(); // resize(cur_plane_features->size());
            for (size_t i = 0; i < cur_plane_features->size(); i++) {
                Measure::SurfaceFeature temp(cur_plane_features->at(i));
                temp.translate(feautre.world_tran);
                feautre.world_plane_features->push_back(std::move(temp));
            }
        }
    }
}

void GLGizmoMeasure::update_feature_by_tran(Measure::SurfaceFeature &feature)
{
    Measure::Measuring *cur_measuring = get_measuring_of_mesh(feature.mesh, feature.world_tran);
    switch (feature.get_type()) {
    case Measure::SurfaceFeatureType::Point:
    case Measure::SurfaceFeatureType::Edge:
    case Measure::SurfaceFeatureType::Circle:
    case Measure::SurfaceFeatureType::Plane: {
        feature.clone(*feature.origin_surface_feature);
        feature.translate(feature.world_tran);
        if (feature.get_type() == Measure::SurfaceFeatureType::Circle) {
            m_feature_circle_first.last_circle_feature  = nullptr;
            m_feature_circle_first.inv_zoom             = 0;
            m_feature_circle_second.last_circle_feature = nullptr;
            m_feature_circle_second.inv_zoom            = 0;
        }
        if (feature.get_type() == Measure::SurfaceFeatureType::Plane) {
            if (cur_measuring) { update_world_plane_features(cur_measuring, feature); }
        }
        break;
    }
    default: {
        break;
    }
    }
}

} // namespace GUI
} // namespace Slic3r
