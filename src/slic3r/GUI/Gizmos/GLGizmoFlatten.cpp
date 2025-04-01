// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoFlatten.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"

#include "libslic3r/Geometry/ConvexHull.hpp"
#include "libslic3r/Model.hpp"

#include <wx/display.h>

#include <numeric>
#include <imgui/imgui_internal.h>
#include <GL/glew.h>

namespace Slic3r {
namespace GUI {


GLGizmoFlatten::GLGizmoFlatten(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
    , m_normal(Vec3d::Zero())
    , m_starting_center(Vec3d::Zero())
{
}

bool GLGizmoFlatten::on_init()
{
    // BBS
    m_shortcut_key = WXK_CONTROL_F;
    return true;
}

void GLGizmoFlatten::on_set_state()
{
    m_hit_facet = -1;
    m_last_hit_facet = -1;
}

CommonGizmosDataID GLGizmoFlatten::on_get_requirements() const
{
    return CommonGizmosDataID(int(CommonGizmosDataID::SelectionInfo) | int(CommonGizmosDataID::InstancesHider) | int(CommonGizmosDataID::Raycaster) |
                              int(CommonGizmosDataID::ObjectClipper));
}

void GLGizmoFlatten::on_render_input_window(float x, float y, float bottom_limit) {
    double screen_scale = wxDisplay(wxGetApp().plater()).GetScaleFactor();
    static float last_y       = 0.0f;
    static float last_h       = 0.0f;
    const float  win_h        = ImGui::GetWindowHeight();
    y                         = std::min(y, bottom_limit - win_h);
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 0.0f, 0.0f);
    if (last_h != win_h || last_y != y) {
        // ask canvas for another frame to render the window in the correct position
        m_imgui->set_requires_extra_frame();
        if (last_h != win_h) last_h = win_h;
        if (last_y != y) last_y = y;
    }
    ImGuiWrapper::push_toolbar_style(m_parent.get_scale());
    GizmoImguiBegin(on_get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    float space_size    = m_imgui->get_style_scaling() * 8;
    float mode_cap     = m_imgui->calc_text_size(_L("Mode") + ":").x;
    float caption_size  = mode_cap + space_size + ImGui::GetStyle().WindowPadding.x;
    ImGui::AlignTextToFramePadding();
    m_imgui->text(_L("Mode") + ":");
    ImGui::SameLine();//ImGui::SameLine(caption_size);
    bool faltten_type_defult = m_faltten_type == FlattenType::Default;
    auto first_mode_str          = _L("Convex hull");
    if (m_imgui->bbl_checkbox(first_mode_str, faltten_type_defult)) {
        if (faltten_type_defult) {
            m_faltten_type = FlattenType::Default;
        } else {
            m_faltten_type = FlattenType::Triangle;
        }
    }
    ImGui::SameLine();//ImGui::SameLine(new_label_width);
    bool faltten_type_tri = m_faltten_type == FlattenType::Triangle;
    if (m_imgui->bbl_checkbox(_L("Triangular facet"), faltten_type_tri)) {
        if (!faltten_type_tri) {
            m_faltten_type = FlattenType::Default;
        } else {
            m_faltten_type = FlattenType::Triangle;
        }
    }

    if (m_show_warning) {
       m_imgui->warning_text(_L("Warning: All triangle areas are too small,The current function is not working."));
    }

    GizmoImguiEnd();
    ImGuiWrapper::pop_toolbar_style();
}

std::string GLGizmoFlatten::on_get_name() const
{
    if (!on_is_activable() && m_state == EState::Off) {
        return _u8L("Lay on face") + ":\n" + _u8L("Please select single object.");
    } else {
        return _u8L("Lay on face");
    }
}

bool GLGizmoFlatten::on_is_activable() const
{
    // This is assumed in GLCanvas3D::do_rotate, do not change this
    // without updating that function too.
    return m_parent.get_selection().is_single_full_instance();
}

void GLGizmoFlatten::on_start_dragging()
{
    if (m_hover_id != -1) {
        assert(m_planes_valid);
        if (m_faltten_type == FlattenType::Default) {
            m_normal = m_planes[m_hover_id].normal;
        }
        else {
            m_normal = m_hit_object_normal.cast<double>();
        }
        m_starting_center = m_parent.get_selection().get_bounding_box().center();
    }
}

bool GLGizmoFlatten::update_raycast_cache(const Vec2d &mouse_position, const Camera &camera, const std::vector<Transform3d> &trafo_matrices, int &cur_facet)
{
    /*if (m_rr.mouse_position == mouse_position) {
        return false;
    }*/

    Vec3f  normal                       = Vec3f::Zero();
    Vec3f  hit                          = Vec3f::Zero();
    Vec3f  closest_hit                  = Vec3f::Zero();
    Vec3f  closest_nromal               = Vec3f::Zero();
    double closest_hit_squared_distance = std::numeric_limits<double>::max();
    int    closest_hit_mesh_id          = -1;
    size_t facet                        = 0;
    // Cast a ray on all meshes, pick the closest hit and save it for the respective mesh
    for (int mesh_id = 0; mesh_id < int(trafo_matrices.size()); ++mesh_id) {
        if (m_c->raycaster()->raycasters()[mesh_id]->unproject_on_mesh(mouse_position, trafo_matrices[mesh_id], camera, hit, normal, m_c->object_clipper()->get_clipping_plane(),
                                                                       &facet)) {
            // In case this hit is clipped, skip it.
            //if (is_mesh_point_clipped(hit.cast<double>(), trafo_matrices[mesh_id])) continue;

            double hit_squared_distance = (camera.get_position() - trafo_matrices[mesh_id] * hit.cast<double>()).squaredNorm();
            if (hit_squared_distance < closest_hit_squared_distance) {
                closest_hit_squared_distance = hit_squared_distance;
                closest_hit_mesh_id          = mesh_id;
                closest_hit                  = hit;
                closest_nromal               = normal;
                if (m_faltten_type == FlattenType::Triangle) {
                    auto mo                   = m_c->selection_info()->model_object();
                    auto mv                   = mo->volumes[mesh_id];
                    m_hit_object_normal       = mv->get_matrix().cast<float>() * closest_nromal;
                }
            }
        }
    }
    if (closest_hit_mesh_id >= 0) {
        m_rr = {mouse_position, closest_hit_mesh_id, closest_hit, closest_nromal}; // update_raycast_cache berfor click down
        cur_facet = (int)facet;
        return true;
    }
    cur_facet = -1;
    return false;
}

void GLGizmoFlatten::on_render()
{
    const auto& p_flat_shader = wxGetApp().get_shader("flat");
    if (!p_flat_shader) {
        return;
    }

    const Selection& selection = m_parent.get_selection();

    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));

    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glEnable(GL_BLEND));
    wxGetApp().bind_shader(p_flat_shader);
    const Camera &camera = wxGetApp().plater()->get_camera();
    p_flat_shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    if (selection.is_single_full_instance()) {
        if (m_faltten_type == FlattenType::Default) {
            const Transform3d &m                 = selection.get_volume(*selection.get_volume_idxs().begin())->get_instance_transformation().get_matrix();

            const Transform3d  view_model_matrix = camera.get_view_matrix() *
                                                  Geometry::assemble_transform(selection.get_volume(*selection.get_volume_idxs().begin())->get_sla_shift_z() * Vec3d::UnitZ()) *
                                                  m;
            p_flat_shader->set_uniform("view_model_matrix", view_model_matrix);
            if (this->is_plane_update_necessary()) update_planes();
            for (int i = 0; i < (int) m_planes.size(); ++i) {
                p_flat_shader->set_uniform("uniform_color", i == m_hover_id ? GLGizmoBase::FLATTEN_HOVER_COLOR : GLGizmoBase::FLATTEN_COLOR);
                m_planes[i].vbo.render(p_flat_shader);
            }
        }
        else {
            Vec2d mouse_pos = m_parent.get_local_mouse_position();
            const Camera &camera    = wxGetApp().plater()->get_camera();
            const Transform3d view_model_matrix = camera.get_view_matrix();
            p_flat_shader->set_uniform("view_model_matrix", view_model_matrix);

            const Selection &  selection = m_parent.get_selection();
            auto mo = get_selected_model_object(m_parent);
            if (mo) {
                const ModelInstance *    mi = mo->instances[selection.get_instance_idx()];
                std::vector<Transform3d> trafo_matrices;
                for (const ModelVolume *mv : mo->volumes) {
                    if (mv->is_model_part())
                        trafo_matrices.emplace_back(mi->get_transformation().get_matrix() * mv->get_matrix());
                }
                update_raycast_cache(mouse_pos, camera, trafo_matrices,m_hit_facet);
                if (m_hit_facet >= 0) {
                    if (m_last_hit_facet != m_hit_facet) {
                        m_last_hit_facet = m_hit_facet;
                        m_one_tri_model.reset();
                        auto  mv         = mo->volumes[m_rr.mesh_id];
                        auto  world_tran = (mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() * mv->get_matrix()).cast<float>();
                        auto &vertices   = mv->mesh().its.vertices;
                        auto &cur_faces   = mv->mesh().its.indices;
                        if (m_hit_facet < cur_faces.size()) {
                            auto                 v0 = world_tran * vertices[cur_faces[m_hit_facet][0]] + m_rr.normal * 0.05;
                            auto                 v1 = world_tran * vertices[cur_faces[m_hit_facet][1]] + m_rr.normal * 0.05;
                            auto                 v2 = world_tran * vertices[cur_faces[m_hit_facet][2]] + m_rr.normal * 0.05;
                            indexed_triangle_set temp_its;
                            temp_its.indices.push_back({0, 1, 2});
                            temp_its.vertices.push_back(v0);
                            temp_its.vertices.push_back(v1);
                            temp_its.vertices.push_back(v2);
                            m_one_tri_model.init_from(temp_its);
                        }
                    }
                    if (m_one_tri_model.is_initialized()) {
                        glsafe(::glDisable(GL_CULL_FACE));
                        m_one_tri_model.set_color(GLGizmoBase::FLATTEN_HOVER_COLOR);
                        m_one_tri_model.render_geometry();
                    }
                }
            }
        }
    }

    wxGetApp().unbind_shader();

    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glDisable(GL_BLEND));
}

void GLGizmoFlatten::on_render_for_picking()
{
   const auto& p_flat_shader = wxGetApp().get_shader("flat");
    if (!p_flat_shader) {
        return;
    }

    const Selection& selection = m_parent.get_selection();

    glsafe(::glDisable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_BLEND));

    wxGetApp().bind_shader(p_flat_shader);
    const Camera &camera = wxGetApp().plater()->get_picking_camera();
    p_flat_shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    if (selection.is_single_full_instance() && !wxGetKeyState(WXK_CONTROL)) {
        if (m_faltten_type == FlattenType::Default) {
            const Transform3d &m                 = selection.get_volume(*selection.get_volume_idxs().begin())->get_instance_transformation().get_matrix();

            const Transform3d  view_model_matrix = camera.get_view_matrix() *
                                                  Geometry::assemble_transform(selection.get_volume(*selection.get_volume_idxs().begin())->get_sla_shift_z() * Vec3d::UnitZ()) *
                                                  m;
            p_flat_shader->set_uniform("view_model_matrix", view_model_matrix);

            if (this->is_plane_update_necessary()) update_planes();
            for (int i = 0; i < (int) m_planes.size(); ++i) {
                p_flat_shader->set_uniform("uniform_color", picking_color_component(i));
                m_planes[i].vbo.render(p_flat_shader);
            }
        }
        else {
            if (m_one_tri_model.is_initialized()) {
                glsafe(::glDisable(GL_CULL_FACE));
                const Transform3d  view_model_matrix = camera.get_view_matrix();
                p_flat_shader->set_uniform("view_model_matrix", view_model_matrix);
                m_one_tri_model.set_color(picking_color_component(0));
                m_one_tri_model.render_geometry();
            }
        }
    }

    wxGetApp().unbind_shader();
    glsafe(::glEnable(GL_CULL_FACE));
}

void GLGizmoFlatten::set_flattening_data(const ModelObject* model_object)
{
    m_starting_center = Vec3d::Zero();
    if (model_object != m_old_model_object) {
        m_planes.clear();
        m_planes_valid = false;
    }
}

void GLGizmoFlatten::update_planes()
{
    const ModelObject* mo = m_c->selection_info()->model_object();
    TriangleMesh ch;
    for (const ModelVolume* vol : mo->volumes) {
        if (vol->type() != ModelVolumeType::MODEL_PART)
            continue;
        TriangleMesh vol_ch = vol->get_convex_hull();
        vol_ch.transform(vol->get_matrix());
        ch.merge(vol_ch);
    }
    ch = ch.convex_hull_3d();
    m_planes.clear();
    const Transform3d& inst_matrix = mo->instances.front()->get_matrix(true);

    // Following constants are used for discarding too small polygons.
    const float experted_minimal_area  = 5.0f;
    const float minimal_area = 1.0f; // in square mm (world coordinates)
    const float minimal_side = 1.f; // mm
    const float minimal_angle = 1.f; // degree, initial value was 10, but cause bugs

    // Now we'll go through all the facets and append Points of facets sharing the same normal.
    // This part is still performed in mesh coordinate system.
    const int                num_of_facets  = ch.facets_count();
    const std::vector<Vec3f> face_normals   = its_face_normals(ch.its);
    const std::vector<Vec3i> face_neighbors = its_face_neighbors(ch.its);
    std::vector<int>         facet_queue(num_of_facets, 0);
    std::vector<bool>        facet_visited(num_of_facets, false);
    int                      facet_queue_cnt = 0;
    const stl_normal*        normal_ptr      = nullptr;
    int                      facet_idx       = 0;
    while (1) {
        // Find next unvisited triangle:
        for (; facet_idx < num_of_facets; ++ facet_idx)
            if (!facet_visited[facet_idx]) {
                facet_queue[facet_queue_cnt ++] = facet_idx;
                facet_visited[facet_idx] = true;
                normal_ptr = &face_normals[facet_idx];
                m_planes.emplace_back();
                break;
            }
        if (facet_idx == num_of_facets)
            break; // Everything was visited already

        while (facet_queue_cnt > 0) {
            int facet_idx = facet_queue[-- facet_queue_cnt];
            const stl_normal& this_normal = face_normals[facet_idx];
            if (std::abs(this_normal(0) - (*normal_ptr)(0)) < 0.001 && std::abs(this_normal(1) - (*normal_ptr)(1)) < 0.001 && std::abs(this_normal(2) - (*normal_ptr)(2)) < 0.001) {
                const Vec3i face = ch.its.indices[facet_idx];
                for (int j=0; j<3; ++j)
                    m_planes.back().vertices.emplace_back(ch.its.vertices[face[j]].cast<double>());

                facet_visited[facet_idx] = true;
                for (int j = 0; j < 3; ++ j)
                    if (int neighbor_idx = face_neighbors[facet_idx][j]; neighbor_idx >= 0 && ! facet_visited[neighbor_idx])
                        facet_queue[facet_queue_cnt ++] = neighbor_idx;
            }
        }
        m_planes.back().normal = normal_ptr->cast<double>();

        Pointf3s& verts = m_planes.back().vertices;
        // Now we'll transform all the points into world coordinates, so that the areas, angles and distances
        // make real sense.
        verts = transform(verts, inst_matrix);

        // if this is a just a very small triangle, remove it to speed up further calculations (it would be rejected later anyway):
        if (verts.size() == 3 &&
            ((verts[0] - verts[1]).norm() < minimal_side
            || (verts[0] - verts[2]).norm() < minimal_side
            || (verts[1] - verts[2]).norm() < minimal_side))
            m_planes.pop_back();
    }

    // Let's prepare transformation of the normal vector from mesh to instance coordinates.
    Geometry::Transformation t(inst_matrix);
    Vec3d scaling = t.get_scaling_factor();
    t.set_scaling_factor(Vec3d(1./scaling(0), 1./scaling(1), 1./scaling(2)));

    // Now we'll go through all the polygons, transform the points into xy plane to process them:
    for (unsigned int polygon_id=0; polygon_id < m_planes.size(); ++polygon_id) {
        Pointf3s& polygon = m_planes[polygon_id].vertices;
        const Vec3d& normal = m_planes[polygon_id].normal;

        // transform the normal according to the instance matrix:
        Vec3d normal_transformed = t.get_matrix() * normal;

        // We are going to rotate about z and y to flatten the plane
        Eigen::Quaterniond q;
        Transform3d m = Transform3d::Identity();
        m.matrix().block(0, 0, 3, 3) = q.setFromTwoVectors(normal_transformed, Vec3d::UnitZ()).toRotationMatrix();
        polygon = transform(polygon, m);

        // Now to remove the inner points. We'll misuse Geometry::convex_hull for that, but since
        // it works in fixed point representation, we will rescale the polygon to avoid overflows.
        // And yes, it is a nasty thing to do. Whoever has time is free to refactor.
        Vec3d bb_size = BoundingBoxf3(polygon).size();
        float sf = std::min(1./bb_size(0), 1./bb_size(1));
        Transform3d tr = Geometry::assemble_transform(Vec3d::Zero(), Vec3d::Zero(), Vec3d(sf, sf, 1.f));
        polygon = transform(polygon, tr);
        polygon = Slic3r::Geometry::convex_hull(polygon);
        polygon = transform(polygon, tr.inverse());

        // Calculate area of the polygons and discard ones that are too small
        float& area = m_planes[polygon_id].area;
        area = 0.f;
        for (unsigned int i = 0; i < polygon.size(); i++) // Shoelace formula
            area += polygon[i](0)*polygon[i + 1 < polygon.size() ? i + 1 : 0](1) - polygon[i + 1 < polygon.size() ? i + 1 : 0](0)*polygon[i](1);
        area = 0.5f * std::abs(area);

        bool discard = false;
        if (area < minimal_area)
            discard = true;
        else {
            // We also check the inner angles and discard polygons with angles smaller than the following threshold
            const double angle_threshold = ::cos(minimal_angle * (double)PI / 180.0);

            for (unsigned int i = 0; i < polygon.size(); ++i) {
                const Vec3d& prec = polygon[(i == 0) ? polygon.size() - 1 : i - 1];
                const Vec3d& curr = polygon[i];
                const Vec3d& next = polygon[(i == polygon.size() - 1) ? 0 : i + 1];

                if ((prec - curr).normalized().dot((next - curr).normalized()) > angle_threshold) {
                    discard = true;
                    break;
                }
            }
        }

        if (discard) {
            m_planes[polygon_id--] = std::move(m_planes.back());
            m_planes.pop_back();
            continue;
        }

        // We will shrink the polygon a little bit so it does not touch the object edges:
        Vec3d centroid = std::accumulate(polygon.begin(), polygon.end(), Vec3d(0.0, 0.0, 0.0));
        centroid /= (double)polygon.size();
        for (auto& vertex : polygon)
            vertex = 0.9f*vertex + 0.1f*centroid;

        // Polygon is now simple and convex, we'll round the corners to make them look nicer.
        // The algorithm takes a vertex, calculates middles of respective sides and moves the vertex
        // towards their average (controlled by 'aggressivity'). This is repeated k times.
        // In next iterations, the neighbours are not always taken at the middle (to increase the
        // rounding effect at the corners, where we need it most).
        const unsigned int k = 10; // number of iterations
        const float aggressivity = 0.2f;  // agressivity
        const unsigned int N = polygon.size();
        std::vector<std::pair<unsigned int, unsigned int>> neighbours;
        if (k != 0) {
            Pointf3s points_out(2*k*N); // vector long enough to store the future vertices
            for (unsigned int j=0; j<N; ++j) {
                points_out[j*2*k] = polygon[j];
                neighbours.push_back(std::make_pair((int)(j*2*k-k) < 0 ? (N-1)*2*k+k : j*2*k-k, j*2*k+k));
            }

            for (unsigned int i=0; i<k; ++i) {
                // Calculate middle of each edge so that neighbours points to something useful:
                for (unsigned int j=0; j<N; ++j)
                    if (i==0)
                        points_out[j*2*k+k] = 0.5f * (points_out[j*2*k] + points_out[j==N-1 ? 0 : (j+1)*2*k]);
                    else {
                        float r = 0.2+0.3/(k-1)*i; // the neighbours are not always taken in the middle
                        points_out[neighbours[j].first] = r*points_out[j*2*k] + (1-r) * points_out[neighbours[j].first-1];
                        points_out[neighbours[j].second] = r*points_out[j*2*k] + (1-r) * points_out[neighbours[j].second+1];
                    }
                // Now we have a triangle and valid neighbours, we can do an iteration:
                for (unsigned int j=0; j<N; ++j)
                    points_out[2*k*j] = (1-aggressivity) * points_out[2*k*j] +
                                        aggressivity*0.5f*(points_out[neighbours[j].first] + points_out[neighbours[j].second]);

                for (auto& n : neighbours) {
                    ++n.first;
                    --n.second;
                }
            }
            polygon = points_out; // replace the coarse polygon with the smooth one that we just created
        }


        // Raise a bit above the object surface to avoid flickering:
        for (auto& b : polygon)
            b(2) += 0.1f;

        // Transform back to 3D (and also back to mesh coordinates)
        polygon = transform(polygon, inst_matrix.inverse() * m.inverse());
    }
    if (m_planes.size() == 0) {
        m_show_warning = true;
        return;
    }

    // We'll sort the planes by area and only keep the 254 largest ones (because of the picking pass limitations):
    std::sort(m_planes.rbegin(), m_planes.rend(), [](const PlaneData& a, const PlaneData& b) { return a.area < b.area; });

    auto delte_index_to_end                    = [](int index, std::vector<PlaneData>& planes) {
        for (size_t i = planes.size() - 1; i >= index; i--) {
            planes.pop_back();
        }
    };
    const int plane_count = 30;
    for (size_t i = 0; i < m_planes.size(); i++) {
        if (m_planes[i].area < experted_minimal_area) {
            if (i + 1 >= plane_count) {
                delte_index_to_end(plane_count, m_planes);
                break;
            }
            else {//<plane_count
                for (size_t j = i + 1; j < m_planes.size(); j++) {
                    if (j + 1 >= plane_count) {
                        delte_index_to_end(plane_count, m_planes);
                        break;
                    }
                }
                break;
            }
        }
    }

    m_planes.resize(std::min((int)m_planes.size(), 254));

    // Planes are finished - let's save what we calculated it from:
    m_volumes_matrices.clear();
    m_volumes_types.clear();
    for (const ModelVolume* vol : mo->volumes) {
        m_volumes_matrices.push_back(vol->get_matrix());
        m_volumes_types.push_back(vol->type());
    }
    m_first_instance_scale = mo->instances.front()->get_scaling_factor();
    m_first_instance_mirror = mo->instances.front()->get_mirror();
    m_old_model_object = mo;

    // And finally create respective VBOs. The polygon is convex with
    // the vertices in order, so triangulation is trivial.
    for (auto& plane : m_planes) {
        plane.vbo.reserve(plane.vertices.size());
        for (const auto& vert : plane.vertices)
            plane.vbo.push_geometry(vert, plane.normal);
        for (size_t i=1; i<plane.vertices.size()-1; ++i)
            plane.vbo.push_triangle(0, i, i+1); // triangle fan
        plane.vbo.finalize_geometry(true);
        // FIXME: vertices should really be local, they need not
        // persist now when we use VBOs
        plane.vertices.clear();
        plane.vertices.shrink_to_fit();
    }
    m_show_warning = false;
    m_planes_valid = true;
}


bool GLGizmoFlatten::is_plane_update_necessary() const
{
    const ModelObject* mo = m_c->selection_info()->model_object();
    if (m_state != On || ! mo || mo->instances.empty())
        return false;

    if (! m_planes_valid || mo != m_old_model_object
     || mo->volumes.size() != m_volumes_matrices.size())
        return true;

    // We want to recalculate when the scale changes - some planes could (dis)appear.
    if (! mo->instances.front()->get_scaling_factor().isApprox(m_first_instance_scale)
     || ! mo->instances.front()->get_mirror().isApprox(m_first_instance_mirror))
        return true;

    for (unsigned int i=0; i < mo->volumes.size(); ++i)
        if (! mo->volumes[i]->get_matrix().isApprox(m_volumes_matrices[i])
         || mo->volumes[i]->type() != m_volumes_types[i])
            return true;

    return false;
}

Vec3d GLGizmoFlatten::get_flattening_normal() const
{
    Vec3d out = m_normal;
    m_normal = Vec3d::Zero();
    m_starting_center = Vec3d::Zero();
    return out;
}

} // namespace GUI
} // namespace Slic3r
