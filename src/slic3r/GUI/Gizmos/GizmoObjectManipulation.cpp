#include "slic3r/GUI/ImGuiWrapper.hpp"
#include <imgui/imgui_internal.h>

#include "GizmoObjectManipulation.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
//#include "I18N.hpp"
#include "GLGizmosManager.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "libslic3r/AppConfig.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/Geometry.hpp"
#include "slic3r/GUI/Selection.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/MainFrame.hpp"


#include <boost/algorithm/string.hpp>

namespace Slic3r
{
namespace GUI
{

const double GizmoObjectManipulation::in_to_mm = 25.4;
const double GizmoObjectManipulation::mm_to_in = 0.0393700787;

// Helper function to be used by drop to bed button. Returns lowest point of this
// volume in world coordinate system.
static double get_volume_min_z(const GLVolume* volume)
{
    const Transform3f& world_matrix = volume->world_matrix().cast<float>();

    // need to get the ModelVolume pointer
    const ModelObject* mo = wxGetApp().model().objects[volume->composite_id.object_id];
    const ModelVolume* mv = mo->volumes[volume->composite_id.volume_id];
    const TriangleMesh& hull = mv->get_convex_hull();

    float min_z = std::numeric_limits<float>::max();
    for (const stl_facet& facet : hull.stl.facet_start) {
        for (int i = 0; i < 3; ++ i)
            min_z = std::min(min_z, Vec3f::UnitZ().dot(world_matrix * facet.vertex[i]));
    }
    return min_z;
}

GizmoObjectManipulation::GizmoObjectManipulation(GLCanvas3D& glcanvas)
    : m_glcanvas(glcanvas)
{
    m_imperial_units = wxGetApp().app_config->get("use_inches") == "1";
    m_new_unit_string = m_imperial_units ? L("in") : L("mm");
}

void GizmoObjectManipulation::UpdateAndShow(const bool show)
{
	if (show) {
        this->set_dirty();
		this->update_if_dirty();
	}
}

void GizmoObjectManipulation::update_ui_from_settings()
{
    if (m_imperial_units != (wxGetApp().app_config->get("use_inches") == "1")) {
        m_imperial_units = wxGetApp().app_config->get("use_inches") == "1";

        m_new_unit_string = m_imperial_units ? L("in") : L("mm");

        update_buffered_value();
    }
}

void GizmoObjectManipulation::update_settings_value(const Selection& selection)
{
	m_new_move_label_string   = L("Position");
    m_new_rotate_label_string = L("Rotation");
    m_new_scale_label_string  = L("Scale factors");

    m_world_coordinates = true;

    ObjectList* obj_list = wxGetApp().obj_list();
    if (selection.is_single_full_instance()) {
        // all volumes in the selection belongs to the same instance, any of them contains the needed instance data, so we take the first one
        const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
        m_new_position = volume->get_instance_offset();

        // Verify whether the instance rotation is multiples of 90 degrees, so that the scaling in world coordinates is possible.
		if (m_world_coordinates && ! m_uniform_scale && 
            ! Geometry::is_rotation_ninety_degrees(volume->get_instance_rotation())) {
			// Manipulating an instance in the world coordinate system, rotation is not multiples of ninety degrees, therefore enforce uniform scaling.
			m_uniform_scale = true;
		}

        if (m_world_coordinates) {
			m_new_rotate_label_string = L("Rotate");
			m_new_rotation = Vec3d::Zero();
			m_new_size     = selection.get_scaled_instance_bounding_box().size();
			m_new_scale    = m_new_size.cwiseProduct(selection.get_unscaled_instance_bounding_box().size().cwiseInverse()) * 100.;
		} 
        else {
			m_new_rotation = volume->get_instance_rotation() * (180. / M_PI);
			m_new_size     = volume->get_instance_transformation().get_scaling_factor().cwiseProduct(wxGetApp().model().objects[volume->object_idx()]->raw_mesh_bounding_box().size());
			m_new_scale    = volume->get_instance_scaling_factor() * 100.;
		}

        m_new_enabled  = true;
        m_new_title_string = L("Instance Operations");
    }
    else if (selection.is_single_full_object() && obj_list->is_selected(itObject)) {
        const BoundingBoxf3& box = selection.get_bounding_box();
        m_new_position = box.center();
        m_new_rotation = Vec3d::Zero();
        m_new_scale    = Vec3d(100., 100., 100.);
        m_new_size     = box.size();
        m_new_rotate_label_string = L("Rotate");
		m_new_scale_label_string  = L("Scale");
        m_new_enabled  = true;
        m_new_title_string = L("Object Operations");
    }
    else if (selection.is_single_modifier() || selection.is_single_volume()) {
        // the selection contains a single volume
        const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
        m_new_position = volume->get_volume_offset();
        m_new_rotation = volume->get_volume_rotation() * (180. / M_PI);
        m_new_scale    = volume->get_volume_scaling_factor() * 100.;
        m_new_size     = volume->get_instance_transformation().get_scaling_factor().cwiseProduct(volume->get_volume_transformation().get_scaling_factor().cwiseProduct(volume->bounding_box().size()));
        m_new_enabled = true;
        m_new_title_string = L("Volume Operations");
    }
    else if (obj_list->multiple_selection() || obj_list->is_selected(itInstanceRoot)) {
        reset_settings_value();
		m_new_move_label_string   = L("Translate");
		m_new_rotate_label_string = L("Rotate");
		m_new_scale_label_string  = L("Scale");
        m_new_size = selection.get_bounding_box().size();
        m_new_enabled  = true;
        m_new_title_string = L("Group Operations");
    }
	else {
        // No selection, reset the cache.
//		assert(selection.is_empty());
		reset_settings_value();
	}
}

void GizmoObjectManipulation::update_buffered_value()
{
    if (this->m_imperial_units)
        m_buffered_position = this->m_new_position * this->mm_to_in;
    else
        m_buffered_position = this->m_new_position;

    m_buffered_rotation = this->m_new_rotation;

    m_buffered_scale = this->m_new_scale;

    if (this->m_imperial_units)
        m_buffered_size = this->m_new_size * this->mm_to_in;
    else
        m_buffered_size = this->m_new_size;
}

void GizmoObjectManipulation::update_if_dirty()
{
    if (! m_dirty)
        return;

    const Selection &selection = m_glcanvas.get_selection();
    this->update_settings_value(selection);
    this->update_buffered_value();

    auto update_label = [](wxString &label_cache, const std::string &new_label) {
        wxString new_label_localized = _(new_label) + ":";
        if (label_cache != new_label_localized) {
            label_cache = new_label_localized;
        }
    };
    update_label(m_cache.move_label_string,   m_new_move_label_string);
    update_label(m_cache.rotate_label_string, m_new_rotate_label_string);
    update_label(m_cache.scale_label_string,  m_new_scale_label_string);

    enum ManipulationEditorKey
    {
        mePosition = 0,
        meRotation,
        meScale,
        meSize
    };

    for (int i = 0; i < 3; ++ i) {
        auto update = [this, i](Vec3d &cached, Vec3d &cached_rounded,  const Vec3d &new_value) {
			//wxString new_text = double_to_string(new_value(i), 2);
			double new_rounded = round(new_value(i)*100)/100.0;
			//new_text.ToDouble(&new_rounded);
			if (std::abs(cached_rounded(i) - new_rounded) > EPSILON) {
				cached_rounded(i) = new_rounded;
                //const int id = key_id*3+i;
                //if (m_imperial_units && (key_id == mePosition || key_id == meSize))
                //    new_text = double_to_string(new_value(i)*mm_to_in, 2);
                //if (id >= 0) m_editors[id]->set_value(new_text);
            }
			cached(i) = new_value(i);
		};
        update(m_cache.position, m_cache.position_rounded,  m_new_position);
        update(m_cache.scale,    m_cache.scale_rounded,     m_new_scale);
        update(m_cache.size,     m_cache.size_rounded,      m_new_size);
        update(m_cache.rotation, m_cache.rotation_rounded,  m_new_rotation);
    }


    if (selection.requires_uniform_scale()) {
        m_uniform_scale = true;
    }

    update_reset_buttons_visibility();
    //update_mirror_buttons_visibility();

    m_dirty = false;
}

void GizmoObjectManipulation::update_reset_buttons_visibility()
{
    const Selection& selection = m_glcanvas.get_selection();

    if (selection.is_single_full_instance() || selection.is_single_modifier() || selection.is_single_volume()) {
        const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
        Vec3d rotation;
        Vec3d scale;
        double min_z = 0.;

        if (selection.is_single_full_instance()) {
            rotation = volume->get_instance_rotation();
            scale = volume->get_instance_scaling_factor();
        }
        else {
            rotation = volume->get_volume_rotation();
            scale = volume->get_volume_scaling_factor();
            min_z = get_volume_min_z(volume);
        }
        m_show_clear_rotation = !rotation.isApprox(Vec3d::Zero());
        m_show_clear_scale = !scale.isApprox(Vec3d::Ones());
        m_show_drop_to_bed = (std::abs(min_z) > EPSILON);
    }
}


void GizmoObjectManipulation::reset_settings_value()
{
    m_new_position = Vec3d::Zero();
    m_new_rotation = Vec3d::Zero();
    m_new_scale = Vec3d::Ones() * 100.;
    m_new_size = Vec3d::Zero();
    m_new_enabled = false;
    // no need to set the dirty flag here as this method is called from update_settings_value(),
    // which is called from update_if_dirty(), which resets the dirty flag anyways.
//    m_dirty = true;
}

void GizmoObjectManipulation::change_position_value(int axis, double value)
{
    if (std::abs(m_cache.position_rounded(axis) - value) < EPSILON)
        return;

    Vec3d position = m_cache.position;
    position(axis) = value;

    Selection& selection = m_glcanvas.get_selection();
    selection.start_dragging();
    selection.translate(position - m_cache.position, selection.requires_local_axes());
    m_glcanvas.do_move(L("Set Position"));

    m_cache.position = position;
	m_cache.position_rounded(axis) = DBL_MAX;
    this->UpdateAndShow(true);
}

void GizmoObjectManipulation::change_rotation_value(int axis, double value)
{
    if (std::abs(m_cache.rotation_rounded(axis) - value) < EPSILON)
        return;

    Vec3d rotation = m_cache.rotation;
    rotation(axis) = value;

    Selection& selection = m_glcanvas.get_selection();

    TransformationType transformation_type(TransformationType::World_Relative_Joint);
    if (selection.is_single_full_instance() || selection.requires_local_axes())
		transformation_type.set_independent();
	if (selection.is_single_full_instance() && ! m_world_coordinates) {
        //FIXME Selection::rotate() does not process absoulte rotations correctly: It does not recognize the axis index, which was changed.
		// transformation_type.set_absolute();
		transformation_type.set_local();
	}

    selection.start_dragging();
	selection.rotate(
		(M_PI / 180.0) * (transformation_type.absolute() ? rotation : rotation - m_cache.rotation), 
		transformation_type);
    m_glcanvas.do_rotate(L("Set Orientation"));

    m_cache.rotation = rotation;
	m_cache.rotation_rounded(axis) = DBL_MAX;
    this->UpdateAndShow(true);
}

void GizmoObjectManipulation::change_scale_value(int axis, double value)
{
    if (std::abs(m_cache.scale_rounded(axis) - value) < EPSILON)
        return;

    Vec3d scale = m_cache.scale;
	scale(axis) = value;

    this->do_scale(axis, scale);

    m_cache.scale = scale;
	m_cache.scale_rounded(axis) = DBL_MAX;
	this->UpdateAndShow(true);
}


void GizmoObjectManipulation::change_size_value(int axis, double value)
{
    if (std::abs(m_cache.size_rounded(axis) - value) < EPSILON)
        return;

    Vec3d size = m_cache.size;
    size(axis) = value;

    const Selection& selection = m_glcanvas.get_selection();

    Vec3d ref_size = m_cache.size;
	if (selection.is_single_volume() || selection.is_single_modifier())
        ref_size = selection.get_volume(*selection.get_volume_idxs().begin())->bounding_box().size();
    else if (selection.is_single_full_instance())
		ref_size = m_world_coordinates ? 
            selection.get_unscaled_instance_bounding_box().size() :
            wxGetApp().model().objects[selection.get_volume(*selection.get_volume_idxs().begin())->object_idx()]->raw_mesh_bounding_box().size();

    this->do_scale(axis, 100. * Vec3d(size(0) / ref_size(0), size(1) / ref_size(1), size(2) / ref_size(2)));

    m_cache.size = size;
	m_cache.size_rounded(axis) = DBL_MAX;
	this->UpdateAndShow(true);
}

void GizmoObjectManipulation::do_scale(int axis, const Vec3d &scale) const
{
    Selection& selection = m_glcanvas.get_selection();
    Vec3d scaling_factor = scale;

    TransformationType transformation_type(TransformationType::World_Relative_Joint);
    if (selection.is_single_full_instance()) {
        transformation_type.set_absolute();
        if (! m_world_coordinates)
            transformation_type.set_local();
    }

    if (m_uniform_scale || selection.requires_uniform_scale())
        scaling_factor = scale(axis) * Vec3d::Ones();

    selection.start_dragging();
    selection.scale(scaling_factor * 0.01, transformation_type);
    m_glcanvas.do_scale(L("Set Scale"));
}

void GizmoObjectManipulation::on_change(const std::string& opt_key, int axis, double new_value)
{
    if (!m_cache.is_valid())
        return;

    if (m_imperial_units && (opt_key == "position" || opt_key == "size"))
        new_value *= in_to_mm;

    if (opt_key == "position")
        change_position_value(axis, new_value);
    else if (opt_key == "rotation")
        change_rotation_value(axis, new_value);
    else if (opt_key == "scale")
        change_scale_value(axis, new_value);
    else if (opt_key == "size")
        change_size_value(axis, new_value);
}

void GizmoObjectManipulation::reset_position_value()
{
    Selection& selection = m_glcanvas.get_selection();

    if (selection.is_single_volume() || selection.is_single_modifier()) {
        GLVolume* volume = const_cast<GLVolume*>(selection.get_volume(*selection.get_volume_idxs().begin()));
        volume->set_volume_offset(Vec3d::Zero());
    }
    else if (selection.is_single_full_instance()) {
        for (unsigned int idx : selection.get_volume_idxs()) {
            GLVolume* volume = const_cast<GLVolume*>(selection.get_volume(idx));
            volume->set_instance_offset(Vec3d::Zero());
        }
    }
    else
        return;

    // Copy position values from GLVolumes into Model (ModelInstance / ModelVolume), trigger background processing.
    m_glcanvas.do_move(L("Reset Position"));

    UpdateAndShow(true);
}

void GizmoObjectManipulation::reset_rotation_value()
{
    Selection& selection = m_glcanvas.get_selection();

    if (selection.is_single_volume() || selection.is_single_modifier()) {
        GLVolume* volume = const_cast<GLVolume*>(selection.get_volume(*selection.get_volume_idxs().begin()));
        volume->set_volume_rotation(Vec3d::Zero());
    }
    else if (selection.is_single_full_instance()) {
        for (unsigned int idx : selection.get_volume_idxs()) {
            GLVolume* volume = const_cast<GLVolume*>(selection.get_volume(idx));
            volume->set_instance_rotation(Vec3d::Zero());
        }
    }
    else
        return;

    // Update rotation at the GLVolumes.
    selection.synchronize_unselected_instances(Selection::SYNC_ROTATION_GENERAL);
    selection.synchronize_unselected_volumes();
    // Copy rotation values from GLVolumes into Model (ModelInstance / ModelVolume), trigger background processing.
    m_glcanvas.do_rotate(L("Reset Rotation"));

    UpdateAndShow(true);
}

void GizmoObjectManipulation::reset_scale_value()
{
    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _L("Reset scale"));

    change_scale_value(0, 100.);
    change_scale_value(1, 100.);
    change_scale_value(2, 100.);
}

void GizmoObjectManipulation::set_uniform_scaling(const bool new_value)
{ 
    const Selection &selection = m_glcanvas.get_selection();
	if (selection.is_single_full_instance() && m_world_coordinates && !new_value) {
        // Verify whether the instance rotation is multiples of 90 degrees, so that the scaling in world coordinates is possible.
        // all volumes in the selection belongs to the same instance, any of them contains the needed instance data, so we take the first one
        const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
        // Is the angle close to a multiple of 90 degrees?
		if (! Geometry::is_rotation_ninety_degrees(volume->get_instance_rotation())) {
            // Cannot apply scaling in the world coordinate system.
			wxMessageDialog dlg(GUI::wxGetApp().mainframe,
                _L("The currently manipulated object is tilted (rotation angles are not multiples of 90°).\n"
                    "Non-uniform scaling of tilted objects is only possible in the World coordinate system,\n"
                    "once the rotation is embedded into the object coordinates.") + "\n" +
                _L("This operation is irreversible.\n"
                    "Do you want to proceed?"),
                SLIC3R_APP_NAME,
				wxYES_NO | wxCANCEL | wxCANCEL_DEFAULT | wxICON_QUESTION);
            if (dlg.ShowModal() != wxID_YES) {
                // Enforce uniform scaling.
                //m_lock_bnt->SetLock(true);
                m_uniform_scale = true;
                return;
            }
            // Bake the rotation into the meshes of the object.
            wxGetApp().model().objects[volume->composite_id.object_id]->bake_xy_rotation_into_meshes(volume->composite_id.instance_id);
            // Update the 3D scene, selections etc.
            wxGetApp().plater()->update();
            // Recalculate cached values at this panel, refresh the screen.
            this->UpdateAndShow(true);
        }
    }
    m_uniform_scale = new_value;
}

void GizmoObjectManipulation::do_render_input_window(ImGuiWrapper* imgui_wrapper, std::string window_name, float x, float y, float bottom_limit)
{
    //static float last_y = 0.0f;
    //static float last_h = 0.0f;

    //BBS: GUI refactor: move gizmo to the right
#if BBS_TOOLBAR_ON_TOP
    imgui_wrapper->set_next_window_pos(x, y, ImGuiCond_Always, 0.f, 0.0f);
#else
    imgui_wrapper->set_next_window_pos(x, y, ImGuiCond_Always, 1.0f, 0.0f);
#endif
    std::string name = this->m_new_title_string + "##" + window_name;
    imgui_wrapper->begin(_L(name), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    auto update = [this](unsigned int active_id, std::string opt_key, Vec3d original_value,  Vec3d new_value)->int {
        for (int i = 0; i < 3; i++)
        {
            if (original_value[i] != new_value[i])
            {
                if (active_id != m_last_active_item)
                {
                    on_change(opt_key, i, new_value[i]);
                    return i;
                }
            }
        }
        return -1;
    };
    float unit_size = imgui_wrapper->get_style_scaling() * 48.0f;
    float space_size = imgui_wrapper->get_style_scaling()*8;
    float caption_max = imgui_wrapper->calc_text_size(_L("Position:")).x + space_size;
    float end_text_size = imgui_wrapper->calc_text_size(this->m_new_unit_string).x;
    ImGui::AlignTextToFramePadding();
    unsigned int current_active_id = ImGui::GetActiveID();

    imgui_wrapper->text(_L(" "));
    ImGui::SameLine(caption_max);
    bool uniform_scale = this->m_uniform_scale;
    imgui_wrapper->checkbox(_L("uniform scale"), uniform_scale);
    if (uniform_scale != this->m_uniform_scale)
    {
        this->set_uniform_scaling(uniform_scale);
    }

    bool imperial_units = this->m_imperial_units;
    ImGui::SameLine();
    imgui_wrapper->checkbox(_L("Inches"), imperial_units);
    if (imperial_units != this->m_imperial_units)
    {
        //will set in update_ui_from_settings
        //this->m_imperial_units = imperial_units;
        wxGetApp().app_config->set("use_inches", imperial_units? "1" : "0");
        wxGetApp().sidebar().update_ui_from_settings();
    }

    ImGui::Separator();

    imgui_wrapper->text(_L(" "));
    //ImGui::PushItemWidth(unit_size * 1.5);
    ImGui::SameLine(caption_max + 0.5 * unit_size);
    //ImGui::PushItemWidth(unit_size);
    imgui_wrapper->text(_L("X:"));
    ImGui::SameLine(caption_max + 1.5 * unit_size + space_size);
    //ImGui::PushItemWidth(unit_size);
    imgui_wrapper->text(_L("Y:"));
    ImGui::SameLine(caption_max + 2.5 * unit_size + 2 * space_size);
    //ImGui::PushItemWidth(unit_size);
    imgui_wrapper->text(_L("Z:"));

    ImGui::Separator();

    //position
    Vec3d original_position;
    if (this->m_imperial_units)
        original_position = this->m_new_position * this->mm_to_in;
    else
        original_position = this->m_new_position;
    Vec3d display_position = m_buffered_position;
    //ImGui::PushItemWidth(unit_size * 2);
    imgui_wrapper->text(_L("Position:"));
    ImGui::SameLine(caption_max + space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::InputDouble("##POS_X", &display_position[0], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + unit_size + 2 * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::InputDouble("##POS_y", &display_position[1], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + 2 * unit_size + 3 * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::InputDouble("##POS_z", &display_position[2], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + 3 * unit_size + 4 * space_size);
    imgui_wrapper->text(this->m_new_unit_string);
    m_buffered_position = display_position;
    update(current_active_id, "position", original_position, m_buffered_position);
    //the init position values are not zero, won't add reset button

    //Rotation
    Vec3d rotation = this->m_buffered_rotation;
    //ImGui::PushItemWidth(unit_size * 2);
    imgui_wrapper->text(_L("Rotation:"));
    ImGui::SameLine(caption_max + space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::InputDouble("##ROT_x", &rotation[0], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + unit_size + 2 * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::InputDouble("##ROT_y", &rotation[1], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + 2 * unit_size + 3 * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::InputDouble("##ROT_z", &rotation[2], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + 3 * unit_size + 4 * space_size);
    imgui_wrapper->text(_L("°"));
    m_buffered_rotation = rotation;
    update(current_active_id, "rotation", this->m_new_rotation, m_buffered_rotation);
    //if the value is chaged, need reset button
    imgui_wrapper->disabled_begin(!m_show_clear_rotation);
    ImGui::SameLine(caption_max + 3 * unit_size + 5 * space_size + end_text_size);
    const bool reset_rotation = imgui_wrapper->button(_L("reset##rotation"));
    imgui_wrapper->disabled_end();
    if (reset_rotation)
    {
        reset_rotation_value();
    }

    //Scale
    Vec3d scale = m_buffered_scale;
    //ImGui::PushItemWidth(unit_size * 2);
    imgui_wrapper->text(_L("Scale:"));
    ImGui::SameLine(caption_max + space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::InputDouble("##Scale_x", &scale[0], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + unit_size + 2 * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::InputDouble("##Scale_y", &scale[1], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + 2 * unit_size + 3 * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::InputDouble("##Scale_z", &scale[2], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + 3 * unit_size + 4 * space_size);
    imgui_wrapper->text(_L("%"));
    m_buffered_scale = scale;
    //for (int index = 0; index < 3; index++)
    //    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ",before_index="<<index <<boost::format(",scale %1%, buffered %2%, original_id %3%, new_id %4%\n") % this->m_new_scale[index] % m_buffered_scale[index] % m_last_active_item % current_active_id;
    int scale_sel = update(current_active_id, "scale", this->m_new_scale, m_buffered_scale);
    if ((scale_sel >= 0) && uniform_scale)
    {
        //for (int index = 0; index < 3; index++)
        //    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ",after_index="<<index <<boost::format(",scale %1%, buffered %2%, original_id %3%, new_id %4%\n") % this->m_new_scale[index] % m_buffered_scale[index] % m_last_active_item % current_active_id;
        if (scale_sel == 0)
        {
            ImGui::ClearInputTextInitialData("##Scale_y", m_buffered_scale[0]);
            ImGui::ClearInputTextInitialData("##Scale_z", m_buffered_scale[0]);
            ImGui::ClearInputTextInitialData("##Size_x", m_buffered_size[0]);
            ImGui::ClearInputTextInitialData("##Size_y", m_buffered_size[0]);
            ImGui::ClearInputTextInitialData("##Size_z", m_buffered_size[0]);
        }
        else if (scale_sel == 1)
        {
            ImGui::ClearInputTextInitialData("##Scale_x", m_buffered_scale[0]);
            ImGui::ClearInputTextInitialData("##Scale_z", m_buffered_scale[0]);
            ImGui::ClearInputTextInitialData("##Size_x", m_buffered_size[0]);
            ImGui::ClearInputTextInitialData("##Size_y", m_buffered_size[0]);
            ImGui::ClearInputTextInitialData("##Size_z", m_buffered_size[0]);
        }
        else if (scale_sel == 2)
        {
            ImGui::ClearInputTextInitialData("##Scale_x", m_buffered_scale[0]);
            ImGui::ClearInputTextInitialData("##Scale_y", m_buffered_scale[0]);
            ImGui::ClearInputTextInitialData("##Size_x", m_buffered_size[0]);
            ImGui::ClearInputTextInitialData("##Size_y", m_buffered_size[0]);
            ImGui::ClearInputTextInitialData("##Size_z", m_buffered_size[0]);
        }
    }
    //if the value is chaged, need reset button
    imgui_wrapper->disabled_begin(!m_show_clear_scale);
    ImGui::SameLine(caption_max + 3 * unit_size + 5 * space_size + end_text_size);
    const bool reset_scale = imgui_wrapper->button(_L("reset##scale"));
    imgui_wrapper->disabled_end();
    if (reset_scale)
    {
        reset_scale_value();
    }

    //Size
    Vec3d original_size;
    if (this->m_imperial_units)
        original_size = this->m_new_size * this->mm_to_in;
    else
        original_size = this->m_new_size;
    Vec3d display_size = m_buffered_size;
    //ImGui::PushItemWidth(unit_size * 2);
    imgui_wrapper->text(_L("Size:"));
    ImGui::SameLine(caption_max + space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::InputDouble("##Size_x", &display_size[0], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + unit_size + 2 * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::InputDouble("##Size_y", &display_size[1], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + 2 * unit_size + 3 * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::InputDouble("##Size_z", &display_size[2], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_max + 3 * unit_size + 4 * space_size);
    imgui_wrapper->text(this->m_new_unit_string);
    m_buffered_size = display_size;
    int size_sel = update(current_active_id, "size", original_size, m_buffered_size);
    if ((size_sel >= 0) && uniform_scale)
    {
        //for (int index = 0; index < 3; index++)
        //    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ",after_index="<<index <<boost::format(",scale %1%, buffered %2%, original_id %3%, new_id %4%\n") % this->m_new_scale[index] % m_buffered_scale[index] % m_last_active_item % current_active_id;
        if (size_sel == 0)
        {
            ImGui::ClearInputTextInitialData("##Scale_x", m_buffered_scale[0]);
            ImGui::ClearInputTextInitialData("##Scale_y", m_buffered_scale[0]);
            ImGui::ClearInputTextInitialData("##Scale_z", m_buffered_scale[0]);
            ImGui::ClearInputTextInitialData("##Size_y", m_buffered_size[0]);
            ImGui::ClearInputTextInitialData("##Size_z", m_buffered_size[0]);
        }
        else if (size_sel == 1)
        {
            ImGui::ClearInputTextInitialData("##Scale_x", m_buffered_scale[0]);
            ImGui::ClearInputTextInitialData("##Scale_y", m_buffered_scale[0]);
            ImGui::ClearInputTextInitialData("##Scale_z", m_buffered_scale[0]);
            ImGui::ClearInputTextInitialData("##Size_x", m_buffered_size[0]);
            ImGui::ClearInputTextInitialData("##Size_z", m_buffered_size[0]);
        }
        else if (size_sel == 2)
        {
            ImGui::ClearInputTextInitialData("##Scale_x", m_buffered_scale[0]);
            ImGui::ClearInputTextInitialData("##Scale_y", m_buffered_scale[0]);
            ImGui::ClearInputTextInitialData("##Scale_z", m_buffered_scale[0]);
            ImGui::ClearInputTextInitialData("##Size_x", m_buffered_size[0]);
            ImGui::ClearInputTextInitialData("##Size_y", m_buffered_size[0]);
        }
    }
    //if the value is chaged, need reset button
    imgui_wrapper->disabled_begin(!m_show_clear_scale);
    ImGui::SameLine(caption_max + 3 * unit_size + 5 * space_size + end_text_size);
    const bool reset_size = imgui_wrapper->button(_L("reset##size"));
    imgui_wrapper->disabled_end();
    if (reset_size)
    {
        reset_scale_value();
    }

    m_last_active_item = current_active_id;

    imgui_wrapper->end();
}


} //namespace GUI
} //namespace Slic3r 
