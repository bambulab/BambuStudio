// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoMoveRotate.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"

#include <GL/glew.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "slic3r/GUI/Jobs/RotoptimizeJob.hpp"

namespace Slic3r {
namespace GUI {

GLGizmoMoveRotate3D::GLGizmoMoveRotate3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id, GizmoObjectManipulation* obj_manipulation)
    : GLGizmoBase(parent, icon_filename, sprite_id)
    //BBS: GUI refactor: add obj manipulation
    , m_object_manipulation(obj_manipulation)
{
    /* rotate gizmos */
    m_gizmos.emplace_back(new GLGizmoRotate(parent, GLGizmoRotate::X));
    m_gizmos.emplace_back(new GLGizmoRotate(parent, GLGizmoRotate::Y));
    m_gizmos.emplace_back(new GLGizmoRotate(parent, GLGizmoRotate::Z));

    /* move gizmos */
    m_gizmos.emplace_back(new GLGizmoMove(parent, GLGizmoMove::X));
    m_gizmos.emplace_back(new GLGizmoMove(parent, GLGizmoMove::Y));
    m_gizmos.emplace_back(new GLGizmoMove(parent, GLGizmoMove::Z));

    for (unsigned int i = 0; i < 3; ++i) {
        m_gizmos[i]->set_group_id(i);
    }

    for (int i = 0; i < m_gizmos.size(); i++) {
        m_gizmos[i]->set_gizmo_index(i);
    }
}

bool GLGizmoMoveRotate3D::on_init()
{
    for (auto& g : m_gizmos) {
        if (!g->init())
            return false;
    }
    
    // set rotate axes color
    for (unsigned int i = 0; i < 3; ++i) {
        m_gizmos[i]->set_highlight_color(AXES_COLOR[i]);
    }

    m_shortcut_key = WXK_CONTROL_M;
    return true;
}

std::string GLGizmoMoveRotate3D::on_get_name() const
{
    return _u8L("Move and Rotate");
}

bool GLGizmoMoveRotate3D::on_is_activable() const
{
    return !m_parent.get_selection().is_empty();
}

void GLGizmoMoveRotate3D::on_start_dragging()
{
    if (is_move_idx(m_hover_id))
        m_is_move_operation = true;
    else
        m_is_move_operation = false;
    if ((0 <= m_hover_id) && (m_hover_id < m_gizmos.size()))
        m_gizmos[m_hover_id]->start_dragging();
}

void GLGizmoMoveRotate3D::on_stop_dragging()
{
    if ((0 <= m_hover_id) && (m_hover_id < m_gizmos.size()))
        m_gizmos[m_hover_id]->stop_dragging();
}

void GLGizmoMoveRotate3D::on_render()
{
    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));

    for (int i = 0; i < m_gizmos.size(); i++) {
        if (m_hover_id == -1 || m_hover_id == i) {
            m_gizmos[i]->render();
        }
    }
}

void GLGizmoMoveRotate3D::on_render_input_window(float x, float y, float bottom_limit)
{
    if (m_object_manipulation)
        m_object_manipulation->do_render_input_window(m_imgui, "Rotate", x, y, bottom_limit);
}

} // namespace GUI
} // namespace Slic3r
