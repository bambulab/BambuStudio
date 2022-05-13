// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoModifier.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"

#include "libslic3r/Geometry/ConvexHull.hpp"
#include "libslic3r/Model.hpp"

#include <numeric>

#include <GL/glew.h>

namespace Slic3r {
namespace GUI {

GLGizmoModifier::GLGizmoModifier(GLCanvas3D &parent, const std::string &icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
{
}

bool GLGizmoModifier::on_init()
{
    // BBS
    m_shortcut_key = WXK_NONE;
    return true;
}

void GLGizmoModifier::on_set_state()
{
}

void GLGizmoModifier::on_render_input_window(float x, float y, float bottom_limit)
{
    // BBS: GUI refactor: move gizmo to the right
    m_imgui->set_next_window_pos(x, y, ImGuiCond_Always, 0.f, 0.0f);

    // BBS
    ImGuiWrapper::push_toolbar_style();

    std::string name = "Add Modifier##Modifier";
    m_imgui->begin(_L(name), ImGuiWrapper::TOOLBAR_WINDOW_FLAGS);

    for (auto &item : {L("Cube"), L("Cylinder"), L("Sphere"), L("Cone")}) {
        if (m_imgui->button(item)){
           wxGetApp().obj_list()->load_generic_subobject(item, ModelVolumeType::PARAMETER_MODIFIER);
        }
    }

    m_imgui->end();
    ImGuiWrapper::pop_toolbar_style();
}

CommonGizmosDataID GLGizmoModifier::on_get_requirements() const
{
    return CommonGizmosDataID::SelectionInfo;
}

std::string GLGizmoModifier::on_get_name() const
{
    return _u8L("Add Modifier");
}

bool GLGizmoModifier::on_is_activable() const
{
    return m_parent.get_selection().is_single_full_instance() && wxGetApp().get_mode() != comSimple;
}

void GLGizmoModifier::on_start_dragging()
{
    ;
}

void GLGizmoModifier::on_render()
{
    ;
}

void GLGizmoModifier::on_render_for_picking()
{
    ;
}


} // namespace GUI
} // namespace Slic3r
