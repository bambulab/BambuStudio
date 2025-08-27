#include "libslic3r/libslic3r.h"
#include "GLGizmosManager.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/Utils/UndoRedo.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/GLToolbar.hpp"

#include "slic3r/GUI/Gizmos/GLGizmoMove.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoScale.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoRotate.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoFlatten.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoSlaSupports.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoFdmSupports.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoBrimEars.hpp"
// BBS
#include "slic3r/GUI/Gizmos/GLGizmoAdvancedCut.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoFaceDetector.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoHollow.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoSeam.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoMmuSegmentation.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoFuzzySkin.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoSimplify.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoText.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoSVG.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoMeshBoolean.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoAssembly.hpp"

#include "libslic3r/format.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"

#include <wx/glcanvas.h>

namespace Slic3r {
namespace GUI {
//BBS: GUI refactor: to support top layout
#if BBS_TOOLBAR_ON_TOP
const float GLGizmosManager::Default_Icons_Size = 40;
#else
const float GLGizmosManager::Default_Icons_Size = 64;
#endif

GLGizmosManager::GLGizmosManager(GLCanvas3D& parent)
    : m_parent(parent)
    , m_enabled(false)
    , m_current(Undefined)
    , m_serializing(false)
    //BBS: GUI refactor: add object manipulation in gizmo
    , m_object_manipulation(parent)
{
    m_timer_set_color.Bind(wxEVT_TIMER, &GLGizmosManager::on_set_color_timer, this);
}

std::vector<size_t> GLGizmosManager::get_selectable_idxs() const
{
    std::vector<size_t> out;
    if (m_parent.get_canvas_type() == GLCanvas3D::CanvasAssembleView) {
        for (size_t i = 0; i < m_gizmos.size(); ++i)
            if (m_gizmos[i]->get_sprite_id() == (unsigned int) Move ||
                m_gizmos[i]->get_sprite_id() == (unsigned int) Rotate ||
                m_gizmos[i]->get_sprite_id() == (unsigned int) Measure ||
                m_gizmos[i]->get_sprite_id() == (unsigned int) Assembly ||
                m_gizmos[i]->get_sprite_id() == (unsigned int) MmuSegmentation)
                out.push_back(i);
    }
    else {
        for (size_t i = 0; i < m_gizmos.size(); ++i)
            if (m_gizmos[i]->is_selectable())
                out.push_back(i);
    }
    return out;
}

bool GLGizmosManager::init()
{
    if (!m_gizmos.empty())
        return true;
    init_icon_textures();

    // Order of gizmos in the vector must match order in EType!
    //BBS: GUI refactor: add obj manipulation
    m_gizmos.clear();
    unsigned int sprite_id = 0;
    m_gizmos.emplace_back(new GLGizmoMove3D(m_parent, EType::Move, &m_object_manipulation));
    m_gizmos.emplace_back(new GLGizmoRotate3D(m_parent, EType::Rotate, &m_object_manipulation));
    m_gizmos.emplace_back(new GLGizmoScale3D(m_parent, EType::Scale, &m_object_manipulation));
    m_gizmos.emplace_back(new GLGizmoFlatten(m_parent, EType::Flatten));
    m_gizmos.emplace_back(new GLGizmoAdvancedCut(m_parent, EType::Cut));
    m_gizmos.emplace_back(new GLGizmoMeshBoolean(m_parent, EType::MeshBoolean));
    m_gizmos.emplace_back(new GLGizmoAssembly(m_parent, EType::Assembly));
    m_gizmos.emplace_back(new GLGizmoMmuSegmentation(m_parent, EType::MmuSegmentation));
    m_gizmos.emplace_back(new GLGizmoText(m_parent, EType::Text));
    m_gizmos.emplace_back(new GLGizmoSVG(m_parent, EType::Svg));
    m_gizmos.emplace_back(new GLGizmoFdmSupports(m_parent, EType::FdmSupports));
    m_gizmos.emplace_back(new GLGizmoSeam(m_parent, EType::Seam));
    m_gizmos.emplace_back(new GLGizmoBrimEars(m_parent, EType::BrimEars));
    m_gizmos.emplace_back(new GLGizmoFuzzySkin(m_parent, EType::FuzzySkin));
    m_gizmos.emplace_back(new GLGizmoMeasure(m_parent, EType::Measure));
    m_gizmos.emplace_back(new GLGizmoSimplify(m_parent, EType::Simplify));

    //m_gizmos.emplace_back(new GLGizmoSlaSupports(m_parent, sprite_id++));
    //m_gizmos.emplace_back(new GLGizmoFaceDetector(m_parent, sprite_id++));
    //m_gizmos.emplace_back(new GLGizmoHollow(m_parent, sprite_id++));

    m_common_gizmos_data.reset(new CommonGizmosDataPool(&m_parent));
    if(!m_assemble_view_data)
        m_assemble_view_data.reset(new AssembleViewDataPool(&m_parent));

    for (auto& gizmo : m_gizmos) {
        if (! gizmo->init()) {
            m_gizmos.clear();
            return false;
        }
        gizmo->set_common_data_pool(m_common_gizmos_data.get());
        gizmo->on_change_color_mode(m_is_dark);
    }

    m_current = Undefined;
    m_hover = Undefined;
    m_highlight = std::pair<EType, bool>(Undefined, false);

    return true;
}

std::map<int, void *> GLGizmosManager::icon_list = {};
bool GLGizmosManager::init_icon_textures()
{
    if (icon_list.size() > 0) {
        return true;
    }
    ImTextureID texture_id;

    icon_list.clear();
    if (IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/toolbar_reset.svg", 14, 14, texture_id))
        icon_list.insert(std::make_pair((int)IC_TOOLBAR_RESET, texture_id));
    else
        return false;

    if (IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/toolbar_reset_hover.svg", 14, 14, texture_id))
        icon_list.insert(std::make_pair((int)IC_TOOLBAR_RESET_HOVER, texture_id));
    else
        return false;

    if (IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/toolbar_reset_zero.svg", 14, 14, texture_id))
        icon_list.insert(std::make_pair((int) IC_TOOLBAR_RESET_ZERO, texture_id));
    else
        return false;

    if (IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/toolbar_reset_zero_hover.svg", 14, 14, texture_id))
        icon_list.insert(std::make_pair((int) IC_TOOLBAR_RESET_ZERO_HOVER, texture_id));
    else
        return false;

    if (IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/toolbar_tooltip.svg", 30, 22, texture_id))
        icon_list.insert(std::make_pair((int)IC_TOOLBAR_TOOLTIP, texture_id));
    else
        return false;

    if (IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/toolbar_tooltip_hover.svg", 30, 22, texture_id))
        icon_list.insert(std::make_pair((int)IC_TOOLBAR_TOOLTIP_HOVER, texture_id));
    else
        return false;

    if (IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/fit_camera.svg", 64, 64, texture_id))
        icon_list.insert(std::make_pair((int) IC_FIT_CAMERA, texture_id));
    else
        return false;

    if (IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/fit_camera_hover.svg", 64, 64, texture_id))
        icon_list.insert(std::make_pair((int) IC_FIT_CAMERA_HOVER, texture_id));
    else
        return false;

    if (IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/fit_camera_dark.svg", 64, 64, texture_id))
        icon_list.insert(std::make_pair((int) IC_FIT_CAMERA_DARK, texture_id));
    else
        return false;

    if (IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/fit_camera_dark_hover.svg", 64, 64, texture_id))
        icon_list.insert(std::make_pair((int) IC_FIT_CAMERA_DARK_HOVER, texture_id));
    else
        return false;

    if (IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/helio_icon.svg", 16, 16, texture_id))
        icon_list.insert(std::make_pair((int) IC_HELIO_ICON, texture_id));
    else
        return false;

     if (IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/text_B.svg", 20, 20, texture_id))
        icon_list.insert(std::make_pair((int)IC_TEXT_B, texture_id));
    else
        return false;

     if (IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/text_B_dark.svg", 20, 20, texture_id))
         icon_list.insert(std::make_pair((int)IC_TEXT_B_DARK, texture_id));
     else
         return false;

     if (IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/text_T.svg", 20, 20, texture_id))
        icon_list.insert(std::make_pair((int)IC_TEXT_T, texture_id));
    else
        return false;

     if (IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/text_T_dark.svg", 20, 20, texture_id))
         icon_list.insert(std::make_pair((int)IC_TEXT_T_DARK, texture_id));
     else
         return false;

    return true;
}

void GLGizmosManager::refresh_on_off_state()
{
    if (m_serializing || m_current == Undefined || m_gizmos.empty())
        return;

    if (m_current != Undefined
    && ! m_gizmos[m_current]->is_activable() && activate_gizmo(Undefined))
        update_data();
}

void GLGizmosManager::reset_all_states()
{
    if (! m_enabled || m_serializing)
        return;

    const EType current = get_current_type();
    if (current != Undefined)
        // close any open gizmo
        open_gizmo(current);

    activate_gizmo(Undefined);
    //do not clear hover state, as Emboss gizmo can be used without selection
    //m_hover = Undefined;
}

bool GLGizmosManager::open_gizmo(EType type)
{
    int idx = int(type);
    if (m_gizmos[idx]->is_activable()
     && activate_gizmo(m_current == idx ? Undefined : (EType)idx)) {
        update_data();
#ifdef __WXOSX__
        m_parent.post_event(SimpleEvent(wxEVT_PAINT));
#endif
        return true;
    }
    return false;
}

bool GLGizmosManager::open_gizmo(unsigned char type)
{
    return open_gizmo((EType)type);
}

bool GLGizmosManager::check_gizmos_closed_except(EType type) const
{
    if (get_current_type() != type && get_current_type() != Undefined) {
        wxGetApp().plater()->get_notification_manager()->push_notification(
                    NotificationType::CustomSupportsAndSeamRemovedAfterRepair,
                    NotificationManager::NotificationLevel::PrintInfoNotificationLevel,
                    _u8L("Error: Please close all toolbar menus first"));
        return false;
    }
    return true;
}

void GLGizmosManager::set_hover_id(int id)
{
    if (m_current == EType::Measure || m_current == EType::Assembly) { return; }
    if (!m_enabled || m_current == Undefined)
        return;

    m_gizmos[m_current]->set_hover_id(id);
}

void GLGizmosManager::update(const Linef3& mouse_ray, const Point& mouse_pos)
{
    if (!m_enabled)
        return;

    GLGizmoBase* curr = get_current();
    if (curr != nullptr)
        curr->update(GLGizmoBase::UpdateData(mouse_ray, mouse_pos));
}

void GLGizmosManager::update_assemble_view_data()
{
    if (m_assemble_view_data) {
        if (!wxGetApp().plater()->get_assmeble_canvas3D()->get_wxglcanvas()->IsShown())
            m_assemble_view_data->update(AssembleViewDataID(0));
        else
            m_assemble_view_data->update(AssembleViewDataID((int)AssembleViewDataID::ModelObjectsInfo | (int)AssembleViewDataID::ModelObjectsClipper));
    }
}

void GLGizmosManager::update_data()
{
    if (!m_enabled)
        return;
    wxBusyCursor     wait;
    const Selection& selection = m_parent.get_selection();
    if (m_common_gizmos_data) {
        m_common_gizmos_data->update(get_current()
            ? get_current()->get_requirements()
            : CommonGizmosDataID(0));
    }
    if (m_current != Undefined)
        m_gizmos[m_current]->data_changed(m_serializing);

    //BBS: GUI refactor: add object manipulation in gizmo
    if (m_current == EType::Move || m_current == EType::Rotate || m_current == EType::Scale) {
        if (!selection.is_empty()) {
            m_object_manipulation.update_ui_from_settings();
            m_object_manipulation.UpdateAndShow(true);
        }
    }
}

bool GLGizmosManager::is_running() const
{
    if (!m_enabled)
        return false;

    //GLGizmoBase* curr = get_current();
    //return (curr != nullptr) ? (curr->get_state() == GLGizmoBase::On) : false;
    return m_current != Undefined;
}

bool GLGizmosManager::handle_shortcut(int key)
{
    if (!m_enabled)
        return false;

    auto it = std::find_if(m_gizmos.begin(), m_gizmos.end(),
            [key](const std::unique_ptr<GLGizmoBase>& gizmo) {
                int gizmo_key = gizmo->get_shortcut_key();
                return gizmo->is_activable()
                       && ((gizmo_key == key - 64) || (gizmo_key == key - 96));
    });

    if (it == m_gizmos.end())
        return false;

    // allowe open shortcut even when selection is empty
    if (Text == it - m_gizmos.begin()) {
        if (dynamic_cast<GLGizmoText *>(m_gizmos[Text].get())->on_shortcut_key()) {
            return true;
        }
    }

    EType gizmo_type = EType(it - m_gizmos.begin());
    return open_gizmo(gizmo_type);
}

bool GLGizmosManager::is_dragging() const
{
    if (! m_enabled || m_current == Undefined)
        return false;

    return m_gizmos[m_current]->is_dragging();
}

void GLGizmosManager::start_dragging()
{
    if (! m_enabled || m_current == Undefined)
        return;
    m_gizmos[m_current]->start_dragging();
}

void GLGizmosManager::stop_dragging()
{
    if (! m_enabled || m_current == Undefined)
        return;

    m_gizmos[m_current]->stop_dragging();
}

Vec3d GLGizmosManager::get_displacement() const
{
    if (!m_enabled)
        return Vec3d::Zero();

    return dynamic_cast<GLGizmoMove3D*>(m_gizmos[Move].get())->get_displacement();
}

Vec3d GLGizmosManager::get_scale() const
{
    if (!m_enabled)
        return Vec3d::Ones();

    return dynamic_cast<GLGizmoScale3D*>(m_gizmos[Scale].get())->get_scale();
}

void GLGizmosManager::set_scale(const Vec3d& scale)
{
    if (!m_enabled || m_gizmos.empty())
        return;

    dynamic_cast<GLGizmoScale3D*>(m_gizmos[Scale].get())->set_scale(scale);
}

Vec3d GLGizmosManager::get_scale_offset() const
{
    if (!m_enabled || m_gizmos.empty())
        return Vec3d::Zero();

    return dynamic_cast<GLGizmoScale3D*>(m_gizmos[Scale].get())->get_offset();
}

Vec3d GLGizmosManager::get_rotation() const
{
    if (!m_enabled || m_gizmos.empty())
        return Vec3d::Zero();

    return dynamic_cast<GLGizmoRotate3D*>(m_gizmos[Rotate].get())->get_rotation();
}

void GLGizmosManager::set_rotation(const Vec3d& rotation)
{
    if (!m_enabled || m_gizmos.empty())
        return;
    dynamic_cast<GLGizmoRotate3D*>(m_gizmos[Rotate].get())->set_rotation(rotation);
}

void GLGizmosManager::update_paint_base_camera_rotate_rad()
{
    if (m_current == MmuSegmentation || m_current == Seam) {
        auto paint_gizmo = dynamic_cast<GLGizmoPainterBase*>(m_gizmos[m_current].get());
        paint_gizmo->update_front_view_radian();
    }
}

Vec3d GLGizmosManager::get_flattening_normal() const
{
    if (!m_enabled || m_gizmos.empty())
        return Vec3d::Zero();

    return dynamic_cast<GLGizmoFlatten*>(m_gizmos[Flatten].get())->get_flattening_normal();
}

bool GLGizmosManager::is_gizmo_activable_when_single_full_instance()
{
    if (get_current_type() == GLGizmosManager::EType::Flatten ||
        get_current_type() == GLGizmosManager::EType::Cut ||
        get_current_type() == GLGizmosManager::EType::MeshBoolean ||
        get_current_type() == GLGizmosManager::EType::Text ||
        is_paint_gizmo() ||
        get_current_type() == GLGizmosManager::EType::Simplify
        ) {
        return true;
    }
    return false;
}

bool GLGizmosManager::is_gizmo_click_empty_not_exit()
{
   if (get_current_type() == GLGizmosManager::EType::Cut ||
       get_current_type() == GLGizmosManager::EType::MeshBoolean ||
       is_paint_gizmo() ||
       get_current_type() == GLGizmosManager::EType::Measure ||
       get_current_type() == GLGizmosManager::EType::Assembly) {
        return true;
    }
    return false;
}

bool GLGizmosManager::is_only_text_volume() const {
    auto gizmo_text = dynamic_cast<GLGizmoText *>(get_current());
    if (gizmo_text->is_only_text_case()) {
        return true;
    }
    return false;
}

bool GLGizmosManager::is_show_only_active_plate() const
{
    if (get_current_type() == GLGizmosManager::EType::Cut) {
        return true;
    }
    return false;
}

bool GLGizmosManager::is_ban_move_glvolume() const
{
    auto current_type = get_current_type();
    if (current_type == GLGizmosManager::EType::Undefined ||
        current_type == GLGizmosManager::EType::Move ||
        current_type == GLGizmosManager::EType::Rotate ||
        current_type == GLGizmosManager::EType::Scale) {
        return false;
    }
    return true;
}

bool GLGizmosManager::get_gizmo_active_condition(GLGizmosManager::EType type) {
    if (auto cur_gizmo = get_gizmo(type)) {
        return cur_gizmo->is_activable();
    }
    return false;
}

void GLGizmosManager::update_show_only_active_plate()
{
    if (is_show_only_active_plate()) {
        check_object_located_outside_plate();
    }
}

void GLGizmosManager::check_object_located_outside_plate(bool change_plate)
{
    PartPlateList &plate_list       = wxGetApp().plater()->get_partplate_list();
    auto           curr_plate_index = plate_list.get_curr_plate_index();
    Selection &    selection        = m_parent.get_selection();
    auto           idxs             = selection.get_volume_idxs();
    m_object_located_outside_plate  = false;
    if (idxs.size() > 0) {
        const GLVolume *v          = selection.get_volume(*idxs.begin());
        int             object_idx = v->object_idx();
        const Model *   m_model    = m_parent.get_model();
        if (0 <= object_idx && object_idx < (int) m_model->objects.size()) {
            bool         find_object  = false;
            ModelObject *model_object = m_model->objects[object_idx];
            for (size_t i = 0; i < plate_list.get_plate_count(); i++) {
                auto            plate   = plate_list.get_plate(i);
                ModelObjectPtrs objects = plate->get_objects_on_this_plate();
                for (auto object : objects) {
                    if (model_object == object) {
                        if (change_plate && curr_plate_index != i) { // confirm selected model_object at corresponding plate
                            wxGetApp().plater()->get_partplate_list().select_plate(i);
                        }
                        find_object = true;
                    }
                }
            }
            if (!find_object) {
                m_object_located_outside_plate = true;
            }
        }
    }
}

// Returns true if the gizmo used the event to do something, false otherwise.
bool GLGizmosManager::gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down)
{
    if (!m_enabled || m_gizmos.empty())
        return false;

    for (size_t i = 0; i < m_gizmos.size(); ++i) {
        if (m_gizmos[i]->get_sprite_id() == static_cast<int>(m_current)) {
            return m_gizmos[i]->gizmo_event(action, mouse_position, shift_down, alt_down, control_down);
        }
    }

    return false;
}

bool GLGizmosManager::is_paint_gizmo() const
{
    return m_current == EType::FdmSupports ||
           m_current == EType::FuzzySkin ||
           m_current == EType::MmuSegmentation ||
           m_current == EType::Seam;
}

bool GLGizmosManager::is_allow_select_all() const {
    if (m_current == Undefined || m_current == EType::Move||
        m_current == EType::Rotate ||
        m_current == EType::Scale) {
        return true;
    }
    return false;
}

bool GLGizmosManager::is_allow_show_volume_highlight_outline() const
{
    if (m_current == EType::Cut) {
        return false;
    }
    return true;
}

bool GLGizmosManager::is_allow_drag_volume() const
{
    if (m_current == EType::Cut) { return false; }
    return true;
}

bool GLGizmosManager::is_allow_mouse_drag_selected() const
{
    if (m_current == Measure || m_current == Assembly)
        return false;
    return true;
}

ClippingPlane GLGizmosManager::get_clipping_plane() const
{
    if (! m_common_gizmos_data
     || ! m_common_gizmos_data->object_clipper()
     || m_common_gizmos_data->object_clipper()->get_position() == 0.)
        return ClippingPlane::ClipsNothing();
    else {
        const ClippingPlane& clp = *m_common_gizmos_data->object_clipper()->get_clipping_plane();
        return ClippingPlane(-clp.get_normal(), clp.get_data()[3]);
    }
}

ClippingPlane GLGizmosManager::get_assemble_view_clipping_plane() const
{
    if (!m_assemble_view_data
        || !m_assemble_view_data->model_objects_clipper()
        || m_assemble_view_data->model_objects_clipper()->get_position() == 0.)
        return ClippingPlane::ClipsNothing();
    else {
        const ClippingPlane& clp = *m_assemble_view_data->model_objects_clipper()->get_clipping_plane();
        return ClippingPlane(-clp.get_normal(), clp.get_data()[3]);
    }
}

bool GLGizmosManager::wants_reslice_supports_on_undo() const
{
    return (m_current == SlaSupports
        && dynamic_cast<const GLGizmoSlaSupports*>(m_gizmos.at(SlaSupports).get())->has_backend_supports());
}

void GLGizmosManager::on_change_color_mode(bool is_dark) {
    m_is_dark = is_dark;
}

void GLGizmosManager::render_current_gizmo() const
{
    if (!m_enabled || m_current == Undefined)
        return;

    m_gizmos[m_current]->render();
}

void GLGizmosManager::render_painter_gizmo() const
{
    // This function shall only be called when current gizmo is
    // derived from GLGizmoPainterBase.

    if (!m_enabled || m_current == Undefined)
        return;

    auto *gizmo = dynamic_cast<GLGizmoPainterBase*>(get_current());
    assert(gizmo); // check the precondition
    gizmo->render_painter_gizmo();
}

void GLGizmosManager::render_painter_assemble_view() const
{
    if (m_assemble_view_data && m_assemble_view_data->model_objects_clipper())
        m_assemble_view_data->model_objects_clipper()->render_cut();
}

void GLGizmosManager::render_current_gizmo_for_picking_pass() const
{
    if (! m_enabled || m_current == Undefined)

        return;

    m_gizmos[m_current]->render_for_picking();
}

std::string GLGizmosManager::get_tooltip() const
{
    const GLGizmoBase* curr = get_current();
    return (curr != nullptr) ? curr->get_tooltip() : "";
}

bool GLGizmosManager::on_mouse_wheel(wxMouseEvent& evt)
{
    bool processed = false;

    if (m_current == SlaSupports || m_current == Hollow || is_paint_gizmo() || m_current == BrimEars) {
        float rot = (float)evt.GetWheelRotation() / (float)evt.GetWheelDelta();
        if (gizmo_event((rot > 0.f ? SLAGizmoEventType::MouseWheelUp : SLAGizmoEventType::MouseWheelDown), Vec2d::Zero(), evt.ShiftDown(), evt.AltDown()
            // BBS
#ifdef __WXOSX_MAC__
            , evt.RawControlDown()
#else
            , evt.ControlDown()
#endif
            ))
            processed = true;
    }

    return processed;
}

bool GLGizmosManager::on_mouse(wxMouseEvent& evt)
{
    // used to set a right up event as processed when needed
    static bool pending_right_up = false;

    Point pos(evt.GetX(), evt.GetY());
    Vec2d mouse_pos((double)evt.GetX(), (double)evt.GetY());

    Selection& selection = m_parent.get_selection();
    int selected_object_idx = selection.get_object_idx();
    bool processed = false;

    // when control is down we allow scene pan and rotation even when clicking over some object
    bool control_down = evt.CmdDown();
    if (m_current != Undefined) {
        // check if gizmo override method could be slower than simple call virtual function
        // &m_gizmos[m_current]->on_mouse != &GLGizmoBase::on_mouse &&
        m_gizmos[m_current]->on_mouse(evt);
    }
    // mouse anywhere
    if (evt.LeftDClick()) {
        if (m_current == Text  || m_current == Svg) {
            return false;
        }
    }
    else if (evt.Moving()) {
        if (is_paint_gizmo() ||m_current == Text || m_current == BrimEars || m_current == Svg)
            gizmo_event(SLAGizmoEventType::Moving, mouse_pos, evt.ShiftDown(), evt.AltDown(), evt.ControlDown());
    } else if (evt.LeftUp()) {
        if (is_dragging()) {
            switch (m_current) {
            case Move:   {
                wxGetApp().plater()->take_snapshot(_u8L("Tool-Move"), UndoRedo::SnapshotType::GizmoAction);
                m_parent.do_move("");
                break;
            }
            case Scale:  {
                wxGetApp().plater()->take_snapshot(_u8L("Tool-Scale"), UndoRedo::SnapshotType::GizmoAction);
                m_parent.do_scale("");
                break;
            }
            case Rotate: {
                wxGetApp().plater()->take_snapshot(_u8L("Tool-Rotate"), UndoRedo::SnapshotType::GizmoAction);
                m_parent.do_rotate("");
                break;
            }
            default: break;
            }

            stop_dragging();
            update_data();

            // BBS
            //wxGetApp().obj_manipul()->set_dirty();
            // Let the plater know that the dragging finished, so a delayed refresh
            // of the scene with the background processing data should be performed.
            m_parent.post_event(SimpleEvent(EVT_GLCANVAS_MOUSE_DRAGGING_FINISHED));
            // updates camera target constraints
            m_parent.refresh_camera_scene_box();

            processed = true;
        }
    }
    else if (evt.MiddleUp()) {
    }
    else if (evt.RightUp()) {
    }
    else if (evt.Dragging() && !is_dragging()) {
    }
    else if (evt.Dragging() && is_dragging()) {
        if (!m_parent.get_wxglcanvas()->HasCapture())
            m_parent.get_wxglcanvas()->CaptureMouse();

        m_parent.set_mouse_as_dragging();
        update(m_parent.mouse_ray(pos), pos);

        switch (m_current)
        {
        case Move:
        {
            // Apply new temporary offset
            TransformationType trafo_type;
            trafo_type.set_relative();
            switch (wxGetApp().obj_manipul()->get_coordinates_type()) {
                case ECoordinatesType::Instance: {
                    trafo_type.set_instance();
                    break;
                }
                case ECoordinatesType::Local: {
                    trafo_type.set_local();
                    break;
                }
                default: {
                    break;
                }
            }
            selection.translate(get_displacement(), trafo_type);
            // BBS
            //wxGetApp().obj_manipul()->set_dirty();
            break;
        }
        case Scale: {
            // Apply new temporary scale factors
            TransformationType transformation_type;
            if (wxGetApp().obj_manipul()->is_local_coordinates()) {
                transformation_type.set_local();
            } else if (wxGetApp().obj_manipul()->is_instance_coordinates())
                transformation_type.set_instance();
            transformation_type.set_relative();

            if (evt.AltDown())
                 transformation_type.set_independent();
            selection.scale_and_translate(get_scale(), get_scale_offset(), transformation_type);

            // BBS
            //wxGetApp().obj_manipul()->set_dirty();
            break;
        }
        case Rotate:
        {
            // Apply new temporary rotations
            TransformationType transformation_type;
            if (m_parent.get_selection().is_wipe_tower())
                transformation_type = TransformationType::World_Relative_Joint;
            else {
                switch (wxGetApp().obj_manipul()->get_coordinates_type()) {
                default:
                case ECoordinatesType::World: {
                    transformation_type = TransformationType::World_Relative_Joint;
                    break;
                }
                case ECoordinatesType::Instance: {
                    transformation_type = TransformationType::Instance_Relative_Joint;
                    break;
                }
                case ECoordinatesType::Local: {
                    transformation_type = TransformationType::Local_Relative_Joint;
                    break;
                }
                }
            }
            if (evt.AltDown())
                transformation_type.set_independent();
            selection.rotate(get_rotation(), transformation_type);
            // BBS
            //wxGetApp().obj_manipul()->set_dirty();
            break;
        }
        default:
            break;
        }

        m_parent.set_as_dirty();
        processed = true;
    }

    if (evt.LeftDown() && (!control_down || grabber_contains_mouse())) {
        if ((m_current == SlaSupports || m_current == Hollow || m_current == Svg || is_paint_gizmo() || m_current == Text || m_current == Cut || m_current == MeshBoolean ||
            m_current == BrimEars)
            && gizmo_event(SLAGizmoEventType::LeftDown, mouse_pos, evt.ShiftDown(), evt.AltDown()))
            // the gizmo got the event and took some action, there is no need to do anything more
            processed = true;
        else if (!selection.is_empty() && grabber_contains_mouse()) {
            if (is_allow_mouse_drag_selected()) {

                selection.start_dragging();
                start_dragging();

                // Let the plater know that the dragging started
                m_parent.post_event(SimpleEvent(EVT_GLCANVAS_MOUSE_DRAGGING_STARTED));

                if (m_current == Flatten) {
                    // Rotate the object so the normal points downward:
                    m_parent.do_flatten(get_flattening_normal(), L("Tool-Lay on Face"));
                    // BBS
                    // wxGetApp().obj_manipul()->set_dirty();
                }

                m_parent.set_as_dirty();
            }
            processed = true;
        }
    }
    else if (evt.RightDown() && selected_object_idx != -1 && (m_current == SlaSupports || m_current == Hollow || m_current == BrimEars)
        && gizmo_event(SLAGizmoEventType::RightDown, mouse_pos)) {
        // we need to set the following right up as processed to avoid showing the context menu if the user release the mouse over the object
        pending_right_up = true;
        // event was taken care of by the SlaSupports gizmo
        processed = true;
    }
    else if (evt.RightDown() && !control_down && selected_object_idx != -1 && (is_paint_gizmo() || m_current == Cut)
        && gizmo_event(SLAGizmoEventType::RightDown, mouse_pos)) {
        // event was taken care of by the paint_gizmo
        processed = true;
    }
    else if (evt.Dragging() && m_parent.get_move_volume_id() != -1 && (m_current == SlaSupports || m_current == Hollow || is_paint_gizmo() || m_current == BrimEars))
        // don't allow dragging objects with the Sla gizmo on
        processed = true;
    else if (evt.Dragging() && !control_down && (m_current == SlaSupports || m_current == Hollow || is_paint_gizmo() || m_current == Cut || m_current == BrimEars)
        && gizmo_event(SLAGizmoEventType::Dragging, mouse_pos, evt.ShiftDown(), evt.AltDown())) {
        // the gizmo got the event and took some action, no need to do anything more here
        m_parent.set_as_dirty();
        processed = true;
    }
    else if (evt.Dragging() && control_down && (evt.LeftIsDown() || evt.RightIsDown())) {
        // CTRL has been pressed while already dragging -> stop current action
        if (evt.LeftIsDown())
            gizmo_event(SLAGizmoEventType::LeftUp, mouse_pos, evt.ShiftDown(), evt.AltDown(), true);
        else if (evt.RightIsDown())
            gizmo_event(SLAGizmoEventType::RightUp, mouse_pos, evt.ShiftDown(), evt.AltDown(), true);
    }
    else if (evt.LeftUp()
        && (m_current == SlaSupports || m_current == Hollow || is_paint_gizmo() || m_current == Cut || m_current == BrimEars)
        && gizmo_event(SLAGizmoEventType::LeftUp, mouse_pos, evt.ShiftDown(), evt.AltDown(), control_down)
        && !m_parent.is_mouse_dragging()) {
        // in case SLA/FDM gizmo is selected, we just pass the LeftUp event and stop processing - neither
        // object moving or selecting is suppressed in that case
        processed = true;
    }
    else if (evt.LeftUp() && m_current == Svg && m_gizmos[m_current]->get_hover_id() != -1) {
        // BBS
        // wxGetApp().obj_manipul()->set_dirty();
        processed = true;
    }
    else if (evt.LeftUp() && m_current == Flatten && m_gizmos[m_current]->get_hover_id() != -1) {
        // to avoid to loose the selection when user clicks an the white faces of a different object while the Flatten gizmo is active
        selection.stop_dragging();
        // BBS
        //wxGetApp().obj_manipul()->set_dirty();
        processed = true;
    }
    else if (evt.RightUp() && m_current != EType::Undefined && !m_parent.is_mouse_dragging()) {
        gizmo_event(SLAGizmoEventType::RightUp, mouse_pos, evt.ShiftDown(), evt.AltDown(), control_down);
        processed = true;
    }
    else if (evt.LeftUp()) {
        selection.stop_dragging();
        // BBS
        //wxGetApp().obj_manipul()->set_dirty();
    }

    return processed;
}

bool GLGizmosManager::on_char(wxKeyEvent& evt)
{
    // see include/wx/defs.h enum wxKeyCode
    int keyCode = evt.GetKeyCode();
    int ctrlMask = wxMOD_CONTROL;

    bool processed = false;

    if ((evt.GetModifiers() & ctrlMask) != 0)
    {
        switch (keyCode)
        {
#ifdef __APPLE__
        case 'a':
        case 'A':
#else /* __APPLE__ */
        case WXK_CONTROL_A:
#endif /* __APPLE__ */
        {
            //// Sla gizmo selects all support points
            //if ((m_current == SlaSupports || m_current == Hollow) && gizmo_event(SLAGizmoEventType::SelectAll))
            //    processed = true;

            break;
        }
        }
    }
    else if (!evt.HasModifiers())
    {
        switch (keyCode)
        {
        // key ESC
        case WXK_ESCAPE:
        {
            if (m_current != Undefined) {
                if ((m_current == Measure || m_current == Assembly) && gizmo_event(SLAGizmoEventType::Escape)) {
                    // do nothing
                } else if ((m_current != SlaSupports && m_current != BrimEars) || !gizmo_event(SLAGizmoEventType::DiscardChanges))
                    reset_all_states();

                processed = true;
            }
            break;
        }
        //skip some keys when gizmo
        case 'A':
        case 'a':
        {
            if (is_running()) {
                processed = true;
            }
            break;
        }
        //case WXK_BACK:
        case WXK_DELETE: {
            if ((m_current == Cut || m_current == Measure || m_current == Assembly || m_current == BrimEars) && gizmo_event(SLAGizmoEventType::Delete))
                processed = true;
            break;
        }
        //case 'A':
        //case 'a':
        //{
        //    if (m_current == SlaSupports)
        //    {
        //        gizmo_event(SLAGizmoEventType::AutomaticGeneration);
        //        // set as processed no matter what's returned by gizmo_event() to avoid the calling canvas to process 'A' as arrange
        //        processed = true;
        //    }
        //    break;
        //}
        //case 'M':
        //case 'm':
        //{
        //    if ((m_current == SlaSupports) && gizmo_event(SLAGizmoEventType::ManualEditing))
        //        processed = true;

        //    break;
        //}
        //case 'F':
        //case 'f':
        //{
           /* if (m_current == Scale)
            {
                if (!is_dragging())
                    wxGetApp().plater()->scale_selection_to_fit_print_volume();

                processed = true;
            }*/

            //break;
        //}
        // BBS: Skip all keys when in gizmo. This is necessary for 3D text tool.
        default:
        {
            //if (is_running() && m_current == EType::Text) {
            //    processed = true;
            //}
            break;
        }
        }
    }

    if (!processed && !evt.HasModifiers())
    {
        if (handle_shortcut(keyCode))
            processed = true;
    }

    if (processed)
        m_parent.set_as_dirty();

    return processed;
}

bool GLGizmosManager::on_key(wxKeyEvent& evt)
{
    int keyCode = evt.GetKeyCode();
    bool processed = false;

    auto p_current_gizmo = get_current();
    if (p_current_gizmo) {
        processed = p_current_gizmo->on_key(evt);
    }
    if (processed) {
        m_parent.set_as_dirty();
        return processed;
    }

    if (evt.GetEventType() == wxEVT_KEY_UP)
    {
        if (m_current == SlaSupports || m_current == Hollow || m_current == BrimEars)
        {
            bool is_editing = true;
            bool is_rectangle_dragging = false;

            if (m_current == SlaSupports) {
                GLGizmoSlaSupports* gizmo = dynamic_cast<GLGizmoSlaSupports*>(get_current());
                is_editing = gizmo->is_in_editing_mode();
                is_rectangle_dragging = gizmo->is_selection_rectangle_dragging();
            } else if (m_current == BrimEars) {
                GLGizmoBrimEars* gizmo = dynamic_cast<GLGizmoBrimEars*>(get_current());
                is_rectangle_dragging = gizmo->is_selection_rectangle_dragging();
            }
            else {
                GLGizmoHollow* gizmo = dynamic_cast<GLGizmoHollow*>(get_current());
                is_rectangle_dragging = gizmo->is_selection_rectangle_dragging();
            }

            if (keyCode == WXK_SHIFT)
            {
                // shift has been just released - SLA gizmo might want to close rectangular selection.
                if (gizmo_event(SLAGizmoEventType::ShiftUp) || (is_editing && is_rectangle_dragging))
                    processed = true;
            }
            else if (keyCode == WXK_ALT)
            {
                // alt has been just released - SLA gizmo might want to close rectangular selection.
                if (gizmo_event(SLAGizmoEventType::AltUp) || (is_editing && is_rectangle_dragging))
                    processed = true;
            }

            // BBS
            if (m_current == MmuSegmentation && keyCode > '0' && keyCode <= '9') {
                // capture number key
                processed = true;
            }
        }
        if (m_current == Measure || m_current == Assembly) {
            if (keyCode == WXK_CONTROL)
                gizmo_event(SLAGizmoEventType::CtrlUp, Vec2d::Zero(), evt.ShiftDown(), evt.AltDown(), evt.CmdDown());
            else if (keyCode == WXK_SHIFT)
                gizmo_event(SLAGizmoEventType::ShiftUp, Vec2d::Zero(), evt.ShiftDown(), evt.AltDown(), evt.CmdDown());
        }
//        if (processed)
//            m_parent.set_cursor(GLCanvas3D::Standard);
    }
    else if (evt.GetEventType() == wxEVT_KEY_DOWN)
    {
        if ((m_current == SlaSupports) && ((keyCode == WXK_SHIFT) || (keyCode == WXK_ALT))
          && dynamic_cast<GLGizmoSlaSupports*>(get_current())->is_in_editing_mode())
        {
//            m_parent.set_cursor(GLCanvas3D::Cross);
            processed = true;
        }
        else if  ((m_current == BrimEars) && ((keyCode == WXK_SHIFT) || (keyCode == WXK_ALT)))
        {
            processed = true;
        }
        else if (m_current == Cut)
        {
            // BBS
#if 0
            auto do_move = [this, &processed](double delta_z) {
                GLGizmoAdvancedCut* cut = dynamic_cast<GLGizmoAdvancedCut*>(get_current());
                cut->set_cut_z(delta_z + cut->get_cut_z());
                processed = true;
            };

            switch (keyCode)
            {
            case WXK_NUMPAD_UP:   case WXK_UP:   { do_move(1.0); break; }
            case WXK_NUMPAD_DOWN: case WXK_DOWN: { do_move(-1.0); break; }
            default: { break; }
            }
#endif
        } else if (m_current == Simplify && keyCode == WXK_ESCAPE) {
            GLGizmoSimplify *simplify = dynamic_cast<GLGizmoSimplify *>(get_current());
            if (simplify != nullptr)
                processed = simplify->on_esc_key_down();
        }
        // BBS
        else if (m_current == MmuSegmentation) {
            GLGizmoMmuSegmentation* mmu_seg = dynamic_cast<GLGizmoMmuSegmentation*>(get_current());
            if (mmu_seg != nullptr && evt.ControlDown() == false) {
                if (keyCode >= WXK_NUMPAD0 && keyCode <= WXK_NUMPAD9) {
                    keyCode = keyCode- WXK_NUMPAD0+'0';
                }
                if (keyCode >= '0' && keyCode <= '9') {
                    if (keyCode == '1' && !m_timer_set_color.IsRunning()) {
                        m_timer_set_color.StartOnce(500);
                        processed = true;
                    }
                    else if (keyCode < '7' && m_timer_set_color.IsRunning()) {
                        processed = mmu_seg->on_number_key_down(keyCode - '0'+10);
                        m_timer_set_color.Stop();
                    }
                    else {
                        processed = mmu_seg->on_number_key_down(keyCode - '0');
                    }
                }
                else if (keyCode == 'F' || keyCode == 'T' || keyCode == 'S' || keyCode == 'C' || keyCode == 'H' || keyCode == 'G') {
                    processed = mmu_seg->on_key_down_select_tool_type(keyCode);
                    if (processed) {
                        // force extra frame to automatically update window size
                        wxGetApp().imgui()->set_requires_extra_frame();
                    }
                }
            }
        } else if (m_current == FuzzySkin) {
            GLGizmoFuzzySkin *fuzzy_skin = dynamic_cast<GLGizmoFuzzySkin *>(get_current());
            if (fuzzy_skin != nullptr && (keyCode == 'F' || keyCode == 'S' || keyCode == 'C' || keyCode == 'T')) {
                processed = fuzzy_skin->on_key_down_select_tool_type(keyCode);
            }
            if (processed) {
                // force extra frame to automatically update window size
                wxGetApp().imgui()->set_requires_extra_frame();
            }
        }
        else if (m_current == FdmSupports) {
            GLGizmoFdmSupports* fdm_support = dynamic_cast<GLGizmoFdmSupports*>(get_current());
            if (fdm_support != nullptr && (keyCode == 'F' || keyCode == 'S' || keyCode == 'C' || keyCode == 'G')) {
                processed = fdm_support->on_key_down_select_tool_type(keyCode);
            }
            if (processed) {
                // force extra frame to automatically update window size
                wxGetApp().imgui()->set_requires_extra_frame();
            }
        }
        else if (m_current == Seam) {
            GLGizmoSeam* seam = dynamic_cast<GLGizmoSeam*>(get_current());
            if (seam != nullptr && (keyCode == 'S' || keyCode == 'C')) {
                processed = seam->on_key_down_select_tool_type(keyCode);
            }
            if (processed) {
                // force extra frame to automatically update window size
                wxGetApp().imgui()->set_requires_extra_frame();
            }
        } else if (m_current == Measure || m_current == Assembly) {
            if (keyCode == WXK_CONTROL)
                gizmo_event(SLAGizmoEventType::CtrlDown, Vec2d::Zero(), evt.ShiftDown(), evt.AltDown(), evt.CmdDown());
            else if (keyCode == WXK_SHIFT)
                gizmo_event(SLAGizmoEventType::ShiftDown, Vec2d::Zero(), evt.ShiftDown(), evt.AltDown(), evt.CmdDown());
        }
    }

    if (processed)
        m_parent.set_as_dirty();

    return processed;
}

void GLGizmosManager::on_set_color_timer(wxTimerEvent& evt)
{
    if (m_current == MmuSegmentation) {
        GLGizmoMmuSegmentation* mmu_seg = dynamic_cast<GLGizmoMmuSegmentation*>(get_current());
        mmu_seg->on_number_key_down(1);
        m_parent.set_as_dirty();
    }
}

void GLGizmosManager::update_after_undo_redo(const UndoRedo::Snapshot& snapshot)
{
    update_data();
    m_serializing = false;
    if (m_current == SlaSupports
     && snapshot.snapshot_data.flags & UndoRedo::SnapshotData::RECALCULATE_SLA_SUPPORTS)
        dynamic_cast<GLGizmoSlaSupports*>(m_gizmos[SlaSupports].get())->reslice_SLA_supports(true);
}

BoundingBoxf3 GLGizmosManager::get_bounding_box() const
{
    BoundingBoxf3 t_aabb;
    t_aabb.reset();
    if (!m_enabled || m_current == Undefined)
        return t_aabb;

    t_aabb = m_gizmos[m_current]->get_bounding_box();

    return t_aabb;
}

void GLGizmosManager::add_toolbar_items(const std::shared_ptr<GLToolbar>& p_toolbar, uint8_t& sprite_id, const std::function<void(uint8_t& sprite_id)>& p_callback)
{
    if (!p_toolbar) {
        return;
    }

    if (m_gizmos.empty()) {
        return;
    }

    std::vector<size_t> selectable_idxs = get_selectable_idxs();

    auto p_gizmo_manager = this;
    for (size_t i = 0; i < m_gizmos.size(); ++i)
    {
        const auto idx = i;
        if (!m_gizmos[idx]) {
            continue;
        }

        if (m_gizmos[idx]->get_sprite_id() == (unsigned int)EType::Measure) {
            p_callback(sprite_id);
        }

        GLToolbarItem::Data item;

        item.name = convert_gizmo_type_to_string(static_cast<GLGizmosManager::EType>(m_gizmos[idx]->get_sprite_id()));
        item.icon_filename_callback = [p_gizmo_manager, idx](bool is_dark_mode)->std::string {
            return p_gizmo_manager->m_gizmos[idx]->get_icon_filename(is_dark_mode);
        };
        item.tooltip = "";
        item.sprite_id = sprite_id++;
        const auto t_type = m_gizmos[idx]->get_sprite_id();
        item.left.action_callback = [p_gizmo_manager, t_type]() {
            p_gizmo_manager->on_click(t_type);
        };
        item.enabling_callback = [p_gizmo_manager, idx]()->bool {
            return p_gizmo_manager->m_gizmos[idx]->is_activable();
        };
        item.on_hover = [p_gizmo_manager, idx]()->std::string {
            return p_gizmo_manager->on_hover(idx);
        };
        item.left.toggable = true;
        item.b_toggle_disable_others = false;
        item.b_toggle_affectable = false;
        item.left.render_callback = [p_gizmo_manager, idx](float left, float right, float bottom, float top, float toolbar_height) {
            if (p_gizmo_manager->get_current_type() != idx) {
                return;
            }
            float cnv_h = (float)p_gizmo_manager->m_parent.get_canvas_size().get_height();
            p_gizmo_manager->m_gizmos[idx]->render_input_window(left, toolbar_height, cnv_h);
        };
        item.pressed_recheck_callback = [p_gizmo_manager, t_type]()->bool {
            return p_gizmo_manager->m_current == t_type;
        };
        const bool b_is_selectable = (std::find(selectable_idxs.begin(), selectable_idxs.end(), idx) != selectable_idxs.end());
        item.visibility_callback = [p_gizmo_manager, idx, b_is_selectable]()->bool {
            bool rt = b_is_selectable;
            if (idx == EType::Svg) {
                rt = rt && (p_gizmo_manager->m_current == EType::Svg);
            }
            else if (idx == EType::Text) {
                rt = rt && p_gizmo_manager->m_current != EType::Svg;
            }
            return rt;
        };
        item.visible = b_is_selectable;
        p_toolbar->add_item(item);
    }
}

std::string GLGizmosManager::convert_gizmo_type_to_string(Slic3r::GUI::GLGizmosManager::EType t_type)
{
    switch (t_type) {
    case Slic3r::GUI::GLGizmosManager::EType::Move:
        return "Move";
    case Slic3r::GUI::GLGizmosManager::EType::Rotate:
        return "Rotate";
    case Slic3r::GUI::GLGizmosManager::EType::Scale:
        return "Scale";
    case Slic3r::GUI::GLGizmosManager::EType::Flatten:
        return "Flatten";
    case Slic3r::GUI::GLGizmosManager::EType::Cut:
        return "Cut";
    case Slic3r::GUI::GLGizmosManager::EType::MeshBoolean:
        return "MeshBoolean";
    case Slic3r::GUI::GLGizmosManager::EType::FdmSupports:
        return "FdmSupports";
    case Slic3r::GUI::GLGizmosManager::EType::Seam:
        return "Seam";
    case Slic3r::GUI::GLGizmosManager::EType::Text:
        return "Text";
    case Slic3r::GUI::GLGizmosManager::EType::Svg:
        return "Svg";
    case Slic3r::GUI::GLGizmosManager::EType::MmuSegmentation:
        return "Color Painting";
    case Slic3r::GUI::GLGizmosManager::EType::FuzzySkin:
        return "FuzzySkin";
    case Slic3r::GUI::GLGizmosManager::EType::Measure:
        return "Mesause";
    case Slic3r::GUI::GLGizmosManager::EType::Assembly:
        return "Assembly";
    case Slic3r::GUI::GLGizmosManager::EType::Simplify:
        return "Simplify";
    case Slic3r::GUI::GLGizmosManager::EType::BrimEars:
        return "BrimEars";
    case Slic3r::GUI::GLGizmosManager::EType::SlaSupports:
        return "SlaSupports";
    case Slic3r::GUI::GLGizmosManager::EType::Hollow:
        return "Hollow";
    case Slic3r::GUI::GLGizmosManager::EType::Undefined:
        return "Undefined";
    default:
        return "Unknow";
    }
}

GLGizmoBase* GLGizmosManager::get_current() const
{
    return ((m_current == Undefined) || m_gizmos.empty()) ? nullptr : m_gizmos[m_current].get();
}

GLGizmoBase* GLGizmosManager::get_gizmo(GLGizmosManager::EType type) const
{
    return ((type == Undefined) || m_gizmos.empty()) ? nullptr : m_gizmos[type].get();
}

GLGizmosManager::EType GLGizmosManager::get_gizmo_from_name(const std::string& gizmo_name) const
{
    const auto is_dark_mode = m_parent.get_dark_mode_status();
    std::vector<size_t> selectable_idxs = get_selectable_idxs();
    for (size_t idx = 0; idx < selectable_idxs.size(); ++idx)
    {
        std::string filename = m_gizmos[selectable_idxs[idx]]->get_icon_filename(is_dark_mode);
        filename = filename.substr(0, filename.find_first_of('.'));
        if (filename == gizmo_name)
            return (GLGizmosManager::EType)selectable_idxs[idx];
    }
    return GLGizmosManager::EType::Undefined;
}

void GLGizmosManager::update_on_off_state(size_t idx)
{
    if (!m_enabled)
        return;
    if (is_text_first_clicked(idx)) { // open text gizmo
        GLGizmoBase *gizmo_text= m_gizmos[EType::Text].get();
        if (dynamic_cast<GLGizmoText *>(gizmo_text)->on_shortcut_key()) {//create text on mesh
            return;
        }
    }
    if (is_svg_selected(idx)) {// close svg gizmo
        open_gizmo(EType::Svg);
        return;
    }
    if (idx != Undefined && m_gizmos[idx]->is_activable()) {
        activate_gizmo(m_current == idx ? Undefined : (EType)idx);
        // BBS
        wxGetApp().obj_list()->select_object_item((EType) idx <= Scale || (EType) idx == Text);
    }
}

bool GLGizmosManager::activate_gizmo(EType type)
{
    if (m_gizmos.empty() || m_current == type)
        return true;

    GLGizmoBase* old_gizmo = m_current == Undefined ? nullptr : m_gizmos[m_current].get();
    GLGizmoBase* new_gizmo = type == Undefined ? nullptr : m_gizmos[type].get();

    if (old_gizmo) {
        //if (m_current == Text) {
        //    wxGetApp().imgui()->destroy_fonts_texture();
        //}
        old_gizmo->set_state(GLGizmoBase::Off);
        if (old_gizmo->get_state() != GLGizmoBase::Off)
            return false; // gizmo refused to be turned off, do nothing.

        if (! m_parent.get_gizmos_manager().is_serializing()
         && old_gizmo->wants_enter_leave_snapshots())
            Plater::TakeSnapshot snapshot(wxGetApp().plater(),
                old_gizmo->get_gizmo_leaving_text(),
                UndoRedo::SnapshotType::LeavingGizmoWithAction);
    }

    if (new_gizmo && ! m_parent.get_gizmos_manager().is_serializing()
     && new_gizmo->wants_enter_leave_snapshots())
        Plater::TakeSnapshot snapshot(wxGetApp().plater(),
            new_gizmo->get_gizmo_entering_text(),
            UndoRedo::SnapshotType::EnteringGizmo);

    m_current = type;

    if (new_gizmo) {
        //if (m_current == Text) {
        //    wxGetApp().imgui()->load_fonts_texture();
        //}
        new_gizmo->set_serializing(m_serializing);
        new_gizmo->set_state(GLGizmoBase::On);
        new_gizmo->set_serializing(false);
        update_show_only_active_plate();

        try {
            if ((int)m_hover >= 0 && (int)m_hover < m_gizmos.size()) {
                std::string   name = convert_gizmo_type_to_string(m_hover);
                int           count = m_gizmos[m_hover]->get_count();
                NetworkAgent* agent = GUI::wxGetApp().getAgent();
                if (agent) { agent->track_update_property(name, std::to_string(count)); }
            }
        }
        catch (...) {}
    }
    return true;
}


bool GLGizmosManager::grabber_contains_mouse() const
{
    if (!m_enabled)
        return false;

    GLGizmoBase* curr = get_current();
    return (curr != nullptr) ? (curr->get_hover_id() != -1) : false;
}

bool GLGizmosManager::is_text_first_clicked(int idx) const {
    return m_current == Undefined && idx == Text;
}

bool GLGizmosManager::is_svg_selected(int idx) const {
    return m_current == Svg && idx == Text;
}

std::string GLGizmosManager::on_hover(int idx)
{
    std::string t_name{};
    if (is_svg_selected(idx)) {
        t_name = m_gizmos[m_current]->get_name();
    }
    else {
        t_name = m_gizmos[idx]->get_name();
    }

    if (m_gizmos[idx]->is_activable())
        m_hover = (EType)idx;
    return t_name;
}

void GLGizmosManager::on_click(int idx)
{
    Selection &selection = m_parent.get_selection();
    if (selection.is_empty()) {
        if (is_text_first_clicked(idx)) { // open text gizmo
            GLGizmoBase* gizmo_text = m_gizmos[EType::Text].get();
            dynamic_cast<GLGizmoText*>(gizmo_text)->on_shortcut_key();//direct create text on plate
            return;
        }
    }

    update_on_off_state(idx);
    update_data();
    m_parent.set_as_dirty();
}

bool GLGizmosManager::is_in_editing_mode(bool error_notification) const
{
    if (m_current == SlaSupports && dynamic_cast<GLGizmoSlaSupports*>(get_current())->is_in_editing_mode()) {
        return true;
    } else if (m_current == BrimEars) {
        dynamic_cast<GLGizmoBrimEars *>(get_current())->update_model_object();
        return false;
    } else {
        return false;
    }

}


bool GLGizmosManager::is_hiding_instances() const
{
    if (is_paint_gizmo()) {
        return false;
    }
    return (m_common_gizmos_data
         && m_common_gizmos_data->instances_hider()
         && m_common_gizmos_data->instances_hider()->is_valid());
}


int GLGizmosManager::get_shortcut_key(GLGizmosManager::EType type) const
{
    return m_gizmos[type]->get_shortcut_key();
}

void GLGizmosManager::set_highlight(EType gizmo, bool highlight_shown)
{
    m_highlight = std::pair<EType, bool>(gizmo, highlight_shown);
}

} // namespace GUI
} // namespace Slic3r
