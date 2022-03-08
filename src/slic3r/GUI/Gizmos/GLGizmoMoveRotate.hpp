#ifndef slic3r_GLGizmoMoveRotate_hpp_
#define slic3r_GLGizmoMoveRotate_hpp_

#include "GLGizmoBase.hpp"
#include "GLGizmoMove.hpp"
#include "GLGizmoRotate.hpp"
#include "../Jobs/RotoptimizeJob.hpp"
//BBS: add size adjust related
#include "GizmoObjectManipulation.hpp"

namespace Slic3r {
namespace GUI {

class GLGizmoMoveRotate3D : public GLGizmoBase
{
protected:
	std::vector<GLGizmoBase*> m_gizmos;
    bool m_is_move_operation;

	//BBS: add size adjust related
	GizmoObjectManipulation* m_object_manipulation;

public:
	GLGizmoMoveRotate3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id, GizmoObjectManipulation* obj_manipulation);

    std::string get_tooltip() const override
    {
        std::string tooltip;
        for (int i = 0; i < m_gizmos.size();i++) {
            tooltip = m_gizmos[i]->get_tooltip();
            if (!tooltip.empty())
                return tooltip;
        }
        return tooltip;
    }

    const Vec3d& get_displacement() const { return GLGizmoMove::m_displacement; }

    Vec3d get_rotation() const {
        return Vec3d(dynamic_cast<GLGizmoRotate*>(m_gizmos[X])->get_angle(),
            dynamic_cast<GLGizmoRotate*>(m_gizmos[Y])->get_angle(),
            dynamic_cast<GLGizmoRotate*>(m_gizmos[Z])->get_angle());
    }

    void set_rotation(const Vec3d& rotation) {
        dynamic_cast<GLGizmoRotate*>(m_gizmos[X])->set_angle(rotation(0));
        dynamic_cast<GLGizmoRotate*>(m_gizmos[Y])->set_angle(rotation(1));
        dynamic_cast<GLGizmoRotate*>(m_gizmos[Z])->set_angle(rotation(2));
    }

    bool is_rotate_idx(int idx) { return idx >= 0 && idx < 3; }
    bool is_move_idx(int idx) { return idx >= 3 && idx < 6; }
    bool is_move_operation() { return m_is_move_operation; }

protected:
    bool on_init() override;
    std::string on_get_name() const override;

    void on_set_hover_id() override
    {
        for (int i = 0; i < m_gizmos.size(); ++i)
            m_gizmos[i]->set_hover_id((m_hover_id == i) ? i : -1);
    }

    void on_enable_grabber(unsigned int id) override
    {
        if (id < m_gizmos.size())
            m_gizmos[id]->enable_grabber(0);
    }

    void on_disable_grabber(unsigned int id) override
    {
        if (id < m_gizmos.size())
            m_gizmos[id]->disable_grabber(0);
    }

    bool on_is_activable() const override;
    void on_start_dragging() override;
    void on_stop_dragging() override;
    void on_update(const UpdateData& data) override
    {
        if (m_hover_id == -1) {
            for (auto &g : m_gizmos) {
                g->update(data);
            }
        } else if (is_rotate_idx(m_hover_id)) {
            for (int i = 0; i < 3; i++) {
                m_gizmos[i]->update(data);
            }
        } else if (is_move_idx(m_hover_id)) {
            for (int i = 3; i < 6; i++) {
                m_gizmos[i]->update(data);
            }
        }
    }
    void on_render() override;
    void on_render_for_picking() override
    {
        for (auto& g : m_gizmos) {
            g->render_for_picking();
        }
    }

    void on_render_input_window(float x, float y, float bottom_limit) override;
};


} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoRotate_hpp_
