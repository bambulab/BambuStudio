#ifndef slic3r_GLGizmoFlatten_hpp_
#define slic3r_GLGizmoFlatten_hpp_

#include "GLGizmoBase.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "libslic3r/Model.hpp"

namespace Slic3r {
enum class ModelVolumeType : int;
namespace GUI {

class GLGizmoFlatten : public GLGizmoBase
{
// This gizmo does not use grabbers. The m_hover_id relates to polygon managed by the class itself.

private:
    mutable Vec3d m_normal;

    struct PlaneData {
        std::vector<Vec3d> vertices; // should be in fact local in update_planes()
        GLIndexedVertexArray vbo;
        Vec3d normal;
        float area;
    };

    // This holds information to decide whether recalculation is necessary:
    std::vector<Transform3d> m_volumes_matrices;
    std::vector<ModelVolumeType> m_volumes_types;
    Vec3d m_first_instance_scale;
    Vec3d m_first_instance_mirror;

    std::vector<PlaneData> m_planes;
    bool m_planes_valid = false;
    mutable Vec3d m_starting_center;
    const ModelObject* m_old_model_object = nullptr;
    std::vector<const Transform3d*> instances_matrices;

    void update_planes();
    bool is_plane_update_necessary() const;

    enum FlattenType {
        Default,
        Triangle,
    };
    FlattenType m_faltten_type{FlattenType::Default};

public:
    GLGizmoFlatten(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    Vec3d get_flattening_normal() const;
    void  data_changed(bool is_serializing) override;

protected:
    virtual bool on_init() override;
    virtual std::string on_get_name() const override;
    virtual std::string on_get_name_str() override { return "Lay on face"; }
    virtual bool on_is_activable() const override;
    virtual void on_start_dragging() override;
    virtual void on_render() override;
    virtual void on_render_for_picking() override;
    virtual void on_set_state() override;
    virtual CommonGizmosDataID on_get_requirements() const override;
    virtual void               on_render_input_window(float x, float y, float bottom_limit) override;
private:
    bool  m_show_warning{false};
    mutable RaycastResult m_rr;
    mutable int           m_hit_facet;
    mutable int           m_last_hit_facet;
    mutable GLModel  m_one_tri_model;
    Vec3f                 m_hit_object_normal;
    int                   m_old_instance_id{-1};

private:
    bool update_raycast_cache(const Vec2d &mouse_position, const Camera &camera, const std::vector<Transform3d> &trafo_matrices, int &facet);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoFlatten_hpp_
