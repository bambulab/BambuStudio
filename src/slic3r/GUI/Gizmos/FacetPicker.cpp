#include "FacetPicker.hpp"

#include "GLGizmoBase.hpp"
#include "GLGizmosCommon.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Selection.hpp"
#include "libslic3r/Model.hpp"

#include <GL/glew.h>

#include <limits>

namespace Slic3r {
namespace GUI {

Vec3d FacetPicker::world_normal(const Transform3d &trafo, const Vec3f &mesh_normal)
{
    const Vec3d  n   = trafo.linear().inverse().transpose() * mesh_normal.cast<double>();
    const double len = n.norm();
    return (len < EPSILON) ? Vec3d::Zero() : Vec3d(n / len);
}

void FacetPicker::set_active(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (!m_active)
        reset();
}

void FacetPicker::reset()
{
    m_hit            = Hit();
    m_rendered_facet = -1;
    m_highlight.reset();
}

bool FacetPicker::update(const Vec2d &mouse_position, const CommonGizmosDataPool *pool, const Selection &selection, const Camera &camera)
{
    m_hit = Hit();

    if (pool == nullptr || pool->raycaster() == nullptr || pool->selection_info() == nullptr)
        return false;

    const ModelObject *mo           = pool->selection_info()->model_object();
    const int          instance_idx = selection.get_instance_idx();
    if (mo == nullptr || instance_idx < 0 || instance_idx >= int(mo->instances.size()))
        return false;

    // Raycaster builds one MeshRaycaster per model-part volume, in mo->volumes order, so
    // build the matching transform list the same way.
    const ModelInstance *mi        = mo->instances[instance_idx];
    const double         sla_shift = pool->selection_info()->get_sla_shift();

    std::vector<Transform3d>         trafo_matrices;
    std::vector<const ModelVolume *> part_volumes;
    for (const ModelVolume *mv : mo->volumes) {
        if (mv->is_model_part()) {
            trafo_matrices.emplace_back(Geometry::translation_transform(sla_shift * Vec3d::UnitZ()) *
                                        mi->get_transformation().get_matrix() * mv->get_matrix());
            part_volumes.emplace_back(mv);
        }
    }

    const std::vector<const MeshRaycaster *> raycasters = pool->raycaster()->raycasters();
    if (raycasters.size() != trafo_matrices.size())
        return false; // hollowed-mesh or stale-cache case; skip rather than mis-index

    Vec3f  hit = Vec3f::Zero(), normal = Vec3f::Zero();
    Vec3f  closest_hit = Vec3f::Zero(), closest_normal = Vec3f::Zero();
    double closest_sq    = std::numeric_limits<double>::max();
    int    closest_id    = -1;
    size_t closest_facet = 0;

    for (int mesh_id = 0; mesh_id < int(trafo_matrices.size()); ++mesh_id) {
        size_t facet = 0;
        if (raycasters[mesh_id]->unproject_on_mesh(mouse_position, trafo_matrices[mesh_id], camera, hit, normal, nullptr, &facet)) {
            const double sq = (camera.get_position() - trafo_matrices[mesh_id] * hit.cast<double>()).squaredNorm();
            if (sq < closest_sq) {
                closest_sq     = sq;
                closest_id     = mesh_id;
                closest_hit    = hit;
                closest_normal = normal;
                // Captured inside the nearest-hit branch on purpose. The equivalent loop in
                // GLGizmoFlatten::update_raycast_cache captures it after the loop, so on a
                // multi-volume object it can index the last-tested volume's facet into the
                // nearest volume's mesh.
                closest_facet  = facet;
            }
        }
    }

    if (closest_id < 0)
        return false;

    const ModelVolume *mv = part_volumes[closest_id];
    if (closest_facet >= mv->mesh().its.indices.size())
        return false;

    const Vec3d n = world_normal(trafo_matrices[closest_id], closest_normal);
    if (n.isZero())
        return false; // degenerate facet

    m_hit.facet        = int(closest_facet);
    m_hit.volume       = mv;
    m_hit.trafo        = trafo_matrices[closest_id];
    m_hit.world_pos    = trafo_matrices[closest_id] * closest_hit.cast<double>();
    m_hit.world_normal = n;
    return true;
}

void FacetPicker::render(const Camera &camera)
{
    if (!m_hit.valid())
        return;

    // Rebuild only when the hovered facet actually changes.
    if (m_rendered_facet != m_hit.facet) {
        m_rendered_facet = m_hit.facet;
        m_highlight.reset();

        const indexed_triangle_set &its = m_hit.volume->mesh().its;
        const auto                 &tri = its.indices[m_hit.facet];
        // Lift off the surface so the highlight wins the depth test against the model.
        const Vec3d bias = m_hit.world_normal * 0.05;

        indexed_triangle_set temp_its;
        for (int i = 0; i < 3; ++i) {
            const Vec3d v = m_hit.trafo * its.vertices[tri[i]].cast<double>() + bias;
            temp_its.vertices.push_back(v.cast<float>());
        }
        temp_its.indices.push_back({0, 1, 2});
        m_highlight.init_from(temp_its);
    }

    if (!m_highlight.is_initialized())
        return;

    const auto &p_flat_shader = wxGetApp().get_shader("flat");
    if (!p_flat_shader)
        return;

    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_CULL_FACE));
    wxGetApp().bind_shader(p_flat_shader);
    p_flat_shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    // Vertices are already in world space, so the model part of the matrix is identity.
    p_flat_shader->set_uniform("view_model_matrix", camera.get_view_matrix());
    m_highlight.set_color(GLGizmoBase::FLATTEN_HOVER_COLOR);
    m_highlight.render_geometry();
    wxGetApp().unbind_shader();
    glsafe(::glEnable(GL_CULL_FACE));
}

} // namespace GUI
} // namespace Slic3r
