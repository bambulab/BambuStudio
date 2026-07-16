#include "AssemblyStepsUtils.hpp"
#include "AssemblyStepsUtilsInternal.hpp"
#include "AssemblyPdfExportDialog.hpp"
#include "AssemblyExportProgressWindow.hpp"
#include "TinyExportMardDown.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/PNGReadWrite.hpp"
#include "libslic3r/AppConfig.hpp"

#include "../I18N.hpp"
#include "../ImGuiWrapper.hpp"
#include "../GUI_App.hpp"
#include "../GUI.hpp"
#include "../GUI_ObjectList.hpp"
#include "../GLCanvas3D.hpp"
#include "../MainFrame.hpp"
#include "../Plater.hpp"
#include "../MsgDialog.hpp"
#include "../NotificationManager.hpp"
#include "../OpenGLManager.hpp"
#include "../imgui/imgui_stdlib.h"
#include "../MP4/PBOReader.hpp"
#include "../MP4/Mp4Recorder.hpp"

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>

#include <hpdf/hpdf.h>

#include <GL/glew.h>
#include <wx/glcanvas.h>
#include <imgui/imgui_internal.h>

#define _steps_nodes m_model->get_assembly_steps_tree_data().nodes
#define _steps_roots m_model->get_assembly_steps_tree_data().roots

namespace Slic3r {
namespace GUI {
using namespace Slic3r;

namespace {
// Monotonic wall-clock seconds. NOTE: ImGui::GetTime() is NOT wall-clock here:
inline double assembly_now_seconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ASSEMBLY_LABEL_* font-size constants moved to AssemblyStepsUtilsInternal.hpp.
const char *assembly_view_export_type_name(ExportType type)
{
    switch (type) {
    case ExportType::PDF:      return "pdf";
    case ExportType::MarkDown: return "markdown";
    case ExportType::MP4:      return "mp4";
    }
    return "";
}

// utf8_truncate_with_ellipsis / utf8_fit_with_ellipsis moved to
// AssemblyStepsUtilsImgui.cpp (only the ImGui panels use them).

std::string remove_markdown_path_special_chars(const std::string &filename_stem)
{
    static const std::string unsafe_chars = "#%?[]()<>`!|{}^~";
    std::string out;
    out.reserve(filename_stem.size());
    for (char ch : filename_stem) {
        if (unsafe_chars.find(ch) == std::string::npos)
            out.push_back(ch);
    }
    return out;
}

std::string sanitize_export_output_path(const std::string &path, const std::string &fallback_stem)
{
    namespace fs = boost::filesystem;
    fs::path output_path(path);
    const std::string extension = output_path.extension().string();
    std::string stem = remove_markdown_path_special_chars(output_path.stem().string());
    if (stem.empty())
        stem = fallback_stem.empty() ? "Assembly Guide" : fallback_stem;
    output_path = output_path.parent_path() / (stem + extension);
    return output_path.string();
}
} // namespace

void AssemblyStepsUtils::set_selection_origin(SelectionOrigin origin)
{
    if (m_selection_origin != origin) {
        if (origin == SelectionOrigin::None) {
            commit_part_label_rename();
            exit_note_edit();
            //ban exit_render_assembly_tree_ui() here;
            //this clear_when_no_selection(); Trigger camera rotation and exit the current step editing with a single click
        }
        m_selection_origin = origin;
    }
}

void AssemblyStepsUtils::clear_selected_node()
{
    m_selected_node = -1;
}

void AssemblyStepsUtils::clear_when_no_selection()
{
    if (m_selection_origin == SelectionOrigin::None) {
        clear_selected_node();
        m_keyframe_selected = -1;
        m_last_folder_idx   = -1;
    }
}

void AssemblyStepsUtils::exit_assembly_steps_editing()
{
    // Mirror what double-clicking a blank area of the assembly view used to do,
    // but driven explicitly from the exit button: drop any inline note/tree
    // editing UI, force the selection origin back to None so the step node is
    // actually cleared, then ask the canvas to deselect all volumes.
    exit_note_edit();
    exit_render_assembly_tree_ui();
    m_selection_origin = SelectionOrigin::None;
    clear_when_no_selection();
    apply_keyframe_display_mode();

    do_commond_callback("deselect_all");
    do_commond_callback("exit_gizmo");
    do_commond_callback("request_extra_frame");
}

void AssemblyStepsUtils::on_escape_key()
{
    if (has_selected_node()) {
        exit_assembly_steps_editing();
    } else {
        do_commond_callback("return_to_3d_view");
    }
}

void AssemblyStepsUtils::update_model_object_tree() {
    m_model->set_assembly_tree_data(build_model_object_tree_data());
}

AssemblyStepsUtils::AssemblyStepsUtils()
{
    m_keyframe_display_mode = KeyframeDisplayMode::Highlight;
    m_keyframe_max_count = 2;
    m_play_transition_duration = 2.f;
    m_play_transition_expect_duration = 2.f;
    m_play_interval_step_to_step_expect = 2.f;
    //m_play_interval_step_to_step = 1.5s;
    //m_only_final_assembly_endframe_effect_real_assembly = false
}

// Out-of-line destructor: PBOReader / Mp4Recorder are forward-declared in the
// header, so unique_ptr's deleter must be instantiated where the full type is
// visible (this translation unit, after the MP4 includes above).
AssemblyStepsUtils::~AssemblyStepsUtils() = default;

void AssemblyStepsUtils::reset_state_on_model_changed()
{
    clear_runtime_state();

    clear_note_selection();
    exit_render_assembly_tree_ui();
    m_active_assembly_tree_checked = nullptr;
    m_assembly_tree_ui_current_folder_node = -1;
    m_assembly_tree_ui_original_checked.clear();
    m_assembly_tree_search_text.clear();
    m_assembly_tree_search_active = false;
    m_assembly_tree_search_focus_pending = false;
    m_show_assembly_tree_step_quick_select = false;

    m_structure_select_popup_pending_card = -1;
    m_structure_select_popup_active_card = -1;
    m_structure_select_popup_checked_card = -1;
    m_structure_select_popup_tree_card = -1;
    m_structure_select_popup_tree_step_node = -1;
    m_structure_select_popup_tree.clear();
    m_structure_select_popup_checked.clear();
    m_structure_select_labels.clear();
    m_structure_select_show_default.clear();
    m_structure_add_tree_card = -1;
    m_structure_add_tree_step_node = -1;
    m_structure_step_rename_node = -1;
    m_structure_step_rename_open_pending = false;
    m_structure_step_rename_had_focus = false;
    m_structure_scroll_to_node = -1;

    apply_keyframe_display_mode(KeyframeDisplayMode::Highlight);

    m_last_rendered_selected_node_for_notes_ = -2;
    m_last_rendered_keyframe_selected_ = -2;
    m_last_has_selected_node_ = false;
    m_last_notified_step_hint_objs_.clear();
    pn_screen_centers_.clear();
    m_pn_autolayout_pending = false;
    m_render_interpolated_part_number_labels = false;

    hide_assembly_export_progress();
}

void AssemblyStepsUtils::set_input(ImGuiWrapper *imgui, Model *model, Camera *camera, Selection *selection, GLVolumeCollection *volumes, bool gizmo_active)
{
    if (imgui != m_imgui) { m_imgui = imgui; }
    if (model != m_model) { m_model = model; }
    if (camera != m_camera) { m_camera = camera; }
    if (selection != m_selection) { m_selection = selection; }
    if (volumes != m_volumes) { m_volumes = volumes; }
    if (gizmo_active != m_gizmo_active) {  m_gizmo_active = gizmo_active; }
}

void AssemblyStepsUtils::set_render_input(bool is_dark, const std::string &images_dir, float imgui_scale)
{
    if (is_dark != m_is_dark) { m_is_dark = is_dark; }
    if (images_dir != m_images_dir) {
        m_images_dir = images_dir;
        init_tree_icons();
    }
    set_imgui_scale(imgui_scale);
}

void AssemblyStepsUtils::set_commond_callback(CanvasCallBack calback)
{
    if (calback) {
        m_commond_callback = calback;
    }
}

void AssemblyStepsUtils::do_commond_callback(std::string command)
{
    if (m_commond_callback) {
        m_commond_callback(command);
    }
}

void AssemblyStepsUtils::set_in_assembly_view(bool in_assembly_view)
{
    if (m_in_assembly_view != in_assembly_view) {
        m_in_assembly_view = in_assembly_view;
    }
}

void AssemblyStepsUtils::set_note_selection(AssemblyNoteSelectionType type, int idx)
{
    m_note_selected_type = type;
    m_note_selected_idx  = idx;
}

std::string AssemblyStepsUtils::assembly_step_display_name(const AssemblyStepsTreeNode &node) const
{
    wxString label = _L("Step") + wxString::Format("%d ", node.step) + wxString::FromUTF8(node.name.c_str());
    return std::string(label.ToUTF8().data());
}

void AssemblyStepsUtils::save_assembly_steps_json_to_model()
{
    if (!m_model) { return; }
    std::string json_str = build_steps_json_string();
    if (json_str.empty()) { return; }
    m_model->set_assembly_steps_json_str(json_str);
    wxGetApp().plater()->set_plater_dirty(true);
}

void AssemblyStepsUtils::save_assembly_steps_json_to_model_and_request_extra_frame()
{
    save_assembly_steps_json_to_model();
    do_commond_callback("dirty");
    do_commond_callback("request_extra_frame");
}

 std::string AssemblyStepsUtils::get_object_name(int object_idx) {
     if (m_model) {
         return m_model->objects[object_idx]->name;
     }
     return "";
 }

 bool AssemblyStepsUtils::has_instance(int object_idx)
 {
     if (m_model) {
         return m_model->objects[object_idx]->instances.empty();
     }
     return false;
 }

 std::string AssemblyStepsUtils::get_volume_name(int object_idx, int volume_idx) const
 {
     if (!m_model || object_idx < 0 || object_idx >= (int) m_model->objects.size())
         return "";
     const ModelObject *obj = m_model->objects[object_idx];
     if (!obj || volume_idx < 0 || volume_idx >= (int) obj->volumes.size())
         return "";
     const ModelVolume *mv = obj->volumes[volume_idx];
     // Prefer the volume's own name; fall back to the object's name when the
     // volume has no individual name set (typical for single-volume objects).
     return (mv && !mv->name.empty()) ? mv->name : obj->name;
 }

 Geometry::Transformation AssemblyStepsUtils::get_instance_transform(int object_idx) const
 {
     if (!m_model || object_idx < 0 || object_idx >= (int) m_model->objects.size())
         return Geometry::Transformation();
     const ModelObject *obj = m_model->objects[object_idx];
     if (!obj || obj->instances.empty())
         return Geometry::Transformation();
     return obj->instances[0]->get_assemble_transformation();
 }

 Geometry::Transformation AssemblyStepsUtils::get_volume_transform(int object_idx, int volume_idx) const
 {
     if (!m_model || object_idx < 0 || object_idx >= (int) m_model->objects.size())
         return Geometry::Transformation();
     const ModelObject *obj = m_model->objects[object_idx];
     if (!obj || volume_idx < 0 || volume_idx >= (int) obj->volumes.size())
         return Geometry::Transformation();
     const ModelVolume *mv = obj->volumes[volume_idx];
     return mv ? mv->get_assemble_transformation() : Geometry::Transformation();
 }


 int AssemblyStepsUtils::find_model_object_idx_by_id(size_t object_id) {
     if (object_id == 0) return -1;
     for (int i = 0; i < (int) m_model->objects.size(); ++i)
         if (m_model->objects[i]->id().id == object_id) return i;
     return -1;
}

 int AssemblyStepsUtils::get_object_id_id(size_t object_id)
 {
     if (m_model) {
         return m_model->objects[object_id]->id().id;
     }
     return -1;
 }

 void AssemblyStepsUtils::clear_selection()
{
    if (m_selection) {
        m_selection->clear();
    }
}

void AssemblyStepsUtils::clear_selection_and_lock_volume_mode()
{
    if (!m_selection)
        return;
    set_selection_origin(SelectionOrigin::TreeNode);
    clear_selection();
    m_selection->unlock_volume_selection_mode();
    m_selection->set_volume_selection_mode(Selection::Volume);
    m_selection->set_mode(Selection::Volume);
    m_selection->lock_volume_selection_mode();
}

void AssemblyStepsUtils::add_volumes_and_lock_volume_mode(const std::vector<unsigned int> &gl_volume_idxs)
{
    if (!m_selection || gl_volume_idxs.empty())
        return;
    // Lock Part mode so the next LeftDown does not reset to Instance
    // (GLCanvas3D sets Instance unless Alt is held when unlocked).
    m_selection->unlock_volume_selection_mode();
    m_selection->set_volume_selection_mode(Selection::Volume);
    m_selection->add_volumes(Selection::Volume, gl_volume_idxs, true);
    m_selection->lock_volume_selection_mode();
}

void AssemblyStepsUtils::select_steps_tree_node_for_canvas(int node_idx)
{
    if (m_model == nullptr || m_selection == nullptr)
        return;
    if (node_idx < 0 || node_idx >= (int) _steps_nodes.size())
        return;

    exit_title_mode_if_paused();

    const int object_count = (int) m_model->objects.size();
    auto     &node         = _steps_nodes[node_idx];

    // Reset interaction state: this is a click-from-tree, not a click-from-canvas.
    set_selection_origin(SelectionOrigin::TreeNode);
    m_selected_node            = node_idx;
    clear_selection();
    // Collect every distinct ModelObject reachable from the clicked node so a
    std::vector<unsigned int> obj_idxs;
    obj_idxs.reserve(8);
    std::set<int> visited_nodes;
    std::set<int> seen_obj_idxs;

    std::function<void(int)> collect = [&](int idx) {
        if (idx < 0 || idx >= (int) _steps_nodes.size()) return;
        if (!visited_nodes.insert(idx).second) return; // cycle guard
        auto &n = _steps_nodes[idx];
        if (n.type == AssemblyStepsTreeNode::Type::Object && n.object_idx >= 0 &&
            n.object_idx < object_count && seen_obj_idxs.insert(n.object_idx).second) {
            obj_idxs.push_back((unsigned int) n.object_idx);
        }
        for (int ci : n.children)
            collect(ci);
    };

    if (node.type == AssemblyStepsTreeNode::Type::Folder) {
        // Prefer the step's volume-level membership (from List OK confirm) so
        // switching cards keeps Selection::Volume / part mode instead of
        // collapsing back to whole-object Instance selection.
        select_folder_volumes_on_canvas(node_idx);
        on_selected_node_changed();
        do_commond_callback("dirty");
        return;
    } else if (node.type == AssemblyStepsTreeNode::Type::Object &&
               node.object_idx >= 0 && node.object_idx < object_count) {
        obj_idxs.push_back((unsigned int) node.object_idx);
    } else {
        collect(node_idx);
    }

    // Object-row click: still select in Volume (part) mode.
    std::vector<unsigned int> gl_volume_idxs;
    for (unsigned int oid : obj_idxs) {
        const auto idxs = m_selection->get_volume_idxs_from_object(oid);
        gl_volume_idxs.insert(gl_volume_idxs.end(), idxs.begin(), idxs.end());
    }
    if (!gl_volume_idxs.empty())
        m_selection->add_volumes(Selection::Volume, gl_volume_idxs, true);

    on_selected_node_changed();
    do_commond_callback("dirty");
}

void AssemblyStepsUtils::select_part_label_glvolume(const PartNumberLabel &lbl)
{
    if (!m_selection || !m_model)
        return;
    if (lbl.object_idx < 0 || lbl.object_idx >= (int) m_model->objects.size())
        return;

    // Treat this as a UI-driven selection (like clicking a tree node) so the
    // per-frame canvas->tree sync does not remap or clear it. The selected step
    // node (m_selected_node) is intentionally left unchanged.
    set_selection_origin(SelectionOrigin::TreeNode);
    clear_selection();
    if (lbl.volume_idx >= 0)
        m_selection->add_volume((unsigned int) lbl.object_idx, (unsigned int) lbl.volume_idx, 0, false);
    else
        m_selection->add_object((unsigned int) lbl.object_idx, false);

    do_commond_callback("dirty");
    do_commond_callback("request_extra_frame");
}

void AssemblyStepsUtils::apply_tree_items_selection_to_canvas()
{
    if (!m_selection || !m_model)
        return;

    // Same UI-driven selection path as a part-number label click: treat it as a
    set_selection_origin(SelectionOrigin::TreeNode);
    clear_selection();

    // Collect GLVolume indices first, then apply in one shot. Mixing add_volume
    // (Volume mode) with add_object (Instance mode) would flip m_mode mid-way and
    // break multi-object + multi-volume selections.
    std::vector<unsigned int> gl_volume_idxs;
    bool                      any_volume_row = false;
    for (const auto &item : m_assembly_tree_selected_items) {
        const int object_idx = item.first;
        const int volume_idx = item.second;
        if (object_idx < 0 || object_idx >= (int) m_model->objects.size())
            continue;
        if (volume_idx >= 0) {
            any_volume_row = true;
            const auto idxs = m_selection->get_volume_idxs_from_volume(
                (unsigned int) object_idx, 0, (unsigned int) volume_idx);
            gl_volume_idxs.insert(gl_volume_idxs.end(), idxs.begin(), idxs.end());
        } else {
            const auto idxs = m_selection->get_volume_idxs_from_object((unsigned int) object_idx);
            gl_volume_idxs.insert(gl_volume_idxs.end(), idxs.begin(), idxs.end());
        }
    }
    if (!gl_volume_idxs.empty()) {
        if (any_volume_row) {
            add_volumes_and_lock_volume_mode(gl_volume_idxs);
        } else {
            for (const auto &item : m_assembly_tree_selected_items) {
                if (item.first >= 0 && item.first < (int) m_model->objects.size() && item.second < 0)
                    m_selection->add_object((unsigned int) item.first, false);
            }
        }
    }

    do_commond_callback("dirty");
    do_commond_callback("request_extra_frame");
}

void AssemblyStepsUtils::seed_tree_selected_items_from_canvas(const AssemblyTreeData &tree)
{
    m_assembly_tree_selected_items.clear();
    if (!m_selection || !m_model || tree.nodes.empty())
        return;

    // Current canvas selection as (object_idx, volume_idx) pairs.
    std::set<std::pair<int, int>> sel_pairs;
    for (auto idx : m_selection->get_volume_idxs()) {
        const GLVolume *gv = m_selection->get_volume(idx);
        if (gv == nullptr)
            continue;
        sel_pairs.emplace(gv->object_idx(), gv->volume_idx());
    }
    if (sel_pairs.empty())
        return;

    // Object rows: an object is highlighted at the object level when every one of
    // its selectable volume children is selected (a leaf object node with no
    // volume children is highlighted when any of its volumes is selected). This
    // matches the single-child collapse in the renderer, where the object row
    // represents a single-volume object.
    std::set<int> whole_objects;
    for (const auto &n : tree.nodes) {
        if (!n.selectable || n.volume_idx >= 0 || n.object_idx < 0)
            continue;
        bool any_child = false;
        bool all_child = true;
        for (int ci : n.children) {
            if (ci < 0 || ci >= (int) tree.nodes.size())
                continue;
            const auto &cn = tree.nodes[ci];
            if (!cn.selectable || cn.volume_idx < 0)
                continue;
            any_child = true;
            if (sel_pairs.count({cn.object_idx, cn.volume_idx}) == 0) {
                all_child = false;
                break;
            }
        }
        bool object_selected = any_child && all_child;
        if (!any_child) {
            // Leaf object node (no volume children): selected if any of its
            // volumes is part of the canvas selection.
            for (const auto &p : sel_pairs) {
                if (p.first == n.object_idx) {
                    object_selected = true;
                    break;
                }
            }
        }
        if (object_selected) {
            whole_objects.insert(n.object_idx);
            m_assembly_tree_selected_items.emplace(n.object_idx, -1);
        }
    }

    // Volume rows: highlight a selected volume unless its object is already
    // highlighted as a whole (keeps per-row toggling unambiguous).
    for (const auto &n : tree.nodes) {
        if (!n.selectable || n.volume_idx < 0 || n.object_idx < 0)
            continue;
        if (whole_objects.count(n.object_idx) > 0)
            continue;
        if (sel_pairs.count({n.object_idx, n.volume_idx}) > 0)
            m_assembly_tree_selected_items.emplace(n.object_idx, n.volume_idx);
    }
}

bool AssemblyStepsUtils::is_standalone_assembly_tree_list_visible() const
{
    return !has_selected_node() && !is_render_assembly_tree_ui_open();
}

void AssemblyStepsUtils::sync_tree_ui_selection_from_canvas()
{
    // Mirror onto tree row highlights while either the step-editing List popup
    // or the read-only standalone Assembly list is visible.
    if (!m_model)
        return;
    if (!is_render_assembly_tree_ui_open() && !is_standalone_assembly_tree_list_visible())
        return;
    seed_tree_selected_items_from_canvas(m_model->get_assembly_tree_data());
}

void AssemblyStepsUtils::hover_tree_item_logic(int id)
{
    // Canvas-side hover effect of a tree-view row. The argument is the unique
    // ObjectID of the hovered ModelObject / ModelVolume (-1 when the hover left
    // every row). Hovering an item previews it as the canvas selection (which
    // draws its bounding box); leaving restores the persistent click selection.
    if (m_assembly_tree_hover_id == id)
        return;
    m_assembly_tree_hover_id = id;

    if (!m_selection || !m_model)
        return;

    if (id < 0) {
        // Hover left the tree: restore the rows selected by clicking.
        apply_tree_items_selection_to_canvas();
        return;
    }

    // Resolve the (object_idx, volume_idx) backing the hovered ObjectID.
    int obj_idx = -1, vol_idx = -1;
    for (int oi = 0; oi < (int) m_model->objects.size() && obj_idx < 0; ++oi) {
        const ModelObject *mo = m_model->objects[oi];
        if (mo == nullptr)
            continue;
        if ((int) mo->id().id == id) {
            obj_idx = oi;
            vol_idx = -1;
            break;
        }
        for (int vi = 0; vi < (int) mo->volumes.size(); ++vi) {
            if (mo->volumes[vi] != nullptr && (int) mo->volumes[vi]->id().id == id) {
                obj_idx = oi;
                vol_idx = vi;
                break;
            }
        }
    }
    if (obj_idx < 0) {
        // Unknown id (group/folder row or stale): keep the click selection.
        apply_tree_items_selection_to_canvas();
        return;
    }

    // Preview the hovered item; the selection rendering shows its bounding box.
    // Treat it as a tree-node selection so the per-frame canvas->tree sync keeps
    // it stable and m_selected_node is left untouched.
    set_selection_origin(SelectionOrigin::TreeNode);
    clear_selection();
    if (vol_idx >= 0)
        m_selection->add_volume((unsigned int) obj_idx, (unsigned int) vol_idx, 0, false);
    else
        m_selection->add_object((unsigned int) obj_idx, false);

    do_commond_callback("dirty");
    do_commond_callback("request_extra_frame");
}


void AssemblyStepsUtils::clear_all_keyframe_part_number_labels()
{
    bool changed = false;
    for (auto &node : _steps_nodes) {
        for (auto &entry : node.kf_data.entries) {
            auto &labels = entry.data.assembly_note.part_number_labels;
            if (labels.empty())
                continue;
            // Keep show_part_labels as-is so refresh_guide_show_part_numbers_from_current()
            // regenerates the labels for the frame the next time it is displayed.
            labels.clear();
            entry.need_save = true;
            changed         = true;
        }
    }
    if (!changed)
        return;
    save_assembly_steps_json_to_model();
    do_commond_callback("dirty");
    do_commond_callback("request_extra_frame");
}

void AssemblyStepsUtils::on_selected_node_changed()
{
    int cur_folder = find_parent_folder(m_selected_node);
    if (cur_folder != m_last_folder_idx) {
        on_selected_node_step_changed(cur_folder);//include apply_keyframe_display_mode
        m_last_folder_idx = cur_folder;
    }

    refresh_guide_show_part_numbers_from_current();
    m_selected_screen_center_dirty_ = true;
    do_commond_callback("reset_explosion_ratio");
}

void AssemblyStepsUtils::on_selected_node_step_changed(int folder_idx)
{
    // Switching step card must leave any in-progress note / connection editing,
    // and close the add-object tree panel (it belongs to the previous step).
    clear_note_selection();
    exit_render_assembly_tree_ui();
    if (folder_idx >= 0) {
        if (m_only_final_assembly_endframe_effect_real_assembly) {
            apply_final_assembly_end_keyframe();
        } else {
            apply_end_keyframe(m_last_folder_idx); // bbl logic
        }
        apply_end_keyframe(folder_idx);
        refresh_guide_show_part_numbers_from_current();
    }
    m_keyframe_selected = default_keyframe_index();

    apply_keyframe_display_mode();
    do_commond_callback("exit_gizmo");

    // Sync progress bar: find the global frame index matching this folder's
    // selected keyframe so the bar position reflects the current step.
    sync_play_index_to_selection();

    if (m_last_folder_idx < 0 || m_last_folder_idx >= (int) _steps_nodes.size()) {
        return;
    }
    //change folder
}

void AssemblyStepsUtils::sync_play_index_to_selection()
{
    // Map the currently selected step folder + keyframe to its global play-bar
    if (m_play_frame_refs.empty()) {
        m_assembly_play_index = 1;
        return;
    }
    const int folder_idx = find_parent_folder(m_selected_node);
    if (folder_idx >= 0) {
        const int target_frame_idx = m_keyframe_selected;
        for (int gi = 0; gi < (int) m_play_frame_refs.size(); ++gi) {
            if (m_play_frame_refs[gi].node_idx == folder_idx &&
                m_play_frame_refs[gi].frame_idx == target_frame_idx) {
                m_assembly_play_index = gi + 1;
                return;
            }
        }
    }
    // No selection match (e.g. the selected step was just deleted): clamp the stale index
    // into the rebuilt range so the play bar can't point past the last frame.
    const int count = (int) m_play_frame_refs.size();
    if (m_assembly_play_index < 1)
        m_assembly_play_index = 1;
    else if (m_assembly_play_index > count)
        m_assembly_play_index = count;
}

void AssemblyStepsUtils::reschedule_play_bar_after_structure_change()
{
    invalidate_play_frame_refs();
    rebuild_play_frame_refs();
    sync_play_index_to_selection();

    save_assembly_steps_json_to_model();
    do_commond_callback("dirty");
    do_commond_callback("request_extra_frame");
}

void AssemblyStepsUtils::apply_final_assembly_end_keyframe(bool apply_camera_view)
{
    for (int i = 0; i < (int) _steps_nodes.size(); ++i) {
        if (_steps_nodes[i].type == AssemblyStepsTreeNode::Type::Folder && _steps_nodes[i].is_final_assembly) {
            apply_end_keyframe(i, apply_camera_view);
            return;
        }
    }
}

void AssemblyStepsUtils::apply_end_keyframe(int folder_idx, bool apply_camera_view)
{
    if (folder_idx >= 0 && folder_idx < _steps_nodes.size()) {
        auto &_entries = _steps_nodes[folder_idx].kf_data.entries;
        for (const auto &entry : _entries) {
            if (entry.is_last() && entry.need_save) {
                apply_keyframe_to_canvas(entry.data, apply_camera_view);
                return;
            }
        }
    }
}

void AssemblyStepsUtils::auto_apply_final_assembly_on_selection_cleared()
{
    // Edge-trigger only: act on the "had a node selected -> nothing selected"
    const bool now = has_selected_node();
    if (m_last_has_selected_node_ && !now)
        // Restore the final-assembly model state on selection clear, but keep
        // the user's current camera view (e.g. double-click to exit a step
        // should not rotate the camera back to the saved keyframe angle).
        apply_final_assembly_end_keyframe(/*apply_camera_view*/ false);
    m_last_has_selected_node_ = now;
}

void AssemblyStepsUtils::update_step_screen_center()
{
    if (!m_selected_screen_center_dirty_) { return; }
    if (!m_camera || !m_volumes) {
        m_selected_screen_center_ = Vec2d::Zero();
        return;
    }

    std::set<int> obj_idxs;
    if (m_only_step_node_create_key_frame) {
        int folder_idx = find_parent_folder(m_selected_node);
        if (folder_idx >= 0)
            obj_idxs = collect_node_object_indices(folder_idx);
    } else {
        if (m_selected_node >= 0 && m_selected_node < (int) _steps_nodes.size()) {
            obj_idxs = collect_node_object_indices(m_selected_node);
        }
    }

    std::vector<GLVolume*> filtered;
    for (GLVolume *vol : m_volumes->volumes) {
        if (!vol || !vol->is_active)
            continue;
        if (obj_idxs.find(vol->composite_id.object_id) != obj_idxs.end())
            filtered.push_back(vol);
    }

    m_selected_screen_center_ = compute_selected_volumes_screen_center(*m_camera, filtered);
    // Snapshot the camera state so render_main's per-frame check can detect
    // any later rotate / pan / zoom that did not go through one of the
    // explicit dirty-marking sites (selection change, apply_keyframe,
    // consume_play_queue_frame).
    m_last_view_matrix_for_anchor_ = m_camera->get_view_matrix();
    m_last_proj_matrix_for_anchor_ = m_camera->get_projection_matrix();
    m_selected_screen_center_dirty_ = false;
}

void AssemblyStepsUtils::fill_folder_keyframes_from_children(int folder_idx, bool use_glvolume_tran)
{
    if (!m_model || folder_idx < 0 || folder_idx >= (int)_steps_nodes.size())
        return;
    const int obj_count = (int)m_model->objects.size();
    std::set<int> child_objs = collect_node_object_indices(folder_idx);
    auto         &nd         = _steps_nodes[folder_idx];
    if (nd.type != AssemblyStepsTreeNode::Type::Folder)
        return;

    // When use_glvolume_tran is set, capture the pose from the live GLVolume assemble
    auto find_glvolume = [&](int oi, int vi) -> const GLVolume * {
        if (!m_volumes)
            return nullptr;
        for (const GLVolume *vol : m_volumes->volumes) {
            if (vol && vol->object_idx() == oi && vol->volume_idx() == vi)
                return vol;
        }
        return nullptr;
    };
    auto find_glvolume_for_object = [&](int oi) -> const GLVolume * {
        if (!m_volumes)
            return nullptr;
        for (const GLVolume *vol : m_volumes->volumes) {
            if (vol && vol->object_idx() == oi)
                return vol;
        }
        return nullptr;
    };
    auto &kf_entries = nd.kf_data.entries;
    for (auto &entry : kf_entries) {
        for (int oi : child_objs) {
            if (oi < 0 || oi >= obj_count)
                continue;
            const ModelObject *obj = m_model->objects[oi];
            if (!obj)
                continue;
            // Capture both halves of the GLVolume world transform:
            if (!obj->instances.empty()) {
                const GLVolume *gv = use_glvolume_tran ? find_glvolume_for_object(oi) : nullptr;
                entry.data.object_transformations[oi] = gv ? gv->get_instance_transformation()
                                                           : get_instance_transform(oi);
            }
            for (int vi = 0; vi < (int) obj->volumes.size(); ++vi) {
                const ModelVolume *mv = obj->volumes[vi];
                if (!mv)
                    continue;
                const std::pair<int, int> key{oi, vi};
                const GLVolume *gv = use_glvolume_tran ? find_glvolume(oi, vi) : nullptr;
                entry.data.volume_transformations[key] = gv ? gv->get_volume_transformation()
                                                            : get_volume_transform(oi, vi);
                entry.data.volume_names[key]           = !mv->name.empty() ? mv->name : obj->name;
            }
            entry.need_save = true;
        }
        entry.need_save = true;
    }
    if (nd.is_final_assembly)
        record_current_model_as_last_final_assembly();
}

std::vector<int> AssemblyStepsUtils::selected_assembly_object_indices() const
 {
     if (!m_model || !m_selection)
         return {};

     std::vector<int> selected_object_idxs;
     const auto      &content = m_selection->get_content();
     selected_object_idxs.reserve(content.size());
     for (const auto &pair : content) {
         selected_object_idxs.push_back(pair.first);
     }
     return selected_object_indices((int) m_model->objects.size(), selected_object_idxs);
 }

 void AssemblyStepsUtils::add_selected_to_new_assembly_step() {
     create_assembly_step_from_objects(selected_assembly_object_indices());
 }

 void AssemblyStepsUtils::add_selected_to_current_assembly_step() {
     const int folder_idx = find_parent_folder(m_selected_node);
     if (folder_idx < 0 || folder_idx >= (int) _steps_nodes.size() ||  _steps_nodes[folder_idx].type != AssemblyStepsTreeNode::Type::Folder || _steps_nodes[folder_idx].is_final_assembly)
         return;

     const std::vector<int> object_idxs = selected_assembly_object_indices();
     if (_steps_nodes[folder_idx].name == _u8L("Install parts") && !object_idxs.empty()) {//modify name
         const int first_object_idx = object_idxs.front();
         if (first_object_idx >= 0 && first_object_idx < (int)m_model->objects.size() &&
             m_model->objects[first_object_idx] &&
             !m_model->objects[first_object_idx]->name.empty())
             _steps_nodes[folder_idx].name = m_model->objects[first_object_idx]->name;
     }
     add_objects_to_assembly_step(folder_idx, object_idxs);
 }

 int AssemblyStepsUtils::non_final_assembly_step_count() const
 {
     if (!m_model)
         return 0;
     int count = 0;
     for (int ri : _steps_roots) {
         if (ri < 0 || ri >= (int) _steps_nodes.size())
             continue;
         const auto &n = _steps_nodes[ri];
         if (n.type == AssemblyStepsTreeNode::Type::Folder && !n.is_final_assembly)
             ++count;
     }
     return count;
 }

 bool AssemblyStepsUtils::can_add_non_final_assembly_step() const
 {
     const bool can_add = non_final_assembly_step_count() < MAX_NON_FINAL_ASSEMBLY_STEPS;
     // Cache the limit-reached state for the Copy/Add Step tooltips (see header).
     m_non_final_assembly_step_limit_reached = !can_add;
     return can_add;
 }

 void AssemblyStepsUtils::add_assembly_step() {
     if (!m_model)
         return;

    int        selected_folder         = find_parent_folder(m_selected_node);
    const bool selected_final_assembly = selected_folder >= 0 && selected_folder < (int) _steps_nodes.size() &&
                                         _steps_nodes[selected_folder].is_final_assembly;

    // Cap the number of user-created steps; final assembly is excluded from the count.
    if (!can_add_non_final_assembly_step())
        return;

    // Resolve the insertion point as a (reference node, before/after) pair, then reuse
    int  ref_node_idx = -1;
    bool insert_before = true;
    if (selected_final_assembly) {
        ref_node_idx  = ensure_final_assembly_folder();
        insert_before = true;
    } else if (selected_folder >= 0 && selected_folder < (int) _steps_nodes.size() &&
               _steps_nodes[selected_folder].type == AssemblyStepsTreeNode::Type::Folder) {
        ref_node_idx  = selected_folder;
        insert_before = false;
    } else {
        ref_node_idx  = ensure_final_assembly_folder();
        insert_before = true;
    }

    insert_structure_step_relative(ref_node_idx, insert_before, _u8L("Install parts"), /*copy=*/false);


}

 void AssemblyStepsUtils::copy_assembly_step() {
     if (!m_model)
         return;

     int source_folder = find_parent_folder(m_selected_node);
     if (source_folder < 0 || source_folder >= (int) _steps_nodes.size())
         return;
     if (_steps_nodes[source_folder].type != AssemblyStepsTreeNode::Type::Folder || _steps_nodes[source_folder].is_final_assembly)
         return;

     // Copy == clone the current step and insert it right after the source.
     insert_structure_step_relative(source_folder, /*before=*/false, std::string(), /*copy=*/true);
 }

 void AssemblyStepsUtils::add_selected_to_assembly_step(int folder_idx) {
     add_objects_to_assembly_step(folder_idx, selected_assembly_object_indices());
 }

 bool AssemblyStepsUtils::can_add_selected_to_current_assembly_step() const
 {
     if (!m_selection) { return false; }
     const int folder_idx = const_cast<AssemblyStepsUtils*>(this)->find_parent_folder(m_selected_node);
     if (folder_idx < 0 || folder_idx >= (int) _steps_nodes.size() || _steps_nodes[folder_idx].type != AssemblyStepsTreeNode::Type::Folder ||
         _steps_nodes[folder_idx].is_final_assembly)
         return false;
     if (is_empty_structure_step(folder_idx)) {
         return true;
     }
     // Volume / Part selection is allowed: membership is recorded per ModelVolume.
     return can_add_objects_to_step(selected_assembly_object_indices());
 }

 bool AssemblyStepsUtils::can_add_selected_to_assembly_step() const
 {
     if (!m_selection)
         return false;
     if (has_selected_node()) { return false; }
     // Volume / Part selection is allowed: same path as the List UI part checks.
     return can_add_objects_to_step(selected_assembly_object_indices());
 }

 void AssemblyStepsUtils::record_camera(KeyFrame &kf)
 {
      if (m_camera) {
          Camera &cam          = *m_camera;
          kf.view_matrix       = cam.get_view_matrix();
          kf.projection_matrix = cam.get_projection_matrix();
          kf.camera_target     = cam.get_target();
          kf.camera_zoom       = cam.get_zoom();
          // Remember the viewport this zoom was framed for, so a later restore into a
          if (m_model) {
              const std::array<int, 4> vp = cam.get_viewport();
              if (vp[2] > 0 && vp[3] > 0) {
                  AssemblyStepsTreeData &tree = m_model->get_assembly_steps_tree_data();
                  tree.camera_ref_viewport_w  = vp[2];
                  tree.camera_ref_viewport_h  = vp[3];
              }
          }
      }
 }

void AssemblyStepsUtils::rescale_user_camera_zoom_to_viewport(const KeyFrame &kf)
{
    // Adjust a restored user-framed camera's zoom so the model keeps the same relative
    // size when the current viewport differs from the one the zoom was captured at.
    // Uses the limiting axis (min ratio) so the originally framed content stays fully
    // visible regardless of aspect-ratio changes. The reference viewport is a single
    // document-level value shared by all keyframes (see AssemblyStepsTreeData).
    if (!m_camera || !m_model)
        return;
    const AssemblyStepsTreeData &tree = m_model->get_assembly_steps_tree_data();
    if (tree.camera_ref_viewport_w <= 0 || tree.camera_ref_viewport_h <= 0)
        return;
    const std::array<int, 4> vp = m_camera->get_viewport();
    if (vp[2] <= 0 || vp[3] <= 0)
        return;
    const double sx    = (double) vp[2] / (double) tree.camera_ref_viewport_w;
    const double sy    = (double) vp[3] / (double) tree.camera_ref_viewport_h;
    const double scale = std::min(sx, sy);
    m_camera->set_zoom(kf.camera_zoom * scale);
}

 void AssemblyStepsUtils::record_selected_volumes_by_mo_mv(KeyFrame &kf)
 {
    if (!m_model) return;
     kf.object_transformations.clear();
     kf.volume_transformations.clear();
     kf.volume_names.clear();
    auto snapshot_object_volumes = [&](int object_idx) {
        if (object_idx < 0 || object_idx >= (int) m_model->objects.size())
            return;
        const ModelObject *obj = m_model->objects[object_idx];
        if (!obj)
            return;
        if (!obj->instances.empty())
            kf.object_transformations[object_idx] = get_instance_transform(object_idx);
        for (int vi = 0; vi < (int) obj->volumes.size(); ++vi) {
            const ModelVolume *mv = obj->volumes[vi];
            if (!mv)
                continue;
            const std::pair<int, int> key{object_idx, vi};
            kf.volume_transformations[key] = get_volume_transform(object_idx, vi);
            kf.volume_names[key]           = !mv->name.empty() ? mv->name : obj->name;
        }
    };

    if (m_only_step_node_create_key_frame) {
        int folder_idx = find_parent_folder(m_selected_node);
        if (folder_idx >= 0) {
            std::set<int> obj_idxs = collect_node_object_indices(folder_idx);
            for (int oi : obj_idxs)
                snapshot_object_volumes(oi);
        }
    } else {
         const auto &sel_content = m_selection->get_content();
         for (const auto &pair : sel_content) {
             snapshot_object_volumes(pair.first);
         }
     }
 }

void AssemblyStepsUtils::update_final_assembly_end_keyframe_from_current_selection()
{
    if (!m_model || !m_selection)
        return;

    // Locate the (single) final-assembly folder.
    int final_folder_idx = -1;
    for (int i = 0; i < (int) _steps_nodes.size(); ++i) {
        if (_steps_nodes[i].type == AssemblyStepsTreeNode::Type::Folder && _steps_nodes[i].is_final_assembly) {
            final_folder_idx = i;
            break;
        }
    }
    if (final_folder_idx < 0)
        return;

    // Find that folder's end-frame entry (id == 0).
    auto          &fa_entries = _steps_nodes[final_folder_idx].kf_data.entries;
    KeyFrameEntry *end_entry  = nullptr;
    for (auto &e : fa_entries) {
        if (e.is_last()) {
            end_entry = &e;
            break;
        }
    }
    if (!end_entry)
        return;

    KeyFrame &kf = end_entry->data;

    // Patch only the transforms of objects/volumes currently picked on the
    bool any_patched = false;
    const auto &sel_content = m_selection->get_content();
    for (const auto &pair : sel_content) {
        const int object_idx = pair.first;
        if (object_idx < 0 || object_idx >= (int)m_model->objects.size())
            continue;
        const ModelObject *obj = m_model->objects[object_idx];
        if (!obj)
            continue;
        if (!obj->instances.empty()) {
            kf.object_transformations[object_idx] = get_instance_transform(object_idx);
            any_patched = true;
        }
        for (int vi = 0; vi < (int)obj->volumes.size(); ++vi) {
            const ModelVolume *mv = obj->volumes[vi];
            if (!mv)
                continue;
            const std::pair<int, int> key{object_idx, vi};
            kf.volume_transformations[key] = get_volume_transform(object_idx, vi);
            kf.volume_names[key]           = !mv->name.empty() ? mv->name : obj->name;
            any_patched = true;
        }
    }
    if (!any_patched)
        return;

    end_entry->need_save = true;
    save_assembly_steps_json_to_model();
}

void AssemblyStepsUtils::record_selected_gl_volume_transforms_to_current_keyframe()
{
    auto *entries = get_current_kf_entries();
    if (!entries || m_keyframe_selected < 0 || m_keyframe_selected >= (int)entries->size())
        return;
    if (!m_model || !m_selection)
        return;

    KeyFrameEntry &entry = (*entries)[m_keyframe_selected];
    KeyFrame      &kf    = entry.data;

    // Read GLVolume::m_instance_transformation / m_volume_transformation
    bool        any_patched = false;
    const auto &sel_idxs    = m_selection->get_volume_idxs();
    for (unsigned int gi : sel_idxs) {
        const GLVolume *gv = m_selection->get_volume(gi);
        if (!gv)
            continue;
        const int oi = gv->object_idx();
        const int vi = gv->volume_idx();
        if (oi < 0 || oi >= (int)m_model->objects.size())
            continue;
        const ModelObject *obj = m_model->objects[oi];
        if (!obj)
            continue;

        if (!obj->instances.empty()) {
            kf.object_transformations[oi] = gv->get_instance_transformation();
            any_patched                   = true;
        }
        if (vi >= 0 && vi < (int)obj->volumes.size()) {
            const ModelVolume        *mv = obj->volumes[vi];
            const std::pair<int, int> key{oi, vi};
            kf.volume_transformations[key] = gv->get_volume_transformation();
            if (mv)
                kf.volume_names[key] = !mv->name.empty() ? mv->name : obj->name;
            any_patched = true;
        }
    }
    if (!any_patched)
        return;

    entry.need_save = true;
    record_camera(kf);
    // Explicit user edit (gizmo move on a regular step, or re-record): the camera
    kf.camera_user_defined = true;
    save_assembly_steps_json_to_model();
}

void AssemblyStepsUtils::record_all_glvolumes_in_cur_step__to_current_keyframe()
{
    auto *entries = get_current_kf_entries();
    if (!entries || m_keyframe_selected < 0 || m_keyframe_selected >= (int)entries->size())
        return;
    if (!m_model || !m_volumes)
        return;

    KeyFrameEntry &entry = (*entries)[m_keyframe_selected];
    KeyFrame      &kf    = entry.data;

    std::set<int> step_objs = collect_node_object_indices(m_selected_node);

    bool any_patched = false;
    for (const GLVolume *gv : m_volumes->volumes) {
        if (!gv)
            continue;
        const int oi = gv->object_idx();
        if (oi < 0 || oi >= (int)m_model->objects.size())
            continue;
        if (step_objs.count(oi) == 0)
            continue;
        const int vi = gv->volume_idx();

        const ModelObject *obj = m_model->objects[oi];
        if (!obj)
            continue;

        if (!obj->instances.empty()) {
            kf.object_transformations[oi] = gv->get_instance_transformation();
            any_patched                   = true;
        }
        if (vi >= 0 && vi < (int)obj->volumes.size()) {
            const ModelVolume        *mv = obj->volumes[vi];
            const std::pair<int, int> key{oi, vi};
            kf.volume_transformations[key] = gv->get_volume_transformation();
            if (mv)
                kf.volume_names[key] = !mv->name.empty() ? mv->name : obj->name;
            any_patched = true;
        }
    }
    if (!any_patched)
        return;

    entry.need_save = true;
    record_camera(kf);
    kf.camera_user_defined = true;
    save_assembly_steps_json_to_model();
}

 void AssemblyStepsUtils::show_all_volumes(bool show)
 {
     if (m_volumes) {
         for (GLVolume *vol : m_volumes->volumes) {
             vol->is_active = show;
         }
     }
     do_commond_callback("dirty");
 }

void AssemblyStepsUtils::show_volume(int object_id, bool show)
{
     if (m_volumes) {
         for (GLVolume *vol : m_volumes->volumes) {
             if (vol->composite_id.object_id == object_id)
             {
                 vol->is_active = show;
             }
         }
     }
     do_commond_callback("dirty");
}

void AssemblyStepsUtils::apply_camera(const KeyFrame &frame)
{
    if (m_camera) {
        Camera &cam = *m_camera;
        cam.set_view_projection(frame.view_matrix, frame.projection_matrix, frame.camera_target, frame.camera_zoom);
    }
}

void AssemblyStepsUtils::fit_camera_to_current_step_main_plane(double margin_factor)
{
    if (!m_camera || !m_volumes || !m_model)
        return;

    const int folder = find_parent_folder(m_selected_node);
    if (folder < 0 || folder >= (int) _steps_nodes.size())
        return;
    const bool final_assembly = _steps_nodes[folder].is_final_assembly;

    std::set<int> step_objs;
    if (!final_assembly)
        step_objs = collect_node_object_indices(folder);

    BoundingBoxf3 bbox;
    bool has_any = false;
    for (const GLVolume *vol : m_volumes->volumes) {
        if (!vol)
            continue;
        if (!final_assembly && step_objs.find(vol->composite_id.object_id) == step_objs.end())
            continue;
        bbox.merge(vol->transformed_bounding_box());
        has_any = true;
    }
    if (!has_any || !bbox.defined)
        return;

    const Vec3d size   = bbox.size();
    const Vec3d center = bbox.center();

    // Pick the bbox face with the largest area and look straight at it: the
    // viewing direction is that face's normal (the remaining axis).
    const double area_xy = size.x() * size.y(); // normal = Z (top view)
    const double area_xz = size.x() * size.z(); // normal = Y (front view)
    const double area_yz = size.y() * size.z(); // normal = X (side view)

    // look_at() makes the screen-horizontal axis = up x normal (the in-plane
    Vec3d normal, up;
    if (area_xy >= area_xz && area_xy >= area_yz) {
        // Largest face = XY (normal Z); in-plane edges along X and Y.
        normal = Vec3d::UnitZ();
        up     = (size.x() >= size.y()) ? Vec3d::UnitY() : Vec3d::UnitX();
    } else if (area_xz >= area_yz) {
        // Largest face = XZ (normal Y); in-plane edges along X and Z.
        normal = Vec3d::UnitY();
        up     = (size.x() >= size.z()) ? Vec3d::UnitZ() : Vec3d::UnitX();
    } else {
        // Largest face = YZ (normal X); in-plane edges along Y and Z.
        normal = Vec3d::UnitX();
        up     = (size.y() >= size.z()) ? Vec3d::UnitZ() : Vec3d::UnitY();
    }

    const double radius   = std::max(size.norm(), 1.0);
    const Vec3d  position = center + normal * radius * 2.0;
    m_camera->look_at(position, center, up);
    // zoom first (sets target + zoom), then refresh the projection so the
    // matrices are coherent even before the next full canvas render re-applies
    // its own projection. margin_factor controls the empty room left around the
    // model (Camera's own default 1.025 is almost flush to the viewport edge).
    m_camera->zoom_to_box(bbox, std::max(margin_factor, 1.0));
    m_camera->apply_projection(bbox);
}

KeyFrameEntry *AssemblyStepsUtils::get_selected_keyframe_entry()
{
    auto *entries = get_current_kf_entries();
    if (!entries || m_keyframe_selected < 0 || m_keyframe_selected >= (int) entries->size())
        return nullptr;
    return &(*entries)[m_keyframe_selected];
}

void AssemblyStepsUtils::apply_camera_margin_to_selected_keyframe(float margin_factor, bool commit)
{
    KeyFrameEntry *entry = get_selected_keyframe_entry();
    if (entry == nullptr)
        return;

    entry->data.camera_margin_factor = margin_factor;
    // Live preview: re-frame the current step with the new margin and capture the
    fit_camera_to_current_step_main_plane(margin_factor);
    record_camera(entry->data);
    entry->data.is_camera_define = true;
    entry->need_save = true;

    do_commond_callback("dirty");
    do_commond_callback("request_extra_frame");
    // Only persist to the model when the edit is finished (slider released), to
    if (commit)
        save_assembly_steps_json_to_model();
}

void AssemblyStepsUtils::apply_instance_transform(int object_idx, const Geometry::Transformation &transform)
{
    if (!m_volumes || !m_model || object_idx < 0 || object_idx >= (int) m_model->objects.size())
        return;
    ModelObject *obj = m_model->objects[object_idx];
    if (!obj || obj->instances.empty())
        return;
    // instance side: obj->instances[0]->set_assemble_transformation(transform);
    for (GLVolume *vol : m_volumes->volumes) {
        if (vol->composite_id.object_id == object_idx)
            vol->set_instance_transformation(transform);
    }
}

void AssemblyStepsUtils::apply_volume_transform(int object_idx, int volume_idx, const Geometry::Transformation &transform)
{
    if (!m_volumes || !m_model || object_idx < 0 || object_idx >= (int) m_model->objects.size())
        return;
    ModelObject *obj = m_model->objects[object_idx];
    if (!obj || volume_idx < 0 || volume_idx >= (int) obj->volumes.size())
        return;
    // GLVolume side: only the matching (object, volume) pair carries this
    for (GLVolume *vol : m_volumes->volumes) {
        if (vol->composite_id.object_id == object_idx && vol->composite_id.volume_id == volume_idx)
            vol->set_volume_transformation(transform);
    }
}

void AssemblyStepsUtils::apply_regular_steps_start_frame_transforms_to_current(bool include_volume_transforms)
{
    if (!m_model)
        return;

    int folder = find_parent_folder(m_selected_node);
    if (folder < 0 || folder >= (int) _steps_nodes.size() || !_steps_nodes[folder].is_final_assembly)
        return;

    auto *current_entries = get_current_kf_entries();
    if (!current_entries || m_keyframe_selected < 0 || m_keyframe_selected >= (int)current_entries->size())
        return;
    KeyFrameEntry &current_entry = (*current_entries)[m_keyframe_selected];
    if (current_entry.is_last())
        return;

    KeyFrame &target = current_entry.data;
    AssemblyStructurePanelData panel_data = build_assembly_structure_panel_data();
    for (const AssemblyStructureCard &card : panel_data.cards) {
        if (card.tag_style != AssemblyStructureCard::TagStyle::Step || card.is_final_assembly)
            continue;
        const int root_idx = card.node_idx;
        if (root_idx < 0 || root_idx >= (int) _steps_nodes.size())
            continue;

        const auto &root = _steps_nodes[root_idx];
        if (root.type != AssemblyStepsTreeNode::Type::Folder || root.is_final_assembly)
            continue;

        const KeyFrameEntry *start_entry = nullptr;
        for (const auto &entry : root.kf_data.entries) {
            if (entry.is_start()) {
                start_entry = &entry;
                break;
            }
        }
        if (!start_entry) {
            for (const auto &entry : root.kf_data.entries) {
                if (!entry.is_last()) {
                    start_entry = &entry;
                    break;
                }
            }
        }
        if (!start_entry)
            continue;

        for (const auto &item : start_entry->data.object_transformations)
            target.object_transformations[item.first] = item.second;
        if (include_volume_transforms) {
            for (const auto &item : start_entry->data.volume_transformations)
                target.volume_transformations[item.first] = item.second;
            for (const auto &item : start_entry->data.volume_names)
                target.volume_names[item.first] = item.second;
        }
    }

    for (const auto &item : target.object_transformations)
        apply_instance_transform(item.first, item.second);
    if (include_volume_transforms) {
        for (const auto &item : target.volume_transformations)
            apply_volume_transform(item.first.first, item.first.second, item.second);
    }

    current_entry.need_save = true;
    invalidate_play_frame_refs();
    save_assembly_steps_json_to_model();
    m_selected_screen_center_dirty_ = true;
    do_commond_callback("dirty");
}

void AssemblyStepsUtils::apply_final_assembly_end_frame_transforms_to_current_keyframe()
{
    if (!m_model)
        return;

    // Locate the final-assembly folder + its end-frame entry.
    int final_folder_idx = -1;
    for (int i = 0; i < (int) _steps_nodes.size(); ++i) {
        if (_steps_nodes[i].type == AssemblyStepsTreeNode::Type::Folder && _steps_nodes[i].is_final_assembly) {
            final_folder_idx = i;
            break;
        }
    }
    if (final_folder_idx < 0)
        return;
    KeyFrameEntry *src_end_entry = nullptr;
    for (auto &e : _steps_nodes[final_folder_idx].kf_data.entries) {
        if (e.is_last()) {
            src_end_entry = &e;
            break;
        }
    }
    if (!src_end_entry)
        return;

    // Collect the canvas selection as object / volume keys. Each selected
    // GLVolume contributes its object (instance-level) and, when it maps to a
    // ModelVolume, its (object, volume) pair (part-level).
    std::set<int>                  selected_objects;
    std::set<std::pair<int, int>>  selected_volumes;
    if (m_selection) {
        const auto &sel_idxs = m_selection->get_volume_idxs();
        for (unsigned int gi : sel_idxs) {
            const GLVolume *gv = m_selection->get_volume(gi);
            if (!gv)
                continue;
            const int oi = gv->object_idx();
            const int vi = gv->volume_idx();
            if (oi < 0 || oi >= (int) m_model->objects.size())
                continue;
            selected_objects.insert(oi);
            if (vi >= 0)
                selected_volumes.insert({oi, vi});
        }
    }

    // With an active selection, only apply the final-assembly pose to the
    // selected objects/parts. (Covers both single and multi selection.)
    if (!selected_objects.empty() || !selected_volumes.empty()) {
        apply_src_frame_transforms_to_current_keyframe(*src_end_entry, selected_objects, selected_volumes, true);
        return;
    }

    // No selection: ask before applying the final-assembly pose to every
    // object/part that was added to the current step.
    MessageDialog msg_dlg(nullptr,
        _L("Apply the final assembly pose to the objects or parts added in the current step?"),
        _L("Apply final assembly pose"),
        wxICON_QUESTION | wxYES_NO);
    if (msg_dlg.ShowModal() != wxID_YES)
        return;

    // Gather the objects that belong to the current step (descendant Object nodes
    // of the selected step folder), then restrict the apply to those.
    const int folder = find_parent_folder(m_selected_node);
    std::set<int>                 step_objects;
    std::set<std::pair<int, int>> step_volumes;
    if (folder >= 0 && folder < (int) _steps_nodes.size()) {
        std::function<void(int)> collect = [&](int idx) {
            if (idx < 0 || idx >= (int) _steps_nodes.size())
                return;
            const auto &node = _steps_nodes[idx];
            if (node.type == AssemblyStepsTreeNode::Type::Object && node.object_idx >= 0)
                step_objects.insert(node.object_idx);
            for (int c : node.children)
                collect(c);
        };
        collect(folder);
    }
    for (const auto &item : src_end_entry->data.volume_transformations) {
        if (step_objects.count(item.first.first))
            step_volumes.insert(item.first);
    }
    apply_src_frame_transforms_to_current_keyframe(*src_end_entry, step_objects, step_volumes, true);
}

void AssemblyStepsUtils::apply_src_frame_transforms_to_current_keyframe(KeyFrameEntry &src,
    const std::set<int> &object_filter,
    const std::set<std::pair<int, int>> &volume_filter,
    bool restrict_to_filters)
{
    if (!m_model)
        return;

    // Validate the destination: must be a non-final step's currently-selected
    const int folder = find_parent_folder(m_selected_node);
    if (folder < 0 || folder >= (int) _steps_nodes.size() || _steps_nodes[folder].is_final_assembly)
        return;
    auto *current_entries = get_current_kf_entries();
    if (!current_entries || m_keyframe_selected < 0 || m_keyframe_selected >= (int)current_entries->size())
        return;
    KeyFrameEntry &current_entry = (*current_entries)[m_keyframe_selected];

    // Patch in-place from the source keyframe and push to canvas.
    KeyFrame       &target   = current_entry.data;
    const KeyFrame &src_data = src.data;

    for (const auto &item : src_data.object_transformations)
        if (!restrict_to_filters || object_filter.count(item.first))
            target.object_transformations[item.first] = item.second;
    for (const auto &item : src_data.volume_transformations)
        if (!restrict_to_filters || volume_filter.count(item.first))
            target.volume_transformations[item.first] = item.second;
    for (const auto &item : src_data.volume_names)
        if (!restrict_to_filters || volume_filter.count(item.first))
            target.volume_names[item.first] = item.second;

    for (const auto &item : target.object_transformations)
        apply_instance_transform(item.first, item.second);
    for (const auto &item : target.volume_transformations)
        apply_volume_transform(item.first.first, item.first.second, item.second);

    current_entry.need_save         = true;
    save_assembly_steps_json_to_model();
    m_selected_screen_center_dirty_ = true;
    m_selection->mark_bounding_boxes_dirty();
    do_commond_callback("exit_gizmo");
    do_commond_callback("dirty");
    if (m_select_good_camera_layout_laber_after_auto_explode && m_guide_show_part_numbers) {
        toggle_part_number_labels_to_keyframe(src, true);
    }
}

void AssemblyStepsUtils::apply_final_assembly_end_frame_transforms_to_keyframe(KeyFrameEntry &target)
{
    if (!m_model)
        return;

    // Locate the final-assembly folder's end frame as the assembled source pose.
    const KeyFrameEntry *src_end_entry = nullptr;
    for (const auto &node : _steps_nodes) {
        if (node.type != AssemblyStepsTreeNode::Type::Folder || !node.is_final_assembly)
            continue;
        for (const auto &candidate : node.kf_data.entries) {
            if (candidate.is_last()) {
                src_end_entry = &candidate;
                break;
            }
        }
        if (src_end_entry)
            break;
    }
    if (!src_end_entry)
        return;

    // Update target keyframe data only; target is not the displayed keyframe so
    // the live canvas must stay on the current (exploded) frame.
    const KeyFrame &src = src_end_entry->data;
    KeyFrame       &dst = target.data;
    for (const auto &item : src.object_transformations)
        dst.object_transformations[item.first] = item.second;
    for (const auto &item : src.volume_transformations)
        dst.volume_transformations[item.first] = item.second;
    for (const auto &item : src.volume_names)
        dst.volume_names[item.first] = item.second;

    target.need_save = true;
    save_assembly_steps_json_to_model();
}

bool AssemblyStepsUtils::current_keyframe_matches_final_assembly_end_frame_transforms() const
{
    if (!m_model)
        return false;

    // Find the final-assembly folder + end frame entry (read-only).
    int final_folder_idx = -1;
    for (int i = 0; i < (int) _steps_nodes.size(); ++i) {
        if (_steps_nodes[i].type == AssemblyStepsTreeNode::Type::Folder && _steps_nodes[i].is_final_assembly) {
            final_folder_idx = i;
            break;
        }
    }
    if (final_folder_idx < 0)
        return false;
    const KeyFrameEntry *src_end_entry = nullptr;
    for (const auto &e : _steps_nodes[final_folder_idx].kf_data.entries) {
        if (e.is_last()) {
            src_end_entry = &e;
            break;
        }
    }
    if (!src_end_entry)
        return false;

    // Resolve the current entry without going through the non-const helper.
    const int folder = find_parent_folder(m_selected_node);
    if (folder < 0 || folder >= (int) _steps_nodes.size() || _steps_nodes[folder].is_final_assembly)
        return false;
    const auto &dst_entries = _steps_nodes[folder].kf_data.entries;
    if (m_keyframe_selected < 0 || m_keyframe_selected >= (int)dst_entries.size())
        return false;
    const KeyFrame &target = dst_entries[m_keyframe_selected].data;
    const KeyFrame &src    = src_end_entry->data;

    // Treat "matches" as: every transform recorded on the final-assembly end
    constexpr double kOffsetTolerance = 1.0;
    auto same_offset = [&](const Geometry::Transformation &a, const Geometry::Transformation &b) {
        return (a.get_offset() - b.get_offset()).norm() <= kOffsetTolerance;
    };

    for (const auto &item : src.object_transformations) {
        auto it = target.object_transformations.find(item.first);
        if (it == target.object_transformations.end())
            return false;
        if (!same_offset(it->second, item.second))
            return false;
    }
    for (const auto &item : src.volume_transformations) {
        auto it = target.volume_transformations.find(item.first);
        if (it == target.volume_transformations.end())
            return false;
        if (!same_offset(it->second, item.second))
            return false;
    }
    return true;
}

void AssemblyStepsUtils::record_current_model_as_last_final_assembly()
{
    m_last_recorded_volumes.clear();
    if (!m_model)
        return;

    for (const ModelObject *obj : m_model->objects) {
        if (!obj)
            continue;
        for (const ModelVolume *volume : obj->volumes) {
            if (volume)
                m_last_recorded_volumes.insert(volume->ensure_part_guid());
        }
    }
}

bool AssemblyStepsUtils::final_assembly_end_frame_matches_model() const
{
    if (!m_model)
        return false;
    if (m_last_recorded_volumes.empty()) {
        return false;
    }
    // Locate the final-assembly folder and its end-frame (id == 0) keyframe.
    const KeyFrameEntry *end_entry = nullptr;
    for (const auto &node : _steps_nodes) {
        if (node.type != AssemblyStepsTreeNode::Type::Folder || !node.is_final_assembly)
            continue;
        for (const auto &e : node.kf_data.entries) {
            if (e.is_last()) {
                end_entry = &e;
                break;
            }
        }
        break;
    }
    if (!end_entry)
        return false;

    // Build the expected volume key set from the live model using part GUID.
    std::set<std::string>  expected_volumes;
    for (const ModelObject *obj : m_model->objects) {
        if (!obj)
            continue;
        for (const ModelVolume *volume : obj->volumes) {
            if (volume)
                expected_volumes.insert(volume->ensure_part_guid());
        }
    }
    // Exact match in both directions: no missing and no stale keys.
    return m_last_recorded_volumes == expected_volumes;
}

bool AssemblyStepsUtils::is_mouse_over_blocking_panel() const
{
    const ImVec2 mouse_pos = ImGui::GetIO().MousePos;
    auto in_rect = [](const ImVec2 &p, const ImVec2 &mn, const ImVec2 &mx) {
        return mx.x > mn.x && mx.y > mn.y &&
               p.x >= mn.x && p.x <= mx.x &&
               p.y >= mn.y && p.y <= mx.y;
    };
    if (in_rect(mouse_pos, m_panel_rect_structure_min, m_panel_rect_structure_max) ||
        in_rect(mouse_pos, m_panel_rect_guide_min, m_panel_rect_guide_max) ||
        in_rect(mouse_pos, m_panel_rect_playbar_min, m_panel_rect_playbar_max) ||
        // Camera overlays + bottom control bar + assembly-info panel, fed each
        // frame from GLCanvas3D's overlay render functions.
        in_rect(mouse_pos, m_overlay_rect_navigator_min, m_overlay_rect_navigator_max) ||
        in_rect(mouse_pos, m_overlay_rect_fit_camera_min, m_overlay_rect_fit_camera_max) ||
        in_rect(mouse_pos, m_overlay_rect_assemble_control_min, m_overlay_rect_assemble_control_max) ||
        in_rect(mouse_pos, m_overlay_rect_return_toolbar_min, m_overlay_rect_return_toolbar_max))
        return true;

    if (Plater *plater = wxGetApp().plater()) {
        if (NotificationManager *nm = plater->get_notification_manager())
            return nm->is_point_over_any_notification(mouse_pos);
    }
    return false;
}

void AssemblyStepsUtils::track_assembly_view_export(ExportType type) const
{
    NetworkAgent *agent = GUI::wxGetApp().getAgent();
    if (!agent)
        return;

    const std::string export_type = assembly_view_export_type_name(type);
    try {
        nlohmann::json j;
        j["assembly_view_export_type"] = export_type;
        agent->track_event("assembly_view_export", j.dump());

        const std::string property_name = "assembly_view_export_" + export_type;
        std::string count;
        agent->track_get_property(property_name, count);
        int export_count = 0;
        if (!count.empty())
            export_count = std::stoi(count);
        agent->track_update_property(property_name, std::to_string(export_count + 1));
    } catch (...) {
        BOOST_LOG_TRIVIAL(warning) << "track assembly view export failed, type=" << export_type;
    }
}

void AssemblyStepsUtils::set_cursor(AssemblyNoteCursorType cursor_type)
{
    if (cursor_type != AssemblyNoteCursorType::Standard && is_mouse_over_blocking_panel()) {
        do_commond_callback("set_cursor:Standard");
        return;
    }

    switch (cursor_type) {
    case AssemblyNoteCursorType::Hand:
        do_commond_callback("set_cursor:Hand");
        break;
    case AssemblyNoteCursorType::Move:
        do_commond_callback("set_cursor:Move");
        break;
    case AssemblyNoteCursorType::ResizeNWSE:
        do_commond_callback("set_cursor:ResizeNWSE");
        break;
    case AssemblyNoteCursorType::ResizeNESW:
        do_commond_callback("set_cursor:ResizeNESW");
        break;
    case AssemblyNoteCursorType::Standard:
        do_commond_callback("set_cursor:Standard");
        break;
    }
}

void AssemblyStepsUtils::reset_cursor_if_note_cursor() {
    do_commond_callback("reset_cursor");
}

const float AssemblyStepsUtils::get_imgui_scale() const { return m_imgui_scale; }

void AssemblyStepsUtils::set_imgui_scale(float scale) {
    if (m_imgui_scale != scale) {
        m_imgui_scale = scale;
    }
}

void AssemblyStepsUtils::apply_object_state(int object_idx, const KeyframeObjectDisplayState &state)
{
    if (!m_volumes) { return; }
    for (GLVolume *vol : m_volumes->volumes) {
        if (!vol || vol->composite_id.object_id != object_idx)
            continue;
        apply_glvolume_state(vol, state);
    }
}

void AssemblyStepsUtils::apply_glvolume_state(GLVolume *vol, const KeyframeObjectDisplayState &state)
{
    if (!vol)
        return;
    vol->is_active = state.active;
    if (vol->printable) {
        vol->color[3]           = state.active ? state.alpha : GLVolume::MODEL_HIDDEN_COL[3];
        vol->render_color[3]    = vol->color[3];
        vol->force_native_color = state.force_native_color;
    } else {
        vol->render_color = GLVolume::UNPRINTABLE_COLOR;
    }
}

std::set<std::pair<int, int>> AssemblyStepsUtils::collect_folder_volume_pairs(int folder_idx) const
{
    std::set<std::pair<int, int>> out;
    if (!m_model || folder_idx < 0 || folder_idx >= (int) _steps_nodes.size())
        return out;

    const auto &folder = _steps_nodes[folder_idx];

    // Final assembly is always the whole model. A leftover / partial
    // assembly_tree_checked (List UI, sync lag after new objects) would make
    // OnlyCurrentStep hide parts and X-Ray dim them to 0.15 — wrong for Final assembly.
    if (folder.is_final_assembly) {
        for (int oi = 0; oi < (int) m_model->objects.size(); ++oi) {
            const ModelObject *obj = m_model->objects[oi];
            if (!obj)
                continue;
            for (int vi = 0; vi < (int) obj->volumes.size(); ++vi) {
                if (obj->volumes[vi])
                    out.emplace(oi, vi);
            }
        }
        return out;
    }

    const AssemblyTreeData &tree = m_model->get_assembly_tree_data();

    if (folder.assembly_tree_checked) {
        std::set<int> objects_with_vol_checks;
        for (const auto &node : tree.nodes) {
            if (node.volume_idx < 0 || node.object_idx < 0)
                continue;
            auto it = folder.assembly_tree_checked->find(node.uid);
            if (it == folder.assembly_tree_checked->end() || !it->second)
                continue;
            out.emplace(node.object_idx, node.volume_idx);
            objects_with_vol_checks.insert(node.object_idx);
        }
        for (const auto &node : tree.nodes) {
            if (node.volume_idx >= 0 || node.object_idx < 0)
                continue;
            auto it = folder.assembly_tree_checked->find(node.uid);
            if (it == folder.assembly_tree_checked->end() || !it->second)
                continue;
            if (objects_with_vol_checks.count(node.object_idx) > 0)
                continue;
            if (node.object_idx >= (int) m_model->objects.size())
                continue;
            const ModelObject *obj = m_model->objects[node.object_idx];
            if (!obj)
                continue;
            for (int vi = 0; vi < (int) obj->volumes.size(); ++vi) {
                if (obj->volumes[vi])
                    out.emplace(node.object_idx, vi);
            }
        }
        if (!out.empty())
            return out;
    }

    for (int object_idx : collect_node_object_indices(folder_idx)) {
        if (object_idx < 0 || object_idx >= (int) m_model->objects.size())
            continue;
        const ModelObject *obj = m_model->objects[object_idx];
        if (!obj)
            continue;
        for (int vi = 0; vi < (int) obj->volumes.size(); ++vi) {
            if (obj->volumes[vi])
                out.emplace(object_idx, vi);
        }
    }
    return out;
}

void AssemblyStepsUtils::look_cur_frame_logic(const KeyFrameEntry &entry)
{
    if (!entry.need_save)
        return;
    apply_keyframe_to_canvas(entry.data);
    // When the user explicitly framed this keyframe (gizmo move / re-record)
    if (!entry.data.camera_user_defined)
        fit_camera_to_current_step_main_plane(entry.data.camera_margin_factor);
    else
        // Keep the user's framing but adapt the zoom if the viewport size changed
        // since it was recorded (e.g. window resized between frame switches).
        rescale_user_camera_zoom_to_viewport(entry.data);
}

int AssemblyStepsUtils::get_object_volume_count(int object_idx)
{
    if (object_idx < 0 || object_idx >= (int) m_model->objects.size())
        return 0;
    return (int) m_model->objects[object_idx]->volumes.size();
}

std::string AssemblyStepsUtils::get_object_volume_name(int object_idx, int volume_idx)
{
    if (object_idx < 0 || object_idx >= (int) m_model->objects.size())
        return {};
    const ModelObject *obj = m_model->objects[object_idx];
    if (volume_idx < 0 || volume_idx >= (int) obj->volumes.size())
        return {};
    return obj->volumes[volume_idx]->name;
}

bool AssemblyStepsUtils::goto_global_frame(int global_idx)
{
    rebuild_play_frame_refs();
    int ref_idx = global_idx - 1;
    if (ref_idx < 0 || ref_idx >= (int)m_play_frame_refs.size())
        return false;

    const auto &ref = m_play_frame_refs[ref_idx];
    if (ref.node_idx < 0 || ref.node_idx >= (int) _steps_nodes.size())
        return false;

    select_node_and_show_volumes(ref.node_idx);

    int folder_idx = find_parent_folder(ref.node_idx);
    if (folder_idx >= 0) {
        m_selected_node = folder_idx;
        //todo scroll listview
        clear_selection();
        if (!is_play_or_export_mode())
            select_folder_volumes_on_canvas(folder_idx);
        on_selected_node_changed();
        // Apply the target keyframe LAST. The selection switch above
        auto &entries = _steps_nodes[ref.node_idx].kf_data.entries;
        if (ref.frame_idx >= 0 && ref.frame_idx < (int)entries.size()) {
            m_keyframe_selected = ref.frame_idx;
            refresh_guide_show_part_numbers_from_current();
            look_cur_frame_logic(entries[ref.frame_idx]);
        }
    }
    m_assembly_play_index = global_idx;
    do_commond_callback("dirty");
    do_commond_callback("request_extra_frame");
    return true;
}

bool AssemblyStepsUtils::seek_global_frame_from_mouse_x(float mouse_x, float progress_x0, float progress_w, int total_frames)
{
    if (total_frames <= 0)
        return false;
    float t = (progress_w > 0.0f) ? ((mouse_x - progress_x0) / progress_w) : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    int target_frame = 1 + static_cast<int>(std::round(t * static_cast<float>(total_frames - 1)));
    target_frame = std::clamp(target_frame, 1, total_frames);
    // Skip redundant work while dragging: only seek when crossing into a new frame.
    if (target_frame == m_assembly_play_index)
        return false;
    const bool ok = goto_global_frame(target_frame);
    if (ok) {
        // A manual seek redefines the playback position. Drop any transient
        m_play_queue.clear();
        m_pending_global_frame_index = -1;
        m_play_different_folder_waiting = false;
        m_play_different_folder_phase = 0;
        m_play_end_waiting = false;
        m_render_interpolated_part_number_labels = false;
    }
    return ok;
}

void AssemblyStepsUtils::pause_global_frame()
{
    m_keyframe_playing = false;
    clear_playback_pause_state();
    clear_global_playback_state();
}

void AssemblyStepsUtils::clear_playback_pause_state()
{
    m_playback_paused = false;
    m_playback_pause_started_at = 0.0;
}

void AssemblyStepsUtils::clear_global_playback_state()
{
    m_play_global = false;
    m_play_different_folder_waiting = false;
    m_play_different_folder_phase = 0;
    m_play_end_waiting = false;
    m_show_video_title_mode = false;
    m_video_intro_active = false;
    m_video_intro_phase = 0;
    m_video_intro_start_time = 0.0;
    m_video_intro_cover_duration = VIDEO_INTRO_COVER_DURATION;
    m_video_intro_step_duration = VIDEO_INTRO_STEP_DURATION;
    m_render_interpolated_part_number_labels = false;
    m_pending_global_frame_index = -1;
    m_play_transition_duration = m_play_transition_expect_duration;
    m_play_interval_step_to_step = m_play_interval_step_to_step_expect;
}

void AssemblyStepsUtils::exit_title_mode_if_paused()
{
    if (m_playback_paused && is_show_video_title_mode()) {
        pause_global_frame();
    }
}

void AssemblyStepsUtils::pause_playback()
{
    if (!m_keyframe_playing)
        return;
    m_keyframe_playing = false;
    m_playback_paused = true;
    m_playback_pause_started_at = assembly_now_seconds();
    do_commond_callback("request_extra_frame");
}

void AssemblyStepsUtils::resume_playback()
{
    if (!m_playback_paused)
        return;

    const double now = assembly_now_seconds();
    const double paused_duration = std::max(0.0, now - m_playback_pause_started_at);
    if (m_video_intro_active && m_video_intro_start_time >= 0.0)
        m_video_intro_start_time += paused_duration;
    if (m_play_different_folder_waiting && m_play_different_folder_start_time >= 0.0)
        m_play_different_folder_start_time += paused_duration;
    if (m_play_end_waiting)
        m_play_end_start_time += paused_duration;

    clear_playback_pause_state();
    m_keyframe_playing = true;
    if (m_video_intro_active || m_show_video_title_mode || m_pending_global_frame_index > 0)
        m_play_global = true;
    do_commond_callback("request_extra_frame");
}

void AssemblyStepsUtils::play_different_folder_logic()
{
    if (!m_play_different_folder_waiting) {
        m_play_different_folder_waiting = true;
        m_play_different_folder_start_time = assembly_now_seconds();
        m_play_different_folder_phase = 0;
        m_show_video_title_mode = false;
    }

    do_commond_callback("dirty");
    do_commond_callback("request_extra_frame");

    const double elapsed = assembly_now_seconds() - m_play_different_folder_start_time;
    if (m_play_different_folder_phase == 0 && elapsed >= m_play_interval_step_to_step) {
        m_play_different_folder_phase = 1;
        m_show_video_title_mode = true;
        m_play_different_folder_start_time = -1.0;
        return;
    }

    if (m_play_different_folder_phase == 1 && m_play_different_folder_start_time < 0.0) {
        m_play_different_folder_start_time = assembly_now_seconds();
        return;
    }

    if (m_play_different_folder_phase == 1 && elapsed >= m_video_intro_step_duration) {
        m_play_different_folder_waiting = false;
        m_play_different_folder_phase = 0;
        m_show_video_title_mode = false;
        if (m_pending_global_frame_index > 0) {
            goto_global_frame(m_pending_global_frame_index);
            m_pending_global_frame_index = -1;
        }
    }
}

void AssemblyStepsUtils::begin_video_intro()
{
    // Single entry point shared by the playback preview and the MP4 export.
    m_video_intro_active         = true;
    m_video_intro_phase          = 0;
    m_video_intro_start_time     = -1.0;
    m_video_intro_cover_duration = VIDEO_INTRO_COVER_DURATION;
    m_video_intro_step_duration  = VIDEO_INTRO_STEP_DURATION;
    m_show_video_title_mode      = true;
}

void AssemblyStepsUtils::play_video_intro_logic()
{
    if (!m_video_intro_active)
        return;

    if (m_playback_paused)
        return;

    do_commond_callback("dirty");
    do_commond_callback("request_extra_frame");

    if (m_video_intro_phase == 0) {
        if (m_video_intro_start_time < 0.0) {
            m_video_intro_start_time = assembly_now_seconds();
            return;
        }

        const double elapsed = assembly_now_seconds() - m_video_intro_start_time;
        // Phase 0: hold the cover title until m_video_intro_cover_duration
        // has elapsed. The actual text is rendered by render_main below.
        if (elapsed >= m_video_intro_cover_duration) {
            m_video_intro_phase      = 1;
            m_video_intro_start_time = -1.0;
            m_show_video_title_mode  = true; // still showing an overlay
        }
        return;
    }

    if (m_video_intro_phase == 1) {
        if (m_video_intro_start_time < 0.0) {
            m_video_intro_start_time = assembly_now_seconds();
            return;
        }

        const double elapsed = assembly_now_seconds() - m_video_intro_start_time;
        // Phase 1: hold Step 1's name. When done, turn the overlay off and
        // hand control back to normal playback at the very first frame.
        if (elapsed >= m_video_intro_step_duration) {
            m_video_intro_active    = false;
            m_video_intro_phase     = 0;
            m_show_video_title_mode = false;
            // Make sure normal playback begins at frame 1; a cancelled /
            // restarted run might have moved the index.
            if (m_assembly_play_index != 1)
                goto_global_frame(1);
        }
        return;
    }
}

void AssemblyStepsUtils::play_global_frame(bool from_btn_click)
{
    clear_playback_pause_state();
    rebuild_play_frame_refs();
    m_assembly_play_count = (int)m_play_frame_refs.size();
    if (from_btn_click && m_assembly_play_index >= m_assembly_play_count) {
        // Reached the end restart from the beginning for another loop.
        m_assembly_play_index = 1;
        goto_global_frame(1);
        do_commond_callback("request_extra_frame");
    }
    if (m_assembly_play_count <= 0) {
        pause_global_frame();
        return;
    }

    if (!m_play_global || !m_keyframe_playing) {
        m_play_global = true;
        m_keyframe_playing = true;
        do_commond_callback("request_extra_frame");
        return;
    }

    // Intro overlay (cover title, then Step 1 title) takes priority over the
    // normal playback / between-step-pause state machines. While it is active
    // we just keep ticking the intro timeline; nothing else advances.
    if (m_video_intro_active) {
        play_video_intro_logic();
        return;
    }

    if (m_play_different_folder_waiting) {
        play_different_folder_logic();
        return;
    }

    if (!m_play_queue.empty()) {
        consume_play_queue_frame(false);
        if (m_play_queue.empty() && m_pending_global_frame_index > 0) {
            m_assembly_play_index = m_pending_global_frame_index;
            m_pending_global_frame_index = -1;
        }
        do_commond_callback("request_extra_frame");
        return;
    }

    if (m_assembly_play_index >= m_assembly_play_count) {
        if (!m_play_end_waiting) {
            m_play_end_waiting = true;
            m_play_end_start_time = assembly_now_seconds();
            do_commond_callback("request_extra_frame");
            return;
        }
        const double elapsed = assembly_now_seconds() - m_play_end_start_time;
        if (elapsed < m_play_interval_step_to_step) {
            do_commond_callback("request_extra_frame");
            return;
        }
        m_play_end_waiting = false;
        pause_global_frame();
        return;
    }

    int cur_idx = m_assembly_play_index;
    int next_idx = m_assembly_play_index + 1;
    int cur_ref_idx = cur_idx - 1;
    int next_ref_idx = next_idx - 1;
    if (cur_ref_idx < 0 || next_ref_idx < 0 ||
        cur_ref_idx >= (int)m_play_frame_refs.size() || next_ref_idx >= (int)m_play_frame_refs.size()) {
        pause_global_frame();
        return;
    }

    int folder_cur = find_parent_folder(m_play_frame_refs[cur_ref_idx].node_idx);
    int folder_next = find_parent_folder(m_play_frame_refs[next_ref_idx].node_idx);
    if (folder_cur == folder_next) {
        if (goto_global_frame(cur_idx)) {
            build_local_play_queue();
            m_pending_global_frame_index = next_idx;
            if (!m_play_queue.empty())
                consume_play_queue_frame(false);
        }
    } else {
        m_pending_global_frame_index = next_idx;
        play_different_folder_logic();
    }
    do_commond_callback("request_extra_frame");
    clear_selection();
}

bool AssemblyStepsUtils::prepare_global_playback_with_intro(bool export_mode)
{
    clear_playback_pause_state();
    rebuild_play_frame_refs();
    m_assembly_play_count = (int)m_play_frame_refs.size();
    if (m_assembly_play_count <= 0)
        return false;

    m_is_export_mode = export_mode;
    if (export_mode) {
        if (auto *plater = wxGetApp().plater())
            plater->get_notification_manager()->close_assembly_info_notification();
    }

    m_assembly_play_index = 1;
    goto_global_frame(1);

    begin_video_intro();
    m_play_global                = true;
    m_keyframe_playing             = true;

    return true;
}

void AssemblyStepsUtils::start_playback_with_intro()
{
    if (!prepare_global_playback_with_intro(false))
        return;

    do_commond_callback("dirty");
    do_commond_callback("request_extra_frame");
    play_global_frame();
}

void AssemblyStepsUtils::prepare_export_to_play_global_frame()
{
    m_is_export_mode = true;
    if (auto *plater = wxGetApp().plater())
        plater->get_notification_manager()->close_assembly_info_notification();
    goto_global_frame(1);
    do_commond_callback("request_extra_frame");
}

void AssemblyStepsUtils::auto_explode_current_keyframe()
{
    if (!m_model || !m_volumes)
        return;
    auto *entries = get_current_kf_entries();
    if (!entries || m_keyframe_selected < 0 || m_keyframe_selected >= static_cast<int>(entries->size()))
        return;

    const int folder = find_parent_folder(m_selected_node);
    if (folder < 0 || folder >= static_cast<int>(_steps_nodes.size()))
        return;
    const bool     final_assembly = _steps_nodes[folder].is_final_assembly;
    KeyFrameEntry &entry = (*entries)[m_keyframe_selected];
    if (final_assembly && entry.is_last())
        return;

    //if (m_select_good_camera_layout_laber_after_auto_explode && m_guide_show_part_numbers) {//todo
    //    toggle_part_number_labels(); // auto_explode_current_keyframe
    //    //  The exploded frame is a mid-step (non-end) frame. Keep this node's own end
    //    if (!entry.is_last()) {
    //        for (auto &node_entry : *entries) {
    //            if (node_entry.is_last()) {
    //                if (final_assembly) { // todo
    //                    // toggle_part_number_labels_to_keyframe(node_entry);
    //                } else {
    //                    apply_final_assembly_end_frame_transforms_to_keyframe(node_entry);
    //                }
    //                break;
    //            }
    //        }
    //    }
    //}
    const KeyFrameEntry *base_end_entry = nullptr;
    for (const auto &node : _steps_nodes) {
        if (node.type != AssemblyStepsTreeNode::Type::Folder || !node.is_final_assembly)
            continue;
        for (const auto &candidate : node.kf_data.entries) {
            if (candidate.is_last()) {
                base_end_entry = &candidate;
                break;
            }
        }
        if (base_end_entry)
            break;
    }
    if (!base_end_entry)
        return;
    const KeyFrame &base_frame = base_end_entry->data;

    std::set<int> step_objects;
    if (!final_assembly)
        step_objects = collect_node_object_indices(folder);

    struct ExplodeItem {
        int object_idx{-1};
        int volume_idx{-1};
        bool object_mode{false};
        BoundingBoxf3 bbox;
        Geometry::Transformation transform;
        int dir{0};
        double dist{0.0};
        Vec3d offset{Vec3d::Zero()};
    };

    auto merge_volume_bbox = [&](int object_idx, int volume_idx, bool object_mode,
                                 BoundingBoxf3 &out, Geometry::Transformation &transform) {
        bool found = false;
        auto object_transform = [&](int oi) {
            auto it = base_frame.object_transformations.find(oi);
            return it != base_frame.object_transformations.end() ? it->second : get_instance_transform(oi);
        };
        auto volume_transform = [&](int oi, int vi) {
            auto it = base_frame.volume_transformations.find({oi, vi});
            return it != base_frame.volume_transformations.end() ? it->second : get_volume_transform(oi, vi);
        };
        const Geometry::Transformation base_object_transform = object_transform(object_idx);
        for (GLVolume *vol : m_volumes->volumes) {
            if (!vol || !vol->is_active)
                continue;
            if (vol->object_idx() != object_idx)
                continue;
            if (!object_mode && vol->volume_idx() != volume_idx)
                continue;
            const Geometry::Transformation base_volume_transform = volume_transform(object_idx, vol->volume_idx());
            const Transform3d base_matrix =
                base_object_transform.get_matrix() * base_volume_transform.get_matrix();
            const BoundingBoxf3 base_bbox = vol->bounding_box().transformed(base_matrix);
            if (!found) {
                out = base_bbox;
                transform = object_mode ? base_object_transform : base_volume_transform;
                found = true;
            } else {
                out.merge(base_bbox);
            }
        }
        return found && out.defined;
    };

    std::vector<ExplodeItem> items;
    if (final_assembly) {
        for (int oi = 0; oi < static_cast<int>(m_model->objects.size()); ++oi) {
            if (!m_model->objects[oi])
                continue;
            BoundingBoxf3 bbox;
            Geometry::Transformation transform;
            if (!merge_volume_bbox(oi, -1, true, bbox, transform))
                continue;
            ExplodeItem item;
            item.object_idx = oi;
            item.object_mode = true;
            item.bbox = bbox;
            item.transform = transform;
            items.push_back(std::move(item));
        }
    } else {
        // When the same model object already appeared in an earlier step,
        const bool collapse_repeated_objects = m_show_modelobject_name_when_modelobject_has_occur_before;
        bool       as_whole_object           = m_show_modelobject_name_when_modelobject_has_occur_before && (static_cast<int>(entries->size()) >= 2 && entry.is_last());
        for (int oi : step_objects) {
            if (oi < 0 || oi >= static_cast<int>(m_model->objects.size()))
                continue;
            const ModelObject *obj = m_model->objects[oi];
            if (!obj)
                continue;
            if (!as_whole_object) {
                as_whole_object = collapse_repeated_objects &&
                                  ((final_assembly && is_object_used_in_previous_steps(oi, folder)) ||
                                   (!final_assembly && is_object_used_in_current_step(oi, folder, entry.data.id)));
            }
            if (as_whole_object) {
                BoundingBoxf3 bbox;
                Geometry::Transformation transform;
                if (!merge_volume_bbox(oi, -1, true, bbox, transform))
                    continue;
                ExplodeItem item;
                item.object_idx  = oi;
                item.object_mode = true;
                item.bbox        = bbox;
                item.transform   = transform;
                items.push_back(std::move(item));

                m_explode_collapsed_note_until  = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                continue;
            }
            for (int vi = 0; vi < static_cast<int>(obj->volumes.size()); ++vi) {
                BoundingBoxf3 bbox;
                Geometry::Transformation transform;
                if (!merge_volume_bbox(oi, vi, false, bbox, transform))
                    continue;
                ExplodeItem item;
                item.object_idx = oi;
                item.volume_idx = vi;
                item.bbox = bbox;
                item.transform = transform;
                items.push_back(std::move(item));
            }
        }
    }
    if (items.size() < 1)
        return;

    BoundingBoxf3 overall;
    for (const ExplodeItem &item : items)
        overall.merge(item.bbox);
    if (!overall.defined)
        return;

    const Vec3d center = overall.center();
    auto axis_abs = [](const Vec3d &v, int axis) { return std::abs(v(axis)); };
    for (ExplodeItem &item : items) {
        const Vec3d delta = item.bbox.center() - center;
        int axis = 0;
        if (axis_abs(delta, 1) > axis_abs(delta, axis))
            axis = 1;
        if (axis_abs(delta, 2) > axis_abs(delta, axis))
            axis = 2;
        const bool positive = delta(axis) >= 0.0;
        item.dir = axis * 2 + (positive ? 0 : 1);
        item.dist = std::abs(delta(axis));
    }

    std::array<std::vector<int>, 6> dir_items;
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
        dir_items[items[i].dir].push_back(i);
    for (auto &indices : dir_items) {
        std::sort(indices.begin(), indices.end(), [&](int lhs, int rhs) {
            return items[lhs].dist < items[rhs].dist;
        });
    }

    const double gap = 5.0;
    std::array<double, 6> boundary = {
        overall.max.x(), overall.min.x(),
        overall.max.y(), overall.min.y(),
        overall.max.z(), overall.min.z()
    };
    for (int dir = 0; dir < 6; ++dir) {
        const int axis = dir / 2;
        const bool positive = (dir % 2) == 0;
        for (int item_idx : dir_items[dir]) {
            ExplodeItem &item = items[item_idx];
            double move = 0.0;
            if (positive) {
                move = boundary[dir] + gap - item.bbox.min(axis);
                boundary[dir] = item.bbox.max(axis) + move;
            } else {
                move = boundary[dir] - gap - item.bbox.max(axis);
                boundary[dir] = item.bbox.min(axis) + move;
            }
            item.offset(axis) = move;
        }
    }

    KeyFrame &target = entry.data;
    for (const ExplodeItem &item : items) {
        Geometry::Transformation transform = item.transform;
        transform.set_offset(transform.get_offset() + item.offset);
        if (item.object_mode) {
            target.object_transformations[item.object_idx] = transform;
            apply_instance_transform(item.object_idx, transform);
        } else {
            const std::pair<int, int> key{item.object_idx, item.volume_idx};
            target.volume_transformations[key] = transform;
            if (item.object_idx >= 0 && item.object_idx < static_cast<int>(m_model->objects.size())) {
                const ModelObject *obj = m_model->objects[item.object_idx];
                if (obj && item.volume_idx >= 0 && item.volume_idx < static_cast<int>(obj->volumes.size())) {
                    const ModelVolume *mv = obj->volumes[item.volume_idx];
                    target.volume_names[key] = (mv && !mv->name.empty()) ? mv->name : obj->name;
                }
            }
            apply_volume_transform(item.object_idx, item.volume_idx, transform);
        }
    }

    entry.need_save = true;
    save_assembly_steps_json_to_model();
    m_selected_screen_center_dirty_ = true;
    if (m_selection)
        m_selection->mark_bounding_boxes_dirty();
    if (m_select_good_camera_layout_laber_after_auto_explode && m_guide_show_part_numbers) {
        toggle_part_number_labels();
    }
    do_commond_callback("exit_gizmo");
    do_commond_callback("dirty");
    do_commond_callback("request_extra_frame");
}

void AssemblyStepsUtils::on_export(ExportType type)
{
    auto path = generate_output_path(type);
    if (!path.empty()) {
        // Block the export when the chosen file (PDF / MD / MP4) is held open by
        // another process; writing would otherwise fail mid-way.
        if (is_export_target_locked(path))
            return;
        if (ExportType::PDF == type) {
            on_export_pdf(path);
        } else if (ExportType::MarkDown == type) {
            on_export_markdown(path);
        } else {
            on_export_mp4(path);
        }
    }
}

bool AssemblyStepsUtils::is_export_target_locked(const std::string &path)
{
    namespace fs = boost::filesystem;
    if (path.empty())
        return false;

    boost::system::error_code ec;
    if (!fs::exists(fs::path(path), ec) || ec)
        return false;

    bool locked = false;
    if (FILE *fp = boost::nowide::fopen(path.c_str(), "ab")) {
        std::fclose(fp);
    } else {
        locked = true;
    }

    if (locked) {
        MessageDialog msg_dlg(nullptr,
            _L("The export file is in use by the system. Please close it or export with a different file name, then try again."),
            _L("Export"),
            wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
    }
    return locked;
}


std::string AssemblyStepsUtils::generate_output_path(ExportType type)
{
    namespace fs = boost::filesystem;

    // Build a sane default file name from the current project + the localized
    // "Assembly Guide" tag, mirroring the previous on_export_pdf hard-coded
    // naming. filter_characters() strips characters illegal in Windows paths.
    std::string project_name = "Untitled";
    if (wxGetApp().plater()) {
        project_name = filter_characters(wxGetApp().plater()->get_project_name().ToUTF8().data(),
                                         "<>[]:/\\|?*\"");
        if (project_name.empty())
            project_name = "Untitled";
    }
    std::string steps_name = filter_characters(_L("Assembly Guide").ToUTF8().data(),
                                               "<>[]:/\\|?*\"");
    if (steps_name.empty())
        steps_name = "Assembly Guide";

    const bool is_pdf      = (type == ExportType::PDF);
    const bool is_markdown = (type == ExportType::MarkDown);
    const std::string dot_ext = is_pdf ? ".pdf" : (is_markdown ? ".md" : ".mp4");
    const wxString wildcard = is_pdf
        ? wxString::FromUTF8((_u8L("PDF files") + " (*.pdf)|*.pdf").c_str())
        : (is_markdown
            ? wxString::FromUTF8((_u8L("Markdown files") + " (*.md)|*.md").c_str())
            : wxString::FromUTF8((_u8L("MP4 files") + " (*.mp4)|*.mp4").c_str()));
    const wxString dlg_title = is_pdf
        ? _L("Export Assembly Guide to PDF")
        : (is_markdown
            ? _L("Export Assembly Guide to Markdown")
            : _L("Export Assembly Guide to MP4"));
    const wxString default_file = wxString::FromUTF8((project_name + "_" + steps_name + dot_ext).c_str());

    wxWindow *parent = nullptr;
    if (wxGetApp().plater())
        parent = dynamic_cast<wxWindow *>(wxGetApp().plater());

    wxFileDialog dlg(parent, dlg_title, /*default_dir*/ wxEmptyString, default_file,
                     wildcard, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK)
        return std::string();

    std::string out_path = dlg.GetPath().ToUTF8().data();
    if (!boost::algorithm::iends_with(out_path, dot_ext))
        out_path += dot_ext;
    out_path = sanitize_export_output_path(out_path, project_name + "_" + steps_name);
    return out_path;
}

wxWindow* AssemblyStepsUtils::assembly_export_progress_anchor() const
{
    if (wxGetApp().plater()) {
        if (GLCanvas3D *canvas = wxGetApp().plater()->get_assmeble_canvas3D())
            return canvas->get_wxglcanvas();
    }
    return wxGetApp().mainframe;
}

void AssemblyStepsUtils::show_assembly_export_progress(ExportType type, const std::string &path, int value, int maximum)
{
    update_assembly_export_progress(type, path, value, maximum);
}

void AssemblyStepsUtils::update_assembly_export_progress(ExportType type, const std::string &path, int value, int maximum)
{
    wxWindow *anchor = assembly_export_progress_anchor();
    if (!anchor)
        return;

    if (!m_export_progress_window)
        m_export_progress_window = std::make_unique<AssemblyExportProgressWindow>(wxGetApp().mainframe);

    const wxString filename = path.empty()
        ? wxString()
        : from_path(boost::filesystem::path(path).filename());
    wxString message = _L("Exporting file");
    if (!filename.empty())
        message += ": " + filename;

    m_export_progress_window->update_progress(message, value, maximum, anchor);
}

void AssemblyStepsUtils::hide_assembly_export_progress()
{
    if (m_export_progress_window)
        m_export_progress_window->Hide();
}

void AssemblyStepsUtils::on_export_pdf(std::string path)
{
    namespace fs = boost::filesystem;
    if (m_steps_export_active || m_steps_video_export_active ||
        m_camera == nullptr || m_model == nullptr)
        return;


    rebuild_play_frame_refs();
    const int total = (int)m_play_frame_refs.size();
    if (total <= 0) {
        BOOST_LOG_TRIVIAL(info) << "assembly steps export: no playable frames, abort";
        return;
    }

    if (path.empty()) {
        BOOST_LOG_TRIVIAL(info) << "assembly steps export: empty output path, abort";
        return;
    }

    // `path` is the full target PDF path picked via wxFileDialog. Intermediate
    // PNG snapshots are kept under the app cache so user folders are not polluted.
    const fs::path     pdf_path(path);
    const std::string  out_dir = pdf_path.parent_path().string();
    if (!out_dir.empty() && !fs::exists(out_dir))
        fs::create_directories(out_dir);

    const fs::path cache_dir = fs::path(Slic3r::data_dir()) / "cache" / "assembly_steps";
    boost::system::error_code ec;
    if (fs::exists(cache_dir, ec)) {
        fs::directory_iterator end;
        for (fs::directory_iterator it(cache_dir, ec); !ec && it != end; it.increment(ec))
            fs::remove_all(it->path(), ec);
    }
    fs::create_directories(cache_dir, ec);
    if (ec) {
        BOOST_LOG_TRIVIAL(error) << "assembly steps export: failed to prepare cache dir "
                                 << cache_dir.string() << ", error=" << ec.message();
        return;
    }

    m_steps_export_total      = total;
    m_steps_export_images.clear();
    m_steps_export_titles.clear();
    m_steps_export_step_indices.clear();
    m_steps_export_dir          = cache_dir.string();
    m_steps_export_output_path  = path;
    m_steps_export_type         = ExportType::PDF;

    m_steps_export_original_play_index    = m_assembly_play_index;
    m_steps_export_original_selected_node = m_selected_node;

    prepare_export_to_play_global_frame();
    m_steps_export_active     = true;
    m_steps_export_wait_frame = true;
    show_assembly_export_progress(m_steps_export_type, m_steps_export_output_path, 0, m_steps_export_total);
    do_commond_callback("request_extra_frame");

    BOOST_LOG_TRIVIAL(info) << "assembly steps export: start -> " << m_steps_export_output_path
                            << ", total=" << total;
}

void AssemblyStepsUtils::on_export_markdown(std::string path)
{
    namespace fs = boost::filesystem;
    if (m_steps_export_active || m_steps_video_export_active ||
        m_camera == nullptr || m_model == nullptr)
        return;

    rebuild_play_frame_refs();
    const int total = (int)m_play_frame_refs.size();
    if (total <= 0) {
        BOOST_LOG_TRIVIAL(info) << "assembly steps markdown export: no playable frames, abort";
        return;
    }

    if (path.empty()) {
        BOOST_LOG_TRIVIAL(info) << "assembly steps markdown export: empty output path, abort";
        return;
    }

    const fs::path     md_path(path);
    const std::string  out_dir = md_path.parent_path().string();
    if (!out_dir.empty() && !fs::exists(out_dir))
        fs::create_directories(out_dir);

    const fs::path cache_dir = fs::path(Slic3r::data_dir()) / "cache" / "assembly_steps";
    boost::system::error_code ec;
    if (fs::exists(cache_dir, ec)) {
        fs::directory_iterator end;
        for (fs::directory_iterator it(cache_dir, ec); !ec && it != end; it.increment(ec))
            fs::remove_all(it->path(), ec);
    }
    fs::create_directories(cache_dir, ec);
    if (ec) {
        BOOST_LOG_TRIVIAL(error) << "assembly steps markdown export: failed to prepare cache dir "
                                 << cache_dir.string() << ", error=" << ec.message();
        return;
    }

    m_steps_export_total      = total;
    m_steps_export_images.clear();
    m_steps_export_titles.clear();
    m_steps_export_step_indices.clear();
    m_steps_export_dir          = cache_dir.string();
    m_steps_export_output_path  = path;
    m_steps_export_type         = ExportType::MarkDown;

    m_steps_export_original_play_index    = m_assembly_play_index;
    m_steps_export_original_selected_node = m_selected_node;

    prepare_export_to_play_global_frame();
    m_steps_export_active     = true;
    m_steps_export_wait_frame = true;
    show_assembly_export_progress(m_steps_export_type, m_steps_export_output_path, 0, m_steps_export_total);
    do_commond_callback("request_extra_frame");

    BOOST_LOG_TRIVIAL(info) << "assembly steps markdown export: start -> " << m_steps_export_output_path
                            << ", total=" << total;
}

void AssemblyStepsUtils::on_export_mp4(std::string path)
{
    namespace fs = boost::filesystem;
    if (m_steps_video_export_active || m_steps_export_active ||
        m_camera == nullptr || m_model == nullptr || path.empty()) {
        return;
    }

    rebuild_play_frame_refs();
    const int total = static_cast<int>(m_play_frame_refs.size());
    if (total <= 0) {
        BOOST_LOG_TRIVIAL(info) << "assembly steps video export: no playable frames, abort";
        return;
    }

    if (!m_mp4_recorder)
        m_mp4_recorder = std::make_unique<Mp4Recorder>();
    if (m_mp4_recorder->is_recording()) {
        m_mp4_recorder->stop();
        m_pbo_reader.reset();
    }

    const std::array<int, 4> &vp = m_camera->get_viewport();
    const int w = vp[2];
    const int h = vp[3];
    if (w <= 0 || h <= 0)
        return;

    boost::system::error_code ec;
    const fs::path output_path(path);
    const fs::path output_dir = output_path.parent_path();
    if (!output_dir.empty() && !fs::exists(output_dir, ec))
        fs::create_directories(output_dir, ec);
    if (ec) {
        BOOST_LOG_TRIVIAL(error) << "assembly steps video export: failed to prepare output dir "
                                 << output_dir.string() << ", error=" << ec.message();
        return;
    }

    // Order matters here. The MP4 capture state machine in
    // process_assembly_steps_video_export() pushes a frame the moment
    // m_steps_video_export_active becomes true, so we have to:
    //   1. Move the scene to frame 1 (prepare_*) and set up the intro overlay
    //      flags FIRST. Any render kicked off during these steps would still
    //      be ignored by the capture pipeline because the active flag below
    //      is not set yet.
    //   2. Open the encoder.
    //   3. ONLY THEN flip m_steps_video_export_active = true and request an
    //      extra frame so the very first captured frame already carries the
    //      cover-title overlay (otherwise the user sees the raw assembly
    //      view as the video's first frame).
    if (!prepare_global_playback_with_intro(true))
        return;

    // Phase 0 shows the cover title (m_pdf_export_title or project name),
    // phase 1 shows Step 1's name, then normal frame playback starts. Durations
    // and playback state are initialized by prepare_global_playback_with_intro()
    // so MP4 export and live preview share the same playback path.

    const int fps = 30;
    if (!m_mp4_recorder->start(static_cast<uint32_t>(w), static_cast<uint32_t>(h), fps, path)) {
        BOOST_LOG_TRIVIAL(error) << "assembly steps video export: failed to start recording -> " << path;
        // Roll back the intro / playback flags we just set so we don't leave
        // the assembly view in a half-recording state.
        m_video_intro_active    = false;
        m_show_video_title_mode = false;
        m_play_global           = false;
        m_keyframe_playing        = false;
        m_is_export_mode        = false;
        return;
    }

    m_video_recording           = true;
    m_steps_video_export_path   = path;
    // Drop the first capture frame: the overlay flags above only apply to
    // future paints, but a paint scheduled before this point may already be
    // queued with the pre-export framebuffer. Skipping one capture cycle
    // guarantees the very first frame written into the MP4 was rendered AFTER
    // m_show_video_title_mode / m_video_intro_active were live.
    m_video_export_skip_first_frame = true;
    m_steps_video_export_active     = true;
    show_assembly_export_progress(ExportType::MP4, m_steps_video_export_path, 0, total);

    // Force one repaint so the first frame the recorder ever sees is the
    // freshly-rendered cover-title overlay, not whatever was on screen before.
    do_commond_callback("dirty");
    do_commond_callback("request_extra_frame");

    play_global_frame();

    BOOST_LOG_TRIVIAL(info) << "assembly steps video export: start -> " << path
                            << ", total=" << total;
}

void AssemblyStepsUtils::process_assembly_steps_export()
{
    if (!m_steps_export_active)
        return;

    if (m_steps_export_wait_frame) {
        m_steps_export_wait_frame = false;
        // If all frames have already been captured, finalize now.
        if ((int)m_steps_export_images.size() >= m_steps_export_total) {
            finalize_steps_export();
            return;
        }
        do_commond_callback("request_extra_frame");
        return;
    }

    namespace fs = boost::filesystem;
    const int cur = m_assembly_play_index;
    if (cur < 1 || cur > m_steps_export_total) {
        // Defensive fallback: normal flow finalizes via the wait_frame branch
        // above, so reaching here means m_assembly_play_index was nudged out of
        // range while an export was active. Bail out cleanly so we don't hang.
        BOOST_LOG_TRIVIAL(warning) << "assembly steps export: unexpected play index " << cur << " (total=" << m_steps_export_total << "), force finalize";
        hide_assembly_export_progress();
        return;
    }

    const std::string png_filename = (fs::path(m_steps_export_dir) / (std::string("assembly_step_") + std::to_string(cur) + ".png")).string();

    if (capture_assembly_screenshot_to_png(png_filename)) {
        m_steps_export_images.push_back(png_filename);

        std::string title;
        int step_index = 0;
        const int ref_idx = cur - 1;
        if (ref_idx >= 0 && ref_idx < (int)m_play_frame_refs.size()) {
            const int node_idx   = m_play_frame_refs[ref_idx].node_idx;
            const int folder_idx = find_parent_folder(node_idx);
            if (folder_idx >= 0 && folder_idx < (int) _steps_nodes.size())
                title = assembly_step_display_name(_steps_nodes[folder_idx]);
            // Determine which step (1-based) this folder belongs to.
            int s = 0;
            int prev_folder = -1;
            for (int k = 0; k <= ref_idx; ++k) {
                int fi = find_parent_folder(m_play_frame_refs[k].node_idx);
                if (fi != prev_folder) { ++s; prev_folder = fi; }
            }
            step_index = s;
        }
        if (title.empty())
            title = std::string("Step ") + std::to_string(cur);
        m_steps_export_titles.push_back(std::move(title));
        m_steps_export_step_indices.push_back(step_index);
        update_assembly_export_progress(m_steps_export_type, m_steps_export_output_path, (int)m_steps_export_images.size(), m_steps_export_total);
    } else {
        BOOST_LOG_TRIVIAL(warning) << "assembly steps export: screenshot failed for play index " << cur;
    }

    if (cur >= m_steps_export_total) {
        // All frames captured; wait one more render pass to ensure the last
        // screenshot is fully flushed before building the PDF. Without
        // request_extra_frame the canvas would only repaint on stray UI events
        // (mouse move, focus change, ...) and finalize would lag noticeably.
        m_steps_export_wait_frame = true;
        do_commond_callback("request_extra_frame");
        return;
    }

    // goto_global_frame() updates m_selected_node / keyframe / m_assembly_play_index
    // and triggers a render-extra-frame request internally.
    goto_global_frame(cur + 1);
}

void AssemblyStepsUtils::finalize_steps_export()
{
    update_assembly_export_progress(m_steps_export_type, m_steps_export_output_path, m_steps_export_total, m_steps_export_total);
    wxBusyCursor busy;
    if (m_steps_export_type == ExportType::MarkDown) {
        auto project_name = []() -> std::string {
            if (wxGetApp().plater()) {
                std::string name = wxGetApp().plater()->get_project_name().ToUTF8().data();
                if (!name.empty())
                    return name;
            }
            return "Untitled";
        };

        AssemblyMarkdownExportParams params;
        params.md_filename              = m_steps_export_output_path;
        params.project_title            = m_pdf_export_title.empty() ? project_name() : m_pdf_export_title;
        params.subtitle                 = std::string(" ") + "----" + _u8L("Assembly Guide");
        params.cover_image_path         = m_pdf_export_cover_image_path;
        params.second_page_image_path   = m_pdf_export_second_page_image_path;
        params.frame_images             = m_steps_export_images;
        params.page_titles              = m_steps_export_titles;
        params.step_indices             = m_steps_export_step_indices;
        params.step_label_prefix        = _u8L("Step");
        TinyExportMardDown::build(params);
    } else {
        build_assembly_steps_pdf(m_steps_export_output_path, m_steps_export_images, m_steps_export_titles, m_steps_export_step_indices);
    }

    // Restore play index (and its folder selection / keyframe view) first, then
    // override the camera with the user's pre-export view so a manually rotated
    // camera survives the export.
    const int orig = m_steps_export_original_play_index;
    if (orig >= 1 && orig <= m_steps_export_total) {
        goto_global_frame(orig);
    } else {
        m_selected_node     = m_steps_export_original_selected_node;
        m_keyframe_selected = -1;
        on_selected_node_changed();
    }

    m_steps_export_active     = false;
    m_steps_export_wait_frame = false;
    m_steps_export_total      = 0;
    m_is_export_mode          = false;

    save_existing_project_if_dirty();
    hide_assembly_export_progress();

    do_commond_callback("dirty");
    do_commond_callback("request_extra_frame");

    BOOST_LOG_TRIVIAL(info) << "assembly steps export: done, pages=" << m_steps_export_images.size()
                            << " -> " << m_steps_export_output_path;

    // Reveal the generated PDF / Markdown in the file manager.
    open_export_output_folder(m_steps_export_output_path);
    track_assembly_view_export(m_steps_export_type);
}

void AssemblyStepsUtils::save_existing_project_if_dirty()
{
    Plater *plater = wxGetApp().plater();
    if (!plater || !plater->is_project_dirty())
        return;

    if (plater->get_project_filename(".3mf").IsEmpty())
        return;

    if (plater->save_project() == wxID_YES) {
        if (wxGetApp().mainframe && wxGetApp().mainframe->topbar())
            wxGetApp().mainframe->topbar()->EnableSaveItem(false);
    }
}

void AssemblyStepsUtils::open_export_output_folder(const std::string &file_path)
{
    if (file_path.empty())
        return;
    boost::system::error_code ec;
    if (!boost::filesystem::exists(file_path, ec))
        return;

    if (m_use_notify_open_folder_flag) {
        // Reuse the same UX as the gcode / 3mf "export finished" toast: instead of
        Plater *plater = wxGetApp().plater();
        if (plater != nullptr && plater->get_notification_manager() != nullptr) {
            const std::string dir_path = boost::filesystem::path(file_path).parent_path().string();
            plater->get_notification_manager()->push_exporting_finished_notification(file_path, dir_path, false);
            return;
        }
    }

    desktop_open_any_folder(file_path);
}

// Load a Unicode-capable TTF/TTC font into the libharu document so PDF
// titles render Chinese / Japanese / Korean correctly. Helvetica's

static HPDF_Font load_unicode_pdf_font(HPDF_Doc pdf)
{
    namespace fs = boost::filesystem;
    HPDF_UseUTFEncodings(pdf);

    struct TtcCandidate { const char *path; HPDF_UINT index; };
    static const TtcCandidate ttc_candidates[] = {
#if defined(_WIN32)
        {"C:\\Windows\\Fonts\\msyh.ttc",   0},
        {"C:\\Windows\\Fonts\\msyhl.ttc",  0},
        {"C:\\Windows\\Fonts\\simsun.ttc", 0},
#elif defined(__APPLE__)
        {"/System/Library/Fonts/PingFang.ttc",       0},
        {"/System/Library/Fonts/STHeiti Light.ttc",  0},
        {"/System/Library/Fonts/STHeiti Medium.ttc", 0},
#else
        {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 0},
        {"/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",         0},
        {"/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",           0},
#endif
    };
    static const char *ttf_candidates[] = {
#if defined(_WIN32)
        "C:\\Windows\\Fonts\\msyh.ttf",
        "C:\\Windows\\Fonts\\simhei.ttf",
        "C:\\Windows\\Fonts\\simsun.ttf",
        "C:\\Windows\\Fonts\\arialuni.ttf",
#elif defined(__APPLE__)
        "/Library/Fonts/Arial Unicode.ttf",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
#endif
    };

    // A failed/unsupported font load (e.g. the CFF-flavoured PingFang.ttc on
    // macOS, which libharu cannot parse) leaves an error set on the document.
    // Because HPDF_New() is created without an error handler, that error
    // poisons the whole document: every later HPDF call silently becomes a
    // no-op and the PDF is never written. Reset the error after each failed
    // attempt so we can fall back to the next candidate (e.g. Arial Unicode).
    for (const auto &c : ttc_candidates) {
        if (!fs::exists(c.path))
            continue;
        const char *fname = HPDF_LoadTTFontFromFile2(pdf, c.path, c.index, HPDF_TRUE);
        if (fname) {
            if (HPDF_Font f = HPDF_GetFont(pdf, fname, "UTF-8"))
                return f;
        }
        HPDF_ResetError(pdf);
    }
    for (const char *p : ttf_candidates) {
        if (!fs::exists(p))
            continue;
        const char *fname = HPDF_LoadTTFontFromFile(pdf, p, HPDF_TRUE);
        if (fname) {
            if (HPDF_Font f = HPDF_GetFont(pdf, fname, "UTF-8"))
                return f;
        }
        HPDF_ResetError(pdf);
    }

    HPDF_ResetError(pdf);
    return HPDF_GetFont(pdf, "Helvetica", nullptr);
}

static HPDF_Image load_pdf_image_from_file(HPDF_Doc pdf, const std::string &image_path)
{
    if (image_path.empty())
        return nullptr;
    if (!boost::filesystem::exists(image_path))
        return nullptr;

    boost::nowide::ifstream image_file(image_path, std::ios::binary | std::ios::ate);
    if (!image_file) {
        BOOST_LOG_TRIVIAL(error) << "assembly steps export: failed to open image " << image_path;
        return nullptr;
    }
    const std::streamoff sz = image_file.tellg();
    if (sz <= 0 || sz > static_cast<std::streamoff>(std::numeric_limits<HPDF_UINT>::max()))
        return nullptr;

    image_file.seekg(0, std::ios::beg);
    std::vector<HPDF_BYTE> buf(static_cast<size_t>(sz));
    image_file.read(reinterpret_cast<char *>(buf.data()), static_cast<std::streamsize>(sz));
    if (!image_file)
        return nullptr;

    if (boost::algorithm::iends_with(image_path, ".jpg") ||
        boost::algorithm::iends_with(image_path, ".jpeg"))
        return HPDF_LoadJpegImageFromMem(pdf, buf.data(), static_cast<HPDF_UINT>(buf.size()));
    return HPDF_LoadPngImageFromMem(pdf, buf.data(), static_cast<HPDF_UINT>(buf.size()));
}

static void draw_pdf_image_fit(HPDF_Page page, HPDF_Image image,
                               float x, float y, float w, float h)
{
    if (!page || !image || w <= 0.f || h <= 0.f)
        return;

    const float img_w = static_cast<float>(HPDF_Image_GetWidth(image));
    const float img_h = static_cast<float>(HPDF_Image_GetHeight(image));
    if (img_w <= 0.f || img_h <= 0.f)
        return;

    const float scale = std::min(w / img_w, h / img_h);
    const float draw_w = img_w * scale;
    const float draw_h = img_h * scale;
    HPDF_Page_DrawImage(page, image,
        x + (w - draw_w) * 0.5f,
        y + (h - draw_h) * 0.5f,
        draw_w, draw_h);
}

static std::string pdf_fit_text_with_ellipsis(HPDF_Page page, const std::string &text, float max_width)
{
    if (!page || max_width <= 0.0f)
        return std::string();
    if (HPDF_Page_TextWidth(page, text.c_str()) <= max_width)
        return text;

    const std::string ellipsis = "...";
    if (HPDF_Page_TextWidth(page, ellipsis.c_str()) >= max_width)
        return ellipsis;

    std::string best;
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char b = static_cast<unsigned char>(text[i]);
        size_t adv = 1;
        if      ((b & 0x80) == 0x00) adv = 1;
        else if ((b & 0xE0) == 0xC0) adv = 2;
        else if ((b & 0xF0) == 0xE0) adv = 3;
        else if ((b & 0xF8) == 0xF0) adv = 4;
        if (i + adv > text.size())
            break;

        const std::string candidate = text.substr(0, i + adv) + ellipsis;
        if (HPDF_Page_TextWidth(page, candidate.c_str()) > max_width)
            break;
        best = candidate;
        i += adv;
    }
    return best.empty() ? ellipsis : best;
}

static void draw_pdf_bold_text(HPDF_Page page, float x, float y, const std::string &text)
{
    HPDF_Page_TextOut(page, x, y, text.c_str());
    HPDF_Page_TextOut(page, x + 0.35f, y, text.c_str());
}

bool AssemblyStepsUtils::build_assembly_steps_pdf(const std::string &pdf_filename,
                                                  const std::vector<std::string> &png_images,
                                                  const std::vector<std::string> &page_titles,
                                                  const std::vector<int> &step_indices)
{
    if (png_images.empty())
        return false;

    HPDF_Doc pdf = HPDF_New(nullptr, nullptr);
    if (!pdf)
        return false;

    HPDF_SetCompressionMode(pdf, HPDF_COMP_ALL);
    HPDF_Font   font   = load_unicode_pdf_font(pdf);
    const float margin = 36.0f;
    const float step_title_size = 18.0f;
    const float sub_label_size  = 14.0f;
    const float title_gap       = 8.0f;
    const float sub_label_gap   = 6.0f;
    const float image_spacing   = 16.0f;

    // Group frames by step_index.
    struct FrameInfo { size_t img_idx; int step_idx; int sub_idx; std::string step_title; };
    std::vector<FrameInfo> frames;
    {
        int prev_step = -1;
        int sub_counter = 0;
        for (size_t i = 0; i < png_images.size(); ++i) {
            int si = (i < step_indices.size()) ? step_indices[i] : static_cast<int>(i + 1);
            if (si != prev_step) { sub_counter = 0; prev_step = si; }
            ++sub_counter;
            std::string title = (i < page_titles.size() && !page_titles[i].empty())
                                    ? page_titles[i]
                                    : (std::string("Step ") + std::to_string(si));
            frames.push_back({i, si, sub_counter, std::move(title)});
        }
    }
    // Count how many frames belong to each step.
    auto step_frame_count = [&](int step_idx) -> int {
        int count = 0;
        for (const auto &f : frames)
            if (f.step_idx == step_idx) ++count;
        return count;
    };

    // Load all images upfront.
    std::vector<HPDF_Image> loaded_images(png_images.size(), nullptr);
    for (size_t i = 0; i < png_images.size(); ++i) {
        boost::nowide::ifstream image_file(png_images[i], std::ios::binary | std::ios::ate);
        if (!image_file) {
            BOOST_LOG_TRIVIAL(error) << "assembly steps export: failed to open image " << png_images[i];
            HPDF_Free(pdf);
            return false;
        }
        const std::streamoff sz = image_file.tellg();
        if (sz <= 0 || sz > static_cast<std::streamoff>(std::numeric_limits<HPDF_UINT>::max())) {
            HPDF_Free(pdf);
            return false;
        }
        image_file.seekg(0, std::ios::beg);
        std::vector<HPDF_BYTE> buf(static_cast<size_t>(sz));
        image_file.read(reinterpret_cast<char *>(buf.data()), static_cast<std::streamsize>(sz));
        if (!image_file) { HPDF_Free(pdf); return false; }
        loaded_images[i] = HPDF_LoadPngImageFromMem(pdf, buf.data(), static_cast<HPDF_UINT>(buf.size()));
        if (!loaded_images[i]) { HPDF_Free(pdf); return false; }
    }

    // Layout: place items on pages, multiple images per page when they fit.
    HPDF_Page page      = nullptr;
    float     page_w    = 0.f;
    float     page_h    = 0.f;
    float     cursor_y  = 0.f; // current y position (top-down from page top)

    auto new_page = [&]() {
        page = HPDF_AddPage(pdf);
        HPDF_Page_SetSize(page, HPDF_PAGE_SIZE_A4, HPDF_PAGE_PORTRAIT);
        page_w   = HPDF_Page_GetWidth(page);
        page_h   = HPDF_Page_GetHeight(page);
        cursor_y = margin;
    };

    auto project_name = []() -> std::string {
        if (wxGetApp().plater()) {
            std::string name = wxGetApp().plater()->get_project_name().ToUTF8().data();
            if (!name.empty())
                return name;
        }
        return "Untitled";
    };

    auto draw_cover_pages = [&]() {
        new_page();
        const float title_area_h = page_h / 3.0f;
        const float title_size = 28.0f;
        // Subtitle now sits on its own line BELOW the main title, right-aligned

        const float subtitle_size = 16.0f;
        const std::string subtitle = std::string(" ") + "----"+_u8L("Assembly Guide");

        std::string title = m_pdf_export_title.empty() ? project_name() : m_pdf_export_title;
        HPDF_Page_BeginText(page);
        HPDF_Page_SetFontAndSize(page, font, title_size);
        title = pdf_fit_text_with_ellipsis(page, title, page_w - 2.0f * margin);
        const float title_w = HPDF_Page_TextWidth(page, title.c_str());
        const float title_x = std::max(margin, (page_w - title_w) * 0.5f);
        const float title_y = page_h - title_area_h;
        draw_pdf_bold_text(page, title_x, title_y, title);

        // Subtitle baseline drops one title-line below the title baseline.
        HPDF_Page_SetFontAndSize(page, font, subtitle_size);
        const float subtitle_w = HPDF_Page_TextWidth(page, subtitle.c_str());
        const float subtitle_y = title_y - title_size * 0.8f;
        // Pin to the END of the line, i.e. the right page margin, regardless of
        const float subtitle_x = std::max(margin, page_w - margin - subtitle_w);
        HPDF_Page_TextOut(page, subtitle_x, subtitle_y, subtitle.c_str());
        HPDF_Page_EndText(page);

        if (HPDF_Image cover_image = load_pdf_image_from_file(pdf, m_pdf_export_cover_image_path)) {
            // Image starts under the subtitle line, not the title line.
            const float image_top = subtitle_y - subtitle_size * 0.6f;
            draw_pdf_image_fit(page, cover_image,
                margin, margin,
                page_w - 2.0f * margin,
                image_top - margin);
        }

        if (HPDF_Image second_page_image = load_pdf_image_from_file(pdf, m_pdf_export_second_page_image_path)) {
            new_page();
            draw_pdf_image_fit(page, second_page_image,
                margin, margin,
                page_w - 2.0f * margin,
                page_h - 2.0f * margin);
        }
        page = nullptr;
    };

    draw_cover_pages();

    auto remaining_height = [&]() { return page_h - cursor_y - margin; };

    size_t fi = 0;
    while (fi < frames.size()) {
        const auto &f = frames[fi];
        const int total_in_step = step_frame_count(f.step_idx);
        const bool multi_frame = (total_in_step > 1);
        const bool is_first_of_step = (f.sub_idx == 1);

        // Determine height needed for this item.
        float needed_h = 0.f;
        if (is_first_of_step && multi_frame)
            needed_h += step_title_size + title_gap;

        float label_h = 0.f;
        if (multi_frame)
            label_h = sub_label_size + sub_label_gap;
        else
            label_h = step_title_size + title_gap;
        needed_h += label_h;

        const float img_w_raw = static_cast<float>(HPDF_Image_GetWidth(loaded_images[f.img_idx]));
        const float img_h_raw = static_cast<float>(HPDF_Image_GetHeight(loaded_images[f.img_idx]));
        const float max_img_w = (page == nullptr ? (595.f - margin * 2.f) : (page_w - margin * 2.f));
        const float max_img_h = 320.f;
        const float scale     = std::min(max_img_w / img_w_raw, max_img_h / img_h_raw);
        const float draw_w    = img_w_raw * scale;
        const float draw_h    = img_h_raw * scale;
        needed_h += draw_h;

        if (page == nullptr || remaining_height() < needed_h)
            new_page();

        // If it's the first frame of a multi-frame step, draw the step title.
        if (is_first_of_step && multi_frame) {
            float text_y = page_h - cursor_y - step_title_size;
            HPDF_Page_BeginText(page);
            HPDF_Page_SetFontAndSize(page, font, step_title_size);
            std::string header = f.step_title;
            HPDF_Page_TextOut(page, margin, text_y, header.c_str());
            HPDF_Page_EndText(page);
            cursor_y += step_title_size + title_gap;
        }

        // Draw sub-label or single-step title.
        {
            float text_size = multi_frame ? sub_label_size : step_title_size;
            float text_y = page_h - cursor_y - text_size;
            HPDF_Page_BeginText(page);
            HPDF_Page_SetFontAndSize(page, font, text_size);
            std::string label;
            if (multi_frame) {
                label = _u8L("Step") + " " + std::to_string(f.step_idx) + "." + std::to_string(f.sub_idx); // f.step_title + "." + std::to_string(f.sub_idx);
            } else
                label = f.step_title;
            HPDF_Page_TextOut(page, margin, text_y, label.c_str());
            HPDF_Page_EndText(page);
            cursor_y += text_size + (multi_frame ? sub_label_gap : title_gap);
        }

        // Draw image.
        {
            float x = (page_w - draw_w) * 0.5f;
            float y = page_h - cursor_y - draw_h;
            HPDF_Page_DrawImage(page, loaded_images[f.img_idx], x, y, draw_w, draw_h);
            cursor_y += draw_h + image_spacing;
        }

        ++fi;
    }

    HPDF_STATUS status = HPDF_SaveToStream(pdf);
    if (status != HPDF_OK) {
        BOOST_LOG_TRIVIAL(error) << "assembly steps export: failed to save PDF stream, status=" << status;
        HPDF_Free(pdf);
        return false;
    }

    const HPDF_UINT32 stream_size = HPDF_GetStreamSize(pdf);
    std::vector<HPDF_BYTE> stream_data(stream_size);
    HPDF_UINT32 read_size = stream_size;
    status = HPDF_ReadFromStream(pdf, stream_data.data(), &read_size);
    HPDF_Free(pdf);
    if (status != HPDF_OK || read_size == 0) {
        BOOST_LOG_TRIVIAL(error) << "assembly steps export: failed to read PDF stream, status=" << status;
        return false;
    }

    boost::nowide::ofstream ofs(pdf_filename, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        BOOST_LOG_TRIVIAL(error) << "assembly steps export: failed to open PDF file " << pdf_filename;
        return false;
    }
    ofs.write(reinterpret_cast<const char*>(stream_data.data()), read_size);
    if (!ofs) {
        BOOST_LOG_TRIVIAL(error) << "assembly steps export: failed to write PDF file " << pdf_filename;
        return false;
    }

    BOOST_LOG_TRIVIAL(info) << "assembly steps export: saved to " << pdf_filename;
    return true;
}

void AssemblyStepsUtils::set_gizmo_toolbar_rect(float x0, float y0, float x1, float y1)
{
    m_gizmo_toolbar_rect_min = ImVec2(x0, y0);
    m_gizmo_toolbar_rect_max = ImVec2(x1, y1);
}

void AssemblyStepsUtils::set_assembly_overlay_rect(AssemblyOverlayRect which, const ImVec2 &mn, const ImVec2 &mx)
{
    switch (which) {
    case AssemblyOverlayRect::Navigator:
        m_overlay_rect_navigator_min = mn;
        m_overlay_rect_navigator_max = mx;
        break;
    case AssemblyOverlayRect::FitCamera:
        m_overlay_rect_fit_camera_min = mn;
        m_overlay_rect_fit_camera_max = mx;
        break;
    case AssemblyOverlayRect::AssembleControl:
        m_overlay_rect_assemble_control_min = mn;
        m_overlay_rect_assemble_control_max = mx;
        break;
    case AssemblyOverlayRect::ReturnToolbar:
        m_overlay_rect_return_toolbar_min = mn;
        m_overlay_rect_return_toolbar_max = mx;
        break;
    }
}

ImVec2 AssemblyStepsUtils::export_button_size(float sc) const
{
    // Mirror render_assembly_guide_export_button()'s footprint so collision tests use
    // the exact same width/height as the rendered button.
    const float pad_x      = 8.0f * sc;
    const float pad_y      = 4.0f * sc;
    const float icon_sz    = 24.0f * sc;
    const float label_fs   = ImGui::GetFontSize();
    const float label_line = std::max(20.0f * sc, label_fs + 4.0f * sc);
    float btn_w = icon_sz + pad_x * 2.0f;
    if (ImFont *f = ImGui::GetFont()) {
        const std::string label_str = _u8L("Export");
        const ImVec2      ts        = f->CalcTextSizeA(label_fs, FLT_MAX, 0.0f, label_str.c_str());
        btn_w = std::max(icon_sz, ts.x) + pad_x * 2.0f;
    }
    const float btn_h = pad_y + icon_sz + label_line + pad_y;
    return ImVec2(btn_w, btn_h);
}

float AssemblyStepsUtils::get_guide_panel_y_offset(float guide_x, float guide_y_base, float guide_w, float sc)
{
    // The top gizmo/main toolbar rect (logical px) is fed from GLCanvas3D.
    m_export_btn_corner_mode = false;

    const float tb_x0 = m_gizmo_toolbar_rect_min.x;
    const float tb_y0 = m_gizmo_toolbar_rect_min.y;
    const float tb_x1 = m_gizmo_toolbar_rect_max.x;
    const float tb_y1 = m_gizmo_toolbar_rect_max.y;
    if (tb_x1 <= tb_x0 || tb_y1 <= tb_y0)
        return 0.f; // toolbar not visible / no rect this frame

    // Export button DEFAULT rect (left of the guide panel, top at guide_y_base).
    const ImVec2 bsz = export_button_size(sc);
    const float  gap = 12.0f * sc;
    const float  bx1 = guide_x - gap;          // right edge (gap to the panel)
    const float  bx0 = bx1 - bsz.x;            // left edge
    const float  by0 = guide_y_base;
    const float  by1 = guide_y_base + bsz.y;

    // Inflate the toolbar rect by a small margin so the corner layout also kicks in
    // when the button merely gets close to (not strictly overlapping) the toolbar.
    const float margin = 10.0f * sc;
    const float etb_x0 = tb_x0 - margin;
    const float etb_y0 = tb_y0 - margin;
    const float etb_x1 = tb_x1 + margin;
    const float etb_y1 = tb_y1 + margin;

    // AABB intersection between the export button rect and the (margin-inflated)
    // toolbar rect: this alone decides whether the button switches to the corner.
    const bool btn_hits = (etb_x0 < bx1) && (etb_x1 > bx0) && (etb_y0 < by1) && (etb_y1 > by0);
    m_export_btn_corner_mode = btn_hits;

    // Panel rect: right side of the canvas, extends well below the toolbar.
    const bool panel_hits = (etb_x0 < guide_x + guide_w) && (etb_x1 > guide_x) && (etb_y1 > by0);

    // When the button moves to the corner it is TOP-aligned to the toolbar top, so the
    // panel must clear the corner button's bottom edge (toolbar_top + button height).
    float bottom_obstacle = 0.f;
    if (btn_hits)
        bottom_obstacle = std::max(bottom_obstacle, tb_y0 + bsz.y); // corner button bottom
    if (panel_hits)
        bottom_obstacle = std::max(bottom_obstacle, tb_y1);         // toolbar bottom
    if (bottom_obstacle <= guide_y_base)
        return 0.f;

    // Push the panel down so its top sits below the obstacle, plus an 8px gap.
    return (bottom_obstacle - guide_y_base) + 8.0f * sc;
}


bool AssemblyStepsUtils::prepare_project_save_end_frame()
{
    if (!m_model || m_only_final_assembly_endframe_effect_real_assembly)
        return false;
    auto *entries = get_current_kf_entries();
    if (!entries || entries->empty())
        return false;
    if (m_keyframe_selected >= 0 && m_keyframe_selected < static_cast<int>(entries->size()) &&
        (*entries)[m_keyframe_selected].is_last()) {
        return false;
    }

    int end_frame_idx = -1;
    for (int i = 0; i < static_cast<int>(entries->size()); ++i) {
        if ((*entries)[i].is_last()) {
            end_frame_idx = i;
            break;
        }
    }
    if (end_frame_idx < 0)
        return false;

    m_keyframe_selected = end_frame_idx;
    refresh_guide_show_part_numbers_from_current();
    look_cur_frame_logic((*entries)[end_frame_idx]);
    if (m_keyframe_display_mode != KeyframeDisplayMode::All)
        apply_keyframe_display_mode();

    m_save_project_tip_text = _u8L("Saving project will automatically switch to end frame");
    m_save_project_tip_until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    do_commond_callback("exit_gizmo");
    do_commond_callback("request_extra_frame");
    return true;
}



void AssemblyStepsUtils::clear_runtime_state()
{
    clear_selected_node();
    m_last_folder_idx  = -1;

    m_keyframe_selected = -1;
    on_selected_node_changed();
    set_selection_origin(SelectionOrigin::None);//clear_runtime_state
    m_keyframe_edit_buf[0]   = '\0';
    m_keyframe_playing = false;
    m_play_queue.clear();
    m_assembly_play_index = 1;
    m_assembly_play_count = 0;
    clear_global_playback_state();
    m_play_different_folder_start_time = 0.0;

    m_selected_screen_center_ = Vec2d::Zero();
    m_selected_screen_center_dirty_ = true;
    m_render_interpolated_part_number_labels = false;
    m_last_recorded_volumes.clear();
    invalidate_play_frame_refs();
}

void AssemblyStepsUtils::clear_steps_all()
{
    if (m_model == nullptr) {
        return;
    }
    _steps_nodes.clear();
    _steps_roots.clear();
    clear_runtime_state();
}

void AssemblyStepsUtils::clear_non_final_assembly_steps()
{
    if (m_model == nullptr)
        return;

    MessageDialog msg_dlg(nullptr,
        _L("Are you sure you want to delete all assembly steps?"),
        _L("Delete all steps"),
        wxICON_QUESTION | wxYES_NO);
    if (msg_dlg.ShowModal() != wxID_YES)
        return;

    const int final_folder = ensure_final_assembly_folder();
    _steps_roots.erase(std::remove_if(_steps_roots.begin(), _steps_roots.end(), [&](int root_idx) {
        if (root_idx < 0 || root_idx >= (int)_steps_nodes.size())
            return true;
        const auto &root = _steps_nodes[root_idx];
        return root.type != AssemblyStepsTreeNode::Type::Folder || !root.is_final_assembly;
    }), _steps_roots.end());

    for (auto it = m_structure_select_labels.begin(); it != m_structure_select_labels.end();) {
        const int node_idx = it->first;
        if (node_idx < 0 || node_idx >= (int)_steps_nodes.size() || !_steps_nodes[node_idx].is_final_assembly)
            it = m_structure_select_labels.erase(it);
        else
            ++it;
    }
    for (auto it = m_structure_select_show_default.begin(); it != m_structure_select_show_default.end();) {
        const int node_idx = *it;
        if (node_idx < 0 || node_idx >= (int)_steps_nodes.size() || !_steps_nodes[node_idx].is_final_assembly)
            it = m_structure_select_show_default.erase(it);
        else
            ++it;
    }

    clear_selection();
    m_selected_node = final_folder;
    m_structure_scroll_to_node = final_folder;
    if (final_folder >= 0)
        select_steps_tree_node_for_canvas(final_folder);
    renumber_structure_step_roots();
    reschedule_play_bar_after_structure_change();//clear_non_final_assembly_steps
    save_assembly_steps_json_to_model();
}

void AssemblyStepsUtils::new_project_clear_assembly_steps_tree_view()
{
    clear_steps_all();
    reset_state_on_model_changed();
    save_assembly_steps_json_to_model_and_request_extra_frame();
    clear_when_no_selection();
}

bool AssemblyStepsUtils::has_pending_play_frames() const
{
    return !m_playback_paused && (m_keyframe_playing || m_play_global || m_video_intro_active || m_play_different_folder_waiting || m_play_end_waiting || !m_play_queue.empty());
}

std::vector<int> AssemblyStepsUtils::selected_object_indices(int object_count, const std::vector<int> &selection_object_indices) const
{
    std::vector<int> object_idxs;
    std::set<int> unique;
    for (int object_idx : selection_object_indices) {
        if (object_idx >= 0 && object_idx < object_count && unique.insert(object_idx).second)
            object_idxs.push_back(object_idx);
    }
    return object_idxs;
}

bool AssemblyStepsUtils::has_selected_node() const
{
    return m_selected_node >= 0;
}

bool AssemblyStepsUtils::is_selected_final_assembly_node() const
{
    if (!m_model || m_selected_node < 0)
        return false;

    int folder_idx = find_parent_folder(m_selected_node);
    return is_final_assembly_folder(folder_idx);
}

bool AssemblyStepsUtils::is_final_assembly_folder(int folder_idx) const
{
    return m_model && folder_idx >= 0 && folder_idx < (int) _steps_nodes.size() &&
           _steps_nodes[folder_idx].type == AssemblyStepsTreeNode::Type::Folder &&
           _steps_nodes[folder_idx].is_final_assembly;
}

bool AssemblyStepsUtils::has_selected_step_node() const
{
    return m_selected_node >= 0 && m_selected_node < (int) _steps_nodes.size() && _steps_nodes[m_selected_node].type == AssemblyStepsTreeNode::Type::Folder;
}

bool AssemblyStepsUtils::has_selected_final_assembly_end_keyframe() const
{
    if (!is_selected_final_assembly_node())
        return false;

    auto *entries = const_cast<AssemblyStepsUtils*>(this)->get_current_kf_entries();
    if (entries == nullptr)
        return false;
    if (m_keyframe_selected < 0 || m_keyframe_selected >= (int)entries->size())
        return false;
    return (*entries)[m_keyframe_selected].is_last();
}

int AssemblyStepsUtils::find_parent_folder(int node_idx) const
{
    if (node_idx < 0 || node_idx >= (int) _steps_nodes.size())
        return -1;
    if (_steps_nodes[node_idx].type == AssemblyStepsTreeNode::Type::Folder)
        return node_idx;

    for (int i = 0; i < (int) _steps_nodes.size(); ++i) {
        if (_steps_nodes[i].type != AssemblyStepsTreeNode::Type::Folder)
            continue;
        for (int ci : _steps_nodes[i].children) {
            if (ci == node_idx)
                return i;
        }
    }
    return -1;
}

int AssemblyStepsUtils::next_node_id() const
{
    int max_id = 0;
    for (const auto &node : _steps_nodes)
        if (node.type == AssemblyStepsTreeNode::Type::Folder)
            max_id = std::max(max_id, node.id);
    return max_id + 1;
}

int AssemblyStepsUtils::next_step() const
{
    int max_step = 0;
    for (const auto &node : _steps_nodes)
        if (node.type == AssemblyStepsTreeNode::Type::Folder && !node.is_final_assembly)
            max_step = std::max(max_step, node.step);
    return max_step + 1;
}

int AssemblyStepsUtils::create_folder_node(const std::string &name, int step)
{
    AssemblyStepsTreeNode folder;
    folder.type     = AssemblyStepsTreeNode::Type::Folder;
    folder.id       = next_node_id();
    folder.step     = step;
    folder.name     = name;
    folder.visible  = true;
    int folder_idx  = (int) _steps_nodes.size();
    _steps_nodes.push_back(folder);
    return folder_idx;
}

int AssemblyStepsUtils::create_object_node(int object_idx, const std::string &name, size_t obj_id)
{
    AssemblyStepsTreeNode node;
    node.type       = AssemblyStepsTreeNode::Type::Object;
    node.name       = name;
    node.object_idx = object_idx;
    node.object_id  = obj_id;
    node.visible    = true;
    int node_idx    = (int) _steps_nodes.size();
    _steps_nodes.push_back(node);
    return node_idx;
}

int AssemblyStepsUtils::create_assembly_step_from_objects(const std::vector<int> &object_idxs)
{
    if (!can_add_non_final_assembly_step())
        return -1;
    if (object_idxs.empty())
        return -1;

    AssemblyStepsTreeNode folder;
    folder.type     = AssemblyStepsTreeNode::Type::Folder;
    folder.id = next_node_id();
    folder.step = next_step();
    folder.name = _u8L("New Step");
    int folder_idx  = (int) _steps_nodes.size();
    _steps_nodes.push_back(folder);
    ensure_default_keyframe(folder_idx);
    fill_folder_keyframes_from_children(folder_idx);
    _steps_roots.push_back(folder_idx);
    renumber_structure_step_roots();

    add_objects_to_assembly_step(folder_idx, object_idxs);
    m_selected_node = folder_idx;
    on_selected_node_changed();
    return folder_idx;
}

bool AssemblyStepsUtils::merge_selected_volumes_into_folder_checked(int folder_idx)
{
    if (!m_model || !m_selection || folder_idx < 0 || folder_idx >= (int) _steps_nodes.size())
        return false;
    if (_steps_nodes[folder_idx].type != AssemblyStepsTreeNode::Type::Folder)
        return false;
    if (m_selection->get_volume_idxs().empty())
        return false;

    std::set<int> existing_obj_idxs;
    for (int child_idx : _steps_nodes[folder_idx].children) {
        if (child_idx < 0 || child_idx >= (int) _steps_nodes.size())
            continue;
        const auto &child = _steps_nodes[child_idx];
        if (child.type == AssemblyStepsTreeNode::Type::Object && child.object_idx >= 0)
            existing_obj_idxs.insert(child.object_idx);
    }

    auto &opt_checked = _steps_nodes[folder_idx].assembly_tree_checked;
    if (!opt_checked)
        opt_checked.emplace();

    bool has_any_checked = false;
    for (const auto &p : *opt_checked) {
        if (p.second) {
            has_any_checked = true;
            break;
        }
    }
    bool changed = false;
    // Preserve prior full-object membership before a partial volume merge.
    // Only bootstrap objects already in the step so newly selected parts of a
    // new object are not expanded to the whole ModelObject.
    if (!has_any_checked) {
        for (int oi : existing_obj_idxs) {
            if (oi < 0 || oi >= (int) m_model->objects.size() || !m_model->objects[oi])
                continue;
            (*opt_checked)["object:" + std::to_string(oi)] = true;
            const ModelObject *obj = m_model->objects[oi];
            for (int vi = 0; vi < (int) obj->volumes.size(); ++vi) {
                if (!obj->volumes[vi])
                    continue;
                (*opt_checked)["object:" + std::to_string(oi) + ":volume:" + std::to_string(vi)] = true;
            }
            changed = true;
        }
    }

    for (unsigned int idx : m_selection->get_volume_idxs()) {
        const GLVolume *v = m_selection->get_volume(idx);
        if (!v)
            continue;
        const int oi = v->object_idx();
        const int vi = v->volume_idx();
        if (oi < 0)
            continue;
        const std::string obj_uid = "object:" + std::to_string(oi);
        if (!(*opt_checked)[obj_uid]) {
            (*opt_checked)[obj_uid] = true;
            changed = true;
        }
        if (vi >= 0) {
            const std::string vol_uid = obj_uid + ":volume:" + std::to_string(vi);
            if (!(*opt_checked)[vol_uid]) {
                (*opt_checked)[vol_uid] = true;
                changed = true;
            }
        }
    }
    return changed;
}

bool AssemblyStepsUtils::add_objects_to_assembly_step(int folder_idx, const std::vector<int> &object_idxs)
{
    if (folder_idx < 0 || folder_idx >= (int) _steps_nodes.size())
        return false;
    if (_steps_nodes[folder_idx].type != AssemblyStepsTreeNode::Type::Folder)
        return false;

    const int object_count = m_model ? (int) m_model->objects.size() : 0;

    // Membership is per-step: every step owns its own object node for a given
    std::set<int> existing_obj_idxs;
    for (int child_idx : _steps_nodes[folder_idx].children) {
        if (child_idx < 0 || child_idx >= (int) _steps_nodes.size())
            continue;
        const auto &child = _steps_nodes[child_idx];
        if (child.type == AssemblyStepsTreeNode::Type::Object && child.object_idx >= 0)
            existing_obj_idxs.insert(child.object_idx);
    }

    // Record part-level membership before creating new object nodes so bootstrap
    // only covers objects already in the step.
    bool changed = merge_selected_volumes_into_folder_checked(folder_idx);

    for (int object_idx : object_idxs) {
        if (object_idx < 0 || object_idx >= object_count)
            continue;
        // Already part of the current step -> ignore (no duplicates, no move).
        if (existing_obj_idxs.count(object_idx) > 0)
            continue;
        // Create a fresh object node dedicated to this step and append it. We
        int object_node_idx = create_object_node(object_idx, get_object_name(object_idx), get_object_id_id(object_idx));
        if (object_node_idx < 0)
            continue;
        ensure_default_keyframe(object_node_idx);
        _steps_nodes[folder_idx].children.push_back(object_node_idx);
        existing_obj_idxs.insert(object_idx);
        changed = true;
    }

    if (changed) {
        sync_keyframe_tree();
        // Children are now in place, so aggregate their transforms into the
        fill_folder_keyframes_from_children(folder_idx);
        // The step's object set changed, so its stored part-number labels are
        for (auto &entry : _steps_nodes[folder_idx].kf_data.entries) {
            if (entry.data.assembly_note.part_number_labels.empty())
                continue;
            entry.data.assembly_note.part_number_labels.clear();
            entry.need_save = true;
        }
        save_assembly_steps_json_to_model();
        do_commond_callback("dirty");
        do_commond_callback("request_extra_frame");
        do_commond_callback("exit_gizmo");     // Adding parts is a tree edit, so drop any active gizmo on the canvas.
        apply_keyframe_display_mode();
    }
    return changed;
}

std::vector<int> AssemblyStepsUtils::sorted_step_nodes() const
{
    std::vector<int> step_nodes;
    // Only consider folders still referenced as steps (i.e. present in roots). A deleted
    for (int root_idx : _steps_roots)
        if (root_idx >= 0 && root_idx < (int) _steps_nodes.size() &&
            _steps_nodes[root_idx].type == AssemblyStepsTreeNode::Type::Folder)
            step_nodes.push_back(root_idx);

    std::sort(step_nodes.begin(), step_nodes.end(), [this](int lhs, int rhs) {
        const auto &a = _steps_nodes[lhs];
        const auto &b = _steps_nodes[rhs];
        if (a.step != b.step)
            return a.step < b.step;
        if (a.id != b.id)
            return a.id < b.id;
        return a.name < b.name;
    });
    return step_nodes;
}

bool AssemblyStepsUtils::can_add_objects_to_step(const std::vector<int> &object_idxs) const
{
    return !object_idxs.empty();
}

std::vector<std::pair<int, std::string>> AssemblyStepsUtils::assembly_step_choices() const
{
    std::vector<std::pair<int, std::string>> choices;
    for (int node_idx : sorted_step_nodes()) {
        if (node_idx >= 0 && node_idx < (int) _steps_nodes.size()) {
            if (_steps_nodes[node_idx].is_final_assembly)
                continue;
            choices.push_back({node_idx, assembly_step_display_name(_steps_nodes[node_idx])});
        }
    }
    return choices;
}

std::string AssemblyStepsUtils::build_steps_json_string()
{
    if (!m_model || _steps_nodes.empty() || _steps_roots.empty()) {
        return {};
    }
    // Walk nodes/roots + per-node kf_data and rebuild the AssembleBaseInfo list.
    std::function<std::shared_ptr<AssembleBaseInfo>(int)> build_node;
    build_node = [&](int node_idx) -> std::shared_ptr<AssembleBaseInfo> {
        if (node_idx < 0 || node_idx >= (int) _steps_nodes.size())
            return nullptr;
        const auto &node = _steps_nodes[node_idx];

        if (node.type == AssemblyStepsTreeNode::Type::Folder) {
            auto sub = std::make_shared<AssembleSub>();
            sub->id = node.id;
            sub->step = node.step;
            sub->name = node.name;
            sub->is_final_assembly = node.is_final_assembly;
            sub->assembly_tree_checked = node.assembly_tree_checked;
            for (const auto &e : node.kf_data.entries)
                sub->keyframes.push_back(e.data);
            for (int ci : node.children) {
                auto child_ptr = build_node(ci);
                if (child_ptr)
                    sub->children.push_back(std::move(child_ptr));
            }
            return sub;
        } else {
            auto single = std::make_shared<AssembleSingleInfo>();
            single->name = node.name;
            single->object_idx = node.object_idx;
            single->object_id = node.object_id;
            for (const auto &e : node.kf_data.entries)
                single->keyframes.push_back(e.data);
            return single;
        }
    };

    std::vector<std::shared_ptr<AssembleBaseInfo>> items;
    for (int root_idx : _steps_roots) {
        if (root_idx < 0 || root_idx >= (int) _steps_nodes.size())
            continue;
        auto item = build_node(root_idx);
        if (item)
            items.push_back(std::move(item));
    }
    if (items.empty()) {
        return {};
    }
    AssemblyStepJson                  assmeble_steps_json;
    AssemblyStepJson::PdfExportParams pdf_params;
    pdf_params.title = m_pdf_export_title;
    //pdf_params.cover_image_path = m_pdf_export_cover_image_path;
    //pdf_params.second_page_image_path = m_pdf_export_second_page_image_path;
    assmeble_steps_json.set_pdf_export_params(pdf_params);

    update_part_number_label_font_size_from_config();
    assmeble_steps_json.set_assembly_part_number_label_font_size(m_part_number_label_font_size);

    // Persist the document-level shared reference viewport (single value for all
    // keyframes) so user-framed cameras can be rescaled on reload (see record_camera).
    if (m_model) {
        const AssemblyStepsTreeData &tree = m_model->get_assembly_steps_tree_data();
        assmeble_steps_json.set_camera_ref_viewport(tree.camera_ref_viewport_w, tree.camera_ref_viewport_h);
    }

    assmeble_steps_json.set_items(std::move(items));
#if !BBL_RELEASE_TO_PUBLIC // for debug
    assmeble_steps_json.save(AssemblyStepJson::get_debug_file_path());
#endif
    return assmeble_steps_json.to_json_string();
}

void AssemblyStepsUtils::sync_steps_objects_with_model()
{
    if (!m_model || _steps_nodes.empty())
        return;
    const int obj_count = (int) m_model->objects.size();
    // Older saves missed the final-assembly flag and reopened the "Final assembly"
    for (int root_idx : _steps_roots) {
        if (root_idx < 0 || root_idx >= (int) _steps_nodes.size())
            continue;
        auto &folder = _steps_nodes[root_idx];
        if (folder.type != AssemblyStepsTreeNode::Type::Folder || folder.is_final_assembly)
            continue;
        if (folder.name != _u8L("Final assembly") && folder.name != "Final assembly")
            continue;

        std::set<int> obj_idxs;
        bool only_objects = true;
        for (int ci : folder.children) {
            if (ci < 0 || ci >= (int) _steps_nodes.size()) {
                only_objects = false;
                break;
            }
            const auto &child = _steps_nodes[ci];
            if (child.type != AssemblyStepsTreeNode::Type::Object || child.object_idx < 0) {
                only_objects = false;
                break;
            }
            obj_idxs.insert(child.object_idx);
        }
        if (only_objects && obj_idxs.size() == (size_t)obj_count)
            folder.is_final_assembly = true;
    }

    for (int root_idx : _steps_roots) {
        if (root_idx < 0 || root_idx >= (int) _steps_nodes.size())
            continue;
        auto &folder = _steps_nodes[root_idx];
        if (folder.type != AssemblyStepsTreeNode::Type::Folder)
            continue;

        if (folder.is_final_assembly) {
            std::set<int> existing_obj_idxs;
            for (int ci : folder.children) {
                if (ci >= 0 && ci < (int) _steps_nodes.size() && _steps_nodes[ci].type == AssemblyStepsTreeNode::Type::Object)
                    existing_obj_idxs.insert(_steps_nodes[ci].object_idx);
            }

            for (int oi = 0; oi < obj_count; ++oi) {
                if (existing_obj_idxs.count(oi))
                    continue;
                const auto &obj = m_model->objects[oi];
                if (!obj) continue;
                int obj_node = create_object_node(oi, obj->name, obj->id().id);
                if (obj_node >= 0)
                    folder.children.push_back(obj_node);
            }

            std::vector<int> valid_children;
            for (int ci : folder.children) {
                if (ci < 0 || ci >= (int) _steps_nodes.size()) continue;
                auto &cn = _steps_nodes[ci];
                if (cn.type != AssemblyStepsTreeNode::Type::Object) {
                    valid_children.push_back(ci);
                    continue;
                }
                if (cn.object_idx >= 0 && cn.object_idx < obj_count &&
                    m_model->objects[cn.object_idx] != nullptr) {
                    cn.name = m_model->objects[cn.object_idx]->name;
                    cn.object_id = m_model->objects[cn.object_idx]->id().id;
                    valid_children.push_back(ci);
                }
            }
            folder.children = std::move(valid_children);
            continue;
        }

        std::vector<int> valid_children;
        for (int ci : folder.children) {
            if (ci < 0 || ci >= (int) _steps_nodes.size()) continue;
            auto &cn = _steps_nodes[ci];
            if (cn.type != AssemblyStepsTreeNode::Type::Object) {
                valid_children.push_back(ci);
                continue;
            }
            int real_idx = find_model_object_idx_by_id(cn.object_id);
            if (real_idx >= 0) {
                cn.object_idx = real_idx;
                cn.name = m_model->objects[real_idx]->name;
                valid_children.push_back(ci);
            }
        }
        folder.children = std::move(valid_children);
    }
}

void AssemblyStepsUtils::sync_all_model_object_to_final_assembly_node()
{
    if (!m_model)
        return;
    clear_selected_node();
    const int folder_idx = ensure_final_assembly_folder();
    if (folder_idx < 0 || folder_idx >= (int) _steps_nodes.size())
        return;
    const int obj_count = (int) m_model->objects.size();
    bool      changed   = false;
    // 1) Verify every existing object node in the end frame and drop the ones
    std::vector<int> valid_children;
    std::set<int>    present_obj_idxs;
    valid_children.reserve(_steps_nodes[folder_idx].children.size());
    for (int ci : _steps_nodes[folder_idx].children) {
        if (ci < 0 || ci >= (int) _steps_nodes.size())
            continue;
        auto &cn = _steps_nodes[ci];
        if (cn.type != AssemblyStepsTreeNode::Type::Object) {
            valid_children.push_back(ci);
            continue;
        }
        // Prefer the stable ModelObject id; fall back to the cached index for legacy data that never stored an id.
        int real_idx = find_model_object_idx_by_id(cn.object_id);
        if (real_idx < 0 && cn.object_idx >= 0 && cn.object_idx < obj_count &&
            m_model->objects[cn.object_idx] != nullptr)
            real_idx = cn.object_idx;

        if (real_idx < 0) {// ModelObject was removed -> delete this node from the end frame.
            changed = true;
            continue;
        }

        const ModelObject *obj = m_model->objects[real_idx];
        if (cn.object_idx != real_idx || cn.name != obj->name ||
            cn.object_id != obj->id().id) {
            cn.object_idx = real_idx;
            cn.name       = obj->name;
            cn.object_id  = obj->id().id;
            changed       = true;
        }
        present_obj_idxs.insert(real_idx);
        valid_children.push_back(ci);
    }
    _steps_nodes[folder_idx].children = std::move(valid_children);
    // 2) Append a node for every ModelObject that is not represented yet.
    for (int oi = 0; oi < obj_count; ++oi) {
        if (present_obj_idxs.count(oi))
            continue;
        const ModelObject *obj = m_model->objects[oi];
        if (!obj)
            continue;
        int obj_node = create_object_node(oi, obj->name, obj->id().id);
        if (obj_node >= 0) {
            _steps_nodes[folder_idx].children.push_back(obj_node);
            changed = true;
        }
    }
    // 3) Refresh the end-frame keyframe data so newly added / pruned objects are  reflected in the stored transforms.
    if (changed)
        fill_folder_keyframes_from_children(folder_idx);
}

int AssemblyStepsUtils::ensure_final_assembly_folder()
{
    if (!m_model)
        return -1;

    for (int ri : _steps_roots) {
        if (ri >= 0 && ri < (int) _steps_nodes.size() && _steps_nodes[ri].is_final_assembly)
            return ri;
    }

    int folder_idx = create_folder_node(_u8L("Final assembly"), 0);
    if (folder_idx < 0)
        return -1;
    _steps_nodes[folder_idx].is_final_assembly = true;
    _steps_roots.insert(_steps_roots.begin(), folder_idx);

    for (int oi = 0; oi < (int) m_model->objects.size(); ++oi) {
        const auto &obj = m_model->objects[oi];
        if (!obj) continue;
        int obj_node = create_object_node(oi, obj->name, obj->id().id);
        if (obj_node >= 0) _steps_nodes[folder_idx].children.push_back(obj_node);
    }
    ensure_default_keyframe(folder_idx);
    fill_folder_keyframes_from_children(folder_idx);
    return folder_idx;
}

void AssemblyStepsUtils::sync_keyframe_tree()
{
    // kf_data lives on each node now, so stale-entry pruning happens implicitly
    for (int idx = 0; idx < (int) _steps_nodes.size(); ++idx)
        ensure_default_keyframe(idx);

    invalidate_play_frame_refs();//sync_keyframe_tree()
}

void AssemblyStepsUtils::ensure_default_keyframe(int node_idx) {
    ensure_default_keyframe_for_node(node_idx, _u8L("end frame"));
}

void AssemblyStepsUtils::seed_end_frame_camera_from_current(int node_idx)
{
    // A brand-new step has no parts yet, so its auto-created end frame would carry no
    if (node_idx < 0 || node_idx >= (int) _steps_nodes.size())
        return;
    for (auto &entry : _steps_nodes[node_idx].kf_data.entries) {
        if (entry.is_last()) {
            record_camera(entry.data);
            entry.data.is_camera_define = true;
            entry.need_save             = true;
            break;
        }
    }
}

void AssemblyStepsUtils::ensure_default_keyframe_for_node(int node_idx, const std::string &last_frame_name)
{
    if (node_idx < 0 || node_idx >= (int) _steps_nodes.size())
        return;

    AssemblyStepsTreeNode &node = _steps_nodes[node_idx];

    if (m_only_step_node_create_key_frame &&
        node.type != AssemblyStepsTreeNode::Type::Folder)
        return;
    // A folder (step) node carries the keyframe in m_only_step_node_create_key_frame
    if (node.type == AssemblyStepsTreeNode::Type::Object && node.object_idx < 0)
        return;
    KFNodeData&            kf   = node.kf_data;
    // First-touch initialization: previously the map's emplace marked "this is a
    if (kf.entries.empty() || kf.node_idx != node_idx) {
        kf.node_idx   = node_idx;
        kf.is_folder  = (node.type == AssemblyStepsTreeNode::Type::Folder);
        kf.object_idx = node.object_idx;
    }

    bool has_last = false;
    for (const auto &entry : kf.entries) {
        if (entry.is_last()) {
            has_last = true;
            break;
        }
    }
    if (has_last)
        return;

    KeyFrameEntry last;
    last.data.id   = 0;
    last.data.name = last_frame_name;
    fill_default_transforms(last, node.object_idx);//ensure_default_keyframe_for_node
    kf.entries.push_back(std::move(last));
    invalidate_play_frame_refs();//ensure_default_keyframe_for_node
}

KeyFrameEntryVector* AssemblyStepsUtils::get_current_kf_entries()
{
    if (m_selected_node < 0 || m_selected_node >= (int) _steps_nodes.size())
        return nullptr;
    if (m_only_step_node_create_key_frame) {
        auto new_node_ids = find_parent_folder(m_selected_node);
        if (new_node_ids >= 0) {
            return &_steps_nodes[new_node_ids].kf_data.entries;
        }

    }
    return &_steps_nodes[m_selected_node].kf_data.entries;
}

void AssemblyStepsUtils::fill_default_transforms(KeyFrameEntry &entry,int object_idx)
{
    auto object_count = m_model ? (int) m_model->objects.size() : 0;
    if (object_idx < 0 || object_idx >= object_count)
        return;
    if (has_instance(object_idx))
        return;

    entry.data.object_transformations.clear();
    entry.data.volume_transformations.clear();
    entry.data.volume_names.clear();
    const ModelObject *obj = m_model ? m_model->objects[object_idx] : nullptr;
    if (!obj)
        return;
    // Capture both halves of the GLVolume world transform: the object's
    // instance_assemble once, and each ModelVolume's volume_assemble.
    if (!obj->instances.empty())
        entry.data.object_transformations[object_idx] = get_instance_transform(object_idx);
    for (int vi = 0; vi < (int) obj->volumes.size(); ++vi) {
        const ModelVolume *mv = obj->volumes[vi];
        if (!mv)
            continue;
        const std::pair<int, int> key{object_idx, vi};
        entry.data.volume_transformations[key] = get_volume_transform(object_idx, vi);
        entry.data.volume_names[key]           = !mv->name.empty() ? mv->name : obj->name;
    }
}

int AssemblyStepsUtils::default_keyframe_index()
{
    auto *entries = get_current_kf_entries();
    if (!entries || entries->empty())
        return -1;
    for (int i = 0; i < (int)entries->size(); ++i) {
        if ((*entries)[i].is_last())
            return i;
    }
    return (int)entries->size() - 1;
}

void AssemblyStepsUtils::try_update_selected_keyframe()
{
    auto *entries = get_current_kf_entries();
    if (!entries || m_keyframe_selected < 0 || m_keyframe_selected >= (int)entries->size()) {
        // No step card / keyframe is currently selected. A gizmo edit in
        if (!has_selected_node())
            update_final_assembly_end_keyframe_from_current_selection();
        return;
    }
    if (m_only_final_assembly_endframe_effect_real_assembly) {
        if (is_selected_final_assembly_node()) {
            update_final_assembly_end_keyframe_from_current_selection();
        }
    } else {
        KeyFrameEntry &entry = (*entries)[m_keyframe_selected];
        record_keyframe_logic(entry);
    }
}

bool AssemblyStepsUtils::allow_sync_in_assemble_view()
{
    if (m_only_final_assembly_endframe_effect_real_assembly) {
        if (!has_selected_node()) {
            return true;
        }
        if (!has_selected_final_assembly_end_keyframe()) {
            record_selected_gl_volume_transforms_to_current_keyframe();
            return false;
        }
    }
    return true;
}

bool AssemblyStepsUtils::add_arrow_svg_note(const std::string &svg_name)
{
    auto *entries = get_current_kf_entries();
    if (!entries || m_keyframe_selected < 0 || m_keyframe_selected >= (int)entries->size())
        return false;
    m_selected_screen_center_dirty_ = true;
    KeyFrameEntry &cur_entry = (*entries)[m_keyframe_selected];
    ArrowSvgNote arrow;
    arrow.svg_name = svg_name;
    arrow.color    = note_color_from_palette_index(m_guide_note_color_selected);
    // Bind the arrow to the ModelVolumes currently selected, so its start point can anchor to their on-screen bbox center (computed at render time).
    bind_current_selection_volumes(arrow.bound_volumes);
    cur_entry.data.assembly_note.arrow_svgs.push_back(std::move(arrow));
    m_note_selected_type = AssemblyNoteSelectionType::ArrowSvg;
    m_note_selected_idx = (int)cur_entry.data.assembly_note.arrow_svgs.size() - 1;
    set_note_edit_controls_visible(true);
    set_selection_origin(SelectionOrigin::ImGuiNote);
    cur_entry.need_save = true;
    save_assembly_steps_json_to_model();
    return true;
}

bool AssemblyStepsUtils::add_text_label_note()
{
    auto *entries = get_current_kf_entries();
    if (!entries || m_keyframe_selected < 0 || m_keyframe_selected >= (int)entries->size())
        return false;
    KeyFrameEntry &cur_entry = (*entries)[m_keyframe_selected];
    cur_entry.data.assembly_note.text_labels.emplace_back();
    cur_entry.data.assembly_note.text_labels.back().color =
        note_color_from_palette_index(m_guide_note_color_selected);
    // Anchor the note to the currently selected volumes' on-screen bbox center.
    bind_current_selection_volumes(cur_entry.data.assembly_note.text_labels.back().bound_volumes);
    {
        // Inherit the alpha used historically for the label background so the
        // canvas underneath still shows through.
        std::array<int, 4> bg = note_color_from_palette_index(m_guide_note_bg_color_selected);
        bg[3] = 217;
        cur_entry.data.assembly_note.text_labels.back().background_color = bg;
    }
    m_note_selected_type = AssemblyNoteSelectionType::TextLabel;
    m_note_selected_idx = (int)cur_entry.data.assembly_note.text_labels.size() - 1;
    m_note_text_focus_request = m_note_selected_idx;
    m_note_text_focus_keep_cursor = false; // new label: drop caret at the end
    set_note_edit_controls_visible(true);
    set_selection_origin(SelectionOrigin::ImGuiNote);
    cur_entry.need_save = true;
    save_assembly_steps_json_to_model();
    do_commond_callback("request_extra_frame");
    return true;
}

bool AssemblyStepsUtils::add_circle_note()
{
    auto *entries = get_current_kf_entries();
    if (!entries || m_keyframe_selected < 0 || m_keyframe_selected >= (int)entries->size())
        return false;
    KeyFrameEntry &cur_entry = (*entries)[m_keyframe_selected];
    cur_entry.data.assembly_note.circle_notes.emplace_back();
    cur_entry.data.assembly_note.circle_notes.back().color =
        note_color_from_palette_index(m_guide_note_color_selected);
    // Anchor the note to the currently selected volumes' on-screen bbox center.
    bind_current_selection_volumes(cur_entry.data.assembly_note.circle_notes.back().bound_volumes);
    m_note_selected_type = AssemblyNoteSelectionType::Circle;
    m_note_selected_idx = (int)cur_entry.data.assembly_note.circle_notes.size() - 1;
    set_note_edit_controls_visible(true);
    set_selection_origin(SelectionOrigin::ImGuiNote);
    cur_entry.need_save = true;
    save_assembly_steps_json_to_model();
    return true;
}

bool AssemblyStepsUtils::add_rectangle_note()
{
    auto *entries = get_current_kf_entries();
    if (!entries || m_keyframe_selected < 0 || m_keyframe_selected >= (int)entries->size())
        return false;
    KeyFrameEntry &cur_entry = (*entries)[m_keyframe_selected];
    cur_entry.data.assembly_note.rectangle_notes.emplace_back();
    cur_entry.data.assembly_note.rectangle_notes.back().color =
        note_color_from_palette_index(m_guide_note_color_selected);
    // Anchor the note to the currently selected volumes' on-screen bbox center.
    bind_current_selection_volumes(cur_entry.data.assembly_note.rectangle_notes.back().bound_volumes);
    m_note_selected_type = AssemblyNoteSelectionType::Rectangle;
    m_note_selected_idx = (int)cur_entry.data.assembly_note.rectangle_notes.size() - 1;
    set_note_edit_controls_visible(true);
    set_selection_origin(SelectionOrigin::ImGuiNote);
    cur_entry.need_save = true;
    save_assembly_steps_json_to_model();
    return true;
}

bool AssemblyStepsUtils::add_plain_arrow_note()
{
    auto *entries = get_current_kf_entries();
    if (!entries || m_keyframe_selected < 0 || m_keyframe_selected >= (int)entries->size())
        return false;
    KeyFrameEntry &cur_entry = (*entries)[m_keyframe_selected];
    cur_entry.data.assembly_note.plain_arrows.emplace_back();
    cur_entry.data.assembly_note.plain_arrows.back().color =
        note_color_from_palette_index(m_guide_note_color_selected);
    // Anchor the arrow start to the currently selected volumes' on-screen bbox center.
    bind_current_selection_volumes(cur_entry.data.assembly_note.plain_arrows.back().bound_volumes);
    m_note_selected_type = AssemblyNoteSelectionType::PlainArrow;
    m_note_selected_idx = (int)cur_entry.data.assembly_note.plain_arrows.size() - 1;
    set_note_edit_controls_visible(true);
    set_selection_origin(SelectionOrigin::ImGuiNote);
    cur_entry.need_save = true;
    save_assembly_steps_json_to_model();
    return true;
}

void AssemblyStepsUtils::toggle_part_number_labels(bool user_initiated)
{
    auto *entries = get_current_kf_entries();
    if (!entries || m_keyframe_selected < 0 || m_keyframe_selected >= (int)entries->size())
        return;
    KeyFrameEntry &cur_entry = (*entries)[m_keyframe_selected];
    toggle_part_number_labels_to_keyframe(cur_entry, user_initiated);
}

void AssemblyStepsUtils::set_labels_show_type(LabelsShowType type, bool reframe_camera)
{
    auto *entries = get_current_kf_entries();
    if (!entries || m_keyframe_selected < 0 || m_keyframe_selected >= (int)entries->size())
        return;
    // Re-selecting the active type is allowed: it re-runs the label rebuild +
    // auto-arrange so the user can re-trigger the layout on demand.
    m_cur_labels_show_type = type;
    KeyFrameEntry &cur_entry = (*entries)[m_keyframe_selected];
    cur_entry.data.labels_show_type = type;
    // Picking a label type implies the labels should be visible; the rebuild
    m_guide_show_part_numbers = true;
    toggle_part_number_labels_to_keyframe(cur_entry, true, reframe_camera);
}

void AssemblyStepsUtils::auto_recommend_camera_for_current_view()
{
    auto *entries = get_current_kf_entries();
    if (!entries || m_keyframe_selected < 0 || m_keyframe_selected >= (int)entries->size())
        return;
    KeyFrameEntry &cur_entry = (*entries)[m_keyframe_selected];
    // Mirror the camera half of toggle_part_number_labels_to_keyframe: reframe to
    // the recommended angle and persist it into this keyframe, but leave the
    // part-number labels untouched (no rebuild, no auto-arrange).
    const double used_margin = cur_entry.data.camera_margin_factor;
    fit_camera_to_current_step_main_plane(used_margin);
    record_camera(cur_entry.data);
    cur_entry.data.camera_margin_factor = used_margin;
    cur_entry.data.is_camera_define     = true;
    cur_entry.data.camera_user_defined  = true;
    cur_entry.need_save = true;
    save_assembly_steps_json_to_model();
    do_commond_callback("dirty");
    do_commond_callback("request_extra_frame");
}

void AssemblyStepsUtils::auto_layout_labels_in_current_view()
{
    if (!m_guide_show_part_numbers)
        return;
    m_pn_autolayout_pending = true;
    do_commond_callback("dirty");
    do_commond_callback("request_extra_frame");
}

void AssemblyStepsUtils::begin_part_label_rename(const PartNumberLabel &lbl)
{
    m_pn_label_rename_guid          = lbl.part_guid;
    m_pn_label_rename_object_idx    = lbl.object_idx;
    m_pn_label_rename_buf           = lbl.part_name;
    m_pn_label_rename_focus_pending = true;
}

void AssemblyStepsUtils::commit_part_label_rename()
{
    const bool has_guid = !m_pn_label_rename_guid.empty();
    const bool has_obj  = m_pn_label_rename_object_idx >= 0;
    if (!has_guid && !has_obj)
        return;

    const std::string new_name = m_pn_label_rename_buf;
    const std::string guid     = m_pn_label_rename_guid;
    const int         obj_idx  = m_pn_label_rename_object_idx;
    // Drop the edit state up front so the save/callbacks below cannot re-enter
    // this commit path.
    m_pn_label_rename_guid.clear();
    m_pn_label_rename_object_idx = -1;

    if (has_guid) {
        if (!rename_model_item_from_label(guid, -1, new_name))
            return;
    } else {
        if (!rename_model_item_from_label("", obj_idx, new_name))
            return;
    }

    // Read back the committed name after the model was updated.
    const std::string committed = [&]() -> std::string {
        if (has_guid) {
            for (const ModelObject *obj : m_model->objects) {
                if (!obj) continue;
                for (const ModelVolume *vol : obj->volumes) {
                    if (vol && vol->ensure_part_guid() == guid)
                        return vol->name;
                }
            }
        } else if (obj_idx >= 0 && obj_idx < (int) m_model->objects.size()) {
            return m_model->objects[obj_idx]->name;
        }
        return std::string();
    }();

    // Propagate the renamed label to every keyframe across all step nodes,
    // so the part name is consistent in every frame, not just the current one.
    bool any_dirty = false;
    for (auto &node : _steps_nodes) {
        for (auto &entry : node.kf_data.entries) {
            bool entry_dirty = false;
            for (auto &lbl : entry.data.assembly_note.part_number_labels) {
                bool match = has_guid ? (lbl.part_guid == guid)
                                      : (lbl.object_idx == obj_idx && lbl.volume_idx < 0);
                if (match) {
                    lbl.part_name = committed;
                    entry_dirty   = true;
                }
            }
            if (entry_dirty) {
                entry.need_save = true;
                any_dirty       = true;
            }
        }
    }
    if (any_dirty)
        save_assembly_steps_json_to_model();
    do_commond_callback("dirty");
    do_commond_callback("request_extra_frame");
}

void AssemblyStepsUtils::begin_tree_item_rename(int object_idx, int volume_idx, const std::string &name)
{
    m_tree_item_rename_object_idx    = object_idx;
    m_tree_item_rename_volume_idx    = volume_idx;
    m_tree_item_rename_buf           = name;
    m_tree_item_rename_focus_pending = true;
}

bool AssemblyStepsUtils::rename_model_item_from_label(const std::string &part_guid, int object_idx, const std::string &new_name)
{
    if (!m_model)
        return false;

    std::string trimmed = new_name;
    boost::trim(trimmed);
    if (trimmed.empty())
        return false;

    if (!part_guid.empty()) {
        // Volume-level rename: find in the plater's model (source of truth)
        // and update ObjectList; also mirror to the assembly model.
        Model &prepare_model = wxGetApp().model();
        int prep_oi = -1, prep_vi = -1;
        for (int oi = 0; oi < (int) prepare_model.objects.size(); ++oi) {
            if (prepare_model.objects[oi] == nullptr) continue;
            for (int vi = 0; vi < (int) prepare_model.objects[oi]->volumes.size(); ++vi) {
                if (prepare_model.objects[oi]->volumes[vi] != nullptr &&
                    prepare_model.objects[oi]->volumes[vi]->ensure_part_guid() == part_guid) {
                    prep_oi = oi;
                    prep_vi = vi;
                    break;
                }
            }
            if (prep_oi >= 0) break;
        }
        if (prep_oi < 0 || prep_vi < 0)
            return false;
        if (prepare_model.objects[prep_oi]->volumes[prep_vi]->name == trimmed)
            return false;
        prepare_model.objects[prep_oi]->volumes[prep_vi]->name = trimmed;
        if (ObjectList *obj_list = wxGetApp().obj_list())
            obj_list->sync_name_from_model(prep_oi, prep_vi);

        // Mirror the rename in the assembly model's copy and patch tree labels
        int asm_oi = -1, asm_vi = -1;
        for (int oi = 0; oi < (int) m_model->objects.size(); ++oi) {
            if (m_model->objects[oi] == nullptr) continue;
            for (int vi = 0; vi < (int) m_model->objects[oi]->volumes.size(); ++vi) {
                if (m_model->objects[oi]->volumes[vi] != nullptr &&
                    m_model->objects[oi]->volumes[vi]->ensure_part_guid() == part_guid) {
                    asm_oi = oi;
                    asm_vi = vi;
                    break;
                }
            }
            if (asm_oi >= 0) break;
        }
        if (asm_oi >= 0 && asm_vi >= 0) {
            m_model->objects[asm_oi]->volumes[asm_vi]->name = trimmed;
            auto patch_tree_label = [asm_oi, asm_vi, &trimmed](AssemblyTreeData &tree) {
                for (auto &node : tree.nodes) {
                    if (node.object_idx == asm_oi && node.volume_idx == asm_vi)
                        node.label = trimmed;
                }
            };
            patch_tree_label(m_model->get_assembly_tree_data());
            patch_tree_label(m_structure_select_popup_tree);
        }
        return true;
    }

    // Object-level rename: only in the assembly model (no ObjectList sync)
    if (object_idx < 0 || object_idx >= (int) m_model->objects.size())
        return false;
    ModelObject *obj = m_model->objects[object_idx];
    if (obj == nullptr)
        return false;
    if (obj->name == trimmed)
        return false;
    obj->name = trimmed;
    auto patch_tree_label = [object_idx, &trimmed](AssemblyTreeData &tree) {
        for (auto &node : tree.nodes) {
            if (node.object_idx == object_idx && node.volume_idx < 0)
                node.label = trimmed;
        }
    };
    patch_tree_label(m_model->get_assembly_tree_data());
    patch_tree_label(m_structure_select_popup_tree);
    return true;
}

void AssemblyStepsUtils::on_prepare_volume_renamed(int object_idx, int volume_idx, const std::string &new_name)
{
    if (m_model == nullptr || object_idx < 0 || object_idx >= (int) m_model->objects.size())
        return;
    ModelObject *obj = m_model->objects[object_idx];
    if (obj == nullptr || volume_idx < 0 || volume_idx >= (int) obj->volumes.size())
        return;
    if (obj->volumes[volume_idx] == nullptr)
        return;

    obj->volumes[volume_idx]->name = new_name;

    auto patch = [object_idx, volume_idx, &new_name](AssemblyTreeData &tree) {
        for (auto &node : tree.nodes) {
            if (node.object_idx == object_idx && node.volume_idx == volume_idx)
                node.label = new_name;
        }
    };
    patch(m_model->get_assembly_tree_data());
    patch(m_structure_select_popup_tree);
    do_commond_callback("request_extra_frame");
}

void AssemblyStepsUtils::update_part_number_label_font_size_from_config()
{
    m_part_number_label_font_size = 0.0f;
    if (!wxGetApp().app_config)
        return;

    const std::string value = wxGetApp().app_config->get(ASSEMBLY_LABEL_FONT_SIZE_CONFIG_KEY);
    if (value.empty())
        return;

    try {
        size_t parsed = 0;
        const float font_size = std::stof(value, &parsed);
        if (parsed == 0 || !std::isfinite(font_size) || font_size <= 0.0f)
            return;
        m_part_number_label_font_size = std::clamp(font_size,
            ASSEMBLY_LABEL_DEFAULT_FONT_SIZE,
            ASSEMBLY_LABEL_DEFAULT_FONT_SIZE * ASSEMBLY_LABEL_MAX_FONT_SIZE_FACTOR);
    } catch (...) {
        m_part_number_label_font_size = 0.0f;
    }
}

float AssemblyStepsUtils::part_number_label_font_size() const
{
    return m_part_number_label_font_size > 0.0f ?
        m_part_number_label_font_size :
        ASSEMBLY_LABEL_DEFAULT_FONT_SIZE;
}

void AssemblyStepsUtils::save_part_number_label_font_size_to_config(float font_size, bool save_now)
{
    if (!wxGetApp().app_config)
        return;

    const float clamped_font_size = std::clamp(font_size,
        ASSEMBLY_LABEL_DEFAULT_FONT_SIZE,
        ASSEMBLY_LABEL_DEFAULT_FONT_SIZE * ASSEMBLY_LABEL_MAX_FONT_SIZE_FACTOR);
    m_part_number_label_font_size = clamped_font_size;

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << clamped_font_size;
    wxGetApp().app_config->set(ASSEMBLY_LABEL_FONT_SIZE_CONFIG_KEY, ss.str());
    if (save_now)
        wxGetApp().app_config->save();
}

void AssemblyStepsUtils::collect_part_number_label_refs(int collect_root,
        const std::function<bool(int)> &as_object_label, std::vector<PartNumberLabel> &out) const
{
    if (!m_model)
        return;

    // Prefer the step's volume-level membership (assembly_tree_checked). Falls
    // back to every volume of the folder's object children when unchecked.
    const std::set<std::pair<int, int>> vol_pairs = collect_folder_volume_pairs(collect_root);
    if (vol_pairs.empty())
        return;

    std::map<int, std::vector<int>> vols_by_obj;
    std::vector<int>                obj_order;
    for (const auto &p : vol_pairs) {
        if (vols_by_obj.find(p.first) == vols_by_obj.end())
            obj_order.push_back(p.first);
        vols_by_obj[p.first].push_back(p.second);
    }

    std::set<int> seen_objs;
    const int     object_count = (int) m_model->objects.size();
    for (int object_idx : obj_order) {
        if (object_idx < 0 || object_idx >= object_count)
            continue;
        const ModelObject *obj = m_model->objects[object_idx];
        if (!obj)
            continue;
        if (as_object_label(object_idx)) {
            if (seen_objs.insert(object_idx).second) {
                PartNumberLabel lbl;
                lbl.object_idx = object_idx;
                lbl.volume_idx = -1;
                lbl.part_name  = obj->name;
                out.push_back(std::move(lbl));
            }
        } else {
            for (int vi : vols_by_obj[object_idx]) {
                if (vi < 0 || vi >= (int) obj->volumes.size() || !obj->volumes[vi])
                    continue;
                PartNumberLabel lbl;
                lbl.object_idx = object_idx;
                lbl.volume_idx = vi;
                lbl.part_name  = obj->volumes[vi]->name;
                lbl.part_guid  = obj->volumes[vi]->ensure_part_guid();
                out.push_back(std::move(lbl));
            }
        }
    }
}

void AssemblyStepsUtils::build_part_number_labels_object_only(int collect_root, std::vector<PartNumberLabel> &out) const
{
    collect_part_number_label_refs(collect_root, [](int) { return true; }, out);
}

void AssemblyStepsUtils::build_part_number_labels_volume_only(int collect_root, std::vector<PartNumberLabel> &out) const
{
    collect_part_number_label_refs(collect_root, [](int) { return false; }, out);
}

void AssemblyStepsUtils::build_part_number_labels_auto(int collect_root, bool object_level_only, std::vector<PartNumberLabel> &out) const
{
    // Collapse to one object label only when the object is fully present in this
    // step. Partial ModelVolume membership must stay at part-level labels — never
    // show a single Object label for a handful of volumes.
    const bool collapse_repeated_objects = m_show_modelobject_name_when_modelobject_has_occur_before;
    const std::set<std::pair<int, int>> cur_pairs = collect_folder_volume_pairs(collect_root);

    auto object_fully_in_pairs = [&](int object_idx, const std::set<std::pair<int, int>> &pairs) -> bool {
        if (!m_model || object_idx < 0 || object_idx >= (int) m_model->objects.size())
            return false;
        const ModelObject *obj = m_model->objects[object_idx];
        if (!obj)
            return false;
        int selectable = 0;
        for (int vi = 0; vi < (int) obj->volumes.size(); ++vi) {
            if (!obj->volumes[vi])
                continue;
            ++selectable;
            if (pairs.count({object_idx, vi}) == 0)
                return false;
        }
        return selectable > 0;
    };

    // "Already used" for Auto collapse means fully used in an earlier step, not
    // merely present as a few volumes under an Object node.
    auto object_fully_used_before = [&](int object_idx) -> bool {
        for (int root_idx : _steps_roots) {
            if (root_idx == collect_root)
                break;
            if (root_idx < 0 || root_idx >= (int) _steps_nodes.size())
                continue;
            if (_steps_nodes[root_idx].is_final_assembly)
                continue;
            if (object_fully_in_pairs(object_idx, collect_folder_volume_pairs(root_idx)))
                return true;
        }
        return false;
    };

    collect_part_number_label_refs(collect_root,
        [&](int object_idx) {
            if (!object_fully_in_pairs(object_idx, cur_pairs))
                return false;
            return object_level_only ||
                   (collapse_repeated_objects && object_fully_used_before(object_idx));
        },
        out);
}

bool AssemblyStepsUtils::auto_layout_part_number_labels(std::vector<PartNumberLabel> &pn_labels,
        const Camera &camera, const std::array<int, 4> &viewport, float sc)
{
    if (pn_labels.empty() || !m_volumes)
        return false;
    update_part_number_label_font_size_from_config();

    const Matrix4d w2s = camera.get_projection_matrix().matrix() * camera.get_view_matrix().matrix();
    // Project an arbitrary world point to ImGui screen coords (Y-down).
    auto project_pt = [&](const Vec3d &p) -> Vec2d {
        Vec4d ndc = w2s * Vec4d(p.x(), p.y(), p.z(), 1.0);
        if (std::abs(ndc.w()) < 1e-9)
            return Vec2d(viewport[2] * 0.5, viewport[3] * 0.5);
        return Vec2d(0.5 * (1.0 + ndc.x() / ndc.w()) * viewport[2],
                     0.5 * (1.0 - ndc.y() / ndc.w()) * viewport[3]);
    };
    // 3D union bbox of the volume(s) a label points at.
    auto pn_vol_bbox3 = [&](int obj_idx, int vol_idx) -> BoundingBoxf3 {
        BoundingBoxf3 bb;
        for (const GLVolume *vol : m_volumes->volumes) {
            if (!vol || !vol->is_active)
                continue;
            if (vol->composite_id.object_id != obj_idx)
                continue;
            if (vol_idx >= 0 && vol->composite_id.volume_id != vol_idx)
                continue;
            bb.merge(vol->transformed_bounding_box());
            if (vol_idx >= 0)
                break;
        }
        return bb;
    };
    const float pn_font_sz = part_number_label_font_size() * sc;
    const float pn_pad_h   = 16.0f * sc;
    const float pn_pad_v   =  6.0f * sc;
    ImFont     *pn_font    = ImGui::GetFont();

    // One-shot auto-arrange: bucket every label onto one of the four sides of
    // the step's on-screen bbox (top/bottom/left/right), then stack the labels
    // of each side along its rail so the pills never overlap.
    const int                  label_count = (int) pn_labels.size();
    std::vector<BoundingBoxf3> item_bb(label_count);
    BoundingBoxf3              overall;
    for (int i = 0; i < label_count; ++i) {
        item_bb[i] = pn_vol_bbox3(pn_labels[i].object_idx, pn_labels[i].volume_idx);
        if (item_bb[i].defined)
            overall.merge(item_bb[i]);
    }
    if (!overall.defined)
        return false;

    // Per-label screen metrics: projected center, the part's on-screen bbox and
    // the pill size we are about to place.
    struct LabelMetric {
        int   idx{-1};
        Vec2d ci{Vec2d::Zero()};
        float pw{0.f};
        float ph{0.f};
    };
    std::vector<LabelMetric> metrics;
    metrics.reserve(label_count);
    Vec2d screen_min(DBL_MAX, DBL_MAX), screen_max(-DBL_MAX, -DBL_MAX);

    for (int i = 0; i < label_count; ++i) {
        if (!item_bb[i].defined)
            continue;
        const Vec3d mn = item_bb[i].min;
        const Vec3d mx = item_bb[i].max;
        Vec2d smin(DBL_MAX, DBL_MAX), smax(-DBL_MAX, -DBL_MAX);
        for (int c = 0; c < 8; ++c) {
            const Vec3d corner((c & 1) ? mx.x() : mn.x(),
                               (c & 2) ? mx.y() : mn.y(),
                               (c & 4) ? mx.z() : mn.z());
            const Vec2d s = project_pt(corner);
            smin.x() = std::min(smin.x(), s.x());
            smin.y() = std::min(smin.y(), s.y());
            smax.x() = std::max(smax.x(), s.x());
            smax.y() = std::max(smax.y(), s.y());
        }
        const ImVec2 tsz = pn_font->CalcTextSizeA(pn_font_sz, FLT_MAX, 0.0f,
                                                  pn_labels[i].part_name.c_str());
        LabelMetric m;
        m.idx = i;
        m.ci  = project_pt(item_bb[i].center());
        m.pw  = tsz.x + pn_pad_h * 2.0f;
        m.ph  = tsz.y + pn_pad_v * 2.0f;
        metrics.push_back(m);

        screen_min.x() = std::min(screen_min.x(), smin.x());
        screen_min.y() = std::min(screen_min.y(), smin.y());
        screen_max.x() = std::max(screen_max.x(), smax.x());
        screen_max.y() = std::max(screen_max.y(), smax.y());
    }
    if (metrics.empty())
        return false;

    const double gap    = 30.0;     // model bbox -> rail distance (px)
    const double vgap   = 6.0 * sc; // min gap between stacked pills
    const double margin = 8.0;      // keep pills inside the viewport
    const double vw     = (double) viewport[2];
    const double vh     = (double) viewport[3];

    // Representative pill size used both for the per-rail capacity and for
    // clamping the rails away from the viewport edges.
    double ph_rep = 1.0, pw_rep = 1.0;
    for (const LabelMetric &m : metrics) {
        ph_rep = std::max(ph_rep, (double) m.ph);
        pw_rep = std::max(pw_rep, (double) m.pw);
    }

    // Rail lines, pre-clamped so a full-size pill cannot fall off the viewport.
    const double rail_x_right  = std::min(screen_max.x() + gap, vw - margin - pw_rep);
    const double rail_x_left   = std::max(screen_min.x() - gap, margin + pw_rep);
    const double rail_y_bottom = std::min(screen_max.y() + gap, vh - margin - ph_rep);
    const double rail_y_top    = std::max(screen_min.y() - gap, margin + ph_rep);

    // How many pills each rail can hold without overlapping.
    const int cap_v = std::max(1, (int) ((vh - 2.0 * margin) / std::max(1.0, ph_rep + vgap)));
    const int cap_h = std::max(1, (int) ((vw - 2.0 * margin) / std::max(1.0, pw_rep + vgap)));
    const std::array<int, 4> rail_cap = {cap_v, cap_v, cap_h, cap_h}; // right,left,bottom,top

    // Leader length if a label is sent to a given rail.
    auto side_cost = [&](int k, int side) -> double {
        const Vec2d &ci = metrics[k].ci;
        switch (side) {
        case 0:  return std::abs(rail_x_right  - ci.x());
        case 1:  return std::abs(ci.x() - rail_x_left);
        case 2:  return std::abs(rail_y_bottom - ci.y());
        default: return std::abs(ci.y() - rail_y_top);
        }
    };

    // Capacity-aware greedy assignment: every label prefers the rail with the
    // shortest leader, but each rail only holds rail_cap pills; the overflow
    // spills to the next-cheapest rail.
    struct Pref { double cost; int k; int side; };
    std::vector<Pref> prefs;
    prefs.reserve(metrics.size() * 4);
    for (int k = 0; k < (int) metrics.size(); ++k)
        for (int s = 0; s < 4; ++s)
            prefs.push_back({side_cost(k, s), k, s});
    std::sort(prefs.begin(), prefs.end(),
              [](const Pref &a, const Pref &b) { return a.cost < b.cost; });

    std::vector<int>   assigned(metrics.size(), -1);
    std::array<int, 4> used = {0, 0, 0, 0};
    int                remaining = (int) metrics.size();
    for (const Pref &p : prefs) {
        if (remaining == 0) break;
        if (assigned[p.k] >= 0 || used[p.side] >= rail_cap[p.side])
            continue;
        assigned[p.k] = p.side;
        ++used[p.side];
        --remaining;
    }
    // Total capacity exceeded (very many labels): drop the rest onto the
    // least-loaded rail so nothing is left unplaced.
    for (int k = 0; k < (int) metrics.size() && remaining > 0; ++k) {
        if (assigned[k] >= 0)
            continue;
        int best = 0;
        for (int s = 1; s < 4; ++s)
            if (used[s] < used[best]) best = s;
        assigned[k] = best;
        ++used[best];
        --remaining;
    }

    // Side buckets: 0=right, 1=left, 2=bottom, 3=top.
    std::array<std::vector<int>, 4> buckets;
    for (int k = 0; k < (int) metrics.size(); ++k)
        buckets[assigned[k]].push_back(k);

    // Clamp a pill center so the whole pill stays inside the viewport.
    auto clamp_center = [&](double cx, double cy, double pw, double ph) -> Vec2d {
        const double hx = pw * 0.5, hy = ph * 0.5;
        cx = std::min(std::max(cx, margin + hx), vw - margin - hx);
        cy = std::min(std::max(cy, margin + hy), vh - margin - hy);
        return Vec2d(cx, cy);
    };
    // Rectangles already claimed by placed pills, plus the two forbidden UI
    const float lbl_pad = 2.0f * sc; // breathing room between adjacent pills
    std::vector<std::pair<ImVec2, ImVec2>> occupied;
    auto seed_forbidden = [&](const LabelLayoutForbiddenRect &area) {
        if (area.max.x > area.min.x && area.max.y > area.min.y)
            occupied.emplace_back(area.min, area.max);
    };
    seed_forbidden(m_part_number_label_forbidden_left_area);
    seed_forbidden(m_part_number_label_forbidden_bottom_area);
    if (screen_max.x() > screen_min.x() && screen_max.y() > screen_min.y())
        occupied.emplace_back(ImVec2((float) screen_min.x(), (float) screen_min.y()),
                              ImVec2((float) screen_max.x(), (float) screen_max.y()));

    auto center_to_rect = [&](const Vec2d &center, double pw, double ph) {
        const float hw = static_cast<float>(pw * 0.5) + lbl_pad;
        const float hh = static_cast<float>(ph * 0.5) + lbl_pad;
        return std::make_pair(ImVec2(static_cast<float>(center.x()) - hw,
                                     static_cast<float>(center.y()) - hh),
                              ImVec2(static_cast<float>(center.x()) + hw,
                                     static_cast<float>(center.y()) + hh));
    };
    auto rect_is_blocked = [&](const Vec2d &center, double pw, double ph) -> bool {
        const auto r = center_to_rect(center, pw, ph);
        for (const auto &o : occupied)
            if (rects_overlap(r.first, r.second, o.first, o.second))
                return true;
        return false;
    };
    // Find a free center for a pill: keep the preferred spot if it is clear,
    auto resolve_free_center = [&](const Vec2d &preferred, double pw, double ph, bool vertical_rail) -> Vec2d {
        Vec2d best = clamp_center(preferred.x(), preferred.y(), pw, ph);
        if (!rect_is_blocked(best, pw, ph))
            return best;

        const double step_axis  = std::max(4.0, (vertical_rail ? ph : pw) + vgap);
        const double limit_axis  = vertical_rail ? vh : vw;
        const int    max_steps   = std::max(1, (int) std::ceil(limit_axis / step_axis) + 1);
        for (int i = 1; i <= max_steps; ++i) {
            for (double sign : {-1.0, 1.0}) {
                const double dx = vertical_rail ? 0.0 : sign * step_axis * i;
                const double dy = vertical_rail ? sign * step_axis * i : 0.0;
                Vec2d candidate = clamp_center(preferred.x() + dx, preferred.y() + dy, pw, ph);
                if (!rect_is_blocked(candidate, pw, ph))
                    return candidate;
            }
        }

        // 2D ring search across the viewport: expanding square rings around the
        // preferred center, scanning only each ring's perimeter cells.
        const double sx       = std::max(8.0, pw * 0.5);
        const double sy       = std::max(8.0, ph * 0.5);
        const int    max_ring = std::max(1, (int) std::ceil(std::max(vw, vh) / std::min(sx, sy)) + 1);
        for (int ring = 1; ring <= max_ring; ++ring) {
            for (int gx = -ring; gx <= ring; ++gx) {
                for (int gy = -ring; gy <= ring; ++gy) {
                    if (std::max(std::abs(gx), std::abs(gy)) != ring)
                        continue; // perimeter only
                    Vec2d candidate = clamp_center(preferred.x() + gx * sx,
                                                   preferred.y() + gy * sy, pw, ph);
                    if (!rect_is_blocked(candidate, pw, ph))
                        return candidate;
                }
            }
        }
        return best; // viewport saturated: stay clamped on-screen
    };
    auto claim_rect = [&](const Vec2d &center, double pw, double ph) {
        occupied.push_back(center_to_rect(center, pw, ph));
    };

    // Stack a vertical rail (right/left side): pills laid out top to bottom,
    // sorted by their part's screen Y, spaced so they never overlap.
    auto place_vertical_rail = [&](std::vector<int> &bucket, double rail_x, bool to_right) {
        if (bucket.empty())
            return;
        std::sort(bucket.begin(), bucket.end(),
                  [&](int a, int b) { return metrics[a].ci.y() < metrics[b].ci.y(); });
        const int    n     = (int) bucket.size();
        const double avail = std::max(1.0, vh - 2.0 * margin);
        double sum_h = 0.0;
        for (int k : bucket) sum_h += metrics[k].ph;
        double eff_vgap = vgap;
        if (n > 1 && sum_h + vgap * (n - 1) > avail)
            eff_vgap = std::max(0.0, (avail - sum_h) / (double) (n - 1));
        const double total = sum_h + eff_vgap * (n - 1);
        double center_y = 0.0;
        for (int k : bucket) center_y += metrics[k].ci.y();
        center_y /= (double) n;
        double cursor = center_y - total * 0.5;
        cursor = std::max(cursor, margin);
        if (cursor + total > vh - margin)
            cursor = std::max(margin, vh - margin - total);
        for (int k : bucket) {
            LabelMetric &m = metrics[k];
            const double cx = to_right ? rail_x + m.pw * 0.5 : rail_x - m.pw * 0.5;
            const double cy = cursor + m.ph * 0.5;
            const Vec2d  c  = resolve_free_center(clamp_center(cx, cy, m.pw, m.ph), m.pw, m.ph, true);
            claim_rect(c, m.pw, m.ph);
            pn_labels[m.idx].arrow_start_offset = Vec2d::Zero();
            pn_labels[m.idx].arrow_end_offset   = c - m.ci;
            cursor += m.ph + eff_vgap;
        }
    };

    // Stack a horizontal rail (top/bottom side): same idea along X.
    auto place_horizontal_rail = [&](std::vector<int> &bucket, double rail_y, bool to_bottom) {
        if (bucket.empty())
            return;
        std::sort(bucket.begin(), bucket.end(),
                  [&](int a, int b) { return metrics[a].ci.x() < metrics[b].ci.x(); });
        const int    n     = (int) bucket.size();
        const double avail = std::max(1.0, vw - 2.0 * margin);
        double sum_w = 0.0;
        for (int k : bucket) sum_w += metrics[k].pw;
        double eff_gap = vgap;
        if (n > 1 && sum_w + vgap * (n - 1) > avail)
            eff_gap = std::max(0.0, (avail - sum_w) / (double) (n - 1));
        const double total = sum_w + eff_gap * (n - 1);
        double center_x = 0.0;
        for (int k : bucket) center_x += metrics[k].ci.x();
        center_x /= (double) n;
        double cursor = center_x - total * 0.5;
        cursor = std::max(cursor, margin);
        if (cursor + total > vw - margin)
            cursor = std::max(margin, vw - margin - total);
        for (int k : bucket) {
            LabelMetric &m = metrics[k];
            const double cx = cursor + m.pw * 0.5;
            const double cy = to_bottom ? rail_y + m.ph * 0.5 : rail_y - m.ph * 0.5;
            const Vec2d  c  = resolve_free_center(clamp_center(cx, cy, m.pw, m.ph), m.pw, m.ph, false);
            claim_rect(c, m.pw, m.ph);
            pn_labels[m.idx].arrow_start_offset = Vec2d::Zero();
            pn_labels[m.idx].arrow_end_offset   = c - m.ci;
            cursor += m.pw + eff_gap;
        }
    };

    place_vertical_rail(buckets[0], rail_x_right,    /*to_right*/ true);
    place_vertical_rail(buckets[1], rail_x_left,     /*to_right*/ false);
    place_horizontal_rail(buckets[2], rail_y_bottom, /*to_bottom*/ true);
    place_horizontal_rail(buckets[3], rail_y_top,    /*to_bottom*/ false);

    return true;
}

void AssemblyStepsUtils::toggle_part_number_labels_to_keyframe(KeyFrameEntry &src, bool user_initiated, bool reframe_camera)
{
    AssemblyNote &note = src.data.assembly_note;
    auto &labels = note.part_number_labels;
    // m_guide_show_part_numbers is already toggled by the checkbox before this function is called, so we use the current value directly.
    note.show_part_labels = m_guide_show_part_numbers;

    if (!m_guide_show_part_numbers) {
        src.need_save = true;
        save_assembly_steps_json_to_model();
        do_commond_callback("dirty");
        return;
    }
    const int collect_root = find_parent_folder(m_selected_node);
    if (!m_model || collect_root < 0 || collect_root >= (int) _steps_nodes.size())
        return;
    // Turning the checkbox back on rebuilds every label for the current frame so
    labels.clear();
    // Folder entry count of the step src belongs to; mirrors the old get_current_kf_entries()->size() check.
    auto        *folder_entries = get_current_kf_entries();
    const size_t folder_entry_count = folder_entries ? folder_entries->size() : 0;
    // Final-assembly _steps_ (the "All Objects" step) cover every ModelObject in
    const bool object_level_only = collect_root >= 0 && collect_root < (int) _steps_nodes.size() &&
                                   (_steps_nodes[collect_root].is_final_assembly ||
                                    (m_show_modelobject_name_when_modelobject_has_occur_before && folder_entry_count >= 2 && src.is_last()));

    // Prepare the label data per the current labels-show mode, then fall through
    // to the shared default-offset seeding + (optional) auto layout below.
    switch (m_cur_labels_show_type) {
    case LabelsShowType::OnlyModelObject:
        build_part_number_labels_object_only(collect_root, labels);
        break;
    case LabelsShowType::OnlyModelVolume:
        build_part_number_labels_volume_only(collect_root, labels);
        break;
    case LabelsShowType::AutoRecommend:
    default:
        build_part_number_labels_auto(collect_root, object_level_only, labels);
        break;
    }

    // Seed each pill with a radial default offset; the real positions come from
    // auto_layout_part_number_labels once m_pn_autolayout_pending is honored.
    const int n = (int) labels.size();
    for (int i = 0; i < n; ++i) {
        const double angle  = (n > 1) ? (2.0 * M_PI * i / n) : 0.0;
        const double radius = 80.0;
        labels[i].arrow_end_offset = Vec2d(radius * std::cos(angle), radius * std::sin(angle));
    }
    // Only an explicit user toggle auto-arranges labels, and only when
    // reframe_camera is set does it also reframe the step camera. The label
    // type rows pass reframe_camera == false so they relayout labels in place
    // without moving the camera.
    if (user_initiated) {
        if (reframe_camera) {
            // Every frame uses its own per-keyframe margin (persisted to the 3mf, so

            const double used_margin = src.data.camera_margin_factor;
            fit_camera_to_current_step_main_plane(used_margin);
            // Persist the freshly framed camera (and the margin used) into THIS keyframe.
            record_camera(src.data);
            src.data.camera_margin_factor = used_margin;
            src.data.is_camera_define = true;
        }
        m_pn_autolayout_pending = true;
    }

    src.need_save = true;
    save_assembly_steps_json_to_model();
    do_commond_callback("dirty");
    do_commond_callback("request_extra_frame");
}





Vec2d AssemblyStepsUtils::compute_selected_volumes_screen_center(
    const Camera &camera, const std::vector<GLVolume*> &volumes)
{
    const std::array<int, 4> &viewport = camera.get_viewport();
    Matrix4d world_to_screen = camera.get_projection_matrix().matrix() * camera.get_view_matrix().matrix();

    BoundingBoxf3 merged;
    bool found = false;
    for (const GLVolume *vol : volumes) {
        if (!vol || !vol->is_active)
            continue;
        if (!found) {
            merged = vol->transformed_bounding_box();
            found  = true;
        } else {
            merged.merge(vol->transformed_bounding_box());
        }
    }
    if (!found)
        return Vec2d(viewport[2] * 0.5, viewport[3] * 0.5);

    Vec4d center4(merged.center().x(), merged.center().y(), merged.center().z(), 1.0);
    Vec4d ndc = world_to_screen * center4;
    double x = 0.5 * (1.0 + ndc.x() / ndc.w()) * viewport[2];
    double y = 0.5 * (1.0 - ndc.y() / ndc.w()) * viewport[3];
    return Vec2d(x, y);
}

Vec2d AssemblyStepsUtils::compute_note_anchor_center(const std::vector<std::pair<int, int>> &bound_volumes, const Vec2d &fallback_center)
{
    if (bound_volumes.empty() || !m_camera || !m_volumes)
        return fallback_center;

    // Collect the live GLVolumes whose (object, volume) matches the bound set, so
    // the anchor uses their current (per-keyframe) transformed positions.
    std::vector<GLVolume *> bound;
    for (GLVolume *vol : m_volumes->volumes) {
        if (!vol || !vol->is_active)
            continue;
        for (const auto &key : bound_volumes) {
            if (vol->object_idx() == key.first && vol->volume_idx() == key.second) {
                bound.push_back(vol);
                break;
            }
        }
    }
    if (bound.empty())
        return fallback_center;
    return compute_selected_volumes_screen_center(*m_camera, bound);
}

Vec2d AssemblyStepsUtils::compute_arrow_svg_anchor_center(const ArrowSvgNote &arrow, const Vec2d &fallback_center)
{
    return compute_note_anchor_center(arrow.bound_volumes, fallback_center);
}

void AssemblyStepsUtils::bind_current_selection_volumes(std::vector<std::pair<int, int>> &bound_volumes) const
{
    bound_volumes.clear();
    if (m_selection != nullptr) {
        for (unsigned int idx : m_selection->get_volume_idxs()) {
            const GLVolume *v = m_selection->get_volume(idx);
            if (v != nullptr)
                bound_volumes.emplace_back(v->object_idx(), v->volume_idx());
        }
    }
    if (!bound_volumes.empty())
        return;

    // Canvas selection is often empty after List OK (m_select_all_when_click_in_step_card
    // defaults to false). Fall back to the current step's volume membership so
    // clip/glue/screw/text notes still anchor to the parts in this step.
    const int folder = find_parent_folder(m_selected_node);
    if (folder < 0)
        return;
    for (const auto &key : collect_folder_volume_pairs(folder))
        bound_volumes.push_back(key);
}

void AssemblyStepsUtils::deal_once_when_enter_assembly_view() {
    if (!m_model) { return; }
    // If the previous session left playback paused on the title overlay, exit it.
    exit_title_mode_if_paused();
    if (!AssemblyTreeData::show_origin_step_tree) {
        // Sync only when the final-assembly end frame no longer matches the live

        // m_last_recorded_* is runtime-only (not persisted), so after a fresh 3mf
        AssemblyStepsTreeData &steps_tree = m_model->get_assembly_steps_tree_data();
        if (m_last_recorded_volumes.empty() &&
            steps_tree.has_loaded_recorded_baseline) {
            m_last_recorded_volumes = steps_tree.loaded_recorded_volumes;
            steps_tree.loaded_recorded_volumes.clear();
            // Consume the baseline so later genuine model edits are still detected.
            steps_tree.has_loaded_recorded_baseline = false;
        }

        if (!final_assembly_end_frame_matches_model()) {//(is_model_object_tree_changed(model_object_tree, temp_model_object_tree)) {
            record_current_model_as_last_final_assembly();
            sync_all_model_object_to_final_assembly_node();
            sync_steps_objects_with_model();
            clear_all_keyframe_part_number_labels();
            m_model->set_assembly_tree_data(build_model_object_tree_data());
            clear_selected_node();
            exit_assembly_steps_editing();
            invalidate_play_frame_refs();
        }else{
            if (has_selected_node()) {
                exit_assembly_steps_editing(); // Avoid choosing a starting frame that doesn't match
            }
        }
        const AssemblyTreeData &tree = m_model->get_assembly_tree_data();
        if (tree.empty()){
            m_model->set_assembly_tree_data(build_model_object_tree_data());
        }
        if (!has_selected_node()) {
            do_commond_callback("zoom_to_volumes");
        }
    }else{//todo
    }

    // The assembly canvas selection may have been mapped from the prepare view before the first
    // Assembly list render. Seed the list highlight now so the standalone "Assembly list" opens in
    // sync with the current scene selection instead of waiting for a later mouse event.
    if (m_selection && !m_selection->is_empty())
        seed_tree_selected_items_from_canvas(m_model->get_assembly_tree_data());
    else
        m_assembly_tree_selected_items.clear();

#if !BBL_RELEASE_TO_PUBLIC
    // m_play_video_and_show_panels_debug = true;
    BOOST_LOG_TRIVIAL(info) << "AssemblySteps enter cards: _steps_nodes=" << _steps_nodes.size() << " roots=" << _steps_roots.size();
    int card_idx = 0;
    for (int root_idx : _steps_roots) {
        if (root_idx < 0 || root_idx >= (int) _steps_nodes.size()) {
            BOOST_LOG_TRIVIAL(info) << "  card[" << card_idx++ << "] invalid root=" << root_idx;
            continue;
        }

        const auto &root = _steps_nodes[root_idx];
        if (root.type != AssemblyStepsTreeNode::Type::Folder) {
            BOOST_LOG_TRIVIAL(info) << "  card[" << card_idx++ << "] non-folder root=" << root_idx
                                    << " name=\"" << root.name << "\"";
            continue;
        }

        std::set<int> obj_set;
        std::function<void(int)> collect_objects = [&](int idx) {
            if (idx < 0 || idx >= (int) _steps_nodes.size()) return;
            const auto &node = _steps_nodes[idx];
            if (node.type == AssemblyStepsTreeNode::Type::Object && node.object_idx >= 0)
                obj_set.insert(node.object_idx);
            for (int child_idx : node.children)
                collect_objects(child_idx);
        };
        collect_objects(root_idx);

        const int step_num = root.step > 0 ? root.step : card_idx + 1;
        BOOST_LOG_TRIVIAL(info) << "  card[" << card_idx++ << "] "
                                << (root.is_final_assembly ? "Default" : "Step " + std::to_string(step_num))
                                << " title=\"" << (root.name.empty() ? "Assembly Module" : root.name) << "\""
                                << " node=" << root_idx
                                << " id=" << root.id
                                << " step=" << root.step
                                << " is_final_assembly=" << root.is_final_assembly
                                << " object_count=" << obj_set.size()
                                << " keyframes=" << root.kf_data.entries.size();
        for (int obj_idx : obj_set) {
            std::string obj_name = "Object " + std::to_string(obj_idx + 1);
            if (obj_idx >= 0 && obj_idx < (int)m_model->objects.size() && m_model->objects[obj_idx])
                obj_name = m_model->objects[obj_idx]->name.empty() ? obj_name : m_model->objects[obj_idx]->name;
            BOOST_LOG_TRIVIAL(info) << "      object[" << obj_idx << "] \"" << obj_name << "\"";
        }
    }
#endif

    // [DIAG] dump the assembly model state right after entering the assembly view.
    log_assembly_model("enter-assembly-view");
}

void AssemblyStepsUtils::log_assembly_model(const char *tag) const
{
    if (!m_model) {
        BOOST_LOG_TRIVIAL(warning) << "[assemble-model][" << (tag ? tag : "") << "] m_model == null";
        return;
    }
#if !BBL_RELEASE_TO_PUBLIC
    BOOST_LOG_TRIVIAL(warning) << "[assemble-model][" << (tag ? tag : "") << "] objects=" << m_model->objects.size();
    for (int oi = 0; oi < (int) m_model->objects.size(); ++oi) {
        const ModelObject *obj = m_model->objects[oi];
        if (!obj) {
            BOOST_LOG_TRIVIAL(warning) << "  obj[" << oi << "] <null>";
            continue;
        }
        BOOST_LOG_TRIVIAL(warning) << "  obj[" << oi << "] \"" << obj->name << "\""
                                   << " id=" << obj->id().id
                                   << " instances=" << obj->instances.size()
                                   << " volumes=" << obj->volumes.size();
        for (int vi = 0; vi < (int) obj->volumes.size(); ++vi) {
            const ModelVolume *mv = obj->volumes[vi];
            if (!mv) {
                BOOST_LOG_TRIVIAL(warning) << "      vol[" << vi << "] <null>";
                continue;
            }
            const std::string pg  = mv->part_guid().empty() ? std::string("<empty>") : mv->part_guid();
            const std::string src = mv->assembly_src_guid().empty() ? std::string("<empty>") : mv->assembly_src_guid();
            BOOST_LOG_TRIVIAL(warning) << "      vol[" << vi << "] \"" << mv->name << "\""
                                       << " id=" << mv->id().id
                                       << " model_part=" << mv->is_model_part()
                                       << " part_guid=" << pg
                                       << " assembly_src_guid=" << src;
        }
    }
#endif
}

void AssemblyStepsUtils::show_pdf_export_settings_dialog()
{
    wxWindow *parent = nullptr;
    if (wxGetApp().plater())
        parent = dynamic_cast<wxWindow *>(wxGetApp().plater());

    if (m_pdf_export_title.empty() && m_model && !m_model->get_assembly_steps_json_str().empty()) {
        AssemblyStepJson tmp_json;
        if (tmp_json.load_from_string(m_model->get_assembly_steps_json_str()))
            m_pdf_export_title = tmp_json.get_pdf_export_params().title;
    }

    AssemblyPdfExportParams params;
    params.title = wxString::FromUTF8(m_pdf_export_title.c_str());

    AssemblyPdfExportDialog dlg(parent, params);
    if (dlg.ShowModal() != wxID_OK)
        return;

    params = dlg.get_params();
    m_pdf_export_title = params.title.ToUTF8().data();
    m_pdf_export_cover_image_path       = params.cover_image_path.ToUTF8().data();
    m_pdf_export_second_page_image_path = params.second_page_image_path.ToUTF8().data();
    save_assembly_steps_json_to_model();
}


void AssemblyStepsUtils::sync_canvas_selection_to_tree(bool selection_empty,bool selection_instance,const std::vector<int> &selected_object_indices)
{
    if (selection_empty) {
        // Clearing the canvas selection (e.g. double-clicking a blank area) must
        // NOT exit the assembly-step editing state anymore. Exiting is now driven
        // solely by the dedicated exit button, so keep the selected step node and
        // do not call clear_when_no_selection() here.
        return;
    }

    if (!selection_instance)
        return;

    bool folder_owns_selection = false;
    if (m_selected_node >= 0 && m_selected_node < (int) _steps_nodes.size() && _steps_nodes[m_selected_node].type == AssemblyStepsTreeNode::Type::Folder) {
        std::set<int> folder_objs = collect_node_object_indices(m_selected_node);
        folder_owns_selection = true;
        for (int object_idx : selected_object_indices) {
            if (folder_objs.find(object_idx) == folder_objs.end()) {
                folder_owns_selection = false;
                break;
            }
        }
    }

    if (folder_owns_selection || selected_object_indices.size() != 1)
        return;

    // If the current m_selected_node already points to a node whose object_idx
    int sel_obj_idx = selected_object_indices.front();
    if (m_selected_node >= 0 && m_selected_node < (int) _steps_nodes.size()) {
        auto &cur = _steps_nodes[m_selected_node];
        if (cur.type == AssemblyStepsTreeNode::Type::Object && cur.object_idx == sel_obj_idx)
            return;
    }
}

std::vector<AssemblySelectionMatchInfo> AssemblyStepsUtils::sync_single_canvas_selection_to_tree_or_get_matches(
    bool selection_empty,
    int  selected_object_idx,
    int  selected_volume_idx)
{
    if (selection_empty) {
        return {};
    }
    if (selected_object_idx < 0)
        return {};
    if (m_selection_origin != SelectionOrigin::GLVolume)
        return {};
    size_t selected_object_id = 0;
    if (m_model != nullptr &&
        selected_object_idx >= 0 &&
        selected_object_idx < static_cast<int>(m_model->objects.size()) &&
        m_model->objects[selected_object_idx] != nullptr) {
        selected_object_id = m_model->objects[selected_object_idx]->id().id;
    }

    std::vector<AssemblySelectionMatchInfo> matches;
    matches.reserve(4);

    for (int i = 0; i < (int) _steps_nodes.size(); ++i) {
        const auto &node = _steps_nodes[i];
        if (node.type != AssemblyStepsTreeNode::Type::Object)
            continue;
        if (node.object_idx != selected_object_idx && (selected_object_id == 0 || node.object_id != selected_object_id))
            continue;

        const int folder_idx = find_parent_folder(i);
        AssemblySelectionMatchInfo match;
        match.folder_node_idx = folder_idx;
        match.object_node_idx = i;
        match.object_idx      = node.object_idx >= 0 ? node.object_idx : selected_object_idx;
        match.volume_idx      = selected_volume_idx;
        match.object_name     = node.name;
        if (folder_idx >= 0 && folder_idx < (int) _steps_nodes.size()) match.step_label = assembly_step_display_name(_steps_nodes[folder_idx]);
        matches.emplace_back(std::move(match));
    }

    // When the user has already picked a step tree node (m_selected_node >= 0),
    if (m_selected_node >= 0) {
        sync_canvas_selection_to_selected_node_popup_checked();
        // Always return matches when there is more than one, so the tree
        // view tip can list the candidate steps.
        return matches.size() > 1 ? matches : std::vector<AssemblySelectionMatchInfo>{};
    }
    if (matches.size() == 1) {
        if (!has_selected_node()) // is_final_assembly_folder(matches.front().folder_node_idx)
            return {};

        int prev_folder = find_parent_folder(m_selected_node);
        m_selected_node = matches.front().object_node_idx;

        int cur_folder = find_parent_folder(m_selected_node);

        if (cur_folder != prev_folder) {//sync_single_canvas_selection_to_tree_or_get_matches
            on_selected_node_step_changed(cur_folder);
            m_last_folder_idx = cur_folder;
        }
        on_selected_node_changed();
        return {};
    }

    if (matches.size() > 1) {
        // m_selected_node was -1 here: surface candidates so the user can pick a step explicitly. We still don't auto-clear m_selected_node (it's already -1), but we report matches to the caller.
        return matches;
    }

    return {};
}

// ---------------------------------------------------------------------------
// "Assembly Structure" panel (Figma 732:10276 / 732:10279 / 732:10685)
// ---------------------------------------------------------------------------
AssemblyStructurePanelData AssemblyStepsUtils::build_assembly_structure_panel_data() const
{
    AssemblyStructurePanelData data;
    data.title    = _u8L("Assembly Structure");
    data.subtitle = _u8L("Start your assembly journey from Add Step");

    if (!m_model)
        return data;
    // Currently-selected folder (step) drives the green border highlight.
    const int cur_folder = const_cast<AssemblyStepsUtils*>(this)->find_parent_folder(m_selected_node);
    // First card: Final assembly. Find the is_final_assembly
    {
        int final_folder_idx = -1;
        for (int ri : _steps_roots) {
            if (ri >= 0 && ri < (int) _steps_nodes.size() && _steps_nodes[ri].is_final_assembly) {
                final_folder_idx = ri;
                break;
            }
        }

        AssemblyStructureCard def;
        def.tag_style   = AssemblyStructureCard::TagStyle::Default;
        def.tag_text    = _CTX_utf8(L_CONTEXT("Default", "AssemblyStructure"), "AssemblyStructure");
        def.prefix_text = _u8L("Contain");
        def.node_idx    = final_folder_idx;
        def.selected    = (final_folder_idx >= 0 && m_selected_node == final_folder_idx);
        def.is_final_assembly = true;
        def.title             = (final_folder_idx >= 0 && !_steps_nodes[final_folder_idx].name.empty()) ? _steps_nodes[final_folder_idx].name
                            : _u8L("Final assembly");
        int obj_count = 0;
        for (int obj_idx = 0; obj_idx < static_cast<int>(m_model->objects.size()); ++obj_idx) {
            const ModelObject *obj = m_model->objects[obj_idx];
            if (!obj) continue;
            obj_count++;
            AssemblyStructureChip chip;
            chip.label = obj->name.empty() ? _u8L("Object") : obj->name;
            def.chips.push_back(std::move(chip));
        }
        def.count = obj_count;
        if (final_folder_idx >= 0) {
            const bool has_label_key = m_structure_select_labels.count(final_folder_idx) > 0;
            def.select_show_default = m_structure_select_show_default.count(final_folder_idx) > 0 || !has_label_key;
            if (has_label_key) {
                auto select_label_it = m_structure_select_labels.find(final_folder_idx);
                if (select_label_it != m_structure_select_labels.end())
                    def.select_label = select_label_it->second;
            }
        }
        data.cards.push_back(std::move(def));
    }

    // One step card per top-level folder under roots. `step` is 1-based by
    // construction (create_folder_node), so prefer it when available.
    int step_seq = 1;
    for (int root_idx : _steps_roots) {
        if (root_idx < 0 || root_idx >= (int) _steps_nodes.size())
            continue;
        const auto &n = _steps_nodes[root_idx];
        if (n.type != AssemblyStepsTreeNode::Type::Folder)
            continue;
        if (n.is_final_assembly)
            continue;

        AssemblyStructureCard step;
        step.tag_style       = AssemblyStructureCard::TagStyle::Step;
        step.show_add_button = true;
        step.selected        = (root_idx == cur_folder);
        step.node_idx        = root_idx;
        step.is_final_assembly = n.is_final_assembly;

        const int step_num = step_seq;
        step.tag_text = _u8L("Step") + " " + std::to_string(step_num);
        step.title    = n.name.empty()
                          ? (_u8L("Assembly Module") + " " + std::to_string(step_num))
                          : n.name;
        auto select_label_it = m_structure_select_labels.find(root_idx);
        if (select_label_it != m_structure_select_labels.end())
            step.select_label = select_label_it->second;
        step.select_show_default = m_structure_select_show_default.count(root_idx) > 0;

        std::set<int> obj_set;
        std::function<void(int)> collect = [&](int idx) {
            if (idx < 0 || idx >= (int) _steps_nodes.size()) return;
            const auto &node = _steps_nodes[idx];
            if (node.type == AssemblyStepsTreeNode::Type::Object && node.object_idx >= 0)
                obj_set.insert(node.object_idx);
            for (int ci : node.children) collect(ci);
        };
        collect(root_idx);

        // Prefer volume-level membership from assembly_tree_checked so a partially
        // checked ModelObject shows its ModelVolume chips instead of the object name.
        const auto *checked_map = n.assembly_tree_checked ? &*n.assembly_tree_checked : nullptr;
        auto is_uid_checked = [checked_map](const std::string &uid) {
            if (!checked_map)
                return false;
            auto it = checked_map->find(uid);
            return it != checked_map->end() && it->second;
        };

        for (int obj_idx : obj_set) {
            if (obj_idx < 0 || obj_idx >= (int) m_model->objects.size()) continue;
            const ModelObject *obj = m_model->objects[obj_idx];
            if (!obj) continue;

            std::vector<int> checked_volume_idxs;
            int              selectable_volume_count = 0;
            if (checked_map) {
                for (int vi = 0; vi < (int) obj->volumes.size(); ++vi) {
                    const ModelVolume *vol = obj->volumes[vi];
                    if (!vol)
                        continue;
                    ++selectable_volume_count;
                    const std::string vol_uid = "object:" + std::to_string(obj_idx) + ":volume:" + std::to_string(vi);
                    if (is_uid_checked(vol_uid))
                        checked_volume_idxs.push_back(vi);
                }
            }

            const bool all_volumes_checked = selectable_volume_count > 0 &&
                (int) checked_volume_idxs.size() == selectable_volume_count;
            // No volume-level detail (or every volume checked) → keep the object chip.
            if (!checked_map || checked_volume_idxs.empty() || all_volumes_checked) {
                AssemblyStructureChip chip;
                chip.label = obj->name.empty() ? (_u8L("Part") + " " + std::to_string(obj_idx + 1)) : obj->name;
                step.chips.push_back(std::move(chip));
            } else {
                for (int vi : checked_volume_idxs) {
                    const ModelVolume *vol = obj->volumes[vi];
                    if (!vol)
                        continue;
                    AssemblyStructureChip chip;
                    chip.label = vol->name.empty()
                        ? (_u8L("Volume") + " " + std::to_string(vi + 1))
                        : vol->name;
                    step.chips.push_back(std::move(chip));
                }
            }
        }
        step.count = (int) step.chips.size();
        if (!step.chips.empty())
            step.prefix_text = _u8L("Contain");
        else
            step.placeholder_text = _u8L("Click to add objects to the current step");

        data.cards.push_back(std::move(step));
        ++step_seq;
    }

    return data;
}

void AssemblyStepsUtils::open_structure_add_tree(int card_idx, int step_node_idx, const ImVec2 &pos)
{
    m_structure_add_tree_card = card_idx;
    m_structure_add_tree_step_node = step_node_idx;
    m_structure_add_tree_pos = pos;
    m_structure_add_tree_opened_this_frame = true;
    if (m_model && step_node_idx >= 0)
        reseed_assembly_tree_checked_from_step(step_node_idx, m_model->get_assembly_tree_data());
    // Mirror the current canvas selection onto the tree's row highlight so the
    // popup opens reflecting what the user already has selected on the canvas.
    if (m_model)
        seed_tree_selected_items_from_canvas(m_model->get_assembly_tree_data());
    // Force the tree UI to treat this as a context change so the checked map is
    // re-seeded from the step's current membership (keeps "List" checkboxes in
    // sync with the "Contain" chips even when reopening the same step).
    m_assembly_tree_ui_current_folder_node = -1;
    m_assembly_tree_search_active = false;
    m_assembly_tree_search_focus_pending = false;
    m_assembly_tree_search_text.clear();
}

void AssemblyStepsUtils::auto_open_add_tree_for_selected_step()
{
    const int folder = find_parent_folder(m_selected_node);
    if (folder < 0 || folder >= (int) _steps_nodes.size())
        return;
    if (_steps_nodes[folder].type != AssemblyStepsTreeNode::Type::Folder ||
        _steps_nodes[folder].is_final_assembly)
        return;
    // Defer the actual open to the panel render, where the card's screen rect (and
    // thus the tree anchor position) is known.
    m_structure_add_tree_pending_node = folder;
}

void AssemblyStepsUtils::exit_render_assembly_tree_ui()
{
    m_structure_add_tree_card = -1;
    m_structure_add_tree_step_node = -1;
    m_assembly_tree_ui_current_folder_node = -1;
    m_assembly_tree_ui_original_checked.clear();
    m_structure_add_tree_opened_this_frame = false;
    // Drop panel-local row state so a reopened tree starts clean.
    m_assembly_tree_selected_items.clear();
    m_assembly_tree_hover_id = -1;
    m_tree_item_rename_object_idx = -1;
    m_tree_item_rename_volume_idx = -1;
    m_tree_item_rename_focus_pending = false;
    m_assembly_tree_search_active = false;
    m_assembly_tree_search_focus_pending = false;
    m_assembly_tree_search_text.clear();
}

void AssemblyStepsUtils::renumber_structure_step_roots()
{
    int step = 1;
    for (int root_idx : _steps_roots) {
        if (root_idx < 0 || root_idx >= (int) _steps_nodes.size())
            continue;
        auto &node = _steps_nodes[root_idx];
        if (node.type == AssemblyStepsTreeNode::Type::Folder && !node.is_final_assembly)
            node.step = step++;
    }
    for (int root_idx : _steps_roots) {
        if (root_idx < 0 || root_idx >= (int) _steps_nodes.size())
            continue;
        auto &node = _steps_nodes[root_idx];
        if (node.type == AssemblyStepsTreeNode::Type::Folder && node.is_final_assembly)
            node.step = step++;
    }
}

void AssemblyStepsUtils::update_final_assembly_step_number_to_max()
{
    int final_idx = -1;
    int max_other_step = 0;
    // Only consider folders that are still part of the step list (_steps_roots).
    // delete_structure_step unlinks a deleted step from _steps_roots but keeps it
    // in _steps_nodes (soft delete), so scanning all nodes would pick up the
    // orphaned step's stale number and inflate the final-assembly step (e.g.
    // show "Step 6" after deleting the 5th of 5 steps).
    for (int root_idx : _steps_roots) {
        if (root_idx < 0 || root_idx >= (int) _steps_nodes.size())
            continue;
        const auto &node = _steps_nodes[root_idx];
        if (node.type != AssemblyStepsTreeNode::Type::Folder)
            continue;
        if (node.is_final_assembly) {
            final_idx = root_idx;
            continue;
        }
        max_other_step = std::max(max_other_step, node.step);
    }
    if (final_idx < 0)
        return;

    const int desired = max_other_step + 1;
    if (_steps_nodes[final_idx].step == desired)
        return;
    _steps_nodes[final_idx].step = desired;
    save_assembly_steps_json_to_model();
}

void AssemblyStepsUtils::insert_structure_step_relative(int ref_node_idx, bool before, const std::string &folder_name, bool copy)
{
    if (!m_model)
        return;

    // Every path produces one more non-final step, so honor the upper bound.
    if (!can_add_non_final_assembly_step())
        return;

    int new_idx = -1;
    if (copy) {
        // Clone the reference step folder (its children + per-keyframe pose/camera
        // snapshots) into a brand-new step and place the clone next to it.
        if (ref_node_idx < 0 || ref_node_idx >= (int) _steps_nodes.size())
            return;
        if (_steps_nodes[ref_node_idx].type != AssemblyStepsTreeNode::Type::Folder ||
            _steps_nodes[ref_node_idx].is_final_assembly)
            return;

        std::function<int(int)> clone_node = [&](int src_idx) -> int {
            if (src_idx < 0 || src_idx >= (int) _steps_nodes.size())
                return -1;
            AssemblyStepsTreeNode copied = _steps_nodes[src_idx];
            copied.children.clear();
            if (copied.type == AssemblyStepsTreeNode::Type::Folder) {
                copied.id = next_node_id();
                copied.step = 0;
                copied.is_final_assembly = false;
            }
            // Rebuild the keyframe entries through clone_from so the per-keyframe copy
            // (camera framing + object/volume matrix pose snapshots) is explicit here,
            // instead of relying on the implicit whole-struct copy above.
            {
                const auto &src_entries = _steps_nodes[src_idx].kf_data.entries;
                copied.kf_data.entries.clear();
                copied.kf_data.entries.reserve(src_entries.size());
                for (const auto &src_entry : src_entries) {
                    KeyFrameEntry cloned_entry;
                    cloned_entry.clone_from(src_entry);
                    copied.kf_data.entries.push_back(std::move(cloned_entry));
                }
            }
            copied.kf_data.node_idx   = (int) _steps_nodes.size();
            copied.kf_data.is_folder  = (copied.type == AssemblyStepsTreeNode::Type::Folder);
            copied.kf_data.object_idx = copied.object_idx;

            int copied_idx = (int) _steps_nodes.size();
            _steps_nodes.push_back(std::move(copied));
            for (int child_idx : _steps_nodes[src_idx].children) {
                int copied_child = clone_node(child_idx);
                if (copied_child >= 0)
                    _steps_nodes[copied_idx].children.push_back(copied_child);
            }
            return copied_idx;
        };

        new_idx = clone_node(ref_node_idx);
        if (new_idx < 0)
            return;
        std::string &copied_name = _steps_nodes[new_idx].name;
        if (copied_name.empty())
            copied_name = _u8L("New Step");
        copied_name += _u8L("_copy");
    } else {
        new_idx = create_folder_node(folder_name.empty() ? _u8L("New Step") : folder_name, 0);
        if (new_idx < 0)
            return;
        ensure_default_keyframe(new_idx);
        seed_end_frame_camera_from_current(new_idx);
    }

    auto it = std::find(_steps_roots.begin(), _steps_roots.end(), ref_node_idx);
    if (it != _steps_roots.end())
        _steps_roots.insert(before ? it : it + 1, new_idx);
    else
        _steps_roots.push_back(new_idx);

    clear_selection();
    renumber_structure_step_roots();
    m_selected_node = new_idx;
    m_structure_scroll_to_node = new_idx;
    on_selected_node_changed();
    reschedule_play_bar_after_structure_change();//insert_structure_step_relative
    // Only an empty (newly-created) step shows all volumes as dimmed candidates.
    // A copied step already has children, so on_selected_node_changed() above has
    if (is_empty_structure_step(new_idx))
        show_volumes_as_step_candidates();//insert_structure_step_relative
}

void AssemblyStepsUtils::reorder_structure_step(int moved_node, int before_node)
{
    if (!m_model || moved_node < 0 || moved_node >= (int) _steps_nodes.size())
        return;
    const auto is_reorderable = [&](int idx) {
        return idx >= 0 && idx < (int) _steps_nodes.size() &&
               _steps_nodes[idx].type == AssemblyStepsTreeNode::Type::Folder &&
               !_steps_nodes[idx].is_final_assembly;
    };
    if (!is_reorderable(moved_node))
        return;

    // Collect the current order of non-final step nodes, then permute it.
    std::vector<int> steps;
    for (int root_idx : _steps_roots) {
        if (is_reorderable(root_idx))
            steps.push_back(root_idx);
    }
    steps.erase(std::remove(steps.begin(), steps.end(), moved_node), steps.end());
    auto pos = (before_node >= 0) ? std::find(steps.begin(), steps.end(), before_node) : steps.end();
    steps.insert(pos, moved_node);

    // Write the permuted order back, leaving the final-assembly slot untouched.
    size_t si = 0;
    for (int &root_idx : _steps_roots) {
        if (is_reorderable(root_idx) && si < steps.size())
            root_idx = steps[si++];
    }

    renumber_structure_step_roots();
    reschedule_play_bar_after_structure_change();//reorder_structure_step
}

void AssemblyStepsUtils::delete_structure_step(int node_idx)
{
    if (!m_model || node_idx < 0 || node_idx >= (int) _steps_nodes.size())
        return;
    if (_steps_nodes[node_idx].type != AssemblyStepsTreeNode::Type::Folder || _steps_nodes[node_idx].is_final_assembly)
        return;

    MessageDialog msg_dlg(nullptr,
        _L("Are you sure you want to delete the current step?"),
        _L("Delete step"),
        wxICON_QUESTION | wxYES_NO);
    if (msg_dlg.ShowModal() != wxID_YES)
        return;

    int prev_card_node = -1;
    for (int root_idx : _steps_roots) {
        if (root_idx == node_idx)
            break;
        if (root_idx < 0 || root_idx >= static_cast<int>(_steps_nodes.size()))
            continue;
        const auto &root = _steps_nodes[root_idx];
        if (root.type == AssemblyStepsTreeNode::Type::Folder && !root.is_final_assembly)
            prev_card_node = root_idx;
    }
    if (prev_card_node < 0)
        prev_card_node = ensure_final_assembly_folder();

    _steps_roots.erase(std::remove(_steps_roots.begin(), _steps_roots.end(), node_idx), _steps_roots.end());
    m_structure_select_labels.erase(node_idx);
    m_structure_select_show_default.erase(node_idx);
    clear_selection();
    if (prev_card_node >= 0 && prev_card_node != node_idx) {
        m_selected_node = prev_card_node;
        m_structure_scroll_to_node = prev_card_node;
        select_steps_tree_node_for_canvas(m_selected_node);
    } else if (m_selected_node == node_idx || find_parent_folder(m_selected_node) == node_idx) {
        on_selected_node_changed();
    }

    renumber_structure_step_roots();
    reschedule_play_bar_after_structure_change();//delete_structure_step
}


void AssemblyStepsUtils::begin_structure_step_rename(int node_idx, const std::string &fallback_title)
{
    m_structure_step_rename_node = node_idx;
    memset(m_structure_step_rename_buf, 0, sizeof(m_structure_step_rename_buf));
    if (node_idx >= 0 && node_idx < (int) _steps_nodes.size()) {
        const std::string &title = _steps_nodes[node_idx].name.empty() ? fallback_title : _steps_nodes[node_idx].name;
        strncpy(m_structure_step_rename_buf, title.c_str(), sizeof(m_structure_step_rename_buf) - 1);
    }
    m_structure_step_rename_open_pending = true;
    m_structure_step_rename_had_focus = false;
}


#undef nodes
#undef roots

void AssemblyStepsUtils::apply_canvas_selection_to_popup_checked(int card_idx, const AssemblyTreeData &pt)
{
    if (!m_selection || !m_model)
        return;
    if (pt.nodes.empty())
        return;

    // Collect (object_idx, volume_idx) currently selected on canvas.
    std::set<std::pair<int, int>> sel_pairs;
    const auto &idxs = m_selection->get_volume_idxs();
    for (auto idx : idxs) {
        const GLVolume *gv = m_selection->get_volume(idx);
        if (!gv)
            continue;
        sel_pairs.emplace(gv->object_idx(), gv->volume_idx());
    }

    m_structure_select_popup_checked.clear();

    // Volume nodes: checked iff the (object_idx, volume_idx) pair is selected.
    for (const auto &n : pt.nodes) {
        if (!n.selectable || n.volume_idx < 0)
            continue;
        if (sel_pairs.count({n.object_idx, n.volume_idx}) > 0)
            m_structure_select_popup_checked[n.uid] = true;
    }
    // Object nodes: checked iff every selectable child volume is selected (matches the "object-level selection" semantics used by the popup).
    for (const auto &n : pt.nodes) {
        if (!n.selectable || n.volume_idx >= 0)
            continue;
        bool any = false;
        bool all = true;
        for (int c : n.children) {
            if (c < 0 || c >= static_cast<int>(pt.nodes.size()))
                continue;
            const auto &cn = pt.nodes[c];
            if (!cn.selectable || cn.volume_idx < 0)
                continue;
            any = true;
            if (sel_pairs.count({cn.object_idx, cn.volume_idx}) == 0) {
                all = false;
                break;
            }
        }
        if (any && all)
            m_structure_select_popup_checked[n.uid] = true;
    }

    m_structure_select_popup_checked_card = card_idx;
    update_structure_select_label(card_idx, pt);
}

void AssemblyStepsUtils::sync_canvas_selection_to_selected_node_popup_checked()
{
    if (!m_selection || !m_model)
        return;

    const int folder_idx = find_parent_folder(m_selected_node);
    if (folder_idx < 0)
        return;
    if (m_structure_select_popup_tree_step_node != folder_idx)
        return; // popup tree not built for this folder yet.skip silently
    if (m_structure_select_popup_tree.nodes.empty())
        return;
    // Don't disturb a popup that the user is currently interacting with on a different step.
    if (m_structure_select_popup_active_card >= 0 &&
        m_structure_select_popup_active_card != m_structure_select_popup_tree_card)
        return;

    apply_canvas_selection_to_popup_checked(m_structure_select_popup_tree_card,
                                            m_structure_select_popup_tree);
}

AssemblyTreeData AssemblyStepsUtils::build_structure_card_select_tree_data(int step_node_idx) const
{
    AssemblyTreeData tree;
    if (!m_model)
        return tree;

    std::set<int> object_filter;
    const AssemblyStepsTreeData &step_tree = m_model->get_assembly_steps_tree_data();
    std::function<void(int)> collect_step_objects = [&](int node_idx) {
        if (node_idx < 0 || node_idx >= static_cast<int>(step_tree.nodes.size()))
            return;
        const AssemblyStepsTreeNode &node = step_tree.nodes[node_idx];
        if (node.type == AssemblyStepsTreeNode::Type::Object && node.object_idx >= 0)
            object_filter.insert(node.object_idx);
        else if (node.type == AssemblyStepsTreeNode::Type::Volume && node.object_idx >= 0)
            object_filter.insert(node.object_idx);
        for (int child_idx : node.children)
            collect_step_objects(child_idx);
    };

    if (step_node_idx >= 0)
        collect_step_objects(step_node_idx);

    const bool is_final = step_node_idx >= 0 &&
        step_node_idx < static_cast<int>(step_tree.nodes.size()) &&
        step_tree.nodes[step_node_idx].is_final_assembly;

    int model_root_idx = -1;
    if (is_final) {
        AssemblyTreeNodeData model_node;
        model_node.id         = 0;
        model_node.parent_id  = -1;
        model_node.object_idx = -1;
        model_node.volume_idx = -1;
        model_node.selectable = true;
        model_node.uid        = "model_root";
        model_node.label      = _u8L("Model");
        model_root_idx = 0;
        tree.nodes.push_back(std::move(model_node));
        tree.roots.push_back(model_root_idx);
    }

    for (int object_idx = 0; object_idx < static_cast<int>(m_model->objects.size()); ++object_idx) {
        if (!object_filter.empty() && object_filter.find(object_idx) == object_filter.end())
            continue;
        const ModelObject *obj = m_model->objects[object_idx];
        if (!obj)
            continue;

        AssemblyTreeNodeData object_node;
        object_node.id         = static_cast<int>(tree.nodes.size());
        object_node.parent_id  = model_root_idx;
        object_node.object_idx = object_idx;
        object_node.volume_idx = -1;
        object_node.selectable = true;
        object_node.uid        = "object:" + std::to_string(object_idx);
        object_node.label      = obj->name.empty() ? (_u8L("Part") + " " + std::to_string(object_idx + 1)) : obj->name;

        const int object_node_idx = static_cast<int>(tree.nodes.size());
        tree.nodes.push_back(std::move(object_node));
        if (model_root_idx >= 0)
            tree.nodes[model_root_idx].children.push_back(object_node_idx);
        else
            tree.roots.push_back(object_node_idx);

        for (int volume_idx = 0; volume_idx < static_cast<int>(obj->volumes.size()); ++volume_idx) {
            const ModelVolume *vol = obj->volumes[volume_idx];
            if (!vol)
                continue;

            AssemblyTreeNodeData volume_node;
            volume_node.id         = static_cast<int>(tree.nodes.size());
            volume_node.parent_id  = object_node_idx;
            volume_node.object_idx = object_idx;
            volume_node.volume_idx = volume_idx;
            volume_node.selectable = true;
            volume_node.uid        = "object:" + std::to_string(object_idx) + ":volume:" + std::to_string(volume_idx);
            volume_node.label      = vol->name.empty()
                ? (_u8L("Volume") + " " + std::to_string(volume_idx + 1))
                : vol->name;

            const int volume_node_idx = static_cast<int>(tree.nodes.size());
            tree.nodes.push_back(std::move(volume_node));
            tree.nodes[object_node_idx].children.push_back(volume_node_idx);
        }
    }

    return tree;
}

AssemblyTreeData AssemblyStepsUtils::build_model_object_tree_data(bool include_model_root_node) const
{
    AssemblyTreeData tree;
    if (!m_model)
        return tree;
    int model_root_idx = -1;
    if (include_model_root_node) {
        AssemblyTreeNodeData model_node;
        model_node.id         = static_cast<int>(tree.nodes.size());
        model_node.parent_id  = -1;
        model_node.object_idx = -1;
        model_node.volume_idx = -1;
        model_node.selectable = true;
        model_node.uid        = "model_root";
        model_node.label      = _u8L("Model");

        model_root_idx = static_cast<int>(tree.nodes.size());
        tree.nodes.push_back(std::move(model_node));
        tree.roots.push_back(model_root_idx);
    }

    for (int object_idx = 0; object_idx < static_cast<int>(m_model->objects.size()); ++object_idx) {
        const ModelObject *obj = m_model->objects[object_idx];
        if (!obj)
            continue;

        AssemblyTreeNodeData object_node;
        object_node.id         = static_cast<int>(tree.nodes.size());
        object_node.parent_id  = model_root_idx;
        object_node.object_idx = object_idx;
        object_node.volume_idx = -1;
        object_node.selectable = true;
        object_node.uid        = "object:" + std::to_string(object_idx);
        object_node.label      = obj->name.empty() ? (_u8L("Object") + " " + std::to_string(object_idx + 1)) : obj->name;

        const int object_node_idx = static_cast<int>(tree.nodes.size());
        tree.nodes.push_back(std::move(object_node));
        if (model_root_idx >= 0)
            tree.nodes[model_root_idx].children.push_back(object_node_idx);
        else
            tree.roots.push_back(object_node_idx);

        for (int volume_idx = 0; volume_idx < static_cast<int>(obj->volumes.size()); ++volume_idx) {
            const ModelVolume *vol = obj->volumes[volume_idx];
            if (!vol)
                continue;

            AssemblyTreeNodeData volume_node;
            volume_node.id         = static_cast<int>(tree.nodes.size());
            volume_node.parent_id  = object_node_idx;
            volume_node.object_idx = object_idx;
            volume_node.volume_idx = volume_idx;
            volume_node.selectable = true;
            volume_node.uid        = "object:" + std::to_string(object_idx) + ":volume:" + std::to_string(volume_idx);
            volume_node.label      = vol->name.empty()
                ? (_u8L("Volume") + " " + std::to_string(volume_idx + 1))
                : vol->name;

            const int volume_node_idx = static_cast<int>(tree.nodes.size());
            tree.nodes.push_back(std::move(volume_node));
            tree.nodes[object_node_idx].children.push_back(volume_node_idx);
        }
    }

    return tree;
}


void AssemblyStepsUtils::update_structure_select_label(int card_idx, const AssemblyTreeData& popup_tree)
{
    if (!m_model)
        return;

    int step_node_idx = -1;
    const AssemblyStepsTreeData& step_tree = m_model->get_assembly_steps_tree_data();
    if (card_idx == 0) {
        for (int root_idx : step_tree.roots) {
            if (root_idx < 0 || root_idx >= static_cast<int>(step_tree.nodes.size()))
                continue;
            if (step_tree.nodes[root_idx].type == AssemblyStepsTreeNode::Type::Folder &&
                step_tree.nodes[root_idx].is_final_assembly) {
                step_node_idx = root_idx;
                break;
            }
        }
    } else {
        const int regular_card_idx = card_idx - 1;
        int regular_count = 0;
        for (int root_idx : step_tree.roots) {
            if (root_idx < 0 || root_idx >= static_cast<int>(step_tree.nodes.size()))
                continue;
            const auto &node = step_tree.nodes[root_idx];
            if (node.type != AssemblyStepsTreeNode::Type::Folder || node.is_final_assembly)
                continue;
            if (regular_count == regular_card_idx) {
                step_node_idx = root_idx;
                break;
            }
            ++regular_count;
        }
    }

    if (step_node_idx < 0)
        return;

    const bool is_final = step_node_idx < static_cast<int>(step_tree.nodes.size()) &&
        step_tree.nodes[step_node_idx].is_final_assembly;

    if (is_final) {
        std::set<int> all_objects;
        for (const auto &node : popup_tree.nodes) {
            if (!node.selectable || node.object_idx < 0 || node.volume_idx >= 0)
                continue;
            all_objects.insert(node.object_idx);
        }

        std::set<int> selected_objects;
        for (const auto &node : popup_tree.nodes) {
            if (!node.selectable || node.object_idx < 0)
                continue;
            auto it = m_structure_select_popup_checked.find(node.uid);
            if (it == m_structure_select_popup_checked.end() || !it->second)
                continue;
            selected_objects.insert(node.object_idx);
        }

        const bool all_selected = !all_objects.empty() && selected_objects.size() == all_objects.size();
        if (all_selected) {
            // Chip collapses to "Default"; the tooltip is built on demand at hover.
            m_structure_select_show_default.insert(step_node_idx);
            m_structure_select_labels.erase(step_node_idx);
            return;
        }

        m_structure_select_show_default.erase(step_node_idx);

        std::string label;
        std::set<int> added;
        for (const auto &node : popup_tree.nodes) {
            if (!node.selectable || node.object_idx < 0 || node.volume_idx >= 0)
                continue;
            if (!selected_objects.count(node.object_idx) || added.count(node.object_idx))
                continue;
            added.insert(node.object_idx);
            if (!label.empty())
                label += ", ";
            label += node.label;
        }

        if (label.empty())
            m_structure_select_labels.erase(step_node_idx);
        else
            m_structure_select_labels[step_node_idx] = label;
        return;
    }

    std::vector<std::string> labels;
    std::set<int> objects_with_checked_volumes;
    for (const auto& node : popup_tree.nodes) {
        if (node.volume_idx < 0)
            continue;
        auto it = m_structure_select_popup_checked.find(node.uid);
        if (it == m_structure_select_popup_checked.end() || !it->second)
            continue;
        labels.push_back(node.label);
        objects_with_checked_volumes.insert(node.object_idx);
    }

    for (const auto& node : popup_tree.nodes) {
        if (node.volume_idx >= 0)
            continue;
        if (node.object_idx < 0)
            continue;
        auto it = m_structure_select_popup_checked.find(node.uid);
        if (it == m_structure_select_popup_checked.end() || !it->second)
            continue;
        if (objects_with_checked_volumes.find(node.object_idx) != objects_with_checked_volumes.end())
            continue;
        labels.push_back(node.label);
    }

    std::string label;
    for (size_t i = 0; i < labels.size(); ++i) {
        if (i > 0)
            label += ", ";
        label += labels[i];
    }

    // "Select all" detection for a regular step: every selectable leaf (a volume
    int leaf_total   = 0;
    int leaf_checked = 0;
    for (const auto &node : popup_tree.nodes) {
        if (!node.selectable || node.object_idx < 0)
            continue;
        const bool is_leaf = node.volume_idx >= 0 || node.children.empty();
        if (!is_leaf)
            continue;
        ++leaf_total;
        auto it = m_structure_select_popup_checked.find(node.uid);
        if (it != m_structure_select_popup_checked.end() && it->second)
            ++leaf_checked;
    }
    const bool all_selected = leaf_total > 0 && leaf_checked == leaf_total;
    if (all_selected) {
        // Chip collapses to "Default"; the tooltip is built on demand at hover.
        m_structure_select_show_default.insert(step_node_idx);
        m_structure_select_labels.erase(step_node_idx);
        return;
    }

    m_structure_select_show_default.erase(step_node_idx);
    if (label.empty())
        m_structure_select_labels.erase(step_node_idx);
    else
        m_structure_select_labels[step_node_idx] = label;
}

void AssemblyStepsUtils::sync_structure_select_popup_to_canvas(const AssemblyTreeData& popup_tree)
{
    sync_checked_tree_to_canvas(popup_tree, m_structure_select_popup_checked);
}

void AssemblyStepsUtils::sync_checked_tree_to_canvas(const AssemblyTreeData& tree,
                                                     const std::unordered_map<std::string, bool>& checked)
{
    if (!m_selection || !m_model)
        return;
    set_selection_origin(SelectionOrigin::TreeNode);
    clear_selection();

    // Build one GLVolume index list so multi-object + multi-volume check sets can
    // be applied in Volume mode (same pattern as Plater prepare→assemble selection).
    std::vector<unsigned int> gl_volume_idxs;
    std::set<int>             objects_with_checked_volumes;

    for (const auto& node : tree.nodes) {
        if (node.volume_idx < 0)
            continue;
        auto it = checked.find(node.uid);
        if (it == checked.end() || !it->second)
            continue;
        if (node.object_idx < 0 || node.object_idx >= static_cast<int>(m_model->objects.size()))
            continue;
        const auto idxs = m_selection->get_volume_idxs_from_volume(
            (unsigned int) node.object_idx, 0, (unsigned int) node.volume_idx);
        gl_volume_idxs.insert(gl_volume_idxs.end(), idxs.begin(), idxs.end());
        objects_with_checked_volumes.insert(node.object_idx);
    }

    for (const auto& node : tree.nodes) {
        if (node.volume_idx >= 0)
            continue;
        auto it = checked.find(node.uid);
        if (it == checked.end() || !it->second)
            continue;
        if (objects_with_checked_volumes.find(node.object_idx) != objects_with_checked_volumes.end())
            continue;
        if (node.object_idx < 0 || node.object_idx >= static_cast<int>(m_model->objects.size()))
            continue;
        const auto idxs = m_selection->get_volume_idxs_from_object((unsigned int) node.object_idx);
        gl_volume_idxs.insert(gl_volume_idxs.end(), idxs.begin(), idxs.end());
    }

    if (!gl_volume_idxs.empty())
        add_volumes_and_lock_volume_mode(gl_volume_idxs);
    do_commond_callback("dirty");
}

void AssemblyStepsUtils::select_folder_volumes_on_canvas(int folder_idx)
{
    if (!m_selection || !m_model || folder_idx < 0 || folder_idx >= (int) _steps_nodes.size())
        return;

    // Switching step cards always enters Volume (part) selection mode, even when
    // nothing is selected (m_select_all_when_change_step_card == false).
    clear_selection_and_lock_volume_mode();

    if (!m_select_all_when_change_step_card) {
        do_commond_callback("dirty");
        return;
    }

    std::vector<unsigned int> gl_volume_idxs;
    for (const auto &key : collect_folder_volume_pairs(folder_idx)) {
        if (key.first < 0 || key.second < 0)
            continue;
        const auto idxs = m_selection->get_volume_idxs_from_volume(
            (unsigned int) key.first, 0, (unsigned int) key.second);
        gl_volume_idxs.insert(gl_volume_idxs.end(), idxs.begin(), idxs.end());
    }
    add_volumes_and_lock_volume_mode(gl_volume_idxs);
    do_commond_callback("dirty");
}
void AssemblyStepsUtils::record_keyframe_logic(KeyFrameEntry &entry)
{
    if (!m_only_final_assembly_endframe_effect_real_assembly) {
        return;
    }
    KeyFrame &kf = entry.data;
    record_camera(kf);
    // Explicit user re-record path: preserve this exact camera on return (see
    // look_cur_frame_logic / record_selected_gl_volume_transforms_to_current_keyframe).
    kf.camera_user_defined = true;
    entry.need_save = true;
    record_selected_volumes_by_mo_mv(kf);

    save_assembly_steps_json_to_model();
}

void AssemblyStepsUtils::apply_keyframe_to_canvas(const KeyFrame &kf, bool apply_camera_view)
{
    if (apply_camera_view)
        apply_camera(kf);

    // Apply instance matrices BEFORE per-volume matrices: GLVolume composes
    for (const auto &p : kf.object_transformations) {
        apply_instance_transform(p.first, p.second);
    }
    for (const auto &p : kf.volume_transformations) {
        apply_volume_transform(p.first.first, p.first.second, p.second);
    }

    m_selected_screen_center_dirty_ = true;
    do_commond_callback("dirty");
}

void AssemblyStepsUtils::delete_selected_keyframe()
{
    auto *kf_entries = get_current_kf_entries();
    if (!kf_entries) return;
    auto &entries = *kf_entries;
    if (m_keyframe_selected < 0 || m_keyframe_selected >= (int) entries.size())
        return;
    // Capture this before the erase: deleting a non-end keyframe means the
    const bool cur_was_not_end_frame = !entries[m_keyframe_selected].is_last();

    entries.erase(entries.begin() + m_keyframe_selected);
    invalidate_play_frame_refs();//delete_selected_keyframe
    if (m_keyframe_selected >= (int) entries.size())
        m_keyframe_selected = (int) entries.size() - 1;
    refresh_guide_show_part_numbers_from_current();

    auto *remaining = get_current_kf_entries();
    if (remaining) {
        for (const auto &e : *remaining) {
            if (e.is_last() && e.need_save) {
                apply_keyframe_to_canvas(e.data);
                break;
            }
        }
    }

    // A non-end keyframe was removed; refresh the node's end-frame part-number labels so they reflect the updated keyframe set.
    if (cur_was_not_end_frame && remaining) {
        for (auto &e : *remaining) {
            if (e.is_last()) {
                toggle_part_number_labels_to_keyframe(e);
                break;
            }
        }
    }
}

void AssemblyStepsUtils::insert_keyframe_after_selected()
{
    auto *kf_entries = get_current_kf_entries();
    if (!kf_entries) return;
    auto &entries = *kf_entries;

    int insert_pos = (m_keyframe_selected >= 0 && m_keyframe_selected < (int) entries.size())
                         ? m_keyframe_selected + 1
                         : (int) entries.size() - 1;

    // Locate the "last" frame; the new keyframe must always come before it.
    int last_idx = -1;
    for (int i = 0; i < (int) entries.size(); ++i) {
        if (entries[i].is_last()) {
            last_idx = i;
            break;
        }
    }
    if (insert_pos > last_idx && last_idx >= 0)
        insert_pos = last_idx;

    KeyFrameEntry new_entry;
    if (m_keyframe_selected >= 0 && m_keyframe_selected < (int) entries.size()) {
        auto &cur_entry = entries[m_keyframe_selected];
        new_entry.clone_from(cur_entry);

        record_camera(cur_entry.data);
        record_camera(new_entry.data);
    }
    new_entry.data.id   = (insert_pos == 0) ? 1 : (int) entries.size() + 1;
    new_entry.data.name = (insert_pos == 0) ? _u8L("start frame") : _u8L("transition frame");
    new_entry.need_save = true;
    entries.insert(entries.begin() + insert_pos, new_entry);
    invalidate_play_frame_refs();//insert_keyframe_after_selected
    // Editing the timeline invalidates any paused global playback session. Otherwise the next inline
    // "play current step" click may resume the old global state instead of starting a fresh local queue.
    pause_global_frame();
    m_keyframe_selected = insert_pos;
    refresh_guide_show_part_numbers_from_current();
}

void AssemblyStepsUtils::play_all_keyframes_for_current_node()
{
    clear_playback_pause_state();
    clear_global_playback_state();
    m_keyframe_playing = true;
    build_local_play_queue();
    do_commond_callback("exit_gizmo");
    do_commond_callback("request_extra_frame");
}

bool AssemblyStepsUtils::should_show_panels()
{
    // Hide the assembly chrome (Structure / Guide panels, play bar, part-label
    const bool active_playback = !m_playback_paused && (m_video_intro_active || m_keyframe_playing);
    const bool exporting = m_is_export_mode || m_steps_export_active ||
                           m_steps_video_export_active || active_playback;
    if (exporting) {
        if (m_play_video_and_show_panels_debug) {
            return true;
        } else {
            return false;
        }
    }
    return true;
}

void AssemblyStepsUtils::on_keyframe_list_item_clicked(int idx, KeyFrameEntry &entry)
{
    m_keyframe_selected = idx;
    refresh_guide_show_part_numbers_from_current();
    look_cur_frame_logic(entry);
    if (m_keyframe_display_mode != KeyframeDisplayMode::All) {
        apply_keyframe_display_mode();
    }

    // Sync progress bar to the clicked keyframe's global position.
    int folder = find_parent_folder(m_selected_node);
    if (folder >= 0 && !m_play_frame_refs.empty()) {
        for (int gi = 0; gi < (int) m_play_frame_refs.size(); ++gi) {
            if (m_play_frame_refs[gi].node_idx == folder &&
                m_play_frame_refs[gi].frame_idx == idx) {
                m_assembly_play_index = gi + 1;
                break;
            }
        }
    }

    if (ImGui::IsMouseDoubleClicked(0)) {
        memset(m_keyframe_edit_buf, 0, sizeof(m_keyframe_edit_buf));
        strncpy(m_keyframe_edit_buf, entry.data.name.c_str(), sizeof(m_keyframe_edit_buf) - 1);
    }
}

void AssemblyStepsUtils::record_keyframe_at(int idx)
{
    auto *kf_entries = get_current_kf_entries();
    if (!kf_entries) return;
    auto &entries = *kf_entries;
    if (idx < 0 || idx >= (int) entries.size())
        return;

    m_keyframe_selected = idx;
    if (m_only_final_assembly_endframe_effect_real_assembly) {
        record_all_glvolumes_in_cur_step__to_current_keyframe();
    } else {
        record_keyframe_logic(entries[idx]);
    }
    refresh_guide_show_part_numbers_from_current();
}

bool AssemblyStepsUtils::is_current_keyframe_changed()
{
    if (!m_camera)
        return false;

    auto *entries = get_current_kf_entries();
    if (!entries)
        return false;
    if (m_keyframe_selected < 0 || m_keyframe_selected >= (int) entries->size())
        return false;

    const KeyFrame &kf  = (*entries)[m_keyframe_selected].data;
    const Camera   &cam = *m_camera;

    // Compare the camera view (orientation + eye position). The orthographic
    // zoom does not affect the view matrix, so check it separately with a
    // relative tolerance.
    if (!cam.get_view_matrix().matrix().isApprox(kf.view_matrix.matrix(), 1e-2))
        return true;

    // Compare projection parameters individually (near, far, fov).
    // Extract near/far from the perspective projection matrix.
    double cam_near = cam.get_near_z();
    double cam_far  = cam.get_far_z();
    double cam_fov  = cam.get_fov();
    // Approximate extraction from stored matrix (assumes perspective projection).
    const auto &kf_mat = kf.projection_matrix.matrix();
    double kf_near = cam.get_near_z(); // fallback to current near if extraction not implemented
    double kf_far  = cam.get_far_z();
    double kf_fov  = cam.get_fov();
    // Simple tolerance checks.
    const double tol = 1e-2;
    //ignore aspect ratio
    if (std::abs(cam_near - kf_near) > tol || std::abs(cam_far - kf_far) > tol || std::abs(cam_fov - kf_fov) > tol)
        return true;

    const double cur_zoom = cam.get_zoom();
    if (std::abs(cur_zoom - kf.camera_zoom) > 1e-2)
        return true;

    return false;
}

void AssemblyStepsUtils::sync_canvas_selection_state()
{
    if (!m_selection)
        return;
    static bool last_selection_empty = false;
    auto        selection_empty      = m_selection->is_empty();
    if (selection_empty != last_selection_empty) {
        if (selection_empty && !has_selected_node()) {
            apply_keyframe_display_mode(); // bbs
        }
        last_selection_empty = selection_empty;
    }

    std::vector<int> selected_object_idxs;
    const auto &content = m_selection->get_content();
    selected_object_idxs.reserve(content.size());
    for (const auto &pair : content)
        selected_object_idxs.push_back(pair.first);

    if (m_selection->is_empty()) {
        sync_canvas_selection_to_tree(true, true, {});
    } else if (selected_object_idxs.size() == 1) {
        int selected_object_idx = selected_object_idxs.front();
        int selected_volume_idx = -1;
        bool sync_single_selection = !m_selection->is_any_volume();
        if (m_selection->is_single_volume()) {
            int volume_object_idx = -1;
            int volume_idx = -1;
            if (m_selection->get_selected_single_volume(volume_object_idx, volume_idx) != nullptr) {
                selected_object_idx = volume_object_idx;
                selected_volume_idx = volume_idx;
                sync_single_selection = true;
            }
        }
        if (sync_single_selection) {
            sync_single_canvas_selection_to_tree_or_get_matches(false, selected_object_idx, selected_volume_idx);
        }
    }

    notify_selected_object_steps();
}

void AssemblyStepsUtils::notify_selected_object_steps()
{
    if (!m_model || !m_selection)
        return;

    // A selected step node drives the tree/card UI on its own; only the "loose" scene-selection case
    // (has_selected_node() == false, but an object/part is picked) needs a step-membership hint.
    std::vector<int> sel_objs = has_selected_node() ? std::vector<int>() : selected_assembly_object_indices();

    // sync_canvas_selection_state() runs every frame; fire once per distinct selection.
    if (sel_objs == m_last_notified_step_hint_objs_)
        return;
    m_last_notified_step_hint_objs_ = sel_objs;
    if (sel_objs.empty())
        return;

    Plater *plater = wxGetApp().plater();
    if (plater == nullptr || plater->get_notification_manager() == nullptr)
        return;

    std::string text;
    for (int obj_idx : sel_objs) {
        if (obj_idx < 0 || obj_idx >= (int) m_model->objects.size() || m_model->objects[obj_idx] == nullptr)
            continue;

        std::vector<std::string> step_names;
        for (int ni = 0; ni < (int) _steps_nodes.size(); ++ni) {
            if (_steps_nodes[ni].type != AssemblyStepsTreeNode::Type::Folder || _steps_nodes[ni].is_final_assembly)
                continue;
            if (collect_node_object_indices(ni).count(obj_idx) == 0)
                continue;
            auto step_str = into_u8(wxString::Format(_L("Step %d"), _steps_nodes[ni].step));
            auto all_str = _steps_nodes[ni].name + "(" + step_str + ")";
            step_names.push_back(_steps_nodes[ni].name.empty() ? step_str : all_str);
        }

        if (!text.empty())
            text += "\n";
        text += m_model->objects[obj_idx]->name + ": ";
        if (step_names.empty()) {
            text += into_u8(_L("not used in any assembly step"));
        } else {
            for (size_t i = 0; i < step_names.size(); ++i) {
                if (i > 0)
                    text += ", ";
                text += step_names[i];
            }
        }
    }

    if (text.empty()) {
        return;
    }
    plater->get_notification_manager()->push_notification(NotificationType::SelectObjectInWhichStep, NotificationManager::NotificationLevel::ImportantNotificationLevel, text);
}

void AssemblyStepsUtils::play_cur_keyframe_logic()
{
    if (m_playback_paused)
        return;

    if (m_play_global) {
        play_global_frame();
        return;
    }

    consume_play_queue_frame(true);
}

void AssemblyStepsUtils::consume_play_queue_frame(bool update_global_index)
{
    if (!m_keyframe_playing) {
        m_play_queue.clear();
        m_render_interpolated_part_number_labels = false;
        return;
    }
    if (m_play_queue.empty()) {
        m_keyframe_playing = false;
        m_render_interpolated_part_number_labels = false;
        return;
    }

    const KeyFrame &front = m_play_queue.front();
    if (!front.is_interpolation && front.play_node_idx >= 0) {
        select_node_and_show_volumes(front.play_node_idx);
    }
    // Sync the timeline-selected keyframe whenever the playhead lands on a

    if (!front.is_interpolation && front.play_frame_idx >= 0) {
        auto *entries = get_current_kf_entries();
        if (entries && front.play_frame_idx < (int) entries->size()) {
            m_keyframe_selected = front.play_frame_idx;
            refresh_guide_show_part_numbers_from_current();
        }
        // Local (single-node) playback does not advance the global play index the
        if (!m_play_global) {
            const int folder = find_parent_folder(m_selected_node);
            if (folder >= 0) {
                for (int gi = 0; gi < (int) m_play_frame_refs.size(); ++gi) {
                    if (m_play_frame_refs[gi].node_idx == folder &&
                        m_play_frame_refs[gi].frame_idx == front.play_frame_idx) {
                        m_assembly_play_index = gi + 1;
                        break;
                    }
                }
            }
        }
    }

    if (m_interpolate_part_number_label_arrow_end_offset && front.is_interpolation) {
        m_interpolated_part_number_label_frame = front;
        m_render_interpolated_part_number_labels = true;
    } else {
        m_render_interpolated_part_number_labels = false;
    }

    apply_keyframe_to_canvas(front);
    m_play_queue.pop_front();
    if (update_global_index && m_play_global) {
        if (m_assembly_play_index < m_assembly_play_count) {
            ++m_assembly_play_index;
        }
    }

    if (m_play_queue.empty()) {
        m_keyframe_playing = false;
        m_render_interpolated_part_number_labels = false;

        // Ensure camera matches the end frame state after playback, just like a static click would do.
        if (m_camera) {
            auto *entries = get_current_kf_entries();
            if (entries && m_keyframe_selected >= 0 && m_keyframe_selected < (int)entries->size()) {
                const KeyFrame &end_kf = (*entries)[m_keyframe_selected].data;
                const double cam_near   = m_camera->get_near_z();
                const double cam_far    = m_camera->get_far_z();
                const double cam_zoom   = m_camera->get_zoom();
                const double view_delta = (m_camera->get_view_matrix().matrix() - end_kf.view_matrix.matrix()).norm();
                const double proj_delta = (m_camera->get_projection_matrix().matrix() - end_kf.projection_matrix.matrix()).norm();
                BOOST_LOG_TRIVIAL(info)
                    << "[assembly-play-debug] playback end camera check:"
                    << " near_z=" << cam_near
                    << " far_z=" << cam_far
                    << " zoom=" << cam_zoom
                    << " end_zoom=" << end_kf.camera_zoom
                    << " view_delta=" << view_delta
                    << " proj_delta=" << proj_delta;
                // Re-apply the end frame camera logic (fit or rescale) so playback end
                // matches the static keyframe click result.
                look_cur_frame_logic((*entries)[m_keyframe_selected]);
            }
        }

        do_commond_callback("request_extra_frame");
    }
}

std::set<int> AssemblyStepsUtils::collect_node_object_indices(int node_idx) const
{
    std::set<int> object_indices;
    if (!m_model)
        return object_indices;

    const auto &step_nodes = m_model->get_assembly_steps_tree_data().nodes;
    std::function<void(int)> collect = [&](int idx) {
        if (idx < 0 || idx >= (int)step_nodes.size())
            return;
        const auto &node = step_nodes[idx];
        if (node.type == AssemblyStepsTreeNode::Type::Object && node.object_idx >= 0)
            object_indices.insert(node.object_idx);
        else if (node.type == AssemblyStepsTreeNode::Type::Folder)
            for (int child : node.children)
                collect(child);
    };
    collect(node_idx);
    return object_indices;
}

bool AssemblyStepsUtils::is_object_used_in_previous_steps(int object_idx, int folder_idx) const
{
    if (!m_model || object_idx < 0 || folder_idx < 0)
        return false;

    // Walk the step folders in their display order. As soon as we reach the
    for (int root_idx : _steps_roots) {
        if (root_idx == folder_idx)
            break;
        if (root_idx < 0 || root_idx >= (int) _steps_nodes.size())
            continue;
        // The final-assembly folder aggregates every object, so it must never
        // count as a "previous" introduction.
        if (_steps_nodes[root_idx].is_final_assembly)
            continue;
        const std::set<int> objs = collect_node_object_indices(root_idx);
        if (objs.count(object_idx) > 0)
            return true;
    }
    return false;
}

bool AssemblyStepsUtils::is_object_used_in_current_step(int object_idx, int folder_idx, int frame_id) const
{
    if (!m_model || object_idx < 0 || folder_idx < 0 || folder_idx >= (int) _steps_nodes.size())
        return false;
    // Frames are stored in play order: start frame first, transition frames in the
    // middle, end frame (id == 0) last.
    const auto &entries = _steps_nodes[folder_idx].kf_data.entries;

    // Locate the queried frame by its id within this step.
    int cur_pos = -1;
    for (int i = 0; i < (int) entries.size(); ++i) {
        if (entries[i].data.id == frame_id) {
            cur_pos = i;
            break;
        }
    }
    if (cur_pos < 0)
        return false;
    // The start frame has no preceding frame, so the object can never have been
    // used before within this step.
    if (entries[cur_pos].is_start())
        return false;
    // Transition / end frame: the object counts as already used in this step when it
    // appears (object- or volume-level pose) in any earlier frame of the same step.
    for (int i = 0; i < cur_pos; ++i) {
        const KeyFrame &kf = entries[i].data;
        if (kf.object_transformations.count(object_idx) > 0)
            return true;
        for (const auto &kv : kf.volume_transformations) {
            if (kv.first.first == object_idx)
                return true;
        }
    }
    return false;
}

bool AssemblyStepsUtils::is_empty_structure_step(int folder_idx) const
{
    if (!m_model)
        return true;
    const auto &step_nodes = m_model->get_assembly_steps_tree_data().nodes;
    if (folder_idx < 0 || folder_idx >= static_cast<int>(step_nodes.size()))
        return false;
    const auto &folder = step_nodes[folder_idx];
    if (folder.type != AssemblyStepsTreeNode::Type::Folder || folder.is_final_assembly)
        return false;

    bool has_object = false;
    std::function<void(int)> visit = [&](int idx) {
        if (has_object || idx < 0 || idx >= static_cast<int>(step_nodes.size()))
            return;
        const auto &node = step_nodes[idx];
        if (node.type == AssemblyStepsTreeNode::Type::Object) {
            has_object = true;
            return;
        }
        for (int child : node.children)
            visit(child);
    };
    visit(folder_idx);
    return !has_object;
}

void AssemblyStepsUtils::select_node_and_show_volumes(int node_idx)
{
    if (!m_model)
        return;

    const auto &step_nodes = m_model->get_assembly_steps_tree_data().nodes;
    if (node_idx < 0 || node_idx >= (int)step_nodes.size())
        return;

    for (int object_idx : collect_node_object_indices(node_idx)) {
        if (m_volumes) {
            for (GLVolume *vol : (*m_volumes).volumes) {
                if (vol && vol->composite_id.object_id == object_idx) vol->is_active = true;
            }
        }
    }
    do_commond_callback("dirty");
}

void AssemblyStepsUtils::apply_keyframe_display_mode(KeyframeDisplayMode mode)
{
    m_keyframe_display_mode = mode;
    apply_keyframe_display_mode();
}

void AssemblyStepsUtils::show_all_volume_normal_render() {
    auto &step_nodes = m_model->get_assembly_steps_tree_data().nodes;
    for (const auto &node : step_nodes) {
        if (node.type != AssemblyStepsTreeNode::Type::Object || node.object_idx < 0)
            continue;
        bool hidden = !node.visible;
        apply_object_state(node.object_idx, {!hidden, hidden ? 0.f : 1.f, hidden});
    }
}

void AssemblyStepsUtils::show_volumes_as_step_candidates()
{
    if (!m_model || !m_volumes)
        return;

    // Parts that already belong to the current step stay fully opaque; everything else
    // is dimmed as a selectable candidate (mirrors the Highlight display-mode style).
    std::set<std::pair<int, int>> current_vols;
    if (has_selected_node() && m_selected_node >= 0 && m_selected_node < (int) _steps_nodes.size()) {
        const int folder = find_parent_folder(m_selected_node);
        if (folder >= 0)
            current_vols = collect_folder_volume_pairs(folder);
    }

    const bool visible = m_keyframe_display_mode != KeyframeDisplayMode::OnlyCurrentStep;
    for (GLVolume *vol : m_volumes->volumes) {
        if (!vol)
            continue;
        const bool is_current = current_vols.count({vol->object_idx(), vol->volume_idx()}) > 0;
        apply_glvolume_state(vol, {visible, is_current ? 1.f : 0.15f, !is_current});
    }
    do_commond_callback("dirty");
}

void AssemblyStepsUtils::apply_keyframe_display_mode()
{
    if (!m_model)
        return;

    auto &step_nodes = m_model->get_assembly_steps_tree_data().nodes;
    if (is_empty_structure_step(m_selected_node)) {
        show_volumes_as_step_candidates();
        return;
    }
    if (m_keyframe_display_mode == KeyframeDisplayMode::All) {
        show_all_volume_normal_render();
    } else if (m_keyframe_display_mode == KeyframeDisplayMode::OnlyCurrentStep) {
        if (has_selected_node()) {
            int target = find_parent_folder(m_selected_node);
            std::set<std::pair<int, int>> current_vols;
            if (target >= 0 && target < (int) step_nodes.size())
                current_vols = collect_folder_volume_pairs(target);

            // Keep Object-node.visible in sync for any callers that still read it:
            // true when any of that object's volumes belong to the current step.
            std::set<int> objects_with_current_vol;
            for (const auto &key : current_vols)
                objects_with_current_vol.insert(key.first);
            for (auto &node : step_nodes) {
                if (node.type == AssemblyStepsTreeNode::Type::Object && node.object_idx >= 0)
                    node.visible = objects_with_current_vol.count(node.object_idx) > 0;
            }

            if (m_volumes) {
                for (GLVolume *vol : m_volumes->volumes) {
                    if (!vol)
                        continue;
                    const bool is_current = current_vols.count({vol->object_idx(), vol->volume_idx()}) > 0;
                    apply_glvolume_state(vol, {is_current, is_current ? 1.f : 0.f, !is_current});
                }
            }
        } else {
            for (auto &node : step_nodes) {
                if (node.type == AssemblyStepsTreeNode::Type::Object && node.object_idx >= 0) {
                    node.visible = true;//control glvolume visible
                }
            }
            show_all_volume_normal_render();
        }
    } else if (m_keyframe_display_mode == KeyframeDisplayMode::Highlight) {
        if (has_selected_node()) {
            int target = find_parent_folder(m_selected_node);
            std::set<std::pair<int, int>> current_vols;
            if (target >= 0 && target < (int) step_nodes.size())
                current_vols = collect_folder_volume_pairs(target);

            if (m_volumes) {
                for (GLVolume *vol : m_volumes->volumes) {
                    if (!vol)
                        continue;
                    const bool is_current = current_vols.count({vol->object_idx(), vol->volume_idx()}) > 0;
                    apply_glvolume_state(vol, {true, is_current ? 1.f : 0.15f, !is_current});
                }
            }
        } else {
            show_all_volume_normal_render();
        }
    }
    do_commond_callback("dirty");
}

void AssemblyStepsUtils::apply_tree_checked_display_mode(const AssemblyTreeData& tree,
    const std::unordered_map<std::string, bool>& checked)
{
    if (!m_model || !m_volumes)
        return;
    if (m_keyframe_display_mode == KeyframeDisplayMode::All) {
        show_all_volume_normal_render();
        do_commond_callback("dirty");
        return;
    }

    // Build volume membership from the live checkbox map (volume leaves first;
    // object-only checks expand to every volume of that object).
    std::set<std::pair<int, int>> checked_vols;
    std::set<int>                 objects_with_vol_checks;
    for (const auto &node : tree.nodes) {
        if (!node.selectable || node.object_idx < 0 || node.volume_idx < 0)
            continue;
        auto it = checked.find(node.uid);
        if (it == checked.end() || !it->second)
            continue;
        checked_vols.emplace(node.object_idx, node.volume_idx);
        objects_with_vol_checks.insert(node.object_idx);
    }
    for (const auto &node : tree.nodes) {
        if (!node.selectable || node.object_idx < 0 || node.volume_idx >= 0)
            continue;
        auto it = checked.find(node.uid);
        if (it == checked.end() || !it->second)
            continue;
        if (objects_with_vol_checks.count(node.object_idx) > 0)
            continue;
        if (node.object_idx >= (int) m_model->objects.size())
            continue;
        const ModelObject *obj = m_model->objects[node.object_idx];
        if (!obj)
            continue;
        for (int vi = 0; vi < (int) obj->volumes.size(); ++vi) {
            if (obj->volumes[vi])
                checked_vols.emplace(node.object_idx, vi);
        }
    }

    const bool highlight = m_keyframe_display_mode == KeyframeDisplayMode::Highlight;
    for (GLVolume *vol : m_volumes->volumes) {
        if (!vol)
            continue;
        const bool is_current = checked_vols.count({vol->object_idx(), vol->volume_idx()}) > 0;
        if (highlight)
            apply_glvolume_state(vol, {true, is_current ? 1.f : 0.15f, !is_current});
        else // OnlyCurrentStep
            apply_glvolume_state(vol, {is_current, is_current ? 1.f : 0.f, !is_current});
    }
    do_commond_callback("dirty");
}

KeyFrame AssemblyStepsUtils::interpolate_keyframe(
    const KeyFrame &from,
    const KeyFrame &to,
    double t,
    bool interpolate_part_number_label_arrow_end_offset)
{
    KeyFrame result;
    // A keyframe is "empty for interpolation purposes" when neither the
    // per-object instance matrices nor the per-volume matrices are populated.
    // Either map alone is enough to drive the GLVolume world; reject only
    // when both halves are missing on either side.
    const bool from_empty = from.object_transformations.empty() && from.volume_transformations.empty();
    const bool to_empty   = to.object_transformations.empty()   && to.volume_transformations.empty();
    if (from_empty || to_empty) {
        BOOST_LOG_TRIVIAL(warning) << "interpolate_keyframe: empty keyframe transforms (from_empty=" << from_empty << ", to_empty=" << to_empty << "), skip interpolation";
#if !BBL_RELEASE_TO_PUBLIC
#ifdef _WIN32
        __debugbreak();
#endif
#endif
        return result;
    }
    result.name = to.name;

    Eigen::Quaterniond q_from(from.view_matrix.matrix().block<3, 3>(0, 0));
    Eigen::Quaterniond q_to(to.view_matrix.matrix().block<3, 3>(0, 0));
    q_from.normalize();
    q_to.normalize();
    Eigen::Quaterniond q_interp = q_from.slerp(t, q_to);

    Vec3d tr_from  = from.view_matrix.matrix().block<3, 1>(0, 3);
    Vec3d tr_to    = to.view_matrix.matrix().block<3, 1>(0, 3);
    Vec3d tr_interp = (1.0 - t) * tr_from + t * tr_to;

    result.view_matrix = Transform3d::Identity();
    result.view_matrix.matrix().block<3, 3>(0, 0) = q_interp.toRotationMatrix();
    result.view_matrix.matrix().block<3, 1>(0, 3) = tr_interp;

    result.projection_matrix.matrix() =
        (1.0 - t) * from.projection_matrix.matrix() + t * to.projection_matrix.matrix();

    result.camera_target = (1.0 - t) * from.camera_target + t * to.camera_target;
    result.camera_zoom   = (1.0 - t) * from.camera_zoom + t * to.camera_zoom;

    // Slerp + lerp helper: produces an interpolated rigid transform between

    auto lerp_rigid = [t](const Transform3d &mat_a, const Transform3d &mat_b) {
        // Decompose into rotation + scale so we can interpolate each independently.
        // The 3x3 block of a 4x4 matrix mixes rotation and scale; feeding it directly
        // to Eigen::Quaterniond would give incorrect rotation for non-uniform scale.
        Geometry::Transformation trafo_a(mat_a), trafo_b(mat_b);

        // LERP translation and scale
        Vec3d pos_interp   = (1.0 - t) * mat_a.translation() + t * mat_b.translation();
        Vec3d scale_interp = (1.0 - t) * trafo_a.get_scaling_factor() + t * trafo_b.get_scaling_factor();

        // SLERP rotation from pure rotation matrices (no scale)
        Matrix3d rot_a = trafo_a.get_rotation_matrix().linear();
        Matrix3d rot_b = trafo_b.get_rotation_matrix().linear();
        Eigen::Quaterniond rq_a(rot_a);
        Eigen::Quaterniond rq_b(rot_b);
        rq_a.normalize();
        rq_b.normalize();
        Eigen::Quaterniond rq_interp = rq_a.slerp(t, rq_b);

        // Recombine: rotation * scale + translation
        Transform3d interp_mat   = Transform3d::Identity();
        Matrix3d    rot_scale    = rq_interp.toRotationMatrix();
        rot_scale.col(0)        *= scale_interp.x();
        rot_scale.col(1)        *= scale_interp.y();
        rot_scale.col(2)        *= scale_interp.z();
        interp_mat.linear()      = rot_scale;
        interp_mat.translation() = pos_interp;
        return interp_mat;
    };

    // Per-object instance assemble matrices.
    for (const auto &[obj_key, to_t] : to.object_transformations) {
        auto it_from = from.object_transformations.find(obj_key);
        if (it_from == from.object_transformations.end()) {
            result.object_transformations[obj_key] = to_t;
            continue;
        }
        result.object_transformations[obj_key] =
            Geometry::Transformation(lerp_rigid(it_from->second.get_matrix(), to_t.get_matrix()));
    }

    // Per-volume assemble matrices.
    for (const auto &[volume_key, to_t] : to.volume_transformations) {
        auto it_from = from.volume_transformations.find(volume_key);
        if (it_from == from.volume_transformations.end()) {
            result.volume_transformations[volume_key] = to_t;
            continue;
        }
        result.volume_transformations[volume_key] =
            Geometry::Transformation(lerp_rigid(it_from->second.get_matrix(), to_t.get_matrix()));
    }

    result.volume_names = to.volume_names;
    result.assembly_note = from.assembly_note;
    if (interpolate_part_number_label_arrow_end_offset) {
        for (PartNumberLabel &label : result.assembly_note.part_number_labels) {
            auto to_label = std::find_if(
                to.assembly_note.part_number_labels.begin(),
                to.assembly_note.part_number_labels.end(),
                [&label](const PartNumberLabel &candidate) {
                    return !candidate.part_guid.empty() && candidate.part_guid == label.part_guid;
                });
            if (to_label != to.assembly_note.part_number_labels.end())
                label.arrow_end_offset = (1.0 - t) * label.arrow_end_offset + t * to_label->arrow_end_offset;
        }
    }
    return result;
}

void AssemblyStepsUtils::build_local_play_queue()
{
    m_play_queue.clear();
    int num_frames = std::max(1, (int)(m_play_transition_duration / kPlayFrameInterval));

    auto *cur_entries = get_current_kf_entries();
    if (!cur_entries)
        return;

    std::vector<const KeyFrameEntry *> recorded;
    for (const auto &e : *cur_entries) {
        recorded.push_back(&e);
    }
    if (recorded.size() < 2)
        return;
    for (size_t k = 0; k + 1 < recorded.size(); ++k) {
        const KeyFrame &a = recorded[k]->data;
        const KeyFrame &b = recorded[k + 1]->data;
        int start = (k == 0) ? 0 : 1;
        for (int i = start; i <= num_frames; ++i) {
            double t = (double)i / num_frames;
            KeyFrame kf = interpolate_keyframe(a, b, t, m_interpolate_part_number_label_arrow_end_offset);
            // Tag boundary frames so consume_play_queue_frame can sync the
            const int boundary_entry_idx =
                (i == num_frames)        ? (int) (k + 1) :
                (k == 0 && i == 0)       ? 0             :
                                           -1;
            kf.is_interpolation = (boundary_entry_idx < 0);
            kf.play_frame_idx   = boundary_entry_idx;
            m_play_queue.push_back(std::move(kf));
        }
    }
}

void AssemblyStepsUtils::exit_note_edit()
{
    ImGui::ClearActiveID();
    ImGui::FocusWindow(nullptr);
    clear_note_selection();
}

void AssemblyStepsUtils::clear_note_selection()
{
    set_note_edit_controls_visible(false);
    m_note_selected_type = AssemblyNoteSelectionType::None;
    m_note_selected_idx = -1;
    m_guide_note_tool_selected = -1;
    // Connection Type (Clip / Glue / Screw) shares the note-edit lifecycle, so it
    // must drop its highlighted state on every exit path too.
    m_guide_connection_selected = -1;
}

void AssemblyStepsUtils::invalidate_play_frame_refs()
{
    m_play_frame_refs_dirty = true;
    m_play_frame_refs.clear();
}

void AssemblyStepsUtils::rebuild_play_frame_refs()
{
    if (!m_play_frame_refs_dirty)
        return;
    update_final_assembly_step_number_to_max();
    m_play_frame_refs.clear();
    if (!m_model)
        return;

    auto &step_nodes = m_model->get_assembly_steps_tree_data().nodes;

    struct NodeEntry {
        int node_idx;
        std::vector<int> frame_indices;
    };
    std::vector<NodeEntry> ordered_nodes;

    std::set<int> step_card_nodes;
    int final_assembly_folder = -1;
    const AssemblyStructurePanelData panel_data = build_assembly_structure_panel_data();
    for (const AssemblyStructureCard &card : panel_data.cards) {
        if (card.node_idx < 0 || card.node_idx >= (int) step_nodes.size())
            continue;
        if (step_nodes[card.node_idx].type != AssemblyStepsTreeNode::Type::Folder)
            continue;
        step_card_nodes.insert(card.node_idx);
        if (step_nodes[card.node_idx].is_final_assembly)
            final_assembly_folder = card.node_idx;
    }

    auto node_type_name = [](AssemblyStepsTreeNode::Type type) {
        switch (type) {
        case AssemblyStepsTreeNode::Type::Folder: return "Folder";
        case AssemblyStepsTreeNode::Type::Object: return "Object";
        case AssemblyStepsTreeNode::Type::Volume: return "Volume";
        }
        return "Unknown";
    };

    auto log_unrelated_keyframe_node = [&](int node_idx, const char *reason) {
        const auto &node = step_nodes[node_idx];
        BOOST_LOG_TRIVIAL(warning)
            << "AssemblySteps::rebuild_play_frame_refs skip unrelated keyframe node"
            << " idx=" << node_idx
            << " type=" << node_type_name(node.type)
            << " name=\"" << node.name << "\""
            << " step=" << node.step
            << " id=" << node.id
            << " frames=" << node.kf_data.entries.size()
            << " reason=" << reason;
    };

    auto append_node_entry = [&](int node_idx) {
        auto &entries = step_nodes[node_idx].kf_data.entries;
        if (entries.empty())
            return;
        NodeEntry ne;
        ne.node_idx = node_idx;
        for (int fi = 0; fi < (int) entries.size(); ++fi) {
            if (!entries[fi].data.is_camera_define) {
                record_camera(entries[fi].data);
                entries[fi].data.is_camera_define = true;
            }
            ne.frame_indices.push_back(fi);
        }
        if (!ne.frame_indices.empty())
            ordered_nodes.push_back(std::move(ne));
    };

    // Diagnostic only: warn about keyframe-bearing nodes that no Assembly Structure
    for (int i = 0; i < (int)step_nodes.size(); ++i) {
        if (i == final_assembly_folder)
            continue;
        if (step_nodes[i].kf_data.entries.empty())
            continue;
        if (step_nodes[i].type != AssemblyStepsTreeNode::Type::Folder) {
            log_unrelated_keyframe_node(i, "not_folder");
            continue;
        }
        if (step_card_nodes.find(i) == step_card_nodes.end())
            log_unrelated_keyframe_node(i, "no_step_card");
    }

    // Append playable folders following the Assembly Structure card order, not the node
    for (const AssemblyStructureCard &card : panel_data.cards) {
        const int node_idx = card.node_idx;
        if (node_idx < 0 || node_idx >= (int) step_nodes.size())
            continue;
        if (node_idx == final_assembly_folder)
            continue;
        if (step_nodes[node_idx].type != AssemblyStepsTreeNode::Type::Folder)
            continue;
        append_node_entry(node_idx);
    }

    // Append final assembly folder's frames at the end.
    if (final_assembly_folder >= 0) {
        append_node_entry(final_assembly_folder);
    }

    if (m_play_strategy == PlayStrategy::Sequential) {//todo
    }

    for (const auto &ne : ordered_nodes) {
        for (int fi : ne.frame_indices)
            m_play_frame_refs.push_back({ne.node_idx, fi});
    }

    m_play_frame_refs_dirty = false;
}



// --- Static members for assembly tree UI ---
std::unordered_map<std::string, bool> AssemblyStepsUtils::s_assembly_tree_open_nodes;
AssemblyStepsUtils::AssemblyTreeIcons AssemblyStepsUtils::s_assembly_tree_icons;

bool AssemblyStepsUtils::load_assembly_tree_icons(float sc)
{
    if (s_assembly_tree_icons.loaded)
        return true;

    const std::string image_path = Slic3r::resources_dir() + "/images/";
    const unsigned tree_icon_texture_size = static_cast<unsigned>(std::round(28.0f * sc));
    const unsigned select_icon_texture_size = static_cast<unsigned>(std::round(24.0f * sc));
    s_assembly_tree_icons.loaded = IMTexture::load_from_svg_file(image_path + "tree_expand.svg", tree_icon_texture_size, tree_icon_texture_size,
                                                                  s_assembly_tree_icons.expand) &&
                                   IMTexture::load_from_svg_file(image_path + "tree_collapse.svg", tree_icon_texture_size, tree_icon_texture_size,
                                                                  s_assembly_tree_icons.collapse) &&
                                   IMTexture::load_from_svg_file(image_path + "tree_select.svg", select_icon_texture_size, select_icon_texture_size,
                                                                  s_assembly_tree_icons.select) &&
                                   IMTexture::load_from_svg_file(image_path + "tree_search.svg", select_icon_texture_size, select_icon_texture_size,
                                                                  s_assembly_tree_icons.search);
    // Dark variants are best-effort: a missing file must not flip `loaded`.
    IMTexture::load_from_svg_file(image_path + "tree_expand_dark.svg", tree_icon_texture_size, tree_icon_texture_size,
                                  s_assembly_tree_icons.expand_dark);
    IMTexture::load_from_svg_file(image_path + "tree_collapse_dark.svg", tree_icon_texture_size, tree_icon_texture_size,
                                  s_assembly_tree_icons.collapse_dark);
    IMTexture::load_from_svg_file(image_path + "tree_search_dark.svg", select_icon_texture_size, select_icon_texture_size,
                                  s_assembly_tree_icons.search_dark);
    return s_assembly_tree_icons.loaded;
}


void AssemblyStepsUtils::clear_active_assembly_tree_checked()
{
    m_active_assembly_tree_checked = nullptr;
}

void AssemblyStepsUtils::create_assembly_steps_from_step_import_tree(
    const std::vector<StepImportTreeNode>& step_nodes,
    const std::string&                            source_path)
{
    if (m_model == nullptr || step_nodes.empty())
        return;

    BOOST_LOG_TRIVIAL(info) << "AssemblyStepsSeed: begin path=" << source_path
                            << ", step_nodes=" << step_nodes.size();
    auto valid_object = [this](int object_idx) {
        return m_model &&
               object_idx >= 0 &&
               object_idx < static_cast<int>(m_model->objects.size()) &&
               m_model->objects[object_idx] != nullptr;
    };

    auto child_node = [&step_nodes](size_t child_id) -> const StepImportTreeNode* {
        if (child_id == 0 || child_id > step_nodes.size())
            return nullptr;
        return &step_nodes[child_id - 1];
    };

    auto is_compound = [](const StepImportTreeNode& step_node) {
        return step_node.component_count > 0;
    };

    auto collect_direct_objects = [&](const StepImportTreeNode &step_node, bool ignore_compound = false) {
        std::vector<int> object_idxs;
        for (size_t child_id : step_node.children) {
            const StepImportTreeNode* child = child_node(child_id);
            if (child == nullptr || (!ignore_compound && is_compound(*child)))
                continue;
            if (!valid_object(child->model_object_idx))
                continue;
            if (std::find(object_idxs.begin(), object_idxs.end(), child->model_object_idx) == object_idxs.end())
                object_idxs.push_back(child->model_object_idx);
        }
        return object_idxs;
    };

    auto emit_step_for_compound = [&](const StepImportTreeNode& compound, bool has_compound_child) {
        std::vector<int> top_object_idxs = collect_direct_objects(compound, has_compound_child);
        if (top_object_idxs.empty()) {
            BOOST_LOG_TRIVIAL(info) << "AssemblyStepsSeed: skip compound key=" << compound.object_key
                                    << " (no direct SOLID objects)";
            return;
        }

        int folder_idx = create_folder_node(compound.name, next_step());
        if (folder_idx < 0) {
            BOOST_LOG_TRIVIAL(warning) << "AssemblyStepsSeed: failed to create folder for key=" << compound.object_key;
            return;
        }
        auto& steps_tree = m_model->get_assembly_steps_tree_data();
        steps_tree.roots.push_back(folder_idx);

        for (int object_idx : top_object_idxs) {
            if (!valid_object(object_idx))
                continue;
            const auto &obj = m_model->objects[object_idx];
            int obj_node_idx = create_object_node(
                object_idx, obj->name, obj->id().id);
            if (obj_node_idx < 0)
                continue;
            steps_tree.nodes[folder_idx].children.push_back(obj_node_idx);
        }

        ensure_default_keyframe(folder_idx);
        fill_folder_keyframes_from_children(folder_idx);

        BOOST_LOG_TRIVIAL(info) << "AssemblyStepsSeed: emit step folder=" << folder_idx
                                << " key=" << compound.object_key
                                << " name=" << compound.name
                                << " top_objects=" << top_object_idxs.size()
                                << " children=" << steps_tree.nodes[folder_idx].children.size()
                                << " mixed=" << has_compound_child;
    };

    std::function<void(size_t)> dfs = [&](size_t node_id) {
        const StepImportTreeNode* node = child_node(node_id);
        if (node == nullptr || !is_compound(*node))
            return;

        bool has_compound_child = false;
        bool has_solid_child = false;
        for (size_t child_id : node->children) {
            const StepImportTreeNode* child = child_node(child_id);
            if (child == nullptr)
                continue;
            if (is_compound(*child))
                has_compound_child = true;
            else
                has_solid_child = true;
        }

        if (!has_compound_child && has_solid_child) {
            emit_step_for_compound(*node, false);
            return;
        }

        for (size_t child_id : node->children)
            dfs(child_id);

        if (has_compound_child && has_solid_child)
            emit_step_for_compound(*node, true);
    };

    for (const StepImportTreeNode& step_node : step_nodes) {
        if (step_node.parent_id == 0)
            dfs(step_node.id);
    }

    // Create the final-assembly step AFTER all regular steps so its step number is last.
    {
        auto& steps_tree = m_model->get_assembly_steps_tree_data();
        int final_folder_idx = create_folder_node(_u8L("Final assembly"), next_step());
        if (final_folder_idx >= 0) {
            steps_tree.nodes[final_folder_idx].is_final_assembly = true;
            steps_tree.roots.push_back(final_folder_idx);
            for (int oi = 0; oi < (int) m_model->objects.size(); ++oi) {
                if (!valid_object(oi)) continue;
                const auto &obj = m_model->objects[oi];
                int obj_node = create_object_node(oi, obj->name, obj->id().id);
                if (obj_node >= 0)
                    steps_tree.nodes[final_folder_idx].children.push_back(obj_node);
            }
            ensure_default_keyframe(final_folder_idx);
            fill_folder_keyframes_from_children(final_folder_idx);
        }
    }
    update_model_object_tree();
    // Diagnostic dump
    {
        const auto &steps_tree = m_model->get_assembly_steps_tree_data();
        BOOST_LOG_TRIVIAL(info) << "AssemblyStepsSeed: end nodes=" << steps_tree.nodes.size()
                                << " roots=" << steps_tree.roots.size();
        std::set<int> visited;
        std::function<void(int, int)> dump_node = [&](int node_idx, int depth) {
            if (node_idx < 0 || node_idx >= static_cast<int>(steps_tree.nodes.size())) {
                BOOST_LOG_TRIVIAL(info) << "AssemblyStepsSeed: " << std::string(depth * 2, ' ')
                                        << "- <invalid idx=" << node_idx << ">";
                return;
            }

            const auto &nd = steps_tree.nodes[node_idx];
            const bool is_folder = nd.type == AssemblyStepsTreeNode::Type::Folder;
            BOOST_LOG_TRIVIAL(info) << "AssemblyStepsSeed: " << std::string(depth * 2, ' ')
                                    << "- [" << (is_folder ? "Folder" : "Object") << "]"
                                    << " idx=" << node_idx
                                    << " id=" << nd.id
                                    << " step=" << nd.step
                                    << " name=" << nd.name
                                    << " object_idx=" << nd.object_idx
                                    << " object_id=" << nd.object_id
                                    << " children=" << nd.children.size();

            if (!visited.insert(node_idx).second) {
                BOOST_LOG_TRIVIAL(info) << "AssemblyStepsSeed: " << std::string((depth + 1) * 2, ' ')
                                        << "- <cycle detected>";
                return;
            }

            for (int child_idx : nd.children)
                dump_node(child_idx, depth + 1);
        };

        for (int root_idx : steps_tree.roots)
            dump_node(root_idx, 0);
    }
}

AssemblyTreeData AssemblyStepsUtils::build_assembly_tree_data()
{
    AssemblyTreeData tree;
    if (m_model == nullptr)
        return tree;

    auto add_node = [&tree, this](int parent_id, const std::string& uid, const std::string& label, int object_idx, int volume_idx, bool selectable = true) {
        AssemblyTreeNodeData node;
        node.id         = static_cast<int>(tree.nodes.size());
        node.parent_id  = parent_id;
        node.uid        = uid;
        node.label      = label;
        node.object_idx = object_idx;
        node.volume_idx = volume_idx;
        node.selectable = selectable;
        tree.nodes.emplace_back(std::move(node));

        if (parent_id >= 0 && parent_id < static_cast<int>(tree.nodes.size()))
            tree.nodes[parent_id].children.emplace_back(tree.nodes.back().id);
        else
            tree.roots.emplace_back(tree.nodes.back().id);

        return tree.nodes.back().id;
    };

    std::vector<bool> object_in_step_tree(m_model->objects.size(), false);

    // Carry over existing "step:<N>" subtrees from the previous in-memory
    // AssemblyTreeData. STEP imports register directly via
    // append_step_import_to_assembly_tree() and are persisted in 3MF.
    std::unordered_map<int, int> old_step_id_to_new;
    const AssemblyTreeData& old_assembly_tree = m_model->get_assembly_tree_data();
    old_step_id_to_new.reserve(old_assembly_tree.nodes.size());
    for (const auto& old_node : old_assembly_tree.nodes) {
        if (old_node.uid.rfind("step:", 0) != 0)
            continue;

        int parent_id = -1;
        if (old_node.parent_id >= 0) {
            auto it = old_step_id_to_new.find(old_node.parent_id);
            if (it != old_step_id_to_new.end())
                parent_id = it->second;
        }

        bool selectable = old_node.volume_idx < 0;
        if (selectable && old_node.children.empty() &&
            old_node.object_idx >= 0 && static_cast<size_t>(old_node.object_idx) < m_model->objects.size() &&
            m_model->objects[old_node.object_idx] != nullptr &&
            m_model->objects[old_node.object_idx]->volumes.size() > 1)
            selectable = false;

        const int new_id = add_node(parent_id, old_node.uid, old_node.label,
                                    old_node.object_idx, old_node.volume_idx,
                                    selectable);
        old_step_id_to_new.emplace(old_node.id, new_id);

        if (old_node.object_idx >= 0 && static_cast<size_t>(old_node.object_idx) < object_in_step_tree.size())
            object_in_step_tree[old_node.object_idx] = m_model->objects[old_node.object_idx] != nullptr;
    }

    auto add_object_node = [this, &add_node](int parent_id, int object_idx) {
        if (object_idx < 0 || static_cast<size_t>(object_idx) >= m_model->objects.size())
            return;

        const ModelObject* object = m_model->objects[object_idx];
        if (object == nullptr)
            return;

        const std::string object_label = object->name.empty() ? _u8L("Object") : object->name;
        const int object_node = add_node(parent_id, "object:" + std::to_string(object_idx), object_label, object_idx, -1);
        if (object->volumes.size() <= 1)
            return;

        for (size_t volume_idx = 0; volume_idx < object->volumes.size(); ++volume_idx) {
            const ModelVolume* volume = object->volumes[volume_idx];
            if (volume == nullptr)
                continue;

            const std::string volume_label = volume->name.empty() ? _u8L("Part") : volume->name;
            add_node(object_node,
                     "object:" + std::to_string(object_idx) + "/volume:" + std::to_string(volume_idx),
                     volume_label,
                     object_idx,
                     static_cast<int>(volume_idx),
                     false);
        }
    };

    std::vector<bool> object_added(m_model->objects.size(), false);
    PartPlateList& plate_list = wxGetApp().plater()->get_partplate_list();
    for (int plate_idx = 0; plate_idx < plate_list.get_plate_count(); ++plate_idx) {
        PartPlate* plate = plate_list.get_plate(plate_idx);
        if (plate == nullptr)
            continue;

        std::vector<int> object_indices;
        for (const ModelObject* plate_object : plate->get_objects_on_this_plate()) {
            auto object_it = std::find(m_model->objects.begin(), m_model->objects.end(), plate_object);
            if (object_it == m_model->objects.end())
                continue;
            const int object_idx = static_cast<int>(std::distance(m_model->objects.begin(), object_it));
            if ((object_idx < static_cast<int>(object_in_step_tree.size()) && object_in_step_tree[object_idx]) ||
                (object_idx < static_cast<int>(object_added.size()) && object_added[object_idx]))
                continue;
            object_indices.emplace_back(object_idx);
            object_added[object_idx] = true;
        }

        if (object_indices.empty())
            continue;

        const int plate_node = add_node(-1, "plate:" + std::to_string(plate_idx), (boost::format(_u8L("Plate %1%")) % (plate_idx + 1)).str(), -1, -1);
        for (int object_idx : object_indices)
            add_object_node(plate_node, object_idx);
    }

    for (int object_idx = 0; object_idx < static_cast<int>(m_model->objects.size()); ++object_idx) {
        if ((object_idx >= static_cast<int>(object_in_step_tree.size()) || !object_in_step_tree[object_idx]) &&
            (object_idx >= static_cast<int>(object_added.size()) || !object_added[object_idx]))
            add_object_node(-1, object_idx);
    }

    // Prune nodes whose ModelObject/Volume has been removed
    std::vector<int> old_to_new(tree.nodes.size(), -1);
    std::function<bool(int)> mark_kept_node = [&](int node_id) {
        if (node_id < 0 || node_id >= static_cast<int>(tree.nodes.size()))
            return false;

        const auto& node = tree.nodes[node_id];
        bool has_kept_child = false;
        for (int child_id : node.children)
            has_kept_child = mark_kept_node(child_id) || has_kept_child;

        bool has_valid_model_item = false;
        if (node.object_idx >= 0 && node.object_idx < static_cast<int>(m_model->objects.size()) &&
            m_model->objects[node.object_idx] != nullptr) {
            const ModelObject* object = m_model->objects[node.object_idx];
            has_valid_model_item = node.volume_idx < 0 ||
                                   (static_cast<size_t>(node.volume_idx) < object->volumes.size() && object->volumes[node.volume_idx] != nullptr);
        }

        if (has_valid_model_item || has_kept_child) {
            old_to_new[node_id] = 0;
            return true;
        }
        return false;
    };
    for (int root_id : tree.roots)
        mark_kept_node(root_id);

    AssemblyTreeData pruned_tree;
    std::function<void(int, int)> append_kept_node = [&](int old_id, int parent_id) {
        if (old_id < 0 || old_id >= static_cast<int>(tree.nodes.size()) || old_to_new[old_id] < 0)
            return;

        auto node = tree.nodes[old_id];
        node.id        = static_cast<int>(pruned_tree.nodes.size());
        node.parent_id = parent_id;
        node.children.clear();
        pruned_tree.nodes.emplace_back(std::move(node));
        const int new_id = pruned_tree.nodes.back().id;
        old_to_new[old_id] = new_id;

        if (parent_id >= 0)
            pruned_tree.nodes[parent_id].children.emplace_back(new_id);
        else
            pruned_tree.roots.emplace_back(new_id);

        for (int child_id : tree.nodes[old_id].children)
            append_kept_node(child_id, new_id);
    };
    for (int root_id : tree.roots)
        append_kept_node(root_id, -1);

    tree = std::move(pruned_tree);
    return tree;
}
// NoteColorItem / kNoteColors / note_color_* helpers moved to
// AssemblyStepsUtilsInternal.hpp (shared with AssemblyStepsUtilsImgui.cpp).
void AssemblyStepsUtils::update_part_number_label_forbidden_layout_areas(float canvas_w, float canvas_h)
{
    auto clear_forbidden_area = [](LabelLayoutForbiddenRect &area) {
        area.min = ImVec2(0.0f, 0.0f);
        area.max = ImVec2(0.0f, 0.0f);
    };

    if (canvas_w <= 0.0f || canvas_h <= 0.0f || is_show_video_title_mode()) {
        clear_forbidden_area(m_part_number_label_forbidden_left_area);
        clear_forbidden_area(m_part_number_label_forbidden_bottom_area);
        return;
    }

    const float sc = std::max(0.1f, m_imgui_scale);
    const float pad = 8.0f * sc;

    // Bottom-left floating assembly controls.
    const float left_w = 150.0f * sc;
    const float left_h = 72.0f * sc;
    m_part_number_label_forbidden_left_area.min = ImVec2(0.0f, std::max(0.0f, canvas_h - left_h - pad));
    m_part_number_label_forbidden_left_area.max = ImVec2(std::min(canvas_w, left_w + pad), canvas_h);

    // Bottom-center play bar plus the lower assembly control strip.
    const float bottom_w = std::min(canvas_w, 760.0f * sc);
    const float bottom_h = 118.0f * sc;
    m_part_number_label_forbidden_bottom_area.min = ImVec2(std::max(0.0f, canvas_w * 0.5f - bottom_w * 0.5f),
                                                           std::max(0.0f, canvas_h - bottom_h - pad));
    m_part_number_label_forbidden_bottom_area.max = ImVec2(std::min(canvas_w, canvas_w * 0.5f + bottom_w * 0.5f),
                                                           canvas_h);
}

void AssemblyStepsUtils::reseed_assembly_tree_checked_from_step(int step_node_idx, const AssemblyTreeData &tree)
{
    if (!m_model || step_node_idx < 0)
        return;

    auto &steps_tree = m_model->get_assembly_steps_tree_data();
    if (step_node_idx >= static_cast<int>(steps_tree.nodes.size()))
        return;

    auto &checked = steps_tree.nodes[step_node_idx].assembly_tree_checked;
    if (!checked)
        checked.emplace();

    // assembly_tree_checked is the source of truth for volume-level membership.
    // Keep it when it already records any checked leaf; only bootstrap from the
    // step's Object children when the map is empty (legacy steps / first open).
    bool has_any_checked = false;
    for (const auto &p : *checked) {
        if (p.second) {
            has_any_checked = true;
            break;
        }
    }
    if (!has_any_checked) {
        std::set<int> step_objects;
        std::function<void(int)> collect_step_objects = [&](int node_idx) {
            if (node_idx < 0 || node_idx >= static_cast<int>(steps_tree.nodes.size()))
                return;
            const auto &step_node = steps_tree.nodes[node_idx];
            if (step_node.type == AssemblyStepsTreeNode::Type::Object && step_node.object_idx >= 0)
                step_objects.insert(step_node.object_idx);
            for (int child_idx : step_node.children)
                collect_step_objects(child_idx);
        };
        collect_step_objects(step_node_idx);

        checked->clear();
        for (const auto &node : tree.nodes) {
            if (!node.selectable || node.object_idx < 0)
                continue;
            if (step_objects.find(node.object_idx) == step_objects.end())
                continue;
            // Mark both object and volume rows so the subtree checkbox shows All.
            (*checked)[node.uid] = true;
        }
    }

    m_active_assembly_tree_checked = &*checked;
    m_assembly_tree_ui_original_checked = *checked;
    m_assembly_tree_ui_current_folder_node = step_node_idx;
}


// ----------------------------------------------------------------------------
// Assembly view capture: screenshot (PNG), video recording (MP4), and
// assembly steps export.
// Migrated from GLCanvas3D so the canvas only owns the imgui debug buttons.
// ----------------------------------------------------------------------------

bool AssemblyStepsUtils::capture_assembly_screenshot_to_png(const std::string &filename)
{
    if (m_camera == nullptr)
        return false;

    const std::array<int, 4> &vp = m_camera->get_viewport();
    const int w = vp[2];
    const int h = vp[3];
    if (w <= 0 || h <= 0)
        return false;

    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
    glsafe(::glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data()));

    // write_gl_rgba_to_file() already flips GL bottom-to-top rows.
    const bool ok = Slic3r::png::write_gl_rgba_to_file(filename.c_str(), w, h, pixels.data());
    BOOST_LOG_TRIVIAL(info) << "capture_assembly_screenshot_to_png: screenshot saved to " << filename << ", ok=" << ok;
    return ok;
}

void AssemblyStepsUtils::process_video_capture_per_frame()
{
    if (m_steps_video_export_active) {
        process_assembly_steps_video_export();
        return;
    }

    if (!m_video_recording)
        return;

    const bool has_pbo = m_pbo_reader && m_pbo_reader->is_initialized();
    const bool has_rec = m_mp4_recorder && m_mp4_recorder->is_recording();
    BOOST_LOG_TRIVIAL(debug) << "capture_loop: m_video_recording=1 has_pbo=" << has_pbo << " has_rec=" << has_rec;
    if (!(has_pbo && has_rec))
        return;

    const uint8_t *prev = m_pbo_reader->map_previous();
    BOOST_LOG_TRIVIAL(debug) << "capture_loop: map_previous=" << (prev ? "OK" : "null");
    if (prev) {
        m_mp4_recorder->push_frame(prev, m_pbo_reader->byte_size());
        m_pbo_reader->unmap();
    }
    m_pbo_reader->begin_read();
    do_commond_callback("request_extra_frame");
}

void AssemblyStepsUtils::process_assembly_steps_video_export()
{
    if (!m_steps_video_export_active)
        return;

    if (!m_mp4_recorder || !m_mp4_recorder->is_recording()) {
        // Recorder gone abort.
        m_steps_video_export_active = false;
        m_video_recording = false;
        m_is_export_mode = false;
        m_steps_video_export_path.clear();
        hide_assembly_export_progress();
        do_commond_callback("request_extra_frame");
        return;
    }

    // Defensive: re-assert export mode every frame while we are recording.

    m_is_export_mode = true;

    // Skip the very first capture frame after m_steps_video_export_active was
    if (m_video_export_skip_first_frame) {
        m_video_export_skip_first_frame = false;
        do_commond_callback("dirty");
        do_commond_callback("request_extra_frame");
        return;
    }

    // Capture the current rendered frame into the video.
    if (m_camera) {
        const std::array<int, 4> &vp = m_camera->get_viewport();
        const int w = vp[2];
        const int h = vp[3];
        if (w > 0 && h > 0) {
            std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
            glsafe(::glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data()));
            m_mp4_recorder->push_frame(pixels.data(), pixels.size());
        }
    }

    // Check if playback has finished (not playing and reached the end).
    const bool playback_done = !m_play_global && !m_keyframe_playing && m_play_queue.empty();
    if (playback_done) {
        update_assembly_export_progress(ExportType::MP4, m_steps_video_export_path, (int)m_play_frame_refs.size(), (int)m_play_frame_refs.size());
        m_mp4_recorder->stop();
        m_video_recording = false;
        BOOST_LOG_TRIVIAL(info) << "assembly steps video export: done -> " << m_steps_video_export_path;
        // Reveal the generated MP4 in the file manager before the path is cleared.
        open_export_output_folder(m_steps_video_export_path);
        track_assembly_view_export(ExportType::MP4);
        m_steps_video_export_active = false;
        m_steps_video_export_path.clear();
        m_is_export_mode = false;
        save_existing_project_if_dirty();
        hide_assembly_export_progress();
        do_commond_callback("request_extra_frame");
        return;
    }

    update_assembly_export_progress(ExportType::MP4, m_steps_video_export_path, m_assembly_play_index, std::max(1, (int)m_play_frame_refs.size()));
    do_commond_callback("request_extra_frame");
}

void AssemblyStepsUtils::process_assembly_pdf_capture()
{
    if (m_steps_export_active)
        process_assembly_steps_export();
}

} // namespace GUI
} // namespace Slic3r

