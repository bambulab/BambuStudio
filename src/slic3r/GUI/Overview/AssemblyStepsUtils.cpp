#include "AssemblyStepsUtils.hpp"
#include "AssemblyPdfExportDialog.hpp"
#include "TinyExportMardDown.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/PNGReadWrite.hpp"
#include "libslic3r/AppConfig.hpp"

#include "../I18N.hpp"
#include "../ImGuiWrapper.hpp"
#include "../GLShader.hpp"
#include "../GUI_App.hpp"
#include "../GUI.hpp"
#include "../MainFrame.hpp"
#include "../Plater.hpp"
#include "../NotificationManager.hpp"
#include "../Gizmos/GLGizmoMeasure.hpp"
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

#include <wx/filedlg.h>

#include <hpdf/hpdf.h>

#include <GL/glew.h>
#include <imgui/imgui_internal.h>

#include <chrono>
#include <climits>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>

#define _steps_nodes m_model->get_assembly_steps_tree_data().nodes
#define _steps_roots m_model->get_assembly_steps_tree_data().roots

namespace Slic3r {
namespace GUI {
using namespace Slic3r;

namespace {
static constexpr const char *ASSEMBLY_LABEL_FONT_SIZE_CONFIG_KEY = "assembly_part_number_label_font_size";
static constexpr float ASSEMBLY_LABEL_DEFAULT_FONT_SIZE = 25.0f;
static constexpr float ASSEMBLY_LABEL_MAX_FONT_SIZE_FACTOR = 2.5f;

const char *assembly_view_export_type_name(ExportType type)
{
    switch (type) {
    case ExportType::PDF:      return "pdf";
    case ExportType::MarkDown: return "markdown";
    case ExportType::MP4:      return "mp4";
    }
    return "";
}

// Truncate `s` to at most `max_chars` UTF-8 code points; append "..." when
// truncation actually happens. ASCII counts 1 byte per char, CJK 3 bytes.
// Used by the "Assembly Structure" panel to clip overflow chip labels.
std::string utf8_truncate_with_ellipsis(const std::string &s, size_t max_chars)
{
    size_t count = 0;
    size_t i     = 0;
    while (i < s.size() && count < max_chars) {
        const unsigned char b = static_cast<unsigned char>(s[i]);
        size_t adv = 1;
        if      ((b & 0x80) == 0x00) adv = 1;
        else if ((b & 0xE0) == 0xC0) adv = 2;
        else if ((b & 0xF0) == 0xE0) adv = 3;
        else if ((b & 0xF8) == 0xF0) adv = 4;
        if (i + adv > s.size()) break;
        i += adv;
        ++count;
    }
    if (i >= s.size())
        return s;
    return s.substr(0, i) + "...";
}

std::string utf8_fit_with_ellipsis(const std::string &s, float max_width)
{
    if (max_width <= 0.0f)
        return std::string();
    if (ImGui::CalcTextSize(s.c_str()).x <= max_width)
        return s;

    const std::string ellipsis = "...";
    const float ellipsis_w = ImGui::CalcTextSize(ellipsis.c_str()).x;
    if (ellipsis_w >= max_width)
        return ellipsis;

    std::string best;
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char b = static_cast<unsigned char>(s[i]);
        size_t adv = 1;
        if      ((b & 0x80) == 0x00) adv = 1;
        else if ((b & 0xE0) == 0xC0) adv = 2;
        else if ((b & 0xF0) == 0xE0) adv = 3;
        else if ((b & 0xF8) == 0xF0) adv = 4;
        if (i + adv > s.size())
            break;

        const std::string candidate = s.substr(0, i + adv) + ellipsis;
        if (ImGui::CalcTextSize(candidate.c_str()).x > max_width)
            break;
        best = candidate;
        i += adv;
    }

    return best.empty() ? ellipsis : best;
}

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
            exit_note_edit();
            exit_render_assembly_tree_ui();
            //this clear_when_no_selection(); Trigger camera rotation and exit the current step editing with a single click
        }
        m_selection_origin = origin;
    }
}

void AssemblyStepsUtils::clear_when_no_selection()
{
    selected_node     = -1;
    m_keyframe_selected = -1;
    m_last_folder_idx = -1;
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

void AssemblyStepsUtils::set_assembly_camera_locked(bool locked)
{
    if (m_assembly_camera_locked == locked)
        return;
    m_assembly_camera_locked = locked;
    reset_assembly_camera_lock_attempt();
}

void AssemblyStepsUtils::toggle_assembly_camera_locked()
{
    set_assembly_camera_locked(!m_assembly_camera_locked);
}

void AssemblyStepsUtils::reset_assembly_camera_lock_attempt()
{
    m_assembly_camera_lock_last_attempt_at = {};
}

bool AssemblyStepsUtils::has_assembly_camera_lock_attempt() const
{
    return m_assembly_camera_lock_last_attempt_at.time_since_epoch().count() != 0;
}

bool AssemblyStepsUtils::mark_assembly_camera_lock_attempt_if_due(std::chrono::steady_clock::time_point now,
                                                                  std::chrono::milliseconds min_interval)
{
    const bool stamp_unset = !has_assembly_camera_lock_attempt();
    const auto elapsed = stamp_unset ? min_interval :
        std::chrono::duration_cast<std::chrono::milliseconds>(now - m_assembly_camera_lock_last_attempt_at);
    if (stamp_unset || elapsed >= min_interval) {
        m_assembly_camera_lock_last_attempt_at = now;
        return true;
    }
    return false;
}

std::chrono::milliseconds AssemblyStepsUtils::assembly_camera_lock_attempt_elapsed(std::chrono::steady_clock::time_point now) const
{
    if (!has_assembly_camera_lock_attempt())
        return std::chrono::milliseconds::max();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - m_assembly_camera_lock_last_attempt_at);
}

bool AssemblyStepsUtils::is_assembly_camera_lock_blink_active(std::chrono::steady_clock::time_point now,
                                                              std::chrono::milliseconds blink_window) const
{
    if (!m_assembly_camera_locked || !has_assembly_camera_lock_attempt())
        return false;
    const auto elapsed = assembly_camera_lock_attempt_elapsed(now);
    return elapsed >= std::chrono::milliseconds(0) && elapsed < blink_window;
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

void AssemblyStepsUtils::select_steps_tree_node_for_canvas(int node_idx)
{
    if (m_model == nullptr || m_selection == nullptr)
        return;
    if (node_idx < 0 || node_idx >= (int) _steps_nodes.size())
        return;

    const int object_count = (int) m_model->objects.size();
    auto     &node         = _steps_nodes[node_idx];

    // Reset interaction state: this is a click-from-tree, not a click-from-canvas.
    set_selection_origin(SelectionOrigin::TreeNode);
    int prev_folder = find_parent_folder(selected_node);
    selected_node            = node_idx;
    int cur_folder = find_parent_folder(node_idx);
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
        collect(node_idx);
    } else if (node.type == AssemblyStepsTreeNode::Type::Object &&
               node.object_idx >= 0 && node.object_idx < object_count) {
        obj_idxs.push_back((unsigned int) node.object_idx);
    }

    // Single batch add: as_single_selection=false so successive calls APPEND
    // to the current selection instead of resetting it to a single object.
    for (unsigned int oid : obj_idxs)
        m_selection->add_object(oid, false);

    on_selected_node_changed();
    do_commond_callback("dirty");
}

void AssemblyStepsUtils::refresh_guide_show_part_numbers_from_current()
{
    auto *entries = get_current_kf_entries();
    if (entries && m_keyframe_selected >= 0 && m_keyframe_selected < (int) entries->size()) {
        KeyFrameEntry &entry = (*entries)[m_keyframe_selected];
        // Track the labels-show mode of the frame we just switched to.
        m_cur_labels_show_type = entry.data.labels_show_type;
        AssemblyNote &note = entry.data.assembly_note;
        const int current_folder = find_parent_folder(selected_node);
        if (is_empty_structure_step(current_folder)) {
            if (note.show_part_labels || !note.part_number_labels.empty()) {
                note.show_part_labels = false;
                note.part_number_labels.clear();
                entry.need_save = true;
                save_assembly_steps_json_to_model();
                do_commond_callback("dirty");
            }
            m_guide_show_part_numbers = false;
            return;
        }
        m_guide_show_part_numbers = note.show_part_labels;
        if (m_guide_show_part_numbers && note.part_number_labels.empty())
            toggle_part_number_labels();
    } else {
        m_guide_show_part_numbers = false;
    }
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
    int cur_folder = find_parent_folder(selected_node);
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
    // Switching step card must leave any in-progress note / connection editing.
    clear_note_selection();
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
    if (folder_idx >= 0 && !m_play_frame_refs.empty()) {
        int target_frame_idx = m_keyframe_selected;
        for (int gi = 0; gi < (int) m_play_frame_refs.size(); ++gi) {
            if (m_play_frame_refs[gi].node_idx == folder_idx &&
                m_play_frame_refs[gi].frame_idx == target_frame_idx) {
                m_assembly_play_index = gi + 1;
                break;
            }
        }
    }

    if (m_last_folder_idx < 0 || m_last_folder_idx >= (int) _steps_nodes.size()) {
        return;
    }
    //change folder
}

void AssemblyStepsUtils::apply_final_assembly_end_keyframe()
{
    for (int i = 0; i < (int) _steps_nodes.size(); ++i) {
        if (_steps_nodes[i].type == AssemblyStepsTreeNode::Type::Folder && _steps_nodes[i].is_final_assembly) {
            apply_end_keyframe(i);
            return;
        }
    }
}

void AssemblyStepsUtils::apply_end_keyframe(int folder_idx)
{
    if (folder_idx >= 0 && folder_idx < _steps_nodes.size()) {
        auto &_entries = _steps_nodes[folder_idx].kf_data.entries;
        for (const auto &entry : _entries) {
            if (entry.is_last() && entry.need_save) {
                apply_keyframe_to_canvas(entry.data);
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
        apply_final_assembly_end_keyframe();
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
        int folder_idx = find_parent_folder(selected_node);
        if (folder_idx >= 0)
            obj_idxs = collect_node_object_indices(folder_idx);
    } else {
        if (selected_node >= 0 && selected_node < (int) _steps_nodes.size()) {
            obj_idxs = collect_node_object_indices(selected_node);
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

void AssemblyStepsUtils::fill_folder_keyframes_from_children(int folder_idx)
{
    if (!m_model || folder_idx < 0 || folder_idx >= (int)_steps_nodes.size())
        return;
    const int obj_count = (int)m_model->objects.size();
    std::set<int> child_objs = collect_node_object_indices(folder_idx);
    auto         &nd         = _steps_nodes[folder_idx];
    if (nd.type != AssemblyStepsTreeNode::Type::Folder)
        return;
    auto &kf_entries = nd.kf_data.entries;
    for (auto &entry : kf_entries) {
        for (int oi : child_objs) {
            if (oi < 0 || oi >= obj_count)
                continue;
            const ModelObject *obj = m_model->objects[oi];
            if (!obj)
                continue;
            // Capture both halves of the GLVolume world transform:
            if (!obj->instances.empty())
                entry.data.object_transformations[oi] = get_instance_transform(oi);
            for (int vi = 0; vi < (int) obj->volumes.size(); ++vi) {
                const ModelVolume *mv = obj->volumes[vi];
                if (!mv)
                    continue;
                const std::pair<int, int> key{oi, vi};
                entry.data.volume_transformations[key] = get_volume_transform(oi, vi);
                entry.data.volume_names[key]           = !mv->name.empty() ? mv->name : obj->name;
            }
            entry.need_save = true;
        }
        entry.need_save = true;
    }
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
     const int folder_idx = find_parent_folder(selected_node);
     if (folder_idx < 0 || folder_idx >= (int) _steps_nodes.size() ||  _steps_nodes[folder_idx].type != AssemblyStepsTreeNode::Type::Folder || _steps_nodes[folder_idx].is_final_assembly)
         return;

     const std::vector<int> object_idxs = selected_assembly_object_indices();
     if (_steps_nodes[folder_idx].name == _u8L("Empty Step") && !object_idxs.empty()) {//modify name
         const int first_object_idx = object_idxs.front();
         if (first_object_idx >= 0 && first_object_idx < (int)m_model->objects.size() &&
             m_model->objects[first_object_idx] &&
             !m_model->objects[first_object_idx]->name.empty())
             _steps_nodes[folder_idx].name = m_model->objects[first_object_idx]->name;
     }
     add_objects_to_assembly_step(folder_idx, object_idxs);
 }

 void AssemblyStepsUtils::add_assembly_step() {
     if (!m_model)
         return;

     int selected_folder = find_parent_folder(selected_node);
     if (selected_folder >= 0 && selected_folder < (int) _steps_nodes.size() && _steps_nodes[selected_folder].is_final_assembly)
         return;

     int insert_before_idx = -1;
     int insert_after_idx = -1;
     if (selected_folder >= 0 && selected_folder < (int) _steps_nodes.size() && _steps_nodes[selected_folder].type == AssemblyStepsTreeNode::Type::Folder) {
         insert_after_idx = selected_folder;
     } else {
         insert_before_idx = ensure_final_assembly_folder();
     }

     int new_idx = create_folder_node(_u8L("Empty Step"), 0);
     if (new_idx < 0)
         return;
     ensure_default_keyframe(new_idx);

     if (insert_after_idx >= 0) {
         auto it = std::find(_steps_roots.begin(), _steps_roots.end(), insert_after_idx);
         if (it != _steps_roots.end())
             _steps_roots.insert(it + 1, new_idx);
         else
             _steps_roots.push_back(new_idx);
     } else if (insert_before_idx >= 0) {
         auto it = std::find(_steps_roots.begin(), _steps_roots.end(), insert_before_idx);
         if (it != _steps_roots.end())
             _steps_roots.insert(it, new_idx);
         else
             _steps_roots.push_back(new_idx);
     } else {
         _steps_roots.push_back(new_idx);
     }

     renumber_structure_step_roots();
     selected_node = new_idx;
     m_structure_scroll_to_node = new_idx;
     on_selected_node_changed();
     clear_selection();
     invalidate_play_frame_refs();
     save_assembly_steps_json_to_model();
     do_commond_callback("zoom_to_volumes");
     do_commond_callback("dirty");
     do_commond_callback("request_extra_frame");
 }

 void AssemblyStepsUtils::copy_assembly_step() {
     if (!m_model)
         return;

     int source_folder = find_parent_folder(selected_node);
     if (source_folder < 0 || source_folder >= (int) _steps_nodes.size())
         return;
     if (_steps_nodes[source_folder].type != AssemblyStepsTreeNode::Type::Folder || _steps_nodes[source_folder].is_final_assembly)
         return;
     auto source_root_it = std::find(_steps_roots.begin(), _steps_roots.end(), source_folder);
     if (source_root_it == _steps_roots.end())
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
         copied.kf_data.node_idx   = (int) _steps_nodes.size();
         copied.kf_data.is_folder = (copied.type == AssemblyStepsTreeNode::Type::Folder);
         copied.kf_data.object_idx = copied.object_idx;

         int copied_idx = (int) _steps_nodes.size();
         _steps_nodes.push_back(std::move(copied));
         for (int child_idx : _steps_nodes[src_idx].children) {
             int copied_child = clone_node(child_idx);
             if (copied_child >= 0) _steps_nodes[copied_idx].children.push_back(copied_child);
         }
         return copied_idx;
     };

    int copied_folder = clone_node(source_folder);
    if (copied_folder < 0)
        return;

    {
        std::string &copied_name = _steps_nodes[copied_folder].name;
        if (copied_name.empty())
            copied_name = _u8L("New Step");
        copied_name += _u8L("_copy");
    }

    _steps_roots.insert(source_root_it + 1, copied_folder);
     renumber_structure_step_roots();
     selected_node = copied_folder;
     m_structure_scroll_to_node = copied_folder;
     on_selected_node_changed();
     invalidate_play_frame_refs();
     save_assembly_steps_json_to_model();
     do_commond_callback("dirty");
     do_commond_callback("request_extra_frame");
 }

 void AssemblyStepsUtils::add_selected_to_assembly_step(int folder_idx) {
     add_objects_to_assembly_step(folder_idx, selected_assembly_object_indices());
 }

 bool AssemblyStepsUtils::can_add_selected_to_current_assembly_step() const
 {
     if (!m_selection) { return false; }
     if (m_selection->get_mode() == Selection::Volume) {
         return false;
     }
     const int folder_idx = const_cast<AssemblyStepsUtils*>(this)->find_parent_folder(selected_node);
     if (folder_idx < 0 || folder_idx >= (int) _steps_nodes.size() || _steps_nodes[folder_idx].type != AssemblyStepsTreeNode::Type::Folder ||
         _steps_nodes[folder_idx].is_final_assembly)
         return false;
     if (is_empty_structure_step(folder_idx)) {
         return true;
     }
     if (has_selected_node()) {
         return false;
     }
     return can_add_objects_to_step(m_selection->is_single_volume() || m_selection->is_multiple_volume(), selected_assembly_object_indices());
 }

 bool AssemblyStepsUtils::can_add_selected_to_assembly_step() const
 {
     if (!m_selection || m_selection->get_mode() == Selection::Volume) { return false; }
     if (has_selected_node()) { return false; }
     return can_add_objects_to_step(m_selection->is_single_volume() || m_selection->is_multiple_volume(),selected_assembly_object_indices());
 }

 void AssemblyStepsUtils::record_camera(KeyFrame &kf)
 {
      if (m_camera) {
          Camera &cam          = *m_camera;
          kf.view_matrix       = cam.get_view_matrix();
          kf.projection_matrix = cam.get_projection_matrix();
          kf.camera_target     = cam.get_target();
          kf.camera_zoom       = cam.get_zoom();
      }
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
        int folder_idx = find_parent_folder(selected_node);
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

    const int folder = find_parent_folder(selected_node);
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

    int folder = find_parent_folder(selected_node);
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

    apply_src_frame_transforms_to_current_keyframe(*src_end_entry);
}

void AssemblyStepsUtils::apply_src_frame_transforms_to_current_keyframe(KeyFrameEntry &src)
{
    if (!m_model)
        return;

    // Validate the destination: must be a non-final step's currently-selected
    const int folder = find_parent_folder(selected_node);
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
        target.object_transformations[item.first] = item.second;
    for (const auto &item : src_data.volume_transformations)
        target.volume_transformations[item.first] = item.second;
    for (const auto &item : src_data.volume_names)
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
    const int folder = find_parent_folder(selected_node);
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

bool AssemblyStepsUtils::final_assembly_end_frame_matches_model() const
{
    if (!m_model)
        return false;

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

    const KeyFrame &kf = end_entry->data;

    // Build the expected object / volume key sets from the live model, mirroring
    std::set<int>                 expected_objects;
    std::set<std::pair<int, int>> expected_volumes;
    const int obj_count = (int) m_model->objects.size();
    for (int oi = 0; oi < obj_count; ++oi) {
        const ModelObject *obj = m_model->objects[oi];
        if (!obj)
            continue;
        if (!obj->instances.empty())
            expected_objects.insert(oi);
        for (int vi = 0; vi < (int) obj->volumes.size(); ++vi) {
            if (obj->volumes[vi])
                expected_volumes.insert({oi, vi});
        }
    }

    // Collect what the end frame actually recorded.
    std::set<int>                 recorded_objects;
    for (const auto &item : kf.object_transformations)
        recorded_objects.insert(item.first);
    std::set<std::pair<int, int>> recorded_volumes;
    for (const auto &item : kf.volume_transformations)
        recorded_volumes.insert(item.first);
    // Exact match in both directions: no missing and no stale keys.
    return recorded_objects == expected_objects && recorded_volumes == expected_volumes;
}

bool AssemblyStepsUtils::is_mouse_over_blocking_panel() const
{
    const ImVec2 mouse_pos = ImGui::GetIO().MousePos;
    auto in_rect = [](const ImVec2 &p, const ImVec2 &mn, const ImVec2 &mx) {
        return mx.x > mn.x && mx.y > mn.y &&
               p.x >= mn.x && p.x <= mx.x &&
               p.y >= mn.y && p.y <= mx.y;
    };
    return in_rect(mouse_pos, m_panel_rect_structure_min, m_panel_rect_structure_max) ||
           in_rect(mouse_pos, m_panel_rect_guide_min, m_panel_rect_guide_max) ||
           in_rect(mouse_pos, m_panel_rect_playbar_min, m_panel_rect_playbar_max);
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
        vol->is_active          = state.active;
        if (vol->printable) {
            vol->color[3]           = state.active ? state.alpha : GLVolume::MODEL_HIDDEN_COL[3];
            vol->render_color[3]    = vol->color[3];
            vol->force_native_color = state.force_native_color;
        }else{
            vol->render_color = GLVolume::UNPRINTABLE_COLOR;
        }
    }
}

void AssemblyStepsUtils::look_cur_frame_logic(const KeyFrameEntry &entry)
{
    if (!entry.need_save)
        return;
    apply_keyframe_to_canvas(entry.data);
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
        selected_node = folder_idx;
        //todo scroll listview
        clear_selection();
        for (int object_idx : collect_node_object_indices(folder_idx)) {
            if (object_idx >= 0 && m_model && object_idx < (int)m_model->objects.size() && m_selection)
                m_selection->add_object((unsigned int)object_idx, false);
        }
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
    return goto_global_frame(target_frame);
}

void AssemblyStepsUtils::pause_global_frame()
{
    m_play_global = false;
    m_keyframe_playing = false;
    m_play_different_folder_waiting = false;
    m_play_different_folder_phase = 0;
    m_play_end_waiting = false;
    m_show_video_title_mode = false;
    m_video_intro_active = false;
    m_video_intro_phase = 0;
    m_video_intro_start_time = 0.0;
    m_video_intro_cover_duration = 1.0;
    m_video_intro_step_duration = 0.5;
    m_render_interpolated_part_number_labels = false;
    m_pending_global_frame_index = -1;
    m_play_transition_duration = m_play_transition_expect_duration;
    m_play_interval_step_to_step = m_play_interval_step_to_step_expect;
}

void AssemblyStepsUtils::play_different_folder_logic()
{
    if (!m_play_different_folder_waiting) {
        m_play_different_folder_waiting = true;
        m_play_different_folder_start_time = ImGui::GetTime();
        m_play_different_folder_phase = 0;
        m_show_video_title_mode = false;
    }

    do_commond_callback("dirty");
    do_commond_callback("request_extra_frame");

    const double elapsed = ImGui::GetTime() - m_play_different_folder_start_time;
    if (m_play_different_folder_phase == 0 && elapsed >= m_play_interval_step_to_step) {
        m_play_different_folder_phase = 1;
        m_show_video_title_mode = true;
        m_play_different_folder_start_time = ImGui::GetTime();
        return;
    }

    if (m_play_different_folder_phase == 1 && elapsed >= m_play_interval_step_to_step) {
        m_play_different_folder_waiting = false;
        m_play_different_folder_phase = 0;
        m_show_video_title_mode = false;
        if (m_pending_global_frame_index > 0) {
            goto_global_frame(m_pending_global_frame_index);
            m_pending_global_frame_index = -1;
        }
    }
}

void AssemblyStepsUtils::play_video_intro_logic()
{
    if (!m_video_intro_active)
        return;

    do_commond_callback("dirty");
    do_commond_callback("request_extra_frame");

    const double elapsed = ImGui::GetTime() - m_video_intro_start_time;

    if (m_video_intro_phase == 0) {
        // Phase 0: hold the cover title until m_video_intro_cover_duration
        // has elapsed. The actual text is rendered by render_main below.
        if (elapsed >= m_video_intro_cover_duration) {
            m_video_intro_phase      = 1;
            m_video_intro_start_time = ImGui::GetTime();
            m_show_video_title_mode  = true; // still showing an overlay
        }
        return;
    }

    if (m_video_intro_phase == 1) {
        // Phase 1: hold Step 1's name. When done, turn the overlay off and
        // hand control back to normal playback at the very first frame.
        if (elapsed >= m_video_intro_step_duration) {
            m_video_intro_active    = false;
            m_video_intro_phase     = 0;
            m_show_video_title_mode = false;
            // Make sure we begin at frame 1; on_export_mp4's
            // prepare_export_to_play_global_frame() already did goto(1) but a
            // user-cancelled / restarted export might have moved the index.
            if (m_assembly_play_index != 1)
                goto_global_frame(1);
        }
        return;
    }
}

void AssemblyStepsUtils::play_global_frame(bool from_btn_click)
{
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
            m_play_end_start_time = ImGui::GetTime();
            do_commond_callback("request_extra_frame");
            return;
        }
        const double elapsed = ImGui::GetTime() - m_play_end_start_time;
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
        // Pause at the current step's end frame for m_play_interval_step_to_step,
        // then jump to the next step after the wait completes.
        m_pending_global_frame_index = next_idx;
        play_different_folder_logic();
    }
    do_commond_callback("request_extra_frame");
}

void AssemblyStepsUtils::start_playback_with_intro()
{
    rebuild_play_frame_refs();
    m_assembly_play_count = (int)m_play_frame_refs.size();
    if (m_assembly_play_count <= 0)
        return;

    m_assembly_play_index = 1;
    goto_global_frame(1);

    m_video_intro_active         = true;
    m_video_intro_phase          = 0;
    m_video_intro_start_time     = ImGui::GetTime();
    m_video_intro_cover_duration = 2.0;
    m_video_intro_step_duration  = 1.0;
    m_show_video_title_mode      = true;
    m_play_global                = true;
    m_keyframe_playing             = true;

    m_play_transition_duration   = m_play_transition_expect_duration * 2.0;
    m_play_interval_step_to_step = m_play_interval_step_to_step_expect * 2.0;

    do_commond_callback("dirty");
    do_commond_callback("request_extra_frame");
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

    const int folder = find_parent_folder(selected_node);
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
                as_whole_object = collapse_repeated_objects && is_object_used_in_previous_steps(oi, folder);
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
        if (ExportType::PDF == type) {
            on_export_pdf(path);
        } else if (ExportType::MarkDown == type) {
            on_export_markdown(path);
        } else {
            on_export_mp4(path);
        }
    }
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
    m_steps_export_original_selected_node = selected_node;

    //
    prepare_export_to_play_global_frame();
    m_steps_export_active     = true;
    m_steps_export_wait_frame = true;
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
    m_steps_export_original_selected_node = selected_node;

    prepare_export_to_play_global_frame();
    m_steps_export_active     = true;
    m_steps_export_wait_frame = true;
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
    prepare_export_to_play_global_frame();

    // Phase 0 shows the cover title (m_pdf_export_title or project name) for
    // ~1s, phase 1 shows Step 1's name for ~0.5s, then normal frame playback
    // starts. m_show_video_title_mode is asserted up-front so render_main's
    // overlay branch runs from the very first recorded frame.
    m_video_intro_active     = true;
    m_video_intro_phase      = 0;
    m_video_intro_start_time = ImGui::GetTime();
    m_show_video_title_mode  = true;
    m_play_global            = true;
    m_keyframe_playing         = true;

    const int fps = 30;
    if (!m_mp4_recorder->start(static_cast<uint32_t>(w), static_cast<uint32_t>(h), fps, path)) {
        BOOST_LOG_TRIVIAL(error) << "assembly steps video export: failed to start recording -> " << path;
        // Roll back the intro / playback flags we just set so we don't leave
        // the assembly view in a half-recording state.
        m_video_intro_active    = false;
        m_show_video_title_mode = false;
        m_play_global           = false;
        m_keyframe_playing        = false;
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

    // goto_global_frame() updates selected_node / keyframe / m_assembly_play_index
    // and triggers a render-extra-frame request internally.
    goto_global_frame(cur + 1);
}

void AssemblyStepsUtils::finalize_steps_export()
{
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
        selected_node     = m_steps_export_original_selected_node;
        m_keyframe_selected = -1;
        on_selected_node_changed();
    }

    m_steps_export_active     = false;
    m_steps_export_wait_frame = false;
    m_steps_export_total      = 0;
    m_is_export_mode          = false;

    save_existing_project_if_dirty();

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

    for (const auto &c : ttc_candidates) {
        if (!fs::exists(c.path))
            continue;
        const char *fname = HPDF_LoadTTFontFromFile2(pdf, c.path, c.index, HPDF_TRUE);
        if (fname) {
            if (HPDF_Font f = HPDF_GetFont(pdf, fname, "UTF-8"))
                return f;
        }
    }
    for (const char *p : ttf_candidates) {
        if (!fs::exists(p))
            continue;
        const char *fname = HPDF_LoadTTFontFromFile(pdf, p, HPDF_TRUE);
        if (fname) {
            if (HPDF_Font f = HPDF_GetFont(pdf, fname, "UTF-8"))
                return f;
        }
    }

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

float AssemblyStepsUtils::get_guide_panel_y_offset(float canvas_w, float guide_x) const
{
    if (m_gizmo_toolbar_width <= 0.f || m_gizmo_toolbar_height <= 0.f)
        return 0.f;
    const float toolbar_right = (canvas_w + m_gizmo_toolbar_width) * 0.5f;
    const float export_btn_w = 60.0f * m_imgui_scale;
    const float effective_left = guide_x - export_btn_w;
    if (toolbar_right > effective_left)
        return m_gizmo_toolbar_height;
    return 0.f;
}

void AssemblyStepsUtils::render_main(float canvas_w, float canvas_h) {
    if (!m_camera || !m_volumes || !m_model) { return;}
    auto &sc = m_imgui_scale;
    //logic
    play_cur_keyframe_logic();
    sync_canvas_selection_state();
    // Detect a selection-cleared edge ("a step was selected last frame, and
    auto_apply_final_assembly_on_selection_cleared();
    if (!m_camera->get_view_matrix().matrix().isApprox(m_last_view_matrix_for_anchor_.matrix()) ||
        !m_camera->get_projection_matrix().matrix().isApprox(m_last_proj_matrix_for_anchor_.matrix())) {
        m_selected_screen_center_dirty_ = true;
    }
    update_step_screen_center();
    update_part_number_label_forbidden_layout_areas(canvas_w, canvas_h);
    //imgui
    render_assembly_notes_on_canvas(m_selected_screen_center_);
    if (should_show_panels()) {    // Top-left "Assembly Structure" panel (Figma 732:10276).
        render_assembly_structure_panel(canvas_w, canvas_h);
        //Right-side "Assembly Guide" panel.
        const float guide_w = std::max(260.0f * sc, std::min(300.0f * sc, canvas_w * 0.20f));
        const float guide_x = canvas_w - guide_w - 12.0f * sc;
        const float guide_y_base = 14.0f * sc;
        const float guide_y = guide_y_base + get_guide_panel_y_offset(canvas_w, guide_x);
        const float guide_h = canvas_h - guide_y - 20.0f * sc;
        render_assembly_guide_panel(guide_x, guide_y, guide_w, guide_h, sc, m_is_dark);
    } else {
        m_assembly_structure_right_x = 0.f;
        m_panel_rect_structure_min = m_panel_rect_structure_max = ImVec2(0, 0);
        m_panel_rect_guide_min = m_panel_rect_guide_max = ImVec2(0, 0);
    }
    if (!is_show_video_title_mode()) { // Bottom-centered play bar (Figma node 732:22413).
        if (!is_export_mode()) {
            const float assemble_control_clearance = 95.0f * sc;
            const float play_bar_bottom_y          = canvas_h - assemble_control_clearance;
            render_assemble_play_bar(canvas_w, play_bar_bottom_y);
        }
    } else {
        m_panel_rect_playbar_min = m_panel_rect_playbar_max = ImVec2(0, 0);
        // Resolve which title to draw centered on the canvas:
        std::string title;
        // is_cover_phase: only the very first phase of the MP4 export intro
        bool is_cover_phase = false;
        if (m_video_intro_active) {
            if (m_video_intro_phase == 0) {
                title = m_pdf_export_title;
                if (title.empty() && wxGetApp().plater()) {
                    std::string proj = wxGetApp().plater()->get_project_name().ToUTF8().data();
                    if (!proj.empty())
                        title = std::move(proj);
                }
                if (title.empty())
                    title = _u8L("Assembly Guide");
                is_cover_phase = true;
            } else {
                // Phase 1: pick the folder for the very first playable frame.
                if (!m_play_frame_refs.empty()) {
                    const int folder_idx = find_parent_folder(m_play_frame_refs.front().node_idx);
                    if (folder_idx >= 0 && folder_idx < (int) _steps_nodes.size())
                        title = assembly_step_display_name(_steps_nodes[folder_idx]);
                }
            }
        } else if (m_pending_global_frame_index > 0) {
            const int ref_idx = m_pending_global_frame_index - 1;
            if (ref_idx >= 0 && ref_idx < (int)m_play_frame_refs.size()) {
                const int folder_idx = find_parent_folder(m_play_frame_refs[ref_idx].node_idx);
                if (folder_idx >= 0 && folder_idx < (int) _steps_nodes.size())
                    title = assembly_step_display_name(_steps_nodes[folder_idx]);
            }
        }
        if (title.empty())
            title = _u8L("Assembly Step");

        ImDrawList *dl = ImGui::GetForegroundDrawList();
        ImFont *font = ImGui::GetFont();
        const float title_font_size = std::max(48.0f * sc, ImGui::GetFontSize() * 3.0f);
        const ImVec2 text_size = font->CalcTextSizeA(title_font_size, FLT_MAX, 0.0f, title.c_str());
        const ImVec2 pos((canvas_w - text_size.x) * 0.5f, (canvas_h - text_size.y) * 0.5f);
        const ImU32 title_col = IM_COL32(38, 46, 48, 255);
        dl->AddText(font, title_font_size, pos, title_col, title.c_str());

        if (is_cover_phase) {
            const std::string subtitle = std::string("---- ") + _u8L("Assembly Guide");
            const float subtitle_font_size = std::max(16.0f * sc, ImGui::GetFontSize() * 1.1f);
            const ImVec2 sub_size = font->CalcTextSizeA(subtitle_font_size, FLT_MAX, 0.0f, subtitle.c_str());
            const float title_right = pos.x + text_size.x;
            const float line_gap = 6.0f * sc;
            const float sub_x = title_right - sub_size.x;
            const float sub_y = pos.y + text_size.y + line_gap;
            dl->AddText(font, subtitle_font_size,
                ImVec2(sub_x, sub_y), title_col, subtitle.c_str());
        }
    }
    if (ImGui::IsMouseClicked(0)) {
        if (is_mouse_over_blocking_panel() &&
            m_selection_origin != SelectionOrigin::NoteColorControl &&
            m_selection_origin != SelectionOrigin::ImGuiNote) {
            set_selection_origin(SelectionOrigin::ImGui);
            return;
        }

        if (is_note_edit_controls_visible() &&
            (m_selection_origin == SelectionOrigin::None ||
             m_selection_origin == SelectionOrigin::ImGui)) {
            exit_note_edit();
            do_commond_callback("dirty");
        }
    }
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

// Renders the play bar from Figma node 732:22413: play/pause icon, "Nx" speed pill,
// a step-segmented progress bar with a circular current-step indicator and label,
// and prev/next nav buttons. All hit-testing is done with InvisibleButtons and the
// pixel-perfect visuals are painted via ImDrawList to match the design.
void AssemblyStepsUtils::render_assemble_play_bar(float canvas_w, float bottom_y)
{
    if (!m_imgui || !m_model)
        return;

    // Refresh the global play queue if dirty so step count / current step are accurate.
    if (m_play_frame_refs_dirty)
        rebuild_play_frame_refs();
    if (m_play_frame_refs.empty())
        return;
    auto &sc = m_imgui_scale;
    // Step folders still provide the display label, while the progress bar is
    // driven by every playable keyframe.
    const std::vector<int> step_node_idxs = sorted_step_nodes();
    const int total_frames = static_cast<int>(m_play_frame_refs.size());
    if (total_frames <= 0)
        return;

    // Locate the current step (1-based) from the current global play frame's parent folder.
    const int cur_global = std::clamp(m_assembly_play_index, 1, total_frames);
    const PlayFrameRef &cur_ref = m_play_frame_refs[cur_global - 1];
    const int cur_node_idx = cur_ref.node_idx;
    const bool disable_play_controls = m_keyframe_playing;
    int cur_step_1based = 1;
    for (int i = 0; i < static_cast<int>(step_node_idxs.size()); ++i) {
        if (step_node_idxs[i] == cur_node_idx) { cur_step_1based = i + 1; break; }
    }

    std::string cur_label = _u8L("step") + " " + std::to_string(cur_step_1based);
    if (cur_node_idx >= 0 && cur_node_idx < static_cast<int>(_steps_nodes.size()) && cur_ref.frame_idx >= 0 &&
        cur_ref.frame_idx < static_cast<int>(_steps_nodes[cur_node_idx].kf_data.entries.size()) && _steps_nodes[cur_node_idx].kf_data.entries[cur_ref.frame_idx].is_start()) {
        cur_label += "(start frame)";
    }

    // Speed multiplier shown in the pill. m_play_transition_duration is the time per
    // frame; speed = 1 / duration. Clicking the pill cycles 1.0x -> 0.5x -> 1.5x -> 2.0x.
    const float speed_mult = static_cast<float>(m_play_transition_duration > 0.0 ? (m_play_transition_expect_duration / m_play_transition_duration): 1.0);
    char speed_text[16];
    std::snprintf(speed_text, sizeof(speed_text), "%.1fx", speed_mult);

    // ---- Layout constants (all match Figma node 732:22413, scaled by `sc`) ----
    const float PLAY_BTN_SZ      = 24.0f * sc;
    const float PLAY_ICON_SZ     = 16.0f * sc;
    // Use the same font size as render_assembly_guide_export_button so the
    // Export button "Export" label, the speed pill "1.0x", the step number
    // inside the circle, and the step name below all read at the same scale.
    const float BAR_FONT_PX      = ImGui::GetFontSize();
    // Speed pill height grows with the font so a larger system font never clips
    // descenders inside the pill (Figma spec is 24).
    const float SPEED_BADGE_H    = std::max(24.0f * sc, BAR_FONT_PX + 8.0f * sc);
    const float SPEED_PAD_X      = 8.0f * sc;
    const float SPEED_FONT_PX    = BAR_FONT_PX;
    const float GAP_SECTION1     = 8.0f * sc;   // play -> speed
    // Figma's outer flex gap is 12px on a 11px font. ImGui::GetFontSize() is
    // typically larger, so the bar and circle grow while a fixed 12px gap
    // starts to look pinched. Scale the gaps with the font so the speed pill
    // and the nav buttons stop hugging the progress track at system fonts.
    const float GAP_S1_TO_BAR    = std::max(12.0f * sc, BAR_FONT_PX + 4.0f * sc); // section1 -> progress
    const float PROGRESS_W       = 558.0f * sc;
    const float BAR_H            = 6.0f * sc;
    // Circle must comfortably hold the step number at BAR_FONT_PX. Figma spec is
    // 18 (Figma 11px font); we grow it just enough to keep a margin around the
    // larger system font, but never smaller than the spec.
    const float CIRCLE_D         = std::max(18.0f * sc, BAR_FONT_PX + 6.0f * sc);
    const float TICK_W           = 1.0f * sc;
    const float TICK_HALF        = 3.0f * sc;   // half tick height extending above/below bar
    const float LABEL_FONT_PX    = BAR_FONT_PX;
    const float GAP_BAR_TO_NAV   = std::max(16.0f * sc, BAR_FONT_PX + 8.0f * sc); // section2 -> section3
    const float NAV_BTN_SZ       = 24.0f * sc;
    const float NAV_ICON_SZ      = 16.0f * sc;
    const float NAV_BTN_ROUND    = 5.333f * sc;
    const float NAV_GAP          = 8.0f * sc;

    // Pre-measure speed badge so total width is correct.
    ImFont *font = ImGui::GetFont();
    const ImVec2 speed_text_sz = font
        ? font->CalcTextSizeA(SPEED_FONT_PX, FLT_MAX, 0.0f, speed_text)
        : ImVec2(24.0f * sc, 16.0f * sc);
    const float SPEED_BADGE_W = std::max(SPEED_BADGE_H, speed_text_sz.x + 2.0f * SPEED_PAD_X);

    const float TOTAL_W = PLAY_BTN_SZ + GAP_SECTION1 + SPEED_BADGE_W
                        + GAP_S1_TO_BAR + PROGRESS_W
                        + GAP_BAR_TO_NAV + NAV_BTN_SZ + NAV_GAP + NAV_BTN_SZ;
    // main_cy is the vertical center of the top row (play, speed, progress, nav).
    // It must be at least half the tallest element so nothing clips above the window.
    const float top_half = std::max({PLAY_BTN_SZ * 0.5f, SPEED_BADGE_H * 0.5f, CIRCLE_D * 0.5f});
    const float TOTAL_H = top_half + std::max(top_half + 4.0f * sc, 15.74f * sc + LABEL_FONT_PX + 4.0f * sc);

    // Window position: centered horizontally, anchored so its bottom edge sits at `bottom_y`.
    const float win_x = canvas_w * 0.5f - TOTAL_W * 0.5f;
    const float win_y = bottom_y - TOTAL_H + 15;

    ImGui::SetNextWindowPos(ImVec2(win_x, win_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(TOTAL_W, TOTAL_H), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

    m_imgui->begin(std::string("##assembly_play_bar"),
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus);

    m_panel_rect_playbar_min = ImVec2(win_x, win_y);
    m_panel_rect_playbar_max = ImVec2(win_x + TOTAL_W, win_y + TOTAL_H);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 base = ImGui::GetWindowPos();

    // Section1/section2/section3 share the same vertical center line.
    // Use top_half so no element clips above the window.
    const float main_cy = top_half;

    float cursor_x = 0.0f;

    // ====== Play / pause icon button ======
    {
        const ImVec2 b0(base.x + cursor_x, base.y + main_cy - PLAY_BTN_SZ * 0.5f);
        const ImVec2 b1(b0.x + PLAY_BTN_SZ, b0.y + PLAY_BTN_SZ);

        ImGui::SetCursorScreenPos(b0);
        ImGui::InvisibleButton("##play_pause", ImVec2(PLAY_BTN_SZ, PLAY_BTN_SZ));
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked(0);
        if (hovered)
            dl->AddRectFilled(b0, b1, IM_COL32(0, 0, 0, 50), 6.0f * sc);

        ImTextureID tex = m_keyframe_playing ? m_tree_icon_pause : (m_is_dark ? m_tree_icon_play_dark : m_tree_icon_play);
        if (tex) {
            const ImVec2 i0(b0.x + (PLAY_BTN_SZ - PLAY_ICON_SZ) * 0.5f,
                            b0.y + (PLAY_BTN_SZ - PLAY_ICON_SZ) * 0.5f);
            dl->AddImage(tex, i0, ImVec2(i0.x + PLAY_ICON_SZ, i0.y + PLAY_ICON_SZ));
        }
        if (clicked) {
            if (m_keyframe_playing) pause_global_frame();
            else {
                start_playback_with_intro();//old code play_global_frame(true);
            }
        }
        if (hovered) {
            // unified message for both play and pause states). Window has zero
            // WindowPadding, so the tooltip needs its own padding back.
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * sc, 6.0f * sc));
            m_imgui->tooltip(_u8L("Play all frames for all nodes."), 20.0f * m_imgui->scaled(1.0f));
            ImGui::PopStyleVar();
        }
        cursor_x += PLAY_BTN_SZ + GAP_SECTION1;
    }

    // ====== Speed pill ("1.0x", rounded-100 dark mask) ======
    {
        const ImVec2 b0(base.x + cursor_x, base.y + main_cy - SPEED_BADGE_H * 0.5f);
        const ImVec2 b1(b0.x + SPEED_BADGE_W, b0.y + SPEED_BADGE_H);

        ImGui::SetCursorScreenPos(b0);
        m_imgui->disabled_begin(disable_play_controls);
        ImGui::InvisibleButton("##speed_pill", ImVec2(SPEED_BADGE_W, SPEED_BADGE_H));
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked(0);

        const float r = SPEED_BADGE_H * 0.5f;
        const ImU32 bg = hovered ? IM_COL32(0, 0, 0, 180) : IM_COL32(0, 0, 0, 128);
        dl->AddRectFilled(b0, b1, bg, r);

        if (font) {
            const ImVec2 tp(b0.x + (SPEED_BADGE_W - speed_text_sz.x) * 0.5f,
                            b0.y + (SPEED_BADGE_H - speed_text_sz.y) * 0.5f);
            dl->AddText(font, SPEED_FONT_PX, tp, IM_COL32(255, 255, 255, 255), speed_text);
        }
        if (clicked) {
            // Cycle order matches the Figma "1.0x" default first: 1.0 -> 0.5 -> 1.5 -> 2.0.
            const double cur        = (m_play_transition_duration > 0.0 ? m_play_transition_expect_duration / m_play_transition_duration : 1.0);
            double next_speed = 1.0;
            if      (std::abs(cur - 1.0) < 1e-3) next_speed = 0.5;
            else if (std::abs(cur - 0.5) < 1e-3) next_speed = 1.5;
            else if (std::abs(cur - 1.5) < 1e-3) next_speed = 2.0;
            else                                  next_speed = 1.0;
            m_play_transition_duration = m_play_transition_expect_duration / next_speed;
            m_play_interval_step_to_step = m_play_interval_step_to_step_expect / next_speed;
        }
        if (hovered) {
            // Window has zero WindowPadding; the tooltip needs its own padding back.
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * sc, 6.0f * sc));
            m_imgui->tooltip(_u8L("Toggle playback speed."), 20.0f * m_imgui->scaled(1.0f));
            ImGui::PopStyleVar();
        }
        m_imgui->disabled_end();
        cursor_x += SPEED_BADGE_W + GAP_S1_TO_BAR;
    }

    // ====== Step-segmented progress bar ======
    const float progress_x0 = base.x + cursor_x;
    const float progress_x1 = progress_x0 + PROGRESS_W;
    const float bar_cy      = base.y + main_cy;
    const float bar_y0      = bar_cy - BAR_H * 0.5f;
    const float bar_y1      = bar_cy + BAR_H * 0.5f;

    dl->AddRectFilled(ImVec2(progress_x0, bar_y0), ImVec2(progress_x1, bar_y1),
                      IM_COL32(0xCE, 0xCE, 0xCE, 255), BAR_H * 0.5f);

    auto frame_t = [&](int frame_1based) -> float {
        if (total_frames <= 1) return (frame_1based >= 1) ? 1.0f : 0.0f;
        return static_cast<float>(frame_1based - 1) / static_cast<float>(total_frames - 1);
    };

    const float progress_frac = frame_t(cur_global);
    const float fill_x1 = progress_x0 + PROGRESS_W * progress_frac;
    if (fill_x1 > progress_x0 + 0.5f) {
        dl->AddRectFilled(ImVec2(progress_x0, bar_y0), ImVec2(fill_x1, bar_y1),
                          IM_COL32(0x2C, 0xAD, 0x00, 255), BAR_H * 0.5f);
    }

    // Click-to-seek over the bar (hit area expanded to circle height so clicks near
    // the marker still register).
    {
        const float hit_y0 = bar_cy - CIRCLE_D * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(progress_x0, hit_y0));
        m_imgui->disabled_begin(disable_play_controls);
        ImGui::InvisibleButton("##progress_seek", ImVec2(PROGRESS_W, CIRCLE_D));
        // Click-to-seek plus press-and-drag scrubbing. While the button is held
        // (IsItemActive) the marker follows the cursor; the final position is
        // committed on release (IsItemDeactivated). seek_global_frame_from_mouse_x()
        // throttles itself to only seek when crossing into a new frame.
        if (ImGui::IsItemActive() || ImGui::IsItemDeactivated())
            seek_global_frame_from_mouse_x(ImGui::GetIO().MousePos.x, progress_x0, PROGRESS_W, total_frames);
        m_imgui->disabled_end();
    }

    // Tick marks at every frame boundary except the current frame (which the circle
    // marker replaces). Each tick is a short vertical line straddling the bar.
    for (int i = 1; i <= total_frames; ++i) {
        if (i == cur_global) continue;
        if (i == 1 || i == total_frames) continue; // skip start/end (sit at bar caps)
        const float tx = progress_x0 + PROGRESS_W * frame_t(i);
        dl->AddLine(ImVec2(tx, bar_cy - TICK_HALF), ImVec2(tx, bar_cy + TICK_HALF),
                    IM_COL32(0x9C, 0x9C, 0x9C, 255), TICK_W);
    }

    // Current step circular marker + step number inside it + label below.
    {
        const float cx = progress_x0 + PROGRESS_W * progress_frac;
        const float cy = bar_cy;
        const float rd = CIRCLE_D * 0.5f;

        dl->AddCircleFilled(ImVec2(cx, cy), rd, IM_COL32(255, 255, 255, 255), 32);
        ImGui::SetCursorScreenPos(ImVec2(cx - rd, cy - rd));
        ImGui::InvisibleButton("##progress_current_frame_tip", ImVec2(CIRCLE_D, CIRCLE_D));
        if (ImGui::IsItemHovered()) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * sc, 6.0f * sc));
            m_imgui->tooltip(_u8L("Frame") + " " + std::to_string(cur_global), 20.0f * m_imgui->scaled(1.0f));
            ImGui::PopStyleVar();
        }

        char step_text[16];
        std::snprintf(step_text, sizeof(step_text), "%d", cur_global);
        if (font) {
            const ImVec2 ts = font->CalcTextSizeA(LABEL_FONT_PX, FLT_MAX, 0.0f, step_text);
            dl->AddText(font, LABEL_FONT_PX,
                        ImVec2(cx - ts.x * 0.5f, cy - ts.y * 0.5f),
                        IM_COL32(0x32, 0x3A, 0x3D, 255), step_text);
        }

        if (font && !cur_label.empty()) {
            // Label sits just below the circle (Figma top-[27.74] within a 44h frame
            // places its top about 6-7 px below the circle's bottom).
            const float label_top = base.y + 27.74f * sc;
            const ImVec2 ls = font->CalcTextSizeA(LABEL_FONT_PX, FLT_MAX, 0.0f, cur_label.c_str());
            dl->AddText(font, LABEL_FONT_PX,
                        ImVec2(cx - ls.x * 0.5f, label_top),
                        IM_COL32(0x6B, 0x6B, 0x6B, 255), cur_label.c_str());
        }
    }

    cursor_x += PROGRESS_W + GAP_BAR_TO_NAV;

    // ====== Prev / Next nav buttons (24x24, dark rounded mask) ======
    // Icons come from resources/images/play_left.svg and play_right.svg
    // (loaded once in init_tree_icons()).
    auto draw_nav = [&](const char *id, ImTextureID tex, bool enabled, float x_local,
                        const std::string &tip) -> bool {
        const ImVec2 p0(base.x + x_local, base.y + main_cy - NAV_BTN_SZ * 0.5f);
        const ImVec2 p1(p0.x + NAV_BTN_SZ, p0.y + NAV_BTN_SZ);
        ImGui::SetCursorScreenPos(p0);
        ImGui::InvisibleButton(id, ImVec2(NAV_BTN_SZ, NAV_BTN_SZ));
        const bool hovered_raw = ImGui::IsItemHovered();
        const bool hovered = enabled && hovered_raw;
        const bool clicked = enabled && ImGui::IsItemClicked(0);
        const ImU32 bg = enabled
            ? (hovered ? IM_COL32(0, 0, 0, 180) : IM_COL32(0, 0, 0, 128))
            : IM_COL32(0, 0, 0, 60);
        dl->AddRectFilled(p0, p1, bg, NAV_BTN_ROUND);
        if (tex) {
            const ImVec2 i0(p0.x + (NAV_BTN_SZ - NAV_ICON_SZ) * 0.5f,
                            p0.y + (NAV_BTN_SZ - NAV_ICON_SZ) * 0.5f);
            const ImU32 tint = enabled ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 128);
            dl->AddImage(tex, i0, ImVec2(i0.x + NAV_ICON_SZ, i0.y + NAV_ICON_SZ),
                         ImVec2(0, 0), ImVec2(1, 1), tint);
        }
        // Tooltip is shown even when the button is disabled so the user knows what
        // each side .but only when the cursor is over the button itself.
        if (hovered_raw && !tip.empty()) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * sc, 6.0f * sc));
            m_imgui->tooltip(tip, 20.0f * m_imgui->scaled(1.0f));
            ImGui::PopStyleVar();
        }
        return clicked;
    };

    {
        m_imgui->disabled_begin(disable_play_controls);
        const bool can_prev = cur_global > 1;
        if (draw_nav("##nav_prev", m_play_left_icon, can_prev, cursor_x,
                     _u8L("Play previous frame."))) {
            goto_global_frame(cur_global - 1);
        }
        cursor_x += NAV_BTN_SZ + NAV_GAP;

        const bool can_next = cur_global < total_frames;
        if (draw_nav("##nav_next", m_play_right_icon, can_next, cursor_x,
                     _u8L("Play next frame."))) {
            goto_global_frame(cur_global + 1);
        }
        m_imgui->disabled_end();
    }

    m_imgui->end();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}

void AssemblyStepsUtils::clear_runtime_state()
{
    selected_node = -1;
    m_last_folder_idx  = -1;

    m_keyframe_selected = -1;
    on_selected_node_changed();
    set_selection_origin(SelectionOrigin::None);//clear_runtime_state
    m_keyframe_edit_buf[0]   = '\0';
    m_keyframe_playing = false;
    m_play_queue.clear();
    m_assembly_play_index = 1;
    m_assembly_play_count = 0;
    m_play_global = false;
    m_play_different_folder_waiting = false;
    m_play_different_folder_phase = 0;
    m_play_end_waiting = false;
    m_play_different_folder_start_time = 0.0;
    m_pending_global_frame_index = -1;
    m_show_video_title_mode = false;
    m_video_intro_active = false;
    m_video_intro_phase = 0;
    m_video_intro_start_time = 0.0;

    m_selected_screen_center_ = Vec2d::Zero();
    m_selected_screen_center_dirty_ = true;
    m_render_interpolated_part_number_labels = false;
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

void AssemblyStepsUtils::clear_steps_tree_view(bool save)
{
    clear_steps_all();
    if (save) {
        save_assembly_steps_json_to_model_and_request_extra_frame();
    }
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
    return selected_node >= 0;
}

bool AssemblyStepsUtils::is_selected_final_assembly_node() const
{
    if (!m_model || selected_node < 0)
        return false;

    int folder_idx = find_parent_folder(selected_node);
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
    return selected_node >= 0 && selected_node < (int) _steps_nodes.size() && _steps_nodes[selected_node].type == AssemblyStepsTreeNode::Type::Folder;
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
    selected_node = folder_idx;
    on_selected_node_changed();
    return folder_idx;
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

    bool changed = false;
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
    }
    return changed;
}

std::vector<int> AssemblyStepsUtils::sorted_step_nodes() const
{
    std::vector<int> step_nodes;
    for (int i = 0; i < (int) _steps_nodes.size(); ++i)
        if (_steps_nodes[i].type == AssemblyStepsTreeNode::Type::Folder)
            step_nodes.push_back(i);

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

bool AssemblyStepsUtils::can_add_objects_to_step(bool has_volume_selection, const std::vector<int> &object_idxs) const
{
    return !has_volume_selection && !object_idxs.empty();
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
    selected_node = -1;
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
    if (selected_node < 0 || selected_node >= (int) _steps_nodes.size())
        return nullptr;
    if (m_only_step_node_create_key_frame) {
        auto new_node_ids = find_parent_folder(selected_node);
        if (new_node_ids >= 0) {
            return &_steps_nodes[new_node_ids].kf_data.entries;
        }

    }
    return &_steps_nodes[selected_node].kf_data.entries;
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

// Forward declarations for the file-static note helpers defined further below
static std::array<float, 4> note_color_to_float_array(const std::array<int, 4> &color);
static ImU32 note_color_to_im_u32(const std::array<int, 4> &color);
static int note_palette_index_from_color(const std::array<int, 4> &color);
static std::array<int, 4> note_color_from_palette_index(int idx);

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

void AssemblyStepsUtils::set_labels_show_type(LabelsShowType type)
{
    auto *entries = get_current_kf_entries();
    if (!entries || m_keyframe_selected < 0 || m_keyframe_selected >= (int)entries->size())
        return;
    if (m_cur_labels_show_type == type && m_guide_show_part_numbers)
        return;
    m_cur_labels_show_type = type;
    KeyFrameEntry &cur_entry = (*entries)[m_keyframe_selected];
    cur_entry.data.labels_show_type = type;
    // Picking a label type implies the labels should be visible; the rebuild
    m_guide_show_part_numbers = true;
    toggle_part_number_labels_to_keyframe(cur_entry, true);
}

void AssemblyStepsUtils::auto_layout_labels_in_current_view()
{
    if (!m_guide_show_part_numbers)
        return;
    m_pn_autolayout_pending = true;
    do_commond_callback("dirty");
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
    const int     object_count = (int) m_model->objects.size();
    std::set<int> visited;
    std::set<int> seen_objs; // object-level dedup across nested nodes

    std::function<void(int)> collect = [&](int idx) {
        if (idx < 0 || idx >= (int) _steps_nodes.size()) return;
        if (!visited.insert(idx).second) return;
        const auto &n = _steps_nodes[idx];
        if (n.type == AssemblyStepsTreeNode::Type::Object &&
            n.object_idx >= 0 && n.object_idx < object_count) {
            const auto *obj = m_model->objects[n.object_idx];
            if (obj) {
                if (as_object_label(n.object_idx)) {
                    if (seen_objs.insert(n.object_idx).second) {
                        PartNumberLabel lbl;
                        lbl.object_idx = n.object_idx;
                        lbl.volume_idx = -1;
                        lbl.part_name  = obj->name;
                        out.push_back(std::move(lbl));
                    }
                } else {
                    for (int vi = 0; vi < (int) obj->volumes.size(); ++vi) {
                        PartNumberLabel lbl;
                        lbl.object_idx = n.object_idx;
                        lbl.volume_idx = vi;
                        lbl.part_name  = obj->volumes[vi]->name;
                        out.push_back(std::move(lbl));
                    }
                }
            }
        }
        for (int ci : n.children)
            collect(ci);
    };
    collect(collect_root);
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
    // In a multi-frame step a model object that already appeared earlier is
    // collapsed to a single object-level label; otherwise label per volume.
    const bool collapse_repeated_objects = m_show_modelobject_name_when_modelobject_has_occur_before;
    collect_part_number_label_refs(collect_root,
        [&](int object_idx) {
            return object_level_only ||
                   (collapse_repeated_objects && is_object_used_in_previous_steps(object_idx, collect_root));
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

void AssemblyStepsUtils::toggle_part_number_labels_to_keyframe(KeyFrameEntry &src, bool user_initiated)
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
    const int collect_root = find_parent_folder(selected_node);
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
    // Only an explicit user toggle reframes the step and auto-arranges labels.
    if (user_initiated) {
        if (src.is_last()) {
            fit_camera_to_current_step_main_plane(2.f);
        } else {
            fit_camera_to_current_step_main_plane(m_margin_factor_camera_for_not_last_frame);
        }
        // Persist the freshly framed camera into THIS keyframe right away.
        record_camera(src.data);
        src.data.is_camera_define = true;
        m_pn_autolayout_pending = true;
    }

    src.need_save = true;
    save_assembly_steps_json_to_model();
    do_commond_callback("dirty");
    do_commond_callback("request_extra_frame");
}

void AssemblyStepsUtils::draw_arrow_lines(
    const std::vector<std::pair<ImVec2, ImVec2>> &arrows,
    const std::array<float, 4> &color,
    float thickness,
    const std::array<int, 4> &viewport,
    bool draw_arrowhead)
{
    if (arrows.empty())
        return;

    // Default arrowhead size in screen pixels (tentative; tune if needed).
    static const float TRI_BASE = 12.0f;
    static const float TRI_HEIGHT = TRI_BASE * 1.618033f;
    const int n = (int)arrows.size();

    {
        m_arrow_line_model.reset();
        GLModel::Geometry line_data;
        line_data.format = {GLModel::PrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3};
        line_data.color = ColorRGBA::WHITE();
        line_data.reserve_vertices(2 * n);
        line_data.reserve_indices(2 * n);
        for (int i = 0; i < n; ++i) {
            line_data.add_vertex(Vec3f(arrows[i].first.x, arrows[i].first.y, 0.0f));
            line_data.add_vertex(Vec3f(arrows[i].second.x, arrows[i].second.y, 0.0f));
            line_data.add_line((unsigned)(i * 2), (unsigned)(i * 2 + 1));
        }
        m_arrow_line_model.init_from(std::move(line_data));
    }

    if (draw_arrowhead) {
        m_arrow_tri_model.reset();
        GLModel::Geometry tri_data;
        tri_data.format = {GLModel::PrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3};
        tri_data.color = ColorRGBA::WHITE();
        tri_data.reserve_vertices(3 * n);
        tri_data.reserve_indices(3 * n);
        for (int i = 0; i < n; ++i) {
            Vec2d dir(arrows[i].second.x - arrows[i].first.x, arrows[i].second.y - arrows[i].first.y);
            double len = dir.norm();
            if (len < 1e-3)
                dir = Vec2d(1, 0);
            else
                dir /= len;
            Vec2d perp(-dir.y(), dir.x());

            Vec2d tip(arrows[i].first.x, arrows[i].first.y);
            Vec2d p1 = tip + dir * TRI_HEIGHT - perp * (0.5 * TRI_BASE);
            Vec2d p2 = tip + dir * TRI_HEIGHT + perp * (0.5 * TRI_BASE);

            tri_data.add_vertex(Vec3f((float)tip.x(), (float)tip.y(), 0.0f));
            tri_data.add_vertex(Vec3f((float)p1.x(), (float)p1.y(), 0.0f));
            tri_data.add_vertex(Vec3f((float)p2.x(), (float)p2.y(), 0.0f));
            tri_data.add_triangle((unsigned)(i * 3), (unsigned)(i * 3 + 1), (unsigned)(i * 3 + 2));
        }
        m_arrow_tri_model.init_from(std::move(tri_data), true);
    }

    auto shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    wxGetApp().bind_shader(shader);

    const Transform3d ss_to_ndc = TransformHelper::ndc_to_ss_matrix_inverse(viewport);
    shader->set_uniform("projection_matrix", Transform3d::Identity());
    shader->set_uniform("view_model_matrix", ss_to_ndc);

    glsafe(::glDisable(GL_DEPTH_TEST));

#if ENABLE_GL_CORE_PROFILE
    if (OpenGLManager::get_gl_info().is_core_profile()) {
        wxGetApp().unbind_shader(shader);
        shader = wxGetApp().get_shader("dashed_thick_lines");
        if (shader == nullptr)
            return;
        wxGetApp().bind_shader(shader);
        shader->set_uniform("projection_matrix", Transform3d::Identity());
        shader->set_uniform("view_model_matrix", ss_to_ndc);
        shader->set_uniform("viewport_size", Vec2d(double(viewport[2]), double(viewport[3])));
        shader->set_uniform("width", thickness);
        shader->set_uniform("gap_size", 0.0f);
    }
    else
#endif
    {
        const auto &ogl_manager = wxGetApp().get_opengl_manager();
        if (ogl_manager)
            ogl_manager->set_line_width(thickness);
    }

    m_arrow_line_model.set_color(-1, color);
    m_arrow_line_model.render_geometry();

#if ENABLE_GL_CORE_PROFILE
    if (OpenGLManager::get_gl_info().is_core_profile()) {
        wxGetApp().unbind_shader(shader);
        shader = wxGetApp().get_shader("flat");
        if (shader == nullptr)
            return;
        wxGetApp().bind_shader(shader);
        shader->set_uniform("projection_matrix", Transform3d::Identity());
        shader->set_uniform("view_model_matrix", ss_to_ndc);
    }
    else
#endif
    {
        const auto &ogl_manager = wxGetApp().get_opengl_manager();
        if (ogl_manager)
            ogl_manager->set_line_width(1.0f);
    }

    if (draw_arrowhead) {
        m_arrow_tri_model.set_color(-1, color);
        m_arrow_tri_model.render_geometry();
    }

    glsafe(::glEnable(GL_DEPTH_TEST));
    wxGetApp().unbind_shader();
}

void AssemblyStepsUtils::draw_arrow_svg_icon(int idx, const ImVec2 &center, const ImVec2 &box_sz, ImTextureID tex, bool selected) const
{
    float half_w = box_sz.x * 0.5f;
    float half_h = box_sz.y * 0.5f;
    float icon_pad = std::max(4.0f, std::min(box_sz.x, box_sz.y) * 0.12f);
    float icon_sz = std::max(1.0f, std::min(box_sz.x, box_sz.y) - icon_pad * 2.0f);

    const ImVec2 box_min(center.x - half_w, center.y - half_h);
    const ImVec2 box_max(center.x + half_w, center.y + half_h);

    // Draw the icon body on the background draw list (same approach as the
    const float rounding = 6.0f;
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    dl->AddRectFilled(box_min, box_max, IM_COL32(255, 255, 255, 255), rounding);
    dl->AddRect(box_min, box_max,
                selected ? IM_COL32(25, 166, 77, 242) : IM_COL32(178, 178, 178, 255),
                rounding, 0, 1.0f);
    if (tex) {
        const ImVec2 img_min(center.x - icon_sz * 0.5f, center.y - icon_sz * 0.5f);
        const ImVec2 img_max(center.x + icon_sz * 0.5f, center.y + icon_sz * 0.5f);
        dl->AddImage(tex, img_min, img_max);
    }
}

void AssemblyStepsUtils::render_part_number_labels_on_canvas(
    const std::array<int, 4> &viewport,
    float viewport_height)
{
    if (!m_guide_show_part_numbers || !m_camera || !m_volumes || !m_model)
        return;
    auto *entries = get_current_kf_entries();
    if (!entries || m_keyframe_selected < 0 || m_keyframe_selected >= (int)entries->size())
        return;

    KeyFrameEntry &cur_entry = (*entries)[m_keyframe_selected];
    AssemblyNote &note = m_render_interpolated_part_number_labels ?
        m_interpolated_part_number_label_frame.assembly_note :
        cur_entry.data.assembly_note;
    auto &labels = note.part_number_labels;
    const bool editable = !m_render_interpolated_part_number_labels;
    if (!note.show_part_labels)
        return;
    if (labels.empty())
        return;

    update_part_number_label_font_size_from_config();

    const float sc      = m_imgui_scale;
    const float font_sz = part_number_label_font_size() * sc;
    const float pad_h   = 16.0f * sc;
    const float pad_v   =  6.0f * sc;
    const float rounding = 100.0f * sc;
    const ImU32 bg_col   = IM_COL32(0, 0, 0, 128);
    const ImU32 txt_col  = IM_COL32(255, 255, 255, 204);

    ImDrawList *fg   = ImGui::GetBackgroundDrawList();
    ImFont     *font = ImGui::GetFont();

    const bool block_label_interaction = is_mouse_over_blocking_panel();
    ImGuiWindowFlags drag_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    if (block_label_interaction)
        drag_flags |= ImGuiWindowFlags_NoInputs;

    bool any_changed = false;

    const float handle_sz = 8.0f * sc;

    for (int i = 0; i < (int)labels.size(); ++i) {
        PartNumberLabel &lbl = labels[i];

        Vec2d center = pn_screen_centers_.count(i) ? pn_screen_centers_[i]
                     : Vec2d(viewport[2] * 0.5, viewport[3] * 0.5);
        Vec2d arrow_start = center + lbl.arrow_start_offset;
        Vec2d arrow_end   = arrow_start + lbl.arrow_end_offset;
        // center/arrow_start/arrow_end are already in ImGui screen coords
        // (Y-down), so use directly -- no vp_height flip needed here.
        ImVec2 start_screen((float)arrow_start.x(), (float)arrow_start.y());
        ImVec2 label_screen((float)arrow_end.x(),   (float)arrow_end.y());

        ImVec2 text_sz = font->CalcTextSizeA(font_sz, FLT_MAX, 0.0f, lbl.part_name.c_str());
        float pill_w = text_sz.x + pad_h * 2.0f;
        float pill_h = text_sz.y + pad_v * 2.0f;
        ImVec2 pill_min(label_screen.x - pill_w * 0.5f, label_screen.y - pill_h * 0.5f);
        ImVec2 pill_max(pill_min.x + pill_w, pill_min.y + pill_h);

        fg->AddRectFilled(pill_min, pill_max, bg_col, rounding);

        ImVec2 text_pos(label_screen.x - text_sz.x * 0.5f, label_screen.y - text_sz.y * 0.5f);
        fg->AddText(font, font_sz, text_pos, txt_col, lbl.part_name.c_str());

        if (editable) {
            // Draggable invisible window over the pill label
            char drag_id[64];
            snprintf(drag_id, sizeof(drag_id), "##pn_drag_%d", i);
            ImGui::SetNextWindowPos(pill_min, ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(pill_w, pill_h));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            if (ImGui::Begin(drag_id, nullptr, drag_flags)) {
                ImGui::InvisibleButton("##hit", ImVec2(pill_w, pill_h));
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                    ImVec2 delta = ImGui::GetIO().MouseDelta;
                    lbl.arrow_end_offset.x() += delta.x;
                    lbl.arrow_end_offset.y() += delta.y;
                    any_changed = true;
                }
                if (ImGui::IsItemHovered())
                    set_cursor(AssemblyNoteCursorType::Move);
            }
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
        }

        // Draggable start handle near the volume. Following the PlainArrowNote
        // convention, dragging the start moves arrow_start_offset by delta and
        // absorbs the same delta into arrow_end_offset (with a sign flip), so
        // the pill stays anchored where the user placed it while the line tail
        // travels with the pointer:
        //     pill_pos = center + start_offset + end_offset
        //              = center + (start_offset + delta) + (end_offset - delta)
        //              = center + start_offset + end_offset       (unchanged).
        if (editable) {
            char start_drag_id[64];
            snprintf(start_drag_id, sizeof(start_drag_id), "##pn_start_drag_%d", i);
            const float start_drag_sz = handle_sz * 2.0f;
            ImGui::SetNextWindowPos(ImVec2(start_screen.x - handle_sz, start_screen.y - handle_sz), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(start_drag_sz, start_drag_sz));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            if (ImGui::Begin(start_drag_id, nullptr, drag_flags)) {
                ImGui::InvisibleButton("##pn_start_hit", ImVec2(start_drag_sz, start_drag_sz));
                if (ImGui::IsItemHovered() || ImGui::IsItemActive())
                    set_cursor(AssemblyNoteCursorType::Move);
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                    ImVec2 delta = ImGui::GetIO().MouseDelta;
                    lbl.arrow_start_offset.x() += delta.x;
                    lbl.arrow_start_offset.y() += delta.y;
                    lbl.arrow_end_offset.x()   -= delta.x;
                    lbl.arrow_end_offset.y()   -= delta.y;
                    any_changed = true;
                }
            }
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
        }

        // Visible affordance: small green dot at the start so users discover
        // the handle. Drawn on the foreground list so it floats above the
        // line stroke that render_assembly_notes_on_canvas batched earlier.
        fg->AddCircleFilled(start_screen, handle_sz * 0.6f, IM_COL32(0, 200, 80, 200));
    }

    if (editable && any_changed) {
        cur_entry.need_save = true;
        save_assembly_steps_json_to_model();
        do_commond_callback("dirty");
        do_commond_callback("request_extra_frame");
    }
}

ImVec2 AssemblyStepsUtils::nearest_rect_anchor(const ImVec2 &rect_min, const ImVec2 &rect_max,
                                               const ImVec2 &from, bool include_corners)
{
    float cx = (rect_min.x + rect_max.x) * 0.5f;
    float cy = (rect_min.y + rect_max.y) * 0.5f;
    ImVec2 mid_points[4] = {
        {cx, rect_min.y}, {cx, rect_max.y},
        {rect_min.x, cy}, {rect_max.x, cy},
    };
    ImVec2 corners[4] = {
        {rect_min.x, rect_min.y}, {rect_max.x, rect_min.y},
        {rect_min.x, rect_max.y}, {rect_max.x, rect_max.y},
    };
    ImVec2 best = mid_points[0];
    float  best_d2 = FLT_MAX;
    auto try_point = [&](const ImVec2 &p) {
        float dx = p.x - from.x, dy = p.y - from.y;
        float d2 = dx * dx + dy * dy;
        if (d2 < best_d2) { best_d2 = d2; best = p; }
    };
    for (const auto &p : mid_points)
        try_point(p);
    if (include_corners) {
        for (const auto &p : corners)
            try_point(p);
    }
    return best;
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

void AssemblyStepsUtils::deal_once_when_enter_assembly_view() {
    if (!m_model) { return; }
    if (!AssemblyTreeData::show_origin_step_tree) {
        // Sync only when the final-assembly end frame no longer matches the live
        if (!final_assembly_end_frame_matches_model()) {//(is_model_object_tree_changed(model_object_tree, temp_model_object_tree)) {
            sync_all_model_object_to_final_assembly_node();
            sync_steps_objects_with_model();
            clear_all_keyframe_part_number_labels();
            m_model->set_assembly_tree_data(build_model_object_tree_data());
            selected_node = -1;
            invalidate_play_frame_refs();
        }
        const AssemblyTreeData &tree = m_model->get_assembly_tree_data();
        if (tree.empty()){
            m_model->set_assembly_tree_data(build_model_object_tree_data());
        }
        do_commond_callback("zoom_to_volumes");
    }else{//todo
    }
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
}

static int note_tool_index_from_selection(AssemblyNoteSelectionType type)
{
    // Must mirror the order of `m_note_tools` initialized in
    // render_assembly_guide_panel ("Add Notes" section): rect, circle, vector, tag.
    switch (type) {
    case AssemblyNoteSelectionType::Rectangle:  return 0;
    case AssemblyNoteSelectionType::Circle:     return 1;
    case AssemblyNoteSelectionType::PlainArrow: return 2;
    case AssemblyNoteSelectionType::TextLabel:  return 3;
    default:                                    return -1;
    }
}

void AssemblyStepsUtils::render_assembly_notes_on_canvas(const Vec2d &object_screen_center)
{
    if (!m_camera || is_show_video_title_mode()) {
        return;
    }
    const Camera             &camera   = *m_camera;
    const std::array<int, 4> &viewport = camera.get_viewport();
    auto                      viewport_height = (float) viewport[3];
    // Detect a step-tree / keyframe switch since the previous frame and clear

    if (selected_node != m_last_rendered_selected_node_for_notes_ ||
        m_keyframe_selected != m_last_rendered_keyframe_selected_) {
        exit_note_edit();
        m_last_rendered_selected_node_for_notes_ = selected_node;
        m_last_rendered_keyframe_selected_       = m_keyframe_selected;
    }
    if (!has_selected_node())
        return;
    auto *entries = get_current_kf_entries();
    if (!entries || m_keyframe_selected < 0 || m_keyframe_selected >= (int)entries->size())
        return;

    KeyFrameEntry &cur_entry = (*entries)[m_keyframe_selected];
    AssemblyNote &note = cur_entry.data.assembly_note;
    auto &arrow_svgs = note.arrow_svgs;
    auto &text_labels = note.text_labels;
    auto &circle_notes = note.circle_notes;
    auto &rectangle_notes = note.rectangle_notes;
    auto &plain_arrows = note.plain_arrows;
    AssemblyNote &part_number_note = m_render_interpolated_part_number_labels ?
        m_interpolated_part_number_label_frame.assembly_note :
        note;
    auto &pn_labels = part_number_note.part_number_labels;

    const bool has_pn = m_guide_show_part_numbers && part_number_note.show_part_labels && !pn_labels.empty() && m_volumes && m_model;
    if (arrow_svgs.empty() && text_labels.empty() && circle_notes.empty() && rectangle_notes.empty() && plain_arrows.empty() && !has_pn)
        return;
    auto   &sc           = m_imgui_scale;
    const float handle_sz    = 8.0f * sc;
    const float close_r      = 8.0f * sc;
    const float resize_sz    = 12.0f * sc;
    const float line_hit_tol = 6.0f * sc;
    const float line_thick   = 2.0f;
    const std::array<float, 4> line_col = {0.0f, 0.78f, 0.31f, 0.86f};
    Vec2d obj_center = object_screen_center;
    const float vp_height = viewport_height;

    const int count = (int)arrow_svgs.size();
    const int plain_arrow_count = (int)plain_arrows.size();
    if (is_note_edit_controls_visible()) {
        bool selection_valid =
            (m_note_selected_type == AssemblyNoteSelectionType::ArrowSvg && m_note_selected_idx >= 0 && m_note_selected_idx < count) ||
            (m_note_selected_type == AssemblyNoteSelectionType::TextLabel && m_note_selected_idx >= 0 && m_note_selected_idx < (int)text_labels.size()) ||
            (m_note_selected_type == AssemblyNoteSelectionType::Circle && m_note_selected_idx >= 0 && m_note_selected_idx < (int)circle_notes.size()) ||
            (m_note_selected_type == AssemblyNoteSelectionType::Rectangle && m_note_selected_idx >= 0 && m_note_selected_idx < (int)rectangle_notes.size()) ||
            (m_note_selected_type == AssemblyNoteSelectionType::PlainArrow && m_note_selected_idx >= 0 && m_note_selected_idx < plain_arrow_count);
        if (!selection_valid) {
            exit_note_edit();
        }
    }
    m_guide_note_tool_selected = is_note_edit_controls_visible()
        ? note_tool_index_from_selection(m_note_selected_type)
        : -1;

    // --- Pass 1: collect screen positions & batch-draw all arrow lines ---
    struct ArrowScreenData { ImVec2 start; ImVec2 end; };
    struct ColoredLine { std::pair<ImVec2, ImVec2> line; std::array<float, 4> color; bool draw_arrowhead{false}; };
    std::vector<ArrowScreenData> screen_data(count);
    std::vector<ColoredLine> line_pairs;
    line_pairs.reserve(count + plain_arrow_count + (has_pn ? (int)pn_labels.size() : 0));

    for (int ni = 0; ni < count; ++ni) {
        ArrowSvgNote &arrow = arrow_svgs[ni];
        // Mirror the Pass-2 clamp so the line-anchor math agrees with the icon
        const double svg_min_dim = 32.0 * sc;
        arrow.label_size.x() = std::max(arrow.label_size.x(), svg_min_dim);
        arrow.label_size.y() = std::max(arrow.label_size.y(), svg_min_dim);
        Vec2d start_pos = obj_center + arrow.arrow_start_offset;
        Vec2d end_pos   = start_pos  + arrow.arrow_end_offset;
        screen_data[ni].start = ImVec2((float)start_pos.x(), (float)start_pos.y());
        screen_data[ni].end   = ImVec2((float)end_pos.x(),   (float)end_pos.y());
        // Anchor the line tip to the SVG icon's bbox edge (same trick used by
        ImVec2 icon_min((float)(end_pos.x() - arrow.label_size.x() * 0.5),
                        (float)(end_pos.y() - arrow.label_size.y() * 0.5));
        ImVec2 icon_max((float)(end_pos.x() + arrow.label_size.x() * 0.5),
                        (float)(end_pos.y() + arrow.label_size.y() * 0.5));
        ImVec2 from_pt((float)start_pos.x(), (float)start_pos.y());
        ImVec2 anchor  = nearest_rect_anchor(icon_min, icon_max, from_pt,true);
        line_pairs.push_back({{
            ImVec2((float)start_pos.x(), vp_height - (float)start_pos.y()),
            ImVec2(anchor.x,             vp_height - anchor.y)},
            note_color_to_float_array(arrow.color)});
    }
    std::vector<ArrowScreenData> plain_arrow_screen_data(plain_arrow_count);
    for (int ni = 0; ni < plain_arrow_count; ++ni) {
        const PlainArrowNote &arrow = plain_arrows[ni];
        Vec2d start_pos = obj_center + arrow.arrow_start_offset;
        Vec2d end_pos   = start_pos  + arrow.arrow_end_offset;
        plain_arrow_screen_data[ni].start = ImVec2((float)start_pos.x(), (float)start_pos.y());
        plain_arrow_screen_data[ni].end   = ImVec2((float)end_pos.x(),   (float)end_pos.y());
        line_pairs.push_back({{
            ImVec2((float)start_pos.x(), vp_height - (float)start_pos.y()),
            ImVec2((float)end_pos.x(),   vp_height - (float)end_pos.y())},
            note_color_to_float_array(arrow.color), true});
    }

    // Part-number label lines: compute per-object screen centers and collect lines.
    pn_screen_centers_.clear();
    if (has_pn) {
        Matrix4d w2s = camera.get_projection_matrix().matrix() * camera.get_view_matrix().matrix();
        // vol_idx >= 0 -> anchor at that specific GLVolume; vol_idx < 0 (used
        // by final-assembly object-level labels) -> aggregate all active
        // volumes belonging to the ModelObject and use the union bbox center.
        auto pn_vol_center = [&](int obj_idx, int vol_idx) -> Vec2d {
            BoundingBoxf3 bb;
            bool has_any = false;
            for (const GLVolume *vol : m_volumes->volumes) {
                if (!vol || !vol->is_active)
                    continue;
                if (vol->composite_id.object_id != obj_idx)
                    continue;
                if (vol_idx >= 0 && vol->composite_id.volume_id != vol_idx)
                    continue;
                bb.merge(vol->transformed_bounding_box());
                has_any = true;
                if (vol_idx >= 0)
                    break;
            }
            if (!has_any)
                return Vec2d(viewport[2] * 0.5, viewport[3] * 0.5);
            Vec4d c4(bb.center().x(), bb.center().y(), bb.center().z(), 1.0);
            Vec4d ndc = w2s * c4;
            return Vec2d(0.5 * (1.0 + ndc.x() / ndc.w()) * viewport[2],
                         0.5 * (1.0 - ndc.y() / ndc.w()) * viewport[3]);
        };

        update_part_number_label_font_size_from_config();

        const float pn_font_sz = part_number_label_font_size() * sc;
        const float pn_pad_h  = 16.0f * sc;
        const float pn_pad_v  =  6.0f * sc;
        ImFont *pn_font = ImGui::GetFont();

        // One-shot auto-arrange: bucket every label onto one of the four sides of
        // the step's on-screen bbox (top/bottom/left/right), then stack the labels
        // of each side along its rail so the pills never overlap. Leader lines
        // connect each part back to its pill. Only the editable (non-interpolated)
        // frame is rewritten and persisted.
        if (m_pn_autolayout_pending && !m_render_interpolated_part_number_labels) {
            if (auto_layout_part_number_labels(pn_labels, camera, viewport, sc)) {
                record_camera(cur_entry.data);
                cur_entry.data.is_camera_define = true;
                cur_entry.need_save = true;
                save_assembly_steps_json_to_model();
            }
            m_pn_autolayout_pending = false;
        }

        for (int i = 0; i < (int)pn_labels.size(); ++i) {
            const PartNumberLabel &lbl = pn_labels[i];
            Vec2d center = pn_vol_center(lbl.object_idx, lbl.volume_idx);
            pn_screen_centers_[i] = center;
            Vec2d arrow_start = center + lbl.arrow_start_offset;
            Vec2d pill_center = arrow_start + lbl.arrow_end_offset;

            ImVec2 tsz = pn_font->CalcTextSizeA(pn_font_sz, FLT_MAX, 0.0f, lbl.part_name.c_str());
            float pw = tsz.x + pn_pad_h * 2.0f;
            float ph = tsz.y + pn_pad_v * 2.0f;
            ImVec2 pill_min((float)pill_center.x() - pw * 0.5f, (float)pill_center.y() - ph * 0.5f);
            ImVec2 pill_max(pill_min.x + pw, pill_min.y + ph);

            ImVec2 from_pt((float)arrow_start.x(), (float)arrow_start.y());
            ImVec2 anchor = nearest_rect_anchor(pill_min, pill_max, from_pt);

            line_pairs.push_back({{
                ImVec2((float)arrow_start.x(), vp_height - (float)arrow_start.y()),
                ImVec2(anchor.x,               vp_height - anchor.y)},
                line_col});
        }
    }

    for (const ColoredLine &colored_line : line_pairs) {
        std::vector<std::pair<ImVec2, ImVec2>> one_line{colored_line.line};
        draw_arrow_lines(one_line, colored_line.color, line_thick, viewport, colored_line.draw_arrowhead);
    }

    // --- Pass 2: ImGui svg icons, drag handles, close buttons ---
    bool any_changed = false;
    int  delete_idx  = -1;
    bool note_cursor_requested = false;
    static int s_circle_line_drag_idx = -1;
    static int s_plain_arrow_line_drag_idx = -1;
    if (!ImGui::IsMouseDown(0)) {
        s_circle_line_drag_idx = -1;
        s_plain_arrow_line_drag_idx = -1;
    }
    ImDrawList *draw_list = ImGui::GetBackgroundDrawList();

    const bool block_note_interaction = is_mouse_over_blocking_panel();
    ImGuiWindowFlags drag_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    // The text label hosts an editable InputText so it must stay a real window,
    ImGuiWindowFlags text_label_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (block_note_interaction) {
        drag_flags |= ImGuiWindowFlags_NoInputs;
        text_label_flags |= ImGuiWindowFlags_NoInputs;
    }

    auto draw_note_close_button = [&](const ImVec2 &center, const char *id, float radius = -1.0f,
                                      ImU32 fill_col = IM_COL32(200, 60, 60, 230),
                                      ImU32 cross_col = IM_COL32(255, 255, 255, 255),
                                      float hit_pad = 0.0f) {
        bool want_delete = false;
        float r = radius > 0.0f ? radius : close_r;
        // Paint on the background draw list: it sits below every ImGui window
        draw_list->AddCircleFilled(center, r, fill_col);
        float cross = r * 0.45f;
        draw_list->AddLine(ImVec2(center.x - cross, center.y - cross),
                           ImVec2(center.x + cross, center.y + cross), cross_col, 2.0f);
        draw_list->AddLine(ImVec2(center.x + cross, center.y - cross),
                           ImVec2(center.x - cross, center.y + cross), cross_col, 2.0f);
        // hit_pad enlarges only the clickable area, not the visible circle. The
        const float hit = r + hit_pad;
        ImGui::SetCursorScreenPos(ImVec2(center.x - hit, center.y - hit));
        if (ImGui::InvisibleButton(id, ImVec2(hit * 2.0f, hit * 2.0f))) {
            m_selection_origin = SelectionOrigin::ImGuiNote;
            want_delete = true;
        }
        if (ImGui::IsItemHovered()) {
            set_cursor(AssemblyNoteCursorType::Hand);
            note_cursor_requested = true;
        }
        return want_delete;
    };

    auto draw_note_resize_handle = [&](const ImVec2 &center, float handle_size = -1.0f,
                                       ImU32 fill_col = IM_COL32(0, 200, 80, 230),
                                       ImU32 border_col = IM_COL32(80, 80, 80, 230)) {
        float sz = handle_size > 0.0f ? handle_size : resize_sz;
        ImVec2 min(center.x - sz * 0.5f, center.y - sz * 0.5f);
        ImVec2 max(min.x + sz, min.y + sz);
        // Paint on the background draw list (below the side panels). Drawn after the note bodies, so the handle still renders above them.
        draw_list->AddRectFilled(min, max, fill_col, 2.0f * sc);
        draw_list->AddRect(min, max, border_col, 2.0f * sc, 0, 1.0f);
        return min;
    };
    auto is_note_selected = [&](AssemblyNoteSelectionType type, int idx) {
        return m_note_edit_controls_visible
            && m_note_selected_type == type
            && m_note_selected_idx == idx;
    };
    auto mark_imgui_note_click = [&]() {
        if (ImGui::IsItemClicked(0) || ImGui::IsItemActivated() || ImGui::IsItemActive())
            set_selection_origin(SelectionOrigin::ImGuiNote);
    };
    auto activate_note_edit_controls = [&](AssemblyNoteSelectionType type, int idx) {
        set_selection_origin(SelectionOrigin::ImGuiNote);
        if (!is_note_selected(type, idx)) {
            set_note_edit_controls_visible(true);
            m_note_selected_type = type;
            m_note_selected_idx = idx;
            m_guide_note_tool_selected = note_tool_index_from_selection(type);
            do_commond_callback("dirty");
        }
    };

    auto clamp_note_size = [](Vec2d &size, double min_w, double min_h) {
        size.x() = std::max(size.x(), min_w);
        size.y() = std::max(size.y(), min_h);
    };
    auto wrap_text_to_width = [](const std::string &text, float max_width) {
        if (max_width <= 1.0f || text.empty())
            return text;

        auto next_utf8 = [](const std::string &s, size_t pos) {
            unsigned char c = static_cast<unsigned char>(s[pos]);
            size_t len = 1;
            if ((c & 0x80) == 0)
                len = 1;
            else if ((c & 0xE0) == 0xC0)
                len = 2;
            else if ((c & 0xF0) == 0xE0)
                len = 3;
            else if ((c & 0xF8) == 0xF0)
                len = 4;
            return std::min(pos + len, s.size());
        };

        std::string wrapped;
        std::string line;
        for (size_t pos = 0; pos < text.size();) {
            if (text[pos] == '\n') {
                wrapped += line;
                wrapped += '\n';
                line.clear();
                ++pos;
                continue;
            }

            size_t next = next_utf8(text, pos);
            std::string token = text.substr(pos, next - pos);
            std::string candidate = line + token;
            if (!line.empty() && ImGui::CalcTextSize(candidate.c_str()).x > max_width) {
                wrapped += line;
                wrapped += '\n';
                line = token;
            } else {
                line = std::move(candidate);
            }
            pos = next;
        }
        wrapped += line;
        return wrapped;
    };
    auto unwrap_soft_newlines = [](const std::string &text, float previous_width) {
        if (previous_width <= 1.0f || text.empty())
            return text;

        std::string unwrapped;
        std::string line;
        for (size_t pos = 0; pos < text.size(); ++pos) {
            if (text[pos] == '\n') {
                unwrapped += line;
                if (ImGui::CalcTextSize(line.c_str()).x < previous_width - 10.0f)
                    unwrapped += '\n';
                line.clear();
            } else {
                line += text[pos];
            }
        }
        unwrapped += line;
        return unwrapped;
    };
    auto text_line_count = [](const std::string &text) {
        int count = 1;
        for (char ch : text)
            if (ch == '\n')
                ++count;
        return count;
    };
    auto distance_to_segment = [](const ImVec2 &point, const ImVec2 &start, const ImVec2 &end) {
        float vx = end.x - start.x;
        float vy = end.y - start.y;
        float wx = point.x - start.x;
        float wy = point.y - start.y;
        float len_sq = vx * vx + vy * vy;
        if (len_sq <= 0.0001f) {
            float dx = point.x - start.x;
            float dy = point.y - start.y;
            return std::sqrt(dx * dx + dy * dy);
        }
        float t = std::max(0.0f, std::min(1.0f, (wx * vx + wy * vy) / len_sq));
        float proj_x = start.x + t * vx;
        float proj_y = start.y + t * vy;
        float dx = point.x - proj_x;
        float dy = point.y - proj_y;
        return std::sqrt(dx * dx + dy * dy);
    };
    auto is_point_on_ellipse_outline = [](const ImVec2 &point, const ImVec2 &center, const ImVec2 &radius, float tolerance) {
        if (radius.x <= 1.0f || radius.y <= 1.0f)
            return false;
        float dx = (point.x - center.x) / radius.x;
        float dy = (point.y - center.y) / radius.y;
        float norm = std::sqrt(dx * dx + dy * dy);
        float normalized_tol = tolerance / std::max(1.0f, std::min(radius.x, radius.y));
        return std::fabs(norm - 1.0f) <= normalized_tol;
    };

    for (int ni = 0; ni < count; ++ni) {
        ArrowSvgNote &arrow = arrow_svgs[ni];
        clamp_note_size(arrow.label_size, 32.0 * sc, 32.0 * sc);
        const ImVec2 &arrow_start = screen_data[ni].start;
        const ImVec2 &arrow_end   = screen_data[ni].end;
        ImVec2 icon_box_sz((float)arrow.label_size.x(), (float)arrow.label_size.y());
        bool arrow_selected = is_note_selected(AssemblyNoteSelectionType::ArrowSvg, ni);

        ImTextureID tex = get_arrow_svg_icon(arrow.svg_name);
        draw_arrow_svg_icon(ni, arrow_end, icon_box_sz, tex, arrow_selected);

        // Draggable handle for arrow start (transparent ImGui window)
        if (arrow_selected) {
            char start_win[64];
            snprintf(start_win, sizeof(start_win), "##arrow_start_%d", ni);
            float drag_sz = handle_sz * 2;
            ImGui::SetNextWindowPos(ImVec2(arrow_start.x - handle_sz, arrow_start.y - handle_sz), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(drag_sz, drag_sz));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            if (ImGui::Begin(start_win, nullptr, drag_flags)) {
                ImGui::PushID(ni * 2);
                ImGui::InvisibleButton("##sd", ImVec2(drag_sz, drag_sz));
                if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
                    set_cursor(AssemblyNoteCursorType::Move);
                    note_cursor_requested = true;
                }
                if (ImGui::IsItemClicked(0))
                    activate_note_edit_controls(AssemblyNoteSelectionType::ArrowSvg, ni);
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                    ImVec2 delta = ImGui::GetIO().MouseDelta;
                    arrow.arrow_start_offset.x() += delta.x;
                    arrow.arrow_start_offset.y() += delta.y;
                    any_changed = true;
                }
                ImGui::PopID();
            }
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
            draw_list->AddCircleFilled(arrow_start, handle_sz * 0.6f, IM_COL32(0, 200, 80, 150));
        }

        // Draggable for arrow end (icon area, transparent ImGui window)
        {
            char end_win[64];
            snprintf(end_win, sizeof(end_win), "##arrow_end_%d", ni);
            ImGui::SetNextWindowPos(ImVec2(arrow_end.x - icon_box_sz.x * 0.5f, arrow_end.y - icon_box_sz.y * 0.5f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(icon_box_sz);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            if (ImGui::Begin(end_win, nullptr, drag_flags)) {
                ImGui::PushID(ni * 2 + 1);
                ImGui::InvisibleButton("##ed", icon_box_sz);
                if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
                    set_cursor(AssemblyNoteCursorType::Move);
                    note_cursor_requested = true;
                }
                if (ImGui::IsItemClicked(0))
                    activate_note_edit_controls(AssemblyNoteSelectionType::ArrowSvg, ni);
                if (arrow_selected && ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                    ImVec2 delta = ImGui::GetIO().MouseDelta;
                    arrow.arrow_end_offset.x() += delta.x;
                    arrow.arrow_end_offset.y() += delta.y;
                    any_changed = true;
                }
                ImGui::PopID();
            }
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
        }

        if (arrow_selected) {
            ImVec2 box_min(arrow_end.x - icon_box_sz.x * 0.5f, arrow_end.y - icon_box_sz.y * 0.5f);
            ImVec2 box_max(arrow_end.x + icon_box_sz.x * 0.5f, arrow_end.y + icon_box_sz.y * 0.5f);

            // 4 corner resize handles in the same layout as TextLabelNote
            // (corner index: 0=tl, 1=tr, 2=bl, 3=br). The icon stays centered
            // on arrow_end, so dragging any corner shifts arrow_end_offset by
            // half the mouse delta (the box center moves half of the corner
            // movement when the opposite corner is anchored) while label_size
            // changes by the full delta with per-axis sign:
            //   tl: -dx, -dy   tr: +dx, -dy   bl: -dx, +dy   br: +dx, +dy
            auto drag_svg_resize_handle = [&](const char *id, const ImVec2 &handle_center, int corner) {
                ImVec2 handle_min = draw_note_resize_handle(handle_center);
                ImGui::SetNextWindowPos(handle_min, ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(resize_sz, resize_sz), ImGuiCond_Always);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
                if (ImGui::Begin(id, nullptr, drag_flags)) {
                    ImGui::InvisibleButton("##arrow_svg_resize", ImVec2(resize_sz, resize_sz));
                    mark_imgui_note_click();
                    if (ImGui::IsItemHovered()) {
                        set_cursor(corner == 1 || corner == 2 ? AssemblyNoteCursorType::ResizeNESW : AssemblyNoteCursorType::ResizeNWSE);
                        note_cursor_requested = true;
                    }
                    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                        ImVec2 delta = ImGui::GetIO().MouseDelta;
                        arrow.arrow_end_offset.x() += delta.x * 0.5;
                        arrow.arrow_end_offset.y() += delta.y * 0.5;
                        const float sx = (corner == 1 || corner == 3) ? 1.0f : -1.0f;
                        const float sy = (corner == 2 || corner == 3) ? 1.0f : -1.0f;
                        arrow.label_size.x() += sx * delta.x;
                        arrow.label_size.y() += sy * delta.y;
                        clamp_note_size(arrow.label_size, 32.0 * sc, 32.0 * sc);
                        any_changed = true;
                    }
                }
                ImGui::End();
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(2);
            };
            char resize_win_id[64];
            snprintf(resize_win_id, sizeof(resize_win_id), "##arrow_svg_resize_tl_%d", ni);
            drag_svg_resize_handle(resize_win_id, box_min, 0);
            snprintf(resize_win_id, sizeof(resize_win_id), "##arrow_svg_resize_tr_%d", ni);
            drag_svg_resize_handle(resize_win_id, ImVec2(box_max.x, box_min.y), 1);
            snprintf(resize_win_id, sizeof(resize_win_id), "##arrow_svg_resize_bl_%d", ni);
            drag_svg_resize_handle(resize_win_id, ImVec2(box_min.x, box_max.y), 2);
            snprintf(resize_win_id, sizeof(resize_win_id), "##arrow_svg_resize_br_%d", ni);
            drag_svg_resize_handle(resize_win_id, box_max, 3);

            // Close button sits OUTSIDE the tr resize handle (matches
            // TextLabelNote's layout: tr-handle | gap | close-button) so it
            // never overlaps with the newly-added tr handle.
            ImVec2 close_center(box_max.x + resize_sz + close_r + 2.0f * sc, box_min.y);
            char close_win_id[64];
            snprintf(close_win_id, sizeof(close_win_id), "##arrow_svg_close_win_%d", ni);
            ImGui::SetNextWindowPos(ImVec2(close_center.x - close_r, close_center.y - close_r), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(close_r * 2.0f, close_r * 2.0f), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            if (ImGui::Begin(close_win_id, nullptr, drag_flags)) {
                if (draw_note_close_button(close_center, "##arrow_svg_close"))
                    delete_idx = ni;
            }
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
        }
    }

    int delete_text_idx = -1;
    static std::vector<float> s_text_label_wrap_widths;
    if (s_text_label_wrap_widths.size() < text_labels.size())
        s_text_label_wrap_widths.resize(text_labels.size(), 0.0f);
    for (int ni = 0; ni < (int)text_labels.size(); ++ni) {
        TextLabelNote &label = text_labels[ni];
        clamp_note_size(label.size, 80.0 * sc, 48.0 * sc);
        ImVec2 pos((float)(obj_center.x() + label.pos_offset.x()), (float)(obj_center.y() + label.pos_offset.y()));
        ImVec2 size((float)label.size.x(), (float)label.size.y());
        bool text_selected = is_note_selected(AssemblyNoteSelectionType::TextLabel, ni);

        const auto bg_color_arr = note_color_to_float_array(label.background_color);
        const ImVec4 label_bg(bg_color_arr[0], bg_color_arr[1], bg_color_arr[2], bg_color_arr[3]);

        // Paint the colored body (and selection border) on the background draw list so it stays below the side panels. .
        const float label_rounding = 4.0f * sc;
        draw_list->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                                 ImGui::ColorConvertFloat4ToU32(label_bg), label_rounding);
        if (text_selected)
            draw_list->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                               IM_COL32(25, 166, 77, 242), label_rounding, 0, 1.0f);

        char win_id[64];
        snprintf(win_id, sizeof(win_id), "##text_label_%d", ni);
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f * sc, 6.0f * sc));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, label_rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
        if (ImGui::Begin(win_id, nullptr, text_label_flags)) {
            ImGui::PushID(ni);

            ImVec2 text_size(std::max(1.0f, size.x - 12.0f * sc), std::max(24.0f * sc, size.y - 12.0f * sc));
            float wrap_width = std::max(1.0f, text_size.x - 8.0f * sc);
            // The colored body is now painted on the background draw list, so the InputText frame is kept transparent and the body shows through.
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
            const auto text_color = note_color_to_float_array(label.color);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(text_color[0], text_color[1], text_color[2], text_color[3]));
            // While the multiline input is active, ImGui owns an internal edit
            struct TextWrapCB {
                float cur_wrap_width;
                bool  move_cursor_to_end;
                std::string (*wrap_fn)(const std::string &, float);
            };
            const bool focus_request = (m_note_text_focus_request == ni);
            TextWrapCB cb_data;
            cb_data.cur_wrap_width     = wrap_width;
            cb_data.move_cursor_to_end = focus_request;
            cb_data.wrap_fn            = wrap_text_to_width;
            ImGuiInputTextCallback text_wrap_callback = [](ImGuiInputTextCallbackData *data) -> int {
                TextWrapCB *cb = (TextWrapCB *)data->UserData;
                std::string cur(data->Buf, data->BufTextLen);
                std::string wrapped = cb->wrap_fn(cur, cb->cur_wrap_width);
                if (wrapped != cur) {
                    int visible_before = 0;
                    for (int i = 0; i < data->CursorPos && i < (int)cur.size(); ++i)
                        if (cur[i] != '\n')
                            ++visible_before;
                    data->DeleteChars(0, data->BufTextLen);
                    data->InsertChars(0, wrapped.c_str());
                    int new_pos = 0, seen = 0;
                    while (new_pos < (int)wrapped.size() && seen < visible_before) {
                        if (wrapped[new_pos] != '\n')
                            ++seen;
                        ++new_pos;
                    }
                    data->CursorPos = data->SelectionStart = data->SelectionEnd = new_pos;
                }
                if (cb->move_cursor_to_end)
                    data->CursorPos = data->SelectionStart = data->SelectionEnd = data->BufTextLen;
                return 0;
            };

            ImGuiInputTextFlags text_flags = ImGuiInputTextFlags_Multiline | ImGuiInputTextFlags_NoHorizontalScroll;
            if (text_selected)
                text_flags |= ImGuiInputTextFlags_CallbackAlways;
            else
                text_flags |= ImGuiInputTextFlags_ReadOnly;

            // Only grab keyboard focus on the frame(s) where a focus request is pending (right after the activating click).
            if (focus_request)
                ImGui::SetKeyboardFocusHere();
            bool text_changed = ImGui::InputTextMultiline("##label_text", &label.text, text_size, text_flags,
                                                          text_selected ? text_wrap_callback : nullptr, &cb_data);
            bool text_active = ImGui::IsItemActive();
            if (focus_request && (text_active || ImGui::IsItemFocused()))
                m_note_text_focus_request = -1;
            if (ImGui::IsItemClicked(0)) {
                if (!text_selected)
                    m_note_text_focus_request = ni;
                activate_note_edit_controls(AssemblyNoteSelectionType::TextLabel, ni);
            }
            if (text_changed)
                any_changed = true;
            // Re-flow (unwrap + wrap) only when the input is NOT being edited, e.g. after the box is resized while inactive.
            if (!text_active && std::fabs(s_text_label_wrap_widths[ni] - wrap_width) > 1.0f) {
                std::string unwrapped_text = unwrap_soft_newlines(label.text, s_text_label_wrap_widths[ni]);
                std::string wrapped_text = wrap_text_to_width(unwrapped_text, wrap_width);
                if (wrapped_text != label.text) {
                    label.text = std::move(wrapped_text);
                    any_changed = true;
                }
            }
            s_text_label_wrap_widths[ni] = wrap_width;
            ImGui::PopStyleColor(2);

            double desired_height = text_line_count(label.text) * ImGui::GetTextLineHeightWithSpacing() + 16.0 * sc;
            desired_height = std::min(desired_height, std::max(48.0 * sc, (double)viewport[3] - pos.y - 12.0 * sc));
            if (label.size.y() < desired_height) {
                label.size.y() = desired_height;
                any_changed = true;
            }

            ImGui::PopID();
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);

        if (text_selected) {
            auto drag_border = [&](const char *id, const ImVec2 &border_pos, const ImVec2 &border_size) {
                ImGui::SetNextWindowPos(border_pos, ImGuiCond_Always);
                ImGui::SetNextWindowSize(border_size, ImGuiCond_Always);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
                if (ImGui::Begin(id, nullptr, drag_flags)) {
                    ImGui::InvisibleButton("##label_border_move", border_size);
                    mark_imgui_note_click();
                    if (ImGui::IsItemHovered()) {
                        set_cursor(AssemblyNoteCursorType::Move);
                        note_cursor_requested = true;
                    }
                    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                        ImVec2 delta = ImGui::GetIO().MouseDelta;
                        label.pos_offset.x() += delta.x;
                        label.pos_offset.y() += delta.y;
                        any_changed = true;
                    }
                }
                ImGui::End();
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(2);
            };
            const float border_hot = 6.0f * sc;
            char border_id[64];
            snprintf(border_id, sizeof(border_id), "##label_border_top_%d", ni);
            drag_border(border_id, ImVec2(pos.x + resize_sz, pos.y - border_hot * 0.5f), ImVec2(std::max(1.0f, size.x - resize_sz * 2.0f), border_hot));
            snprintf(border_id, sizeof(border_id), "##label_border_bottom_%d", ni);
            drag_border(border_id, ImVec2(pos.x + resize_sz, pos.y + size.y - border_hot * 0.5f), ImVec2(std::max(1.0f, size.x - resize_sz * 2.0f), border_hot));
            snprintf(border_id, sizeof(border_id), "##label_border_left_%d", ni);
            drag_border(border_id, ImVec2(pos.x - border_hot * 0.5f, pos.y + resize_sz), ImVec2(border_hot, std::max(1.0f, size.y - resize_sz * 2.0f)));
            snprintf(border_id, sizeof(border_id), "##label_border_right_%d", ni);
            drag_border(border_id, ImVec2(pos.x + size.x - border_hot * 0.5f, pos.y + resize_sz), ImVec2(border_hot, std::max(1.0f, size.y - resize_sz * 2.0f)));
        }

        if (text_selected) {
            auto drag_resize_handle = [&](const char *id, const ImVec2 &handle_center, int corner) {
                ImVec2 handle_min = draw_note_resize_handle(handle_center);
                ImGui::SetNextWindowPos(handle_min, ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(resize_sz, resize_sz), ImGuiCond_Always);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
                if (ImGui::Begin(id, nullptr, drag_flags)) {
                    ImGui::InvisibleButton("##label_resize", ImVec2(resize_sz, resize_sz));
                    mark_imgui_note_click();
                    if (ImGui::IsItemHovered()) {
                        set_cursor(corner == 1 || corner == 2 ? AssemblyNoteCursorType::ResizeNESW : AssemblyNoteCursorType::ResizeNWSE);
                        note_cursor_requested = true;
                    }
                    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                        ImVec2 delta = ImGui::GetIO().MouseDelta;
                        if (corner == 0) {
                            label.pos_offset.x() += delta.x;
                            label.pos_offset.y() += delta.y;
                            label.size.x() -= delta.x;
                            label.size.y() -= delta.y;
                        } else if (corner == 1) {
                            label.pos_offset.y() += delta.y;
                            label.size.x() += delta.x;
                            label.size.y() -= delta.y;
                        } else if (corner == 2) {
                            label.pos_offset.x() += delta.x;
                            label.size.x() -= delta.x;
                            label.size.y() += delta.y;
                        } else {
                            label.size.x() += delta.x;
                            label.size.y() += delta.y;
                        }
                        clamp_note_size(label.size, 80.0 * sc, 48.0 * sc);
                        any_changed = true;
                    }
                }
                ImGui::End();
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(2);
            };
            char resize_win_id[64];
            snprintf(resize_win_id, sizeof(resize_win_id), "##label_resize_tl_%d", ni);
            drag_resize_handle(resize_win_id, ImVec2(pos.x, pos.y), 0);
            snprintf(resize_win_id, sizeof(resize_win_id), "##label_resize_tr_%d", ni);
            drag_resize_handle(resize_win_id, ImVec2(pos.x + size.x, pos.y), 1);
            snprintf(resize_win_id, sizeof(resize_win_id), "##label_resize_bl_%d", ni);
            drag_resize_handle(resize_win_id, ImVec2(pos.x, pos.y + size.y), 2);
            snprintf(resize_win_id, sizeof(resize_win_id), "##label_resize_br_%d", ni);
            drag_resize_handle(resize_win_id, ImVec2(pos.x + size.x, pos.y + size.y), 3);
        }

        if (text_selected) {
            const float close_hit_pad = 4.0f * sc;
            const float close_hit_r   = close_r + close_hit_pad;
            ImVec2 close_center(pos.x + size.x + resize_sz + close_r + 2.0f * sc, pos.y);
            char close_win_id[64];
            snprintf(close_win_id, sizeof(close_win_id), "##label_close_win_%d", ni);
            ImGui::SetNextWindowPos(ImVec2(close_center.x - close_hit_r, close_center.y - close_hit_r), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(close_hit_r * 2.0f, close_hit_r * 2.0f), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            if (ImGui::Begin(close_win_id, nullptr, drag_flags)) {
                if (draw_note_close_button(close_center, "##label_close", -1.0f,
                                           IM_COL32(200, 60, 60, 230), IM_COL32(255, 255, 255, 255),
                                           close_hit_pad))
                    delete_text_idx = ni;
            }
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
        }
    }

    int delete_circle_idx = -1;
    for (int ni = 0; ni < (int)circle_notes.size(); ++ni) {
        CircleNote &circle = circle_notes[ni];
        clamp_note_size(circle.size, 24.0 * sc, 24.0 * sc);
        ImVec2 pos((float)(obj_center.x() + circle.pos_offset.x()), (float)(obj_center.y() + circle.pos_offset.y()));
        ImVec2 size((float)circle.size.x(), (float)circle.size.y());
        ImVec2 center(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
        ImVec2 radius(size.x * 0.5f, size.y * 0.5f);
        bool circle_selected = is_note_selected(AssemblyNoteSelectionType::Circle, ni);
        static constexpr double PI = 3.14159265358979323846;

        draw_list->PathClear();
        for (int seg = 0; seg < 48; ++seg) {
            double angle = 2.0 * PI * (double)seg / 48.0;
            draw_list->PathLineTo(ImVec2(center.x + (float)std::cos(angle) * radius.x,
                                         center.y + (float)std::sin(angle) * radius.y));
        }
        draw_list->PathStroke(note_color_to_im_u32(circle.color), true, 2.0f * sc);

        char win_id[64];
        snprintf(win_id, sizeof(win_id), "##circle_note_%d", ni);

        // Moving uses the full circle bounds, while close/resize get their own
        // small overlay windows so they are not swallowed by the move hit area.
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
        if (ImGui::Begin(win_id, nullptr, drag_flags)) {
            ImGui::PushID(ni);
            ImGui::InvisibleButton("##circle_move", size);
            ImVec2 mouse_pos = ImGui::GetIO().MousePos;
            bool on_circle_line = is_point_on_ellipse_outline(mouse_pos, center, radius, line_hit_tol);
            if (ImGui::IsItemHovered() && on_circle_line) {
                set_cursor(AssemblyNoteCursorType::Move);
                note_cursor_requested = true;
            }
            if (ImGui::IsItemClicked(0) && on_circle_line)
                activate_note_edit_controls(AssemblyNoteSelectionType::Circle, ni);
            if (ImGui::IsItemActivated() && on_circle_line)
                s_circle_line_drag_idx = ni;
            if (s_circle_line_drag_idx == ni && ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                set_cursor(AssemblyNoteCursorType::Move);
                note_cursor_requested = true;
                ImVec2 delta = ImGui::GetIO().MouseDelta;
                circle.pos_offset.x() += delta.x;
                circle.pos_offset.y() += delta.y;
                any_changed = true;
            }
            ImGui::PopID();
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);

        if (circle_selected) {
            // 4 corner resize handles in the same layout as TextLabelNote
            // (corner index: 0=tl, 1=tr, 2=bl, 3=br). Each corner anchors the
            // diagonally opposite corner so the ellipse stretches/shrinks
            // toward the dragged corner. pos_offset tracks the bbox top-left
            // (= obj_center + offset), so it shifts whenever the top or left
            // edge moves.
            auto drag_circle_resize_handle = [&](const char *id, const ImVec2 &handle_center, int corner) {
                ImVec2 handle_min = draw_note_resize_handle(handle_center);
                ImGui::SetNextWindowPos(handle_min, ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(resize_sz, resize_sz), ImGuiCond_Always);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
                if (ImGui::Begin(id, nullptr, drag_flags)) {
                    ImGui::InvisibleButton("##circle_resize", ImVec2(resize_sz, resize_sz));
                    mark_imgui_note_click();
                    if (ImGui::IsItemHovered()) {
                        set_cursor(corner == 1 || corner == 2 ? AssemblyNoteCursorType::ResizeNESW : AssemblyNoteCursorType::ResizeNWSE);
                        note_cursor_requested = true;
                    }
                    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                        ImVec2 delta = ImGui::GetIO().MouseDelta;
                        if (corner == 0) {
                            circle.pos_offset.x() += delta.x;
                            circle.pos_offset.y() += delta.y;
                            circle.size.x() -= delta.x;
                            circle.size.y() -= delta.y;
                        } else if (corner == 1) {
                            circle.pos_offset.y() += delta.y;
                            circle.size.x() += delta.x;
                            circle.size.y() -= delta.y;
                        } else if (corner == 2) {
                            circle.pos_offset.x() += delta.x;
                            circle.size.x() -= delta.x;
                            circle.size.y() += delta.y;
                        } else {
                            circle.size.x() += delta.x;
                            circle.size.y() += delta.y;
                        }
                        clamp_note_size(circle.size, 24.0 * sc, 24.0 * sc);
                        any_changed = true;
                    }
                }
                ImGui::End();
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(2);
            };
            char resize_win_id[64];
            snprintf(resize_win_id, sizeof(resize_win_id), "##circle_resize_tl_%d", ni);
            drag_circle_resize_handle(resize_win_id, ImVec2(pos.x, pos.y), 0);
            snprintf(resize_win_id, sizeof(resize_win_id), "##circle_resize_tr_%d", ni);
            drag_circle_resize_handle(resize_win_id, ImVec2(pos.x + size.x, pos.y), 1);
            snprintf(resize_win_id, sizeof(resize_win_id), "##circle_resize_bl_%d", ni);
            drag_circle_resize_handle(resize_win_id, ImVec2(pos.x, pos.y + size.y), 2);
            snprintf(resize_win_id, sizeof(resize_win_id), "##circle_resize_br_%d", ni);
            drag_circle_resize_handle(resize_win_id, ImVec2(pos.x + size.x, pos.y + size.y), 3);

            // Close button sits OUTSIDE the tr resize handle (matches
            // TextLabelNote's layout: tr-handle | gap | close-button) so it
            // never overlaps with the newly-added tr handle.
            ImVec2 close_center(pos.x + size.x + resize_sz + close_r + 2.0f * sc, pos.y);
            char close_win_id[64];
            snprintf(close_win_id, sizeof(close_win_id), "##circle_close_win_%d", ni);
            ImGui::SetNextWindowPos(ImVec2(close_center.x - close_r, close_center.y - close_r), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(close_r * 2.0f, close_r * 2.0f), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            if (ImGui::Begin(close_win_id, nullptr, drag_flags)) {
                if (draw_note_close_button(close_center, "##circle_close"))
                    delete_circle_idx = ni;
            }
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
        }
    }
    // ----------------- Rectangle notes ------- Same interaction model as CircleNote: outline-only stroke, click on the
    static int s_rect_line_drag_idx = -1;
    if (!ImGui::IsMouseDown(0))
        s_rect_line_drag_idx = -1;
    auto is_point_on_rect_outline = [](const ImVec2 &p, const ImVec2 &min_pt, const ImVec2 &max_pt, float tolerance) {
        ImVec2 omin(min_pt.x - tolerance, min_pt.y - tolerance);
        ImVec2 omax(max_pt.x + tolerance, max_pt.y + tolerance);
        if (p.x < omin.x || p.x > omax.x || p.y < omin.y || p.y > omax.y)
            return false;
        ImVec2 imin(min_pt.x + tolerance, min_pt.y + tolerance);
        ImVec2 imax(max_pt.x - tolerance, max_pt.y - tolerance);
        bool inside_inner = (imin.x < imax.x && imin.y < imax.y) &&
                            (p.x > imin.x && p.x < imax.x && p.y > imin.y && p.y < imax.y);
        return !inside_inner;
    };

    int delete_rect_idx = -1;
    for (int ni = 0; ni < (int)rectangle_notes.size(); ++ni) {
        RectangleNote &rect = rectangle_notes[ni];
        clamp_note_size(rect.size, 24.0 * sc, 24.0 * sc);
        ImVec2 pos((float)(obj_center.x() + rect.pos_offset.x()), (float)(obj_center.y() + rect.pos_offset.y()));
        ImVec2 size((float)rect.size.x(), (float)rect.size.y());
        ImVec2 max_pt(pos.x + size.x, pos.y + size.y);
        bool rect_selected = is_note_selected(AssemblyNoteSelectionType::Rectangle, ni);

        draw_list->AddRect(pos, max_pt, note_color_to_im_u32(rect.color), 0.0f, 0, 2.0f * sc);

        char win_id[64];
        snprintf(win_id, sizeof(win_id), "##rect_note_%d", ni);
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
        if (ImGui::Begin(win_id, nullptr, drag_flags)) {
            ImGui::PushID(ni);
            ImGui::InvisibleButton("##rect_move", size);
            ImVec2 mouse_pos = ImGui::GetIO().MousePos;
            bool on_rect_line = is_point_on_rect_outline(mouse_pos, pos, max_pt, line_hit_tol);
            if (ImGui::IsItemHovered() && on_rect_line) {
                set_cursor(AssemblyNoteCursorType::Move);
                note_cursor_requested = true;
            }
            if (ImGui::IsItemClicked(0) && on_rect_line)
                activate_note_edit_controls(AssemblyNoteSelectionType::Rectangle, ni);
            if (ImGui::IsItemActivated() && on_rect_line)
                s_rect_line_drag_idx = ni;
            if (s_rect_line_drag_idx == ni && ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                set_cursor(AssemblyNoteCursorType::Move);
                note_cursor_requested = true;
                ImVec2 delta = ImGui::GetIO().MouseDelta;
                rect.pos_offset.x() += delta.x;
                rect.pos_offset.y() += delta.y;
                any_changed = true;
            }
            ImGui::PopID();
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);

        if (rect_selected) {
            auto drag_rect_resize_handle = [&](const char *id, const ImVec2 &handle_center, int corner) {
                ImVec2 handle_min = draw_note_resize_handle(handle_center);
                ImGui::SetNextWindowPos(handle_min, ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(resize_sz, resize_sz), ImGuiCond_Always);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
                if (ImGui::Begin(id, nullptr, drag_flags)) {
                    ImGui::InvisibleButton("##rect_resize", ImVec2(resize_sz, resize_sz));
                    mark_imgui_note_click();
                    if (ImGui::IsItemHovered()) {
                        set_cursor(corner == 1 || corner == 2 ? AssemblyNoteCursorType::ResizeNESW : AssemblyNoteCursorType::ResizeNWSE);
                        note_cursor_requested = true;
                    }
                    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                        ImVec2 delta = ImGui::GetIO().MouseDelta;
                        if (corner == 0) {
                            rect.pos_offset.x() += delta.x;
                            rect.pos_offset.y() += delta.y;
                            rect.size.x() -= delta.x;
                            rect.size.y() -= delta.y;
                        } else if (corner == 1) {
                            rect.pos_offset.y() += delta.y;
                            rect.size.x() += delta.x;
                            rect.size.y() -= delta.y;
                        } else if (corner == 2) {
                            rect.pos_offset.x() += delta.x;
                            rect.size.x() -= delta.x;
                            rect.size.y() += delta.y;
                        } else {
                            rect.size.x() += delta.x;
                            rect.size.y() += delta.y;
                        }
                        clamp_note_size(rect.size, 24.0 * sc, 24.0 * sc);
                        any_changed = true;
                    }
                }
                ImGui::End();
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(2);
            };
            char resize_win_id[64];
            snprintf(resize_win_id, sizeof(resize_win_id), "##rect_resize_tl_%d", ni);
            drag_rect_resize_handle(resize_win_id, ImVec2(pos.x, pos.y), 0);
            snprintf(resize_win_id, sizeof(resize_win_id), "##rect_resize_tr_%d", ni);
            drag_rect_resize_handle(resize_win_id, ImVec2(pos.x + size.x, pos.y), 1);
            snprintf(resize_win_id, sizeof(resize_win_id), "##rect_resize_bl_%d", ni);
            drag_rect_resize_handle(resize_win_id, ImVec2(pos.x, pos.y + size.y), 2);
            snprintf(resize_win_id, sizeof(resize_win_id), "##rect_resize_br_%d", ni);
            drag_rect_resize_handle(resize_win_id, ImVec2(pos.x + size.x, pos.y + size.y), 3);

            ImVec2 close_center(pos.x + size.x + resize_sz + close_r + 2.0f * sc, pos.y);
            char close_win_id[64];
            snprintf(close_win_id, sizeof(close_win_id), "##rect_close_win_%d", ni);
            ImGui::SetNextWindowPos(ImVec2(close_center.x - close_r, close_center.y - close_r), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(close_r * 2.0f, close_r * 2.0f), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            if (ImGui::Begin(close_win_id, nullptr, drag_flags)) {
                if (draw_note_close_button(close_center, "##rect_close"))
                    delete_rect_idx = ni;
            }
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
        }
    }

    int delete_plain_arrow_idx = -1;
    for (int ni = 0; ni < plain_arrow_count; ++ni) {
        PlainArrowNote &arrow = plain_arrows[ni];
        const ImVec2 &arrow_start = plain_arrow_screen_data[ni].start;
        const ImVec2 &arrow_end   = plain_arrow_screen_data[ni].end;
        bool plain_arrow_selected = is_note_selected(AssemblyNoteSelectionType::PlainArrow, ni);
        const float close_gap = 8.0f * sc;
        ImVec2 close_center(std::max(arrow_start.x, arrow_end.x) + close_r + close_gap,
                            std::min(arrow_start.y, arrow_end.y));
        ImVec2 mouse_pos = ImGui::GetIO().MousePos;
        bool plain_arrow_close_hot = plain_arrow_selected
            && mouse_pos.x >= close_center.x - close_r && mouse_pos.x <= close_center.x + close_r
            && mouse_pos.y >= close_center.y - close_r && mouse_pos.y <= close_center.y + close_r;

        if (!plain_arrow_close_hot) {
            float vx = arrow_end.x - arrow_start.x;
            float vy = arrow_end.y - arrow_start.y;
            float line_len = std::sqrt(vx * vx + vy * vy);
            float endpoint_hot = handle_sz * 2.0f;
            if (line_len > endpoint_hot * 2.0f) {
                ImVec2 line_body_start(arrow_start.x + vx / line_len * endpoint_hot,
                                       arrow_start.y + vy / line_len * endpoint_hot);
                ImVec2 line_body_end(arrow_end.x - vx / line_len * endpoint_hot,
                                     arrow_end.y - vy / line_len * endpoint_hot);
                ImVec2 line_min(std::min(line_body_start.x, line_body_end.x) - line_hit_tol,
                                std::min(line_body_start.y, line_body_end.y) - line_hit_tol);
                ImVec2 line_max(std::max(line_body_start.x, line_body_end.x) + line_hit_tol,
                                std::max(line_body_start.y, line_body_end.y) + line_hit_tol);
                ImVec2 line_size(std::max(1.0f, line_max.x - line_min.x),
                                 std::max(1.0f, line_max.y - line_min.y));
                char line_win_id[64];
                snprintf(line_win_id, sizeof(line_win_id), "##plain_arrow_line_%d", ni);
                ImGui::SetNextWindowPos(line_min, ImGuiCond_Always);
                ImGui::SetNextWindowSize(line_size, ImGuiCond_Always);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
                if (ImGui::Begin(line_win_id, nullptr, drag_flags)) {
                    ImGui::InvisibleButton("##plain_arrow_line_drag", line_size);
                    bool on_arrow_line = distance_to_segment(mouse_pos, line_body_start, line_body_end) <= line_hit_tol;
                    if (ImGui::IsItemHovered() && on_arrow_line) {
                        set_cursor(AssemblyNoteCursorType::Move);
                        note_cursor_requested = true;
                    }
                    if (ImGui::IsItemClicked(0) && on_arrow_line)
                        activate_note_edit_controls(AssemblyNoteSelectionType::PlainArrow, ni);
                    if (ImGui::IsItemActivated() && on_arrow_line)
                        s_plain_arrow_line_drag_idx = ni;
                    if (s_plain_arrow_line_drag_idx == ni && ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                        set_cursor(AssemblyNoteCursorType::Move);
                        note_cursor_requested = true;
                        ImVec2 delta = ImGui::GetIO().MouseDelta;
                        arrow.arrow_start_offset.x() += delta.x;
                        arrow.arrow_start_offset.y() += delta.y;
                        any_changed = true;
                    }
                }
                ImGui::End();
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(2);
            }
        }

        auto drag_arrow_point = [&](const char *name, const ImVec2 &point, bool drag_start) {
            char win_id[64];
            snprintf(win_id, sizeof(win_id), "%s_%d", name, ni);
            // The arrow-end handle also covers the triangle arrowhead area, so
            // users can drag from the visible arrowhead instead of only the dot.
            float drag_sz = drag_start ? handle_sz * 2.0f : std::max(handle_sz * 2.0f, 28.0f * sc);
            float half_drag_sz = drag_sz * 0.5f;
            ImGui::SetNextWindowPos(ImVec2(point.x - half_drag_sz, point.y - half_drag_sz), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(drag_sz, drag_sz));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            if (ImGui::Begin(win_id, nullptr, drag_flags)) {
                ImGui::InvisibleButton("##plain_arrow_drag", ImVec2(drag_sz, drag_sz));
                mark_imgui_note_click();
                if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
                    set_cursor(AssemblyNoteCursorType::ResizeNWSE);
                    note_cursor_requested = true;
                }
                if (ImGui::IsItemClicked(0))
                    activate_note_edit_controls(AssemblyNoteSelectionType::PlainArrow, ni);
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                    ImVec2 delta = ImGui::GetIO().MouseDelta;
                    if (drag_start) {
                        arrow.arrow_start_offset.x() += delta.x;
                        arrow.arrow_start_offset.y() += delta.y;
                        arrow.arrow_end_offset.x() -= delta.x;
                        arrow.arrow_end_offset.y() -= delta.y;
                    } else {
                        arrow.arrow_end_offset.x() += delta.x;
                        arrow.arrow_end_offset.y() += delta.y;
                    }
                    any_changed = true;
                }
            }
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
        };
        if (plain_arrow_selected && !plain_arrow_close_hot) {
            drag_arrow_point("##plain_arrow_start", arrow_start, true);
            drag_arrow_point("##plain_arrow_end", arrow_end, false);
        }
        if (plain_arrow_selected) {
            draw_list->AddCircleFilled(arrow_start, handle_sz * 0.6f, IM_COL32(0, 200, 80, 150));
            draw_list->AddCircleFilled(arrow_end, handle_sz * 0.6f, IM_COL32(0, 160, 220, 150));
        }

        if (plain_arrow_selected) {
            char close_id[64];
            snprintf(close_id, sizeof(close_id), "##plain_arrow_close_%d", ni);
            ImGui::SetNextWindowPos(ImVec2(close_center.x - close_r, close_center.y - close_r), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(close_r * 2.0f, close_r * 2.0f), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            if (ImGui::Begin(close_id, nullptr, drag_flags)) {
                if (draw_note_close_button(close_center, "##close"))
                    delete_plain_arrow_idx = ni;
            }
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
        }
    }

    auto on_note_erased = [&](AssemblyNoteSelectionType type, int idx) {
        if (m_note_selected_type != type)
            return;
        if (m_note_selected_idx == idx) {
            exit_note_edit();
        } else if (m_note_selected_idx > idx) {
            --m_note_selected_idx;
        }
    };

    if (delete_idx >= 0 && delete_idx < count) {
        on_note_erased(AssemblyNoteSelectionType::ArrowSvg, delete_idx);
        arrow_svgs.erase(arrow_svgs.begin() + delete_idx);
        any_changed = true;
    }
    if (delete_text_idx >= 0 && delete_text_idx < (int)text_labels.size()) {
        on_note_erased(AssemblyNoteSelectionType::TextLabel, delete_text_idx);
        text_labels.erase(text_labels.begin() + delete_text_idx);
        any_changed = true;
    }
    if (delete_circle_idx >= 0 && delete_circle_idx < (int)circle_notes.size()) {
        on_note_erased(AssemblyNoteSelectionType::Circle, delete_circle_idx);
        circle_notes.erase(circle_notes.begin() + delete_circle_idx);
        any_changed = true;
    }
    if (delete_rect_idx >= 0 && delete_rect_idx < (int)rectangle_notes.size()) {
        on_note_erased(AssemblyNoteSelectionType::Rectangle, delete_rect_idx);
        rectangle_notes.erase(rectangle_notes.begin() + delete_rect_idx);
        any_changed = true;
    }
    if (delete_plain_arrow_idx >= 0 && delete_plain_arrow_idx < (int)plain_arrows.size()) {
        on_note_erased(AssemblyNoteSelectionType::PlainArrow, delete_plain_arrow_idx);
        plain_arrows.erase(plain_arrows.begin() + delete_plain_arrow_idx);
        any_changed = true;
    }

    if (!note_cursor_requested)
        reset_cursor_if_note_cursor();

    if (any_changed) {
        cur_entry.need_save = true;
        save_assembly_steps_json_to_model();
    }

    // Part-number labels: pill text + drag (lines already drawn above in Pass 1).
    if (has_pn)
        render_part_number_labels_on_canvas(viewport, vp_height);

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

void AssemblyStepsUtils::render_assembly_structure_option_menu(
    ImGuiWrapper &imgui,
    float sc,
    bool is_dark)
{
    ImGui::PushStyleColor(ImGuiCol_PopupBg,
        is_dark ? ImVec4(0.18f, 0.18f, 0.20f, 0.95f) : ImVec4(0.96f, 0.96f, 0.96f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Text,
        is_dark ? ImVec4(0.85f, 0.85f, 0.85f, 1.0f) : ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
        is_dark ? ImVec4(0.30f, 0.55f, 0.80f, 0.60f) : ImVec4(0.26f, 0.59f, 0.98f, 0.31f));
    ImGui::PushStyleColor(ImGuiCol_Separator,
        is_dark ? ImVec4(0.35f, 0.35f, 0.40f, 1.0f) : ImVec4(0.70f, 0.70f, 0.70f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * sc, 6.0f * sc));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * sc, 4.0f * sc));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 4.0f * sc);

    if (ImGui::BeginPopup("##assembly_structure_option_menu")) {
        if (ImGui::MenuItem(_u8L("Set export file parameters").c_str()))
            show_pdf_export_settings_dialog();

        ImGui::Separator();
        {
            const float       margin_slider_w = 120.0f * sc;
            const float       margin_value_w  = 56.0f * sc;
            const std::string margin_tip      = _u8L("Camera margin factor for the start frame. The larger the value, the more margin.");
            auto              set_margin_tip  = [&]() {
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", margin_tip.c_str());
            };

            imgui.text(_u8L("Start frame camera margin"));
            set_margin_tip();

            ImGui::PushItemWidth(margin_slider_w);
            // bbl_slider_float_style only clears the hovered/active frame bg, so the
            // idle track would otherwise show the theme's dark FrameBg. Match hover.
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            imgui.bbl_slider_float_style("##assembly_start_frame_camera_margin", &m_margin_factor_camera_for_not_last_frame, 1.2f, 2.0f, "%.2f");
            ImGui::PopStyleColor();
            set_margin_tip();
            ImGui::SameLine();
            ImGui::PushItemWidth(margin_value_w);
            // Drop the idle gray frame background; keep the hover / active styling.
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::BBLDragFloat("##assembly_start_frame_camera_margin_input", &m_margin_factor_camera_for_not_last_frame, 0.01f, 1.2f, 2.0f, "%.2f");
            ImGui::PopStyleColor();
            set_margin_tip();
        }
        ImGui::Separator();
        {
            update_part_number_label_font_size_from_config();

            const float       label_slider_w = 120.0f * sc;
            const float       label_value_w  = 56.0f * sc;
            const float       label_min      = ASSEMBLY_LABEL_DEFAULT_FONT_SIZE;
            const float       label_max      = ASSEMBLY_LABEL_DEFAULT_FONT_SIZE * ASSEMBLY_LABEL_MAX_FONT_SIZE_FACTOR;
            float             label_font_size = part_number_label_font_size();
            const std::string label_tip      = _u8L("Canvas label text size. The default value uses the current text size.");
            auto              set_label_tip  = [&]() {
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", label_tip.c_str());
            };

            imgui.text(_u8L("Canvas label text size"));
            set_label_tip();

            bool label_changed = false;
            bool label_released = false;
            ImGui::PushItemWidth(label_slider_w);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            label_changed |= imgui.bbl_slider_float_style("##assembly_label_font_size", &label_font_size, label_min, label_max, "%.2f");
            label_released |= imgui.get_last_slider_status().deactivated_after_edit;
            ImGui::PopStyleColor();
            set_label_tip();
            ImGui::SameLine();
            ImGui::PushItemWidth(label_value_w);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            label_changed |= ImGui::BBLDragFloat("##assembly_label_font_size_input", &label_font_size, 0.1f, label_min, label_max, "%.2f");
            label_released |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::PopStyleColor();
            set_label_tip();

            if (label_changed) {
                save_part_number_label_font_size_to_config(label_font_size);
                do_commond_callback("dirty");
                do_commond_callback("request_extra_frame");
            }
            if (label_released) {
                save_part_number_label_font_size_to_config(label_font_size, true);
                auto_layout_labels_in_current_view();
            }
        }
#if !BBL_RELEASE_TO_PUBLIC
 /*       bool explode_repeated_as_whole = m_show_modelobject_name_when_modelobject_has_occur_before;
        if (ImGui::MenuItem(_u8L("Explode repeated model objects as a whole").c_str(), nullptr,
                            &explode_repeated_as_whole)) {
            m_show_modelobject_name_when_modelobject_has_occur_before = explode_repeated_as_whole;
        }
        bool show_origin_step_tree = AssemblyTreeData::show_origin_step_tree;
        if (ImGui::MenuItem(_u8L("Show origin STEP tree").c_str(), nullptr, &show_origin_step_tree)) {
            AssemblyTreeData::show_origin_step_tree = show_origin_step_tree;
            m_assembly_tree_ui_current_folder_node = -1;
            m_assembly_tree_ui_original_checked.clear();
        }*/
#endif
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);
}

void AssemblyStepsUtils::sync_canvas_selection_to_tree(bool selection_empty,bool selection_instance,const std::vector<int> &selected_object_indices)
{
    if (selection_empty) {
        bool keep = selected_node >= 0 && selected_node < (int) _steps_nodes.size() && _steps_nodes[selected_node].type == AssemblyStepsTreeNode::Type::Folder;
        if (!keep) {
            clear_when_no_selection();
        }
        return;
    }

    if (!selection_instance)
        return;

    bool folder_owns_selection = false;
    if (selected_node >= 0 && selected_node < (int) _steps_nodes.size() && _steps_nodes[selected_node].type == AssemblyStepsTreeNode::Type::Folder) {
        std::set<int> folder_objs = collect_node_object_indices(selected_node);
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

    // If the current selected_node already points to a node whose object_idx
    int sel_obj_idx = selected_object_indices.front();
    if (selected_node >= 0 && selected_node < (int) _steps_nodes.size()) {
        auto &cur = _steps_nodes[selected_node];
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

    // When the user has already picked a step tree node (selected_node >= 0),
    if (selected_node >= 0) {
        sync_canvas_selection_to_selected_node_popup_checked();
        // Always return matches when there is more than one, so the tree
        // view tip can list the candidate steps.
        return matches.size() > 1 ? matches : std::vector<AssemblySelectionMatchInfo>{};
    }
    if (matches.size() == 1) {
        if (!has_selected_node()) // is_final_assembly_folder(matches.front().folder_node_idx)
            return {};

        int prev_folder = find_parent_folder(selected_node);
        selected_node = matches.front().object_node_idx;

        int cur_folder = find_parent_folder(selected_node);

        if (cur_folder != prev_folder) {//sync_single_canvas_selection_to_tree_or_get_matches
            on_selected_node_step_changed(cur_folder);
            m_last_folder_idx = cur_folder;
        }
        on_selected_node_changed();
        return {};
    }

    if (matches.size() > 1) {
        // selected_node was -1 here: surface candidates so the user can pick a step explicitly. We still don't auto-clear selected_node (it's already -1), but we report matches to the caller.
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
    const int cur_folder = const_cast<AssemblyStepsUtils*>(this)->find_parent_folder(selected_node);
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
        def.selected    = (final_folder_idx >= 0 && selected_node == final_folder_idx);
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

        std::set<int> obj_set;
        std::function<void(int)> collect = [&](int idx) {
            if (idx < 0 || idx >= (int) _steps_nodes.size()) return;
            const auto &node = _steps_nodes[idx];
            if (node.type == AssemblyStepsTreeNode::Type::Object && node.object_idx >= 0)
                obj_set.insert(node.object_idx);
            for (int ci : node.children) collect(ci);
        };
        collect(root_idx);
        step.count = (int) obj_set.size();

        for (int obj_idx : obj_set) {
            if (obj_idx < 0 || obj_idx >= (int) m_model->objects.size()) continue;
            const ModelObject *obj = m_model->objects[obj_idx];
            if (!obj) continue;
            AssemblyStructureChip chip;
            chip.label = obj->name.empty() ? (_u8L("Part") + " " + std::to_string(obj_idx + 1)) : obj->name;
            step.chips.push_back(std::move(chip));
        }
        if (!step.chips.empty())
            step.prefix_text = _u8L("Contain");
        else
            step.placeholder_text = _u8L("Add object to current step");

        data.cards.push_back(std::move(step));
        ++step_seq;
    }

    return data;
}

bool AssemblyStepsUtils::render_structure_card_select_controls(
    int card_idx,
    const ImVec2& pos,
    const AssemblySelectControlsStyle& style,
    const std::string& value_label,
    const std::string& full_value_label)
{
    ImFont*     font      = ImGui::GetFont();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    const std::string label = _u8L("Select");
    const float label_w = font->CalcTextSizeA(style.font_size, FLT_MAX, 0.f, label.c_str()).x;
    const float width = label_w + 2.0f * style.pad_x;
    const ImVec2 max(pos.x + width, pos.y + style.height);

    draw_list->AddRectFilled(pos, max, style.bg_col, style.radius);
    draw_list->AddText(font, style.font_size,
                       ImVec2(pos.x + style.pad_x, pos.y + (style.height - style.font_size) * 0.5f),
                       style.button_text_col, label.c_str());

    const float value_x = max.x + style.gap;
    const float value_w = value_label.empty()
        ? 0.f
        : font->CalcTextSizeA(style.font_size, FLT_MAX, 0.f, value_label.c_str()).x;
    if (!value_label.empty())
        draw_list->AddText(font, style.font_size,
                           ImVec2(value_x, pos.y + (style.height - style.font_size) * 0.5f),
                           style.label_text_col, value_label.c_str());

    ImGui::SetCursorScreenPos(pos);
    const std::string id = "##asp_sel_" + std::to_string(card_idx);
    ImGui::InvisibleButton(id.c_str(), ImVec2(width, style.height));
    const bool clicked = ImGui::IsItemClicked();
    const bool button_hovered = ImGui::IsItemHovered();
    if (clicked)
        m_structure_select_popup_pending_card = card_idx;

    bool label_hovered = false;
    if (value_w > 0.f) {
        ImGui::SetCursorScreenPos(ImVec2(value_x, pos.y));
        ImGui::PushID((id + "_val").c_str());
        ImGui::InvisibleButton("##val", ImVec2(value_w, style.height));
        label_hovered = ImGui::IsItemHovered();
        if (label_hovered && full_value_label != value_label)
            render_panel_tooltip(full_value_label);
        ImGui::PopID();
    }

    return clicked || button_hovered || label_hovered;
}

void AssemblyStepsUtils::render_panel_tooltip(const std::string &text, bool use_dark_style) const
{
    auto& sc = m_imgui_scale;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * sc, 6.0f * sc));
    if (use_dark_style) {
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(38.0f / 255.0f, 46.0f / 255.0f, 48.0f / 255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    }
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(280.0f * sc);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
    if (use_dark_style)
        ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

void AssemblyStepsUtils::open_structure_add_tree(int card_idx, int step_node_idx, const ImVec2 &pos)
{
    m_structure_add_tree_card = card_idx;
    m_structure_add_tree_step_node = step_node_idx;
    m_structure_add_tree_pos = pos;
    m_structure_add_tree_opened_this_frame = true;
    if (m_model && step_node_idx >= 0)
        reseed_assembly_tree_checked_from_step(step_node_idx, m_model->get_assembly_tree_data());
    // Force the tree UI to treat this as a context change so the checked map is
    // re-seeded from the step's current membership (keeps "List" checkboxes in
    // sync with the "Contain" chips even when reopening the same step).
    m_assembly_tree_ui_current_folder_node = -1;
    m_assembly_tree_search_active = false;
    m_assembly_tree_search_focus_pending = false;
    m_assembly_tree_search_text.clear();
}

void AssemblyStepsUtils::exit_render_assembly_tree_ui()
{
    m_structure_add_tree_card = -1;
    m_structure_add_tree_step_node = -1;
    m_assembly_tree_ui_current_folder_node = -1;
    m_assembly_tree_ui_original_checked.clear();
    m_structure_add_tree_opened_this_frame = false;
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
    for (int i = 0; i < (int) _steps_nodes.size(); ++i) {
        const auto &node = _steps_nodes[i];
        if (node.type != AssemblyStepsTreeNode::Type::Folder)
            continue;
        if (node.is_final_assembly) {
            final_idx = i;
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

void AssemblyStepsUtils::insert_structure_step_relative(int ref_node_idx, bool before)
{
    if (!m_model)
        return;

    int new_idx = create_folder_node(_u8L("New Step"), 0);
    if (new_idx < 0)
        return;

    ensure_default_keyframe(new_idx);
    auto it = std::find(_steps_roots.begin(), _steps_roots.end(), ref_node_idx);
    if (it != _steps_roots.end())
        _steps_roots.insert(before ? it : it + 1, new_idx);
    else
        _steps_roots.push_back(new_idx);

    clear_selection();
    renumber_structure_step_roots();
    selected_node = new_idx;
    m_structure_scroll_to_node = new_idx;
    on_selected_node_changed();
    invalidate_play_frame_refs();//insert_structure_step_relative
    save_assembly_steps_json_to_model();
    do_commond_callback("dirty");
    do_commond_callback("request_extra_frame");
}

void AssemblyStepsUtils::delete_structure_step(int node_idx)
{
    if (!m_model || node_idx < 0 || node_idx >= (int) _steps_nodes.size())
        return;
    if (_steps_nodes[node_idx].type != AssemblyStepsTreeNode::Type::Folder || _steps_nodes[node_idx].is_final_assembly)
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
        selected_node = prev_card_node;
        m_structure_scroll_to_node = prev_card_node;
        select_steps_tree_node_for_canvas(selected_node);
    } else if (selected_node == node_idx || find_parent_folder(selected_node) == node_idx) {
        on_selected_node_changed();
    }

    renumber_structure_step_roots();
    invalidate_play_frame_refs();//delete_structure_step
    save_assembly_steps_json_to_model();
    do_commond_callback("dirty");
    do_commond_callback("request_extra_frame");
}

void AssemblyStepsUtils::render_structure_step_option_menu(
    int card_idx,
    const AssemblyStructureCard& card,
    const ImVec2& anchor,
    float sc,
    bool is_dark)
{
    if (card.node_idx < 0)
        return;

    const std::string popup_id = "##asp_step_option_menu_" + std::to_string(card_idx);
    if (ImGui::IsItemClicked(0)) {
        select_steps_tree_node_for_canvas(card.node_idx);
        ImGui::OpenPopup(popup_id.c_str());
    }

    const std::array<std::string, 4> labels = {
        _u8L("Edit step name"),
        _u8L("Delete step"),
        _u8L("Insert step before"),
        _u8L("Insert step after"),
    };

    const float row_height = 28.0f * sc;
    const float row_spacing = 2.0f * sc;
    const float win_padding = 12.0f * sc;
    const float row_pad_x = 8.0f * sc;
    const float text_right_margin = 8.0f * sc;

    float max_text_width = 0.0f;
    for (const std::string &label : labels)
        max_text_width = std::max(max_text_width, ImGui::CalcTextSize(label.c_str()).x);

    const float menu_width = std::max(168.0f * sc,
        2.0f * win_padding + row_pad_x + max_text_width + text_right_margin);
    const float menu_height = win_padding * 2.0f + row_height * 4.0f + row_spacing * 3.0f;
    ImGui::SetNextWindowPos(ImVec2(anchor.x + 35.0f * sc, anchor.y), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(menu_width, menu_height), ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_PopupBg, is_dark ? ImVec4(45 / 255.0f, 45 / 255.0f, 49 / 255.0f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 77.0f / 255.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, is_dark ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(38.0f / 255.0f, 46.0f / 255.0f, 48.0f / 255.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 4.0f * sc);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(win_padding, win_padding));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

    if (ImGui::BeginPopup(popup_id.c_str(), ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove)) {
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        for (int i = 0; i < (int)labels.size(); ++i) {
            ImGui::PushID(i);
            ImVec2 row_pos = ImGui::GetCursorScreenPos();
            const float row_content_w = ImGui::GetContentRegionAvail().x;
            if (ImGui::InvisibleButton("##item", ImVec2(row_content_w, row_height))) {
                if (i == 0) {
                    begin_structure_step_rename(card.node_idx);
                } else if (i == 1) {
                    delete_structure_step(card.node_idx);
                } else if (i == 2) {
                    insert_structure_step_relative(card.node_idx, true);
                } else {
                    insert_structure_step_relative(card.node_idx, false);
                }
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::IsItemHovered()) {
                const ImU32 bg = is_dark ? IM_COL32(55, 55, 59, 255) : IM_COL32(240, 240, 240, 255);
                draw_list->AddRectFilled(row_pos, ImVec2(row_pos.x + row_content_w, row_pos.y + row_height), bg, 4.0f * sc);
            }
            const ImVec2 text_size = ImGui::CalcTextSize(labels[i].c_str());
            draw_list->AddText(ImVec2(row_pos.x + row_pad_x, row_pos.y + (row_height - text_size.y) * 0.5f),
                               ImGui::GetColorU32(ImGuiCol_Text), labels[i].c_str());
            ImGui::PopID();
            if (i != (int)labels.size() - 1)
                ImGui::Dummy(ImVec2(0.0f, row_spacing));
        }
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(3);
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

void AssemblyStepsUtils::render_assembly_structure_panel(float canvas_w, float canvas_h)
{
    m_assembly_structure_right_x = 0.f;
    if (!m_imgui || !m_model)
        return;
    ImGuiWrapper &imgui = *m_imgui;
    const float   sc    = m_imgui_scale > 0.f ? m_imgui_scale : 1.f;

    AssemblyStructurePanelData data = build_assembly_structure_panel_data();
    if (data.cards.empty())
        return;

    // Font sizes derived from ImGui's active font (same baseline as tree view).
    const float fs       = ImGui::GetFontSize();
    const float fs_title = fs;
    const float fs_chip  = fs * (12.0f / 13.0f);
    const float fs_small = fs * (11.0f / 13.0f);
    const float fs_tiny  = fs * (10.0f / 13.0f);
    const float line_h   = fs * 1.25f;

    // Layout constants -------------------------------------------------------
    const float panel_x        = 12.0f * sc;
    const float panel_y        = 17.0f * sc;
    const float panel_w        = 384.0f * sc;       // 1.5x of original 256*sc
    const float panel_radius   = 4.0f * sc;
    const float header_h       = 64.0f * sc;
    const float side_pad       = 8.0f * sc;
    const float card_gap       = 12.0f * sc;
    const float card_pad       = 8.0f * sc;
    const float card_radius    = 4.0f * sc;
    const float card_h_fixed   = 113.0f * sc;
    const float tag_h          = 20.0f * sc;
    const float tag_h_pad      = 6.0f * sc;
    const float tag_radius     = 6.0f * sc;
    const float chip_h         = 24.0f * sc;
    const float chip_h_pad     = 10.0f * sc;
    const float chip_gap       = 8.0f * sc;
    const float chip_radius    = 6.0f * sc;
    const float action_h       = 34.0f * sc;
    const float bottom_pad     = 16.0f * sc;
    const float icon_sz_hdr    = 16.0f * sc;
    constexpr size_t kChipMaxChars = 20;

    // Colors -----------------------------------------------------------------
    const ImU32 col_white      = m_is_dark ? IM_COL32(55, 55, 59, 255) : IM_COL32(255, 255, 255, 255);
    const ImU32 col_header_top = m_is_dark ? IM_COL32(48, 48, 52, 255) : IM_COL32(0xF8, 0xF8, 0xF8, 255);
    const ImU32 col_header_bot = m_is_dark ? IM_COL32(42, 42, 46, 255) : IM_COL32(0xF1, 0xF1, 0xF1, 255);
    const ImU32 col_text_dark  = m_is_dark ? IM_COL32(0xE0, 0xE0, 0xE0, 255) : IM_COL32(0x26, 0x2E, 0x30, 255);
    const ImU32 col_text_mid   = m_is_dark ? IM_COL32(0xA0, 0xA0, 0xA0, 255) : IM_COL32(0x6B, 0x6B, 0x6B, 255);
    const ImU32 col_text_light = m_is_dark ? IM_COL32(0x80, 0x80, 0x80, 255) : IM_COL32(0xAC, 0xAC, 0xAC, 255);
    const ImU32 col_card_bg    = m_is_dark ? IM_COL32(45, 45, 49, 255) : IM_COL32(0xF8, 0xF8, 0xF8, 255);
    const ImU32 col_card_border= m_is_dark ? IM_COL32(70, 70, 74, 255) : IM_COL32(0xEE, 0xEE, 0xEE, 255);
    const ImU32 col_brand      = IM_COL32(0x00, 0xAE, 0x42, 255);
    const ImU32 col_brand_soft = IM_COL32(0x2C, 0xAD, 0x00, (int) (0.14f * 255.f));
    const ImU32 col_brand_addbg= m_is_dark ? IM_COL32(0x2A, 0x3F, 0x26, 255) : IM_COL32(0xD8, 0xEA, 0xD2, 255);
    const ImU32 col_chip_bg    = m_is_dark ? IM_COL32(65, 65, 69, 255) : IM_COL32(0xEE, 0xEE, 0xEE, 255);

    auto text_w_fn = [&](float size, const std::string &s) {
        return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.f, s.c_str()).x;
    };

    // Footer hint shown when no step card is selected: editing object pose
    std::string footer_hint_str;
    if (!has_selected_node())
        footer_hint_str = _u8L("Tip") + ":" + _u8L("No step card is currently selected. Pose-changing operations such as translation will affect the actual final-assembly view.");
    else
        footer_hint_str = _u8L("Tip") + ":" + _u8L("Double-click an empty area to exit all editing states.");
    const float footer_hint_wrap = panel_w - 2.0f * side_pad;
    const ImVec2 footer_hint_size = footer_hint_str.empty()
        ? ImVec2(0.0f, 0.0f)
        : ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, footer_hint_wrap,
                                          footer_hint_str.c_str(), nullptr, nullptr);
    const float footer_hint_extra_h = footer_hint_str.empty()
        ? 0.0f
        : (footer_hint_size.y + 8.0f * sc);

    // When collapsed only the header is visible.
    const float cards_total = static_cast<float>(data.cards.size()) * (card_h_fixed + card_gap);
    const float scroll_content_h = card_gap + cards_total;
    const float max_scroll_region_h = canvas_h * 0.5f;
    const float scroll_region_h_target = std::min(scroll_content_h, max_scroll_region_h);
    const float full_h      = header_h + scroll_region_h_target + action_h + bottom_pad + card_gap + footer_hint_extra_h;
    const float panel_h     = m_structure_panel_collapsed
                                ? header_h
                                : std::min(canvas_h - panel_y - 12.0f * sc, full_h);

    // Window setup -----------------------------------------------------------
    imgui.set_next_window_pos(panel_x, panel_y, ImGuiCond_Always);
    imgui.set_next_window_size(panel_w, panel_h, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));

    imgui.begin(std::string("##assembly_structure_panel"),
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
                | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
                | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImDrawList *dl  = ImGui::GetWindowDrawList();
    const ImVec2 win_min = ImGui::GetWindowPos();
    const ImVec2 win_max = ImVec2(win_min.x + panel_w, win_min.y + panel_h);
    ImFont *font = ImGui::GetFont();

    // Outer card (background + soft shadow + rounded corners).
    if (!m_is_dark) {
        dl->AddRectFilled(ImVec2(win_min.x + 2.f, win_min.y + 2.f),
                          ImVec2(win_max.x + 2.f, win_max.y + 2.f),
                          IM_COL32(0, 0, 0, 26), panel_radius);
    }
    dl->AddRectFilled(win_min, win_max, col_white, panel_radius);

    // ---- Header ----------------------------------------------------------
    const ImVec2 hd_min = win_min;
    const ImVec2 hd_max = ImVec2(win_max.x, win_min.y + header_h);
    dl->AddRectFilledMultiColor(hd_min, hd_max, col_header_top, col_header_top,
                                 col_header_bot, col_header_bot);

    // Left icon: collapse / expand toggle (vertically centered on title line).
    const float title_line_cy = win_min.y + 6.f * sc + fs_title * 0.5f;
    {
        const ImVec2 toggle_min(win_min.x + 8.f * sc,
                                title_line_cy - icon_sz_hdr * 0.5f);
        const ImVec2 toggle_max(toggle_min.x + icon_sz_hdr, toggle_min.y + icon_sz_hdr);
        ImTextureID toggle_icon = m_structure_panel_collapsed ? m_panel_expand_icon
                                                              : m_panel_collapse_icon;
        if (toggle_icon)
            dl->AddImage(toggle_icon, toggle_min, toggle_max);
        ImGui::SetCursorScreenPos(toggle_min);
        ImGui::PushID("##asp_toggle");
        ImGui::InvisibleButton("##t", ImVec2(icon_sz_hdr, icon_sz_hdr));
        if (ImGui::IsItemClicked(0))
            m_structure_panel_collapsed = !m_structure_panel_collapsed;
        if (ImGui::IsItemHovered()) {
            dl->AddRectFilled(toggle_min, toggle_max, IM_COL32(38, 46, 48, 18), 3.0f * sc);
            render_panel_tooltip(m_structure_panel_collapsed ? _u8L("Expand") : _u8L("Collapse"));
        }
        ImGui::PopID();
    }

    // Far-right icon: option (tree_option.svg).
    {
        const ImVec2 opt_min(win_max.x - 8.f * sc - icon_sz_hdr,
                             title_line_cy - icon_sz_hdr * 0.5f);
        const ImVec2 opt_max(opt_min.x + icon_sz_hdr, opt_min.y + icon_sz_hdr);
        if (m_structure_option_icon)
            dl->AddImage(m_structure_option_icon, opt_min, opt_max);
        ImGui::SetCursorScreenPos(opt_min);
        ImGui::PushID("##asp_option");
        ImGui::InvisibleButton("##o", ImVec2(icon_sz_hdr, icon_sz_hdr));
        if (ImGui::IsItemClicked(0))
            ImGui::OpenPopup("##assembly_structure_option_menu");
        if (ImGui::IsItemHovered()) {
            dl->AddRectFilled(opt_min, opt_max, IM_COL32(38, 46, 48, 18), 3.0f * sc);
            render_panel_tooltip(_u8L("Options"));
        }
        render_assembly_structure_option_menu(imgui, sc, m_is_dark);
        ImGui::PopID();
    }

    // Title + help icon (view_help.svg right after title text) + subtitle.
    {
        const float title_left = win_min.x + 8.f * sc + icon_sz_hdr + 6.f * sc;
        const float title_y    = win_min.y + 6.f * sc;
        const float title_right = win_max.x - 8.f * sc - icon_sz_hdr - 6.f * sc;
        const float title_max_w = title_right - title_left;

        dl->AddText(font, fs_title,
                    ImVec2(title_left, title_y),
                    col_text_dark, data.title.c_str());

        // Help icon (view_help.svg) immediately after title text.
        {
            const float title_tw = text_w_fn(fs_title, data.title);
            const float help_x = title_left + title_tw + 4.f * sc;
            const float help_y = title_line_cy - icon_sz_hdr * 0.5f;
            const ImVec2 help_min(help_x, help_y);
            const ImVec2 help_max(help_x + icon_sz_hdr, help_y + icon_sz_hdr);
            if (m_structure_help_icon)
                dl->AddImage(m_structure_help_icon, help_min, help_max);
            ImGui::SetCursorScreenPos(help_min);
            ImGui::PushID("##asp_help");
            ImGui::InvisibleButton("##h", ImVec2(icon_sz_hdr, icon_sz_hdr));
            if (ImGui::IsItemClicked(0)) {
                wxLaunchDefaultBrowser("https://e.bambulab.com/t?c=T0HuraoU2gH6ufRk");
            }
            if (ImGui::IsItemHovered()) {
                dl->AddRectFilled(help_min, help_max, IM_COL32(38, 46, 48, 18), 3.0f * sc);
                render_panel_tooltip(_u8L("Go to Wiki"));
            }
            ImGui::PopID();
        }

        // Subtitle: starts at the same x as the collapse icon (left-aligned to panel edge).
        const float sub_left = win_min.x + 8.f * sc;
        const float sub_y    = title_y + fs_title + 4.f * sc;
        const float sub_max_w = panel_w - 16.f * sc;
        const float sub_full_w = text_w_fn(fs_small, data.subtitle);
        if (sub_full_w <= sub_max_w) {
            dl->AddText(font, fs_small,
                        ImVec2(sub_left, sub_y),
                        col_text_mid, data.subtitle.c_str());
        } else {
            std::string clipped = data.subtitle;
            while (!clipped.empty()) {
                std::string trial = clipped + "...";
                if (text_w_fn(fs_small, trial) <= sub_max_w)
                    break;
                size_t i = clipped.size();
                while (i > 0 && (static_cast<unsigned char>(clipped[i - 1]) & 0xC0) == 0x80)
                    --i;
                if (i > 0) --i;
                clipped.resize(i);
            }
            const std::string display_sub = clipped + "...";
            dl->AddText(font, fs_small,
                        ImVec2(sub_left, sub_y),
                        col_text_mid, display_sub.c_str());
            const ImVec2 sub_min_pos(sub_left, sub_y);
            ImGui::SetCursorScreenPos(sub_min_pos);
            ImGui::PushID("##asp_subtitle");
            ImGui::InvisibleButton("##sub", ImVec2(sub_max_w, fs_small + 4.f * sc));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", data.subtitle.c_str());
            ImGui::PopID();
        }
    }

    // When collapsed, only the header is visible.
    if (m_structure_panel_collapsed) {
        imgui.end();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(1);
        m_assembly_structure_right_x = panel_x + panel_w;
        return;
    }

    // ---- Scrollable card region -------------------------------------------
    // The child window only owns the cards stack: header sits above it, the
    const float scroll_region_y = win_min.y + header_h;
    const float scroll_region_h = scroll_region_h_target;
    ImGui::SetCursorScreenPos(ImVec2(win_min.x, scroll_region_y));
    ImGuiWindowFlags scroll_flags = ImGuiWindowFlags_NoBackground;
    if (data.always_show_scrollbar || scroll_content_h > scroll_region_h)
        scroll_flags |= ImGuiWindowFlags_AlwaysVerticalScrollbar;
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f * sc);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(144 / 255.f, 144 / 255.f, 144 / 255.f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(144 / 255.f, 144 / 255.f, 144 / 255.f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(120 / 255.f, 120 / 255.f, 120 / 255.f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(38 / 255.f, 46 / 255.f, 48 / 255.f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(245 / 255.f, 247 / 255.f, 248 / 255.f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(236 / 255.f, 240 / 255.f, 242 / 255.f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(228 / 255.f, 235 / 255.f, 238 / 255.f, 1.0f));
    ImGui::BeginChild("##asp_cards_scroll", ImVec2(panel_w, scroll_region_h), false, scroll_flags);

    const ImVec2 child_pos = ImGui::GetWindowPos();
    float scroll_y = ImGui::GetScrollY();
    if (m_structure_scroll_to_node >= 0) {
        for (size_t ci = 0; ci < data.cards.size(); ++ci) {
            if (data.cards[ci].node_idx != m_structure_scroll_to_node)
                continue;

            const float target_y = card_gap + static_cast<float>(ci) * (card_h_fixed + card_gap);
            const float target_scroll_y = std::max(0.0f, target_y - card_gap);
            ImGui::SetScrollY(target_scroll_y);
            scroll_y = target_scroll_y;

            if (target_y >= scroll_y && target_y + card_h_fixed <= scroll_y + scroll_region_h)
                m_structure_scroll_to_node = -1;
            break;
        }
    }
    float cur_y = card_gap;
    ImDrawList *cdl = ImGui::GetWindowDrawList();
    for (size_t ci = 0; ci < data.cards.size(); ++ci) {
        const auto &c = data.cards[ci];
        const float card_x = child_pos.x + side_pad;
        const float card_w = panel_w - 2.f * side_pad;
        const float card_h = card_h_fixed;
        const float card_screen_y = child_pos.y + cur_y - scroll_y;

        const ImVec2 card_min(card_x, card_screen_y);
        const ImVec2 card_max(card_x + card_w, card_screen_y + card_h);
        bool suppress_card_click = false;

        cdl->AddRectFilled(card_min, card_max, c.selected ? col_white : col_card_bg, card_radius);
        cdl->AddRect(card_min, card_max, c.selected ? col_brand : col_card_border, card_radius, 0, 1.0f);

        // Tag pill.
        const ImU32 tag_bg  = (c.tag_style == AssemblyStructureCard::TagStyle::Step) ? col_brand_soft : col_chip_bg;
        const ImU32 tag_txt = (c.tag_style == AssemblyStructureCard::TagStyle::Step) ? col_brand     : col_text_mid;
        const float tag_text_w = text_w_fn(fs_small, c.tag_text);
        const float tag_w      = tag_text_w + 2.f * tag_h_pad;
        const ImVec2 tag_min(card_x + card_pad, card_screen_y + card_pad);
        const ImVec2 tag_max(tag_min.x + tag_w, tag_min.y + tag_h);
        cdl->AddRectFilled(tag_min, tag_max, tag_bg, tag_radius);
        cdl->AddText(font, fs_small,
                    ImVec2(tag_min.x + tag_h_pad, tag_min.y + (tag_h - fs_small) * 0.5f),
                    tag_txt, c.tag_text.c_str());

        // Step option icon (top-right of step cards).
        if (c.show_add_button) {
            const float opt_sz = 20.0f * sc;
            const ImVec2 opt_min(card_max.x - card_pad - opt_sz, tag_min.y);
            const ImVec2 opt_max(opt_min.x + opt_sz, opt_min.y + opt_sz);
            if (m_structure_step_option_icon)
                cdl->AddImage(m_structure_step_option_icon, opt_min, opt_max);
            ImGui::SetCursorScreenPos(opt_min);
            const std::string opt_id = std::string("##asp_step_option_") + std::to_string(ci);
            ImGui::InvisibleButton(opt_id.c_str(), ImVec2(opt_sz, opt_sz));
            if (ImGui::IsItemHovered()) {
                suppress_card_click = true;
                render_panel_tooltip(_u8L("Current step options: edit the current step name, delete the step, or insert a step before/after."));
            }
            render_structure_step_option_menu(static_cast<int>(ci), c, opt_min, sc, m_is_dark);
        }

        // Title row.
        const float title_y      = card_screen_y + card_pad + tag_h + 8.0f * sc;
        const std::string cnt_s  = " (" + std::to_string(c.count) + ")";
        const bool show_select_controls = c.selected && c.node_idx >= 0 &&
            (c.is_final_assembly || c.count > 0);
        const float title_action_sz = 20.0f * sc;
        const float title_action_y = title_y + (fs_title - title_action_sz) * 0.5f;
        const float title_right_anchor = c.show_add_button
            ? (card_max.x - card_pad - title_action_sz)
            : (card_max.x - card_pad);
        const std::string default_select_label = _CTX_utf8(L_CONTEXT("Default", "AssemblyStructure"), "AssemblyStructure");
        const auto select_full_label = [&]() -> std::string {
            if (c.is_final_assembly)
                return c.select_show_default
                    ? default_select_label
                    : c.select_label;
            return c.select_label.empty()
                ? default_select_label
                : c.select_label;
        };
        const auto select_display_label = [&](const std::string &lbl, float right_anchor) -> std::string {
            const float sel_text_w = text_w_fn(fs_chip, _u8L("Select"));
            const float sel_btn_w = sel_text_w + 2.f * chip_h_pad;
            const float available_value_w = std::max(0.f, right_anchor - chip_gap - sel_btn_w - chip_gap - (card_x + card_pad + 72.0f * sc));
            const float default_value_w = text_w_fn(fs_chip, default_select_label + "   ");
            const float max_value_w = std::min(available_value_w, default_value_w);
            return max_value_w > 0.f
                ? utf8_fit_with_ellipsis(lbl, max_value_w)
                : utf8_truncate_with_ellipsis(lbl, 10);
        };
        const auto calc_select_start_x = [&](float right_anchor) -> float {
            if (!show_select_controls)
                return right_anchor;
            const std::string lbl = select_full_label();
            const std::string display_lbl = select_display_label(lbl, right_anchor);
            const float sel_text_w = text_w_fn(fs_chip, _u8L("Select"));
            const float sel_btn_w = sel_text_w + 2.f * chip_h_pad;
            const float value_w = text_w_fn(fs_chip, display_lbl);
            const float controls_w = sel_btn_w + chip_gap + value_w;
            return right_anchor - chip_gap - controls_w;
        };
        const float title_control_start_x = calc_select_start_x(title_right_anchor);
        const float title_reserved_w = text_w_fn(fs_title, "M");///MMMMM
        const float title_left_x = card_x + card_pad;
        const float title_max_w = std::max(0.f, title_control_start_x - title_left_x - title_reserved_w);
        const bool title_editing = c.node_idx >= 0 &&
                                   c.node_idx == m_structure_step_rename_node;
        if (title_editing) {
            const float edit_right = (show_select_controls ? title_control_start_x : title_right_anchor) - title_reserved_w;
            const float edit_w = std::max(60.0f * sc, edit_right - title_left_x);
            ImGui::SetCursorScreenPos(ImVec2(card_x + card_pad, title_y - 2.0f * sc));
            ImGui::SetNextItemWidth(edit_w);
            if (m_structure_step_rename_open_pending) {
                ImGui::SetKeyboardFocusHere();
                m_structure_step_rename_open_pending = false;
                m_structure_step_rename_had_focus = false;
            }
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.86f, 0.86f, 0.86f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.82f, 0.82f, 0.82f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.78f, 0.78f, 0.78f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f * sc, 2.0f * sc));
            bool confirmed = ImGui::InputText("##asp_inline_step_name",
                m_structure_step_rename_buf, sizeof(m_structure_step_rename_buf),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            const ImVec2 edit_min = ImGui::GetItemRectMin();
            const ImVec2 edit_max = ImGui::GetItemRectMax();
            const bool focused = ImGui::IsItemFocused() || ImGui::IsItemActive();
            if (focused)
                m_structure_step_rename_had_focus = true;
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
            suppress_card_click = true;

            const bool lost_focus = m_structure_step_rename_had_focus && !focused;
            const bool clicked_outside = ImGui::IsMouseClicked(0) &&
                !ImGui::IsMouseHoveringRect(edit_min, edit_max, true);
            if (confirmed || lost_focus || clicked_outside) {
                if (m_structure_step_rename_node >= 0 && m_structure_step_rename_node < (int) _steps_nodes.size() &&
                    m_structure_step_rename_buf[0] != '\0') {
                    _steps_nodes[m_structure_step_rename_node].name = m_structure_step_rename_buf;
                    save_assembly_steps_json_to_model();
                    do_commond_callback("dirty");
                }
                m_structure_step_rename_node = -1;
                m_structure_step_rename_open_pending = false;
                m_structure_step_rename_had_focus = false;
            }
        } else {
            const float cnt_text_w = text_w_fn(fs_tiny, cnt_s);
            const std::string display_title = title_max_w > cnt_text_w
                ? utf8_fit_with_ellipsis(c.title, title_max_w - cnt_text_w)
                : utf8_fit_with_ellipsis(c.title, title_max_w);
            const float title_text_w = text_w_fn(fs_title, display_title);
            cdl->AddText(font, fs_title, ImVec2(card_x + card_pad, title_y),
                        col_text_dark, display_title.c_str());
            cdl->AddText(font, fs_tiny,
                        ImVec2(card_x + card_pad + title_text_w, title_y + (fs_title - fs_tiny)),
                        col_text_dark, cnt_s.c_str());

            if (c.node_idx >= 0) {
                const ImVec2 title_hit_min(card_x + card_pad, title_y - 2.0f * sc);
                const ImVec2 title_hit_size(std::min(card_w - 2.0f * card_pad, title_text_w + cnt_text_w + 8.0f * sc),
                                            fs_title + 6.0f * sc);
                ImGui::SetCursorScreenPos(title_hit_min);
                ImGui::InvisibleButton((std::string("##asp_title_rename_") + std::to_string(ci)).c_str(), title_hit_size);
                const bool title_hovered = ImGui::IsItemHovered();
                if (title_hovered) {
                    suppress_card_click = true;
                    if (display_title != c.title)
                        render_panel_tooltip(c.title);
                }
                const bool title_clicked = ImGui::IsItemClicked(0);
                const bool title_double_clicked = title_hovered && ImGui::IsMouseDoubleClicked(0);
                if (!c.selected && title_clicked) {
                    select_steps_tree_node_for_canvas(c.node_idx);
                    suppress_card_click = true;
                } else if (c.selected && (title_clicked || title_double_clicked)) {
                    begin_structure_step_rename(c.node_idx, c.title);
                    suppress_card_click = true;
                }
            }
        }

        // Add-object button on the second row. Clicking it opens the assembly
        // tree panel outside this card, anchored to the button's right side.
        {
            const float add_sz = title_action_sz;
            const float add_y = title_action_y;
            const float right_anchor = title_right_anchor;
            const ImVec2 add_min(card_max.x - card_pad - add_sz, add_y);
            const ImVec2 add_max(add_min.x + add_sz, add_min.y + add_sz);

            // "Select" button + value label, placed on the title row.
            if (show_select_controls) {
                const std::string lbl = select_full_label();
                const float sel_text_w = text_w_fn(fs_chip, _u8L("Select"));
                const float sel_btn_w = sel_text_w + 2.f * chip_h_pad;
                const std::string display_lbl = select_display_label(lbl, right_anchor);
                const float value_w = text_w_fn(fs_chip, display_lbl);
                const float controls_w = sel_btn_w + chip_gap + value_w;
                const ImVec2 sel_min(right_anchor - chip_gap - controls_w,
                                     add_y + (add_sz - chip_h) * 0.5f);
                AssemblySelectControlsStyle sel_style;
                sel_style.height          = chip_h;
                sel_style.pad_x           = chip_h_pad;
                sel_style.gap             = chip_gap;
                sel_style.radius          = chip_radius;
                sel_style.font_size       = fs_chip;
                sel_style.bg_col          = col_brand;
                sel_style.button_text_col = col_white;
                sel_style.label_text_col  = col_text_mid;
                if (render_structure_card_select_controls(static_cast<int>(ci), sel_min, sel_style,
                                                          display_lbl, lbl)) {
                    suppress_card_click = true;
                }
            }

            if (c.show_add_button) {
                // Render the "+ add object to step" affordance with one of
                const bool add_disabled = !c.selected;
                ImTextureID add_tex = c.selected ? m_structure_step_add_icon_edit : m_structure_step_add_icon_unedit;
                if (add_tex) {
                    const ImU32 add_tint = add_disabled ? IM_COL32(255, 255, 255, 128) : IM_COL32_WHITE;
                    cdl->AddImage(add_tex, add_min, add_max, ImVec2(0, 0), ImVec2(1, 1), add_tint);
                } else {
                    const ImU32 add_bg = add_disabled ? IM_COL32(0xD8, 0xEA, 0xD2, 128) : col_brand_addbg;
                    const ImU32 add_fg = add_disabled ? IM_COL32(0x00, 0xAE, 0x42, 128) : col_brand;
                    cdl->AddRectFilled(add_min, add_max, add_bg, card_radius);
                    const float cx = (add_min.x + add_max.x) * 0.5f;
                    const float cy = (add_min.y + add_max.y) * 0.5f;
                    const float arm = 5.f * sc;
                    cdl->AddLine(ImVec2(cx - arm, cy), ImVec2(cx + arm, cy), add_fg, 1.5f);
                    cdl->AddLine(ImVec2(cx, cy - arm), ImVec2(cx, cy + arm), add_fg, 1.5f);
                }
                ImGui::SetCursorScreenPos(add_min);
                const std::string add_id = std::string("##asp_add_") + std::to_string(ci);
                imgui.disabled_begin(add_disabled);
                ImGui::InvisibleButton(add_id.c_str(), ImVec2(add_sz, add_sz));
                if (ImGui::IsItemHovered()) {
                    suppress_card_click = true;
                    render_panel_tooltip(_u8L("Add object to current step"));
                }
                if (ImGui::IsItemClicked() && c.node_idx >= 0) {//popup add object tree
                    open_structure_add_tree(static_cast<int>(ci), c.node_idx, ImVec2(add_max.x + 8.0f * sc, add_min.y));
                }
                imgui.disabled_end();
            }
        }
        // Single-row chip strip (clipped to one line, 20 char ellipsis + hover).
        const float chip_top_y = title_y + line_h + 8.0f * sc;
        if (!c.chips.empty()) {
            float chip_x = card_x + card_pad;
            const float chip_y = chip_top_y;
            const float row_max_x = card_max.x - card_pad;

            if (!c.prefix_text.empty()) {
                const float prefix_w = text_w_fn(fs_chip, c.prefix_text);
                cdl->AddText(font, fs_chip,
                            ImVec2(chip_x, chip_y + (chip_h - fs_chip) * 0.5f),
                            col_text_light, c.prefix_text.c_str());
                chip_x += prefix_w + chip_gap;
            }

            auto draw_chip = [&](const std::string& label, const std::string& tooltip,
                                 size_t id_idx, float w) {
                cdl->AddRectFilled(ImVec2(chip_x, chip_y),
                                  ImVec2(chip_x + w, chip_y + chip_h),
                                  col_chip_bg, chip_radius);
                cdl->AddText(font, fs_chip,
                            ImVec2(chip_x + chip_h_pad,
                                   chip_y + (chip_h - fs_chip) * 0.5f),
                            col_text_mid, label.c_str());

                ImGui::SetCursorScreenPos(ImVec2(chip_x, chip_y));
                const std::string chip_id = std::string("##asp_chip_") + std::to_string(ci) + "_" + std::to_string(id_idx);
                ImGui::InvisibleButton(chip_id.c_str(), ImVec2(w, chip_h));
                if (!tooltip.empty() && ImGui::IsItemHovered()) {
                    suppress_card_click = true;
                    render_panel_tooltip(tooltip);
                }

                chip_x += w + chip_gap;
            };

            struct ChipDrawItem {
                std::string label;
                std::string tooltip;
                size_t id_idx{0};
                float width{0.0f};
            };
            std::vector<ChipDrawItem> chip_items;
            const float row_start_x = chip_x;
            const std::string ellipsis = "...";
            const float ellipsis_w = text_w_fn(fs_chip, ellipsis) + 2.f * chip_h_pad;
            auto join_hidden_labels = [&](size_t begin_idx) {
                std::string hidden_labels;
                for (size_t hi = begin_idx; hi < c.chips.size(); ++hi) {
                    if (!hidden_labels.empty())
                        hidden_labels += "\n";
                    hidden_labels += c.chips[hi].label;
                }
                return hidden_labels;
            };
            auto make_fitted_label = [&](const std::string& label, float available_w) {
                const float text_max_w = std::max(0.0f, available_w - 2.f * chip_h_pad);
                return utf8_fit_with_ellipsis(label, text_max_w);
            };
            auto used_width = [&]() {
                float width = 0.0f;
                for (const ChipDrawItem& item : chip_items)
                    width += item.width;
                if (!chip_items.empty())
                    width += chip_gap * static_cast<float>(chip_items.size() - 1);
                return width;
            };

            for (size_t i = 0; i < c.chips.size(); ++i) {
                const std::string &full_label    = c.chips[i].label;
                const std::string  display_label = utf8_truncate_with_ellipsis(full_label, kChipMaxChars);
                const bool         truncated     = (display_label.size() != full_label.size());
                const float        w             = text_w_fn(fs_chip, display_label) + 2.f * chip_h_pad;
                const float next_right = row_start_x + used_width() + (chip_items.empty() ? 0.0f : chip_gap) + w;
                if (next_right <= row_max_x) {
                    chip_items.push_back({display_label, truncated ? full_label : std::string(), i, w});
                    continue;
                }

                size_t hidden_begin_idx = i;
                if (!chip_items.empty()) {
                    while (!chip_items.empty()) {
                        ChipDrawItem& last_item = chip_items.back();
                        const float used_without_last = used_width() - last_item.width -
                            (chip_items.size() > 1 ? chip_gap : 0.0f);
                        const float gap_before_last = chip_items.size() > 1 ? chip_gap : 0.0f;
                        const float available_for_last = row_max_x - row_start_x - used_without_last -
                            gap_before_last - chip_gap - ellipsis_w;
                        if (available_for_last >= ellipsis_w) {
                            const std::string fitted = make_fitted_label(c.chips[last_item.id_idx].label,
                                                                         available_for_last);
                            const float fitted_w = text_w_fn(fs_chip, fitted) + 2.f * chip_h_pad;
                            if (fitted != c.chips[last_item.id_idx].label && fitted_w <= available_for_last) {
                                last_item.label = fitted;
                                last_item.tooltip = c.chips[last_item.id_idx].label;
                                last_item.width = fitted_w;
                            }
                            break;
                        }
                        hidden_begin_idx = std::min(hidden_begin_idx, last_item.id_idx);
                        chip_items.pop_back();
                    }
                }

                const std::string hidden_labels = join_hidden_labels(hidden_begin_idx);
                if (!hidden_labels.empty() &&
                    row_start_x + used_width() + (chip_items.empty() ? 0.0f : chip_gap) + ellipsis_w <= row_max_x)
                    chip_items.push_back({ellipsis, hidden_labels, c.chips.size(), ellipsis_w});
                break;
            }

            for (const ChipDrawItem& item : chip_items)
                draw_chip(item.label, item.tooltip, item.id_idx, item.width);
        } else if (!c.placeholder_text.empty()) {
            const float ph_h_local = chip_h + 12.0f * sc;
            const ImVec2 ph_min(card_x + card_pad, chip_top_y);
            const ImVec2 ph_max(card_max.x - card_pad, chip_top_y + ph_h_local);
            cdl->AddRectFilled(ph_min, ph_max, col_card_bg, card_radius);
            const float dash_len = 4.f * sc;
            const float gap_len  = 3.f * sc;
            auto draw_dashed = [&](ImVec2 a, ImVec2 b) {
                ImVec2 d(b.x - a.x, b.y - a.y);
                float  L = std::sqrt(d.x * d.x + d.y * d.y);
                if (L < 1e-3f) return;
                ImVec2 n(d.x / L, d.y / L);
                for (float t = 0.f; t < L; t += dash_len + gap_len) {
                    float t1 = std::min(L, t + dash_len);
                    cdl->AddLine(ImVec2(a.x + n.x * t, a.y + n.y * t),
                                ImVec2(a.x + n.x * t1, a.y + n.y * t1),
                                col_card_border, 1.f);
                }
            };
            draw_dashed(ph_min, ImVec2(ph_max.x, ph_min.y));
            draw_dashed(ImVec2(ph_max.x, ph_min.y), ph_max);
            draw_dashed(ph_max, ImVec2(ph_min.x, ph_max.y));
            draw_dashed(ImVec2(ph_min.x, ph_max.y), ph_min);
            const float ph_text_w = text_w_fn(fs_chip, c.placeholder_text);
            cdl->AddText(font, fs_chip,
                        ImVec2((ph_min.x + ph_max.x - ph_text_w) * 0.5f,
                               (ph_min.y + ph_max.y - fs_chip) * 0.5f),
                        col_text_light, c.placeholder_text.c_str());

            if (c.selected && c.node_idx >= 0) {
                ImGui::SetCursorScreenPos(ph_min);
                const std::string ph_id = std::string("##asp_placeholder_add_") + std::to_string(ci);
                ImGui::InvisibleButton(ph_id.c_str(), ImVec2(ph_max.x - ph_min.x, ph_max.y - ph_min.y));
                /*if (ImGui::IsItemHovered()) {
                    suppress_card_click = true;
                    render_panel_tooltip(_u8L("Add object to current step"));
                }*/
                if (ImGui::IsItemClicked()) {
                    open_structure_add_tree(static_cast<int>(ci), c.node_idx, ImVec2(ph_max.x + 8.0f * sc, ph_min.y));
                    suppress_card_click = true;
                }
            }
        }

        const bool block_card_click = m_structure_select_popup_active_card >= 0 ||
                                      m_structure_select_popup_pending_card >= 0 ||
                                      m_structure_add_tree_card >= 0;
        if (!suppress_card_click && !block_card_click &&
            ImGui::IsMouseHoveringRect(card_min, card_max, true) && ImGui::IsMouseClicked(0)) {
            int click_node = c.node_idx;
            if (click_node < 0 && c.tag_style == AssemblyStructureCard::TagStyle::Default) {
                click_node = ensure_final_assembly_folder();
            }
            if (click_node >= 0) {
                select_steps_tree_node_for_canvas(click_node);
            }
        }

        cur_y += card_h + card_gap;
    }
    ImGui::SetCursorPosY(scroll_content_h);
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
    ImGui::EndChild();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(1);

    if (m_structure_add_tree_card >= 0 &&
        m_structure_add_tree_card < static_cast<int>(data.cards.size())) {
        const float tree_w = 300.0f * sc;
        const float tree_h = std::min(420.0f * sc, std::max(180.0f * sc, canvas_h - m_structure_add_tree_pos.y - 12.0f * sc));
        const float tree_x = panel_x + panel_w + 10.0f * sc;
        render_assembly_tree_ui(tree_x, m_structure_add_tree_pos.y, tree_w, tree_h, sc);
    }

    // Popup tree selector for the selected card (rendered outside the child window).
    if (m_model) {
        const int popup_card = m_structure_select_popup_pending_card >= 0
            ? m_structure_select_popup_pending_card
            : m_structure_select_popup_active_card;
        if (popup_card >= 0 && popup_card < static_cast<int>(data.cards.size())) {
            const int popup_step_node = data.cards[popup_card].node_idx;
            if (m_structure_select_popup_tree_card != popup_card ||
                m_structure_select_popup_tree_step_node != popup_step_node) {
                m_structure_select_popup_tree = build_structure_card_select_tree_data(popup_step_node);
                m_structure_select_popup_tree_card = popup_card;
                m_structure_select_popup_tree_step_node = popup_step_node;
                m_structure_select_popup_checked_card = -1;
            }
            render_structure_card_select_popup(popup_card, &m_structure_select_popup_tree);
        }
    }

    // ---- Action buttons --------------------------------------------------
    {
        const float btn_w   = 86.0f * sc;
        const float btn_gap = 12.0f * sc;
        const float btn_pad_x = 14.0f * sc;
        const float copy_w = std::max(btn_w, ImGui::CalcTextSize(_u8L("Copy Step").c_str()).x + 2.0f * btn_pad_x);
        const float add_w  = std::max(btn_w, ImGui::CalcTextSize(_u8L("Add Step").c_str()).x + 2.0f * btn_pad_x);
        const float total_w = copy_w + btn_gap + add_w;
        const float bx0     = win_min.x + (panel_w - total_w) * 0.5f;
        // Anchor the button row to the bottom of the scrollable card region
        const float by      = scroll_region_y + scroll_region_h + card_gap;
        const ImVec2 copy_btn_size(copy_w, action_h);
        const ImVec2 add_btn_size(add_w, action_h);
        const int selected_folder = find_parent_folder(selected_node);
        const bool selected_final_assembly = selected_folder >= 0 && selected_folder < (int) _steps_nodes.size() && _steps_nodes[selected_folder].is_final_assembly;
        const bool copy_disabled = selected_folder < 0 || selected_final_assembly;
        const bool add_disabled  = selected_final_assembly;

        imgui.disabled_begin(copy_disabled);
        if (render_footer_button("##asp_btn_copy", _u8L("Copy Step"), ImVec2(bx0, by), copy_btn_size, false, sc))
            copy_assembly_step();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            render_panel_tooltip(_u8L("Only copy steps. This is independent of the selected objects on the canvas."));
        imgui.disabled_end();

        imgui.disabled_begin(add_disabled);
        if (render_footer_button("##asp_btn_add", _u8L("Add Step"), ImVec2(bx0 + copy_w + btn_gap, by), add_btn_size, true, sc)) {
            add_assembly_step();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            render_panel_tooltip(_u8L("Only add empty steps. This has nothing to do with the selected objects on the canvas."));
        imgui.disabled_end();

        // Footer hint (no-step-selected variant). Drawn directly via the
        // window draw list so we can wrap-render at the panel font size
        // without disturbing the centered button row above. Position is
        // immediately under the Copy/Add button row, with the wrap height
        // already accounted for in full_h above.
        if (!footer_hint_str.empty()) {
            const float hint_y = by + action_h + 8.0f * sc;
            dl->AddText(ImGui::GetFont(), fs,
                ImVec2(win_min.x + side_pad, hint_y),
                col_text_mid, footer_hint_str.c_str(), nullptr, footer_hint_wrap);
        }
    }

    imgui.end();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(1);

    m_assembly_structure_right_x = panel_x + panel_w;
    m_panel_rect_structure_min = ImVec2(panel_x, panel_y);
    m_panel_rect_structure_max = ImVec2(panel_x + panel_w, panel_y + panel_h);

    if (!m_save_project_tip_text.empty()) {
        if (std::chrono::steady_clock::now() >= m_save_project_tip_until) {
            m_save_project_tip_text.clear();
        } else {
            const ImVec2 padding(10.0f * sc, 7.0f * sc);
            const ImVec2 text_size = ImGui::CalcTextSize(m_save_project_tip_text.c_str());
            const ImVec2 tip_pos(panel_x + panel_w + 10.0f * sc, panel_y + 8.0f * sc);
            const ImVec2 tip_size(text_size.x + padding.x * 2.0f, text_size.y + padding.y * 2.0f);
            ImGui::SetNextWindowPos(tip_pos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(tip_size, ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f * sc);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f * sc);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 0.98f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.82f, 0.82f, 0.82f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(38 / 255.0f, 46 / 255.0f, 48 / 255.0f, 1.0f));
            ImGui::Begin("##assembly_save_project_tip", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs);
            ImGui::TextUnformatted(m_save_project_tip_text.c_str());
            ImGui::End();
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(3);
        }
    }
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

    const int folder_idx = find_parent_folder(selected_node);
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
        model_node.label      = "Model";
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

void AssemblyStepsUtils::render_structure_card_select_popup(int card_idx,
                                                            const AssemblyTreeData *popup_tree_ptr)
{
    if (!m_imgui || !popup_tree_ptr || popup_tree_ptr->empty())
        return;
    const float sc = m_imgui_scale;

    const std::string popup_id = "##asp_select_popup_" + std::to_string(card_idx);

    if (m_structure_select_popup_pending_card == card_idx) {
        ImGui::OpenPopup(popup_id.c_str());
        m_structure_select_popup_active_card  = card_idx;
        m_structure_select_popup_pending_card = -1;

        // Force-seed every node's open state on each popup open. The cache
        if (m_model && m_structure_select_popup_tree_step_node >= 0) {
            const auto &step_nodes = m_model->get_assembly_steps_tree_data().nodes;
            const bool  is_final   = m_structure_select_popup_tree_step_node < static_cast<int>(step_nodes.size()) &&
                step_nodes[m_structure_select_popup_tree_step_node].is_final_assembly;
            if (is_final) {
                for (const auto &node : popup_tree_ptr->nodes)
                    s_assembly_tree_open_nodes[node.uid] = (node.parent_id == -1);
            } else {
                for (const auto &node : popup_tree_ptr->nodes)
                    s_assembly_tree_open_nodes[node.uid] = true;
            }
        }

        // Every time the user opens the Select popup AND a step tree node is
        if (selected_node >= 0)
            apply_canvas_selection_to_popup_checked(card_idx, *popup_tree_ptr);
    }

    if (m_structure_select_popup_active_card != card_idx)
        return;

    if (m_structure_select_popup_checked_card != card_idx) {
        m_structure_select_popup_checked.clear();
        for (const auto& node : popup_tree_ptr->nodes) {
            if (node.selectable)
                m_structure_select_popup_checked[node.uid] = true;
        }
        m_structure_select_popup_checked_card = card_idx;
        update_structure_select_label(card_idx, *popup_tree_ptr);
        sync_structure_select_popup_to_canvas(*popup_tree_ptr);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * sc);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * sc, 10.0f * sc));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f * sc);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(1.0f, 1.0f, 1.0f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(38 / 255.0f, 46 / 255.0f, 48 / 255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(144 / 255.f, 144 / 255.f, 144 / 255.f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(144 / 255.f, 144 / 255.f, 144 / 255.f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(120 / 255.f, 120 / 255.f, 120 / 255.f, 1.0f));

    const bool is_final_select_popup = m_model &&
        m_structure_select_popup_tree_step_node >= 0 &&
        m_structure_select_popup_tree_step_node < static_cast<int>(m_model->get_assembly_steps_tree_data().nodes.size()) &&
        m_model->get_assembly_steps_tree_data().nodes[m_structure_select_popup_tree_step_node].is_final_assembly;

    const float header_h_calc   = 28.0f * sc;
    const float row_h_calc      = 36.0f * sc;
    const float window_pad_v    = 10.0f * sc * 2.0f;
    int visible_row_est;
    if (is_final_select_popup) {
        visible_row_est = static_cast<int>(popup_tree_ptr->roots.size());
        for (int root_idx : popup_tree_ptr->roots)
            visible_row_est += static_cast<int>(popup_tree_ptr->nodes[root_idx].children.size());
    } else {
        visible_row_est = static_cast<int>(popup_tree_ptr->nodes.size());
    }
    const int   row_count_est   = std::max(visible_row_est, 1);
    const float popup_h_dynamic = window_pad_v + header_h_calc + row_count_est * row_h_calc + 8.0f * sc;
    const float popup_h_max     = 360.0f * sc;
    const float popup_w         = 260.0f * sc;
    const float popup_h         = std::min(popup_h_dynamic, popup_h_max);
    ImGui::SetNextWindowSize(ImVec2(popup_w, popup_h), ImGuiCond_Always);

    if (ImGui::BeginPopup(popup_id.c_str())) {
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows)) {
            ImGuiIO &io = ImGui::GetIO();
            io.WantCaptureMouse = true;
        }

        // Title row: "Select" on the left, close (cross) icon on the right.
        {
            const ImVec2 header_min = ImGui::GetCursorScreenPos();
            const float  header_w   = ImGui::GetContentRegionAvail().x;
            const float  header_h   = header_h_calc;
            const float  icon_sz    = 18.0f * sc;

            const std::string title      = _u8L("Select");
            const ImVec2      title_size = ImGui::CalcTextSize(title.c_str());
            ImDrawList       *dl         = ImGui::GetWindowDrawList();
            dl->AddText(ImVec2(header_min.x, header_min.y + (header_h - title_size.y) * 0.5f),
                IM_COL32(38, 46, 48, 255), title.c_str());

            if (m_tree_icon_cross) {
                const ImVec2 icon_min(header_min.x + header_w - icon_sz,
                                      header_min.y + (header_h - icon_sz) * 0.5f);
                dl->AddImage(m_tree_icon_cross, icon_min,
                    ImVec2(icon_min.x + icon_sz, icon_min.y + icon_sz));
                ImGui::SetCursorScreenPos(icon_min);
                ImGui::InvisibleButton("##asp_select_popup_close", ImVec2(icon_sz, icon_sz));
                if (ImGui::IsItemClicked(0))
                    ImGui::CloseCurrentPopup();
            }
            ImGui::SetCursorScreenPos(ImVec2(header_min.x, header_min.y + header_h));
        }

        AssemblyTreeRenderOptions options;
        options.allow_object_check = true;
        options.allow_volume_check = true;
        options.show_footer = false;
        options.readonly = false;
        options.child_id = "##structure_select_tree_nodes";
        AssemblyTreeRenderResult result = render_assembly_tree_selector(*popup_tree_ptr, m_structure_select_popup_checked, options, sc);
        if (result.changed) {
            update_structure_select_label(card_idx, *popup_tree_ptr);
            sync_structure_select_popup_to_canvas(*popup_tree_ptr);
            do_commond_callback("update_gizmos_on_off_state");
        }

        ImGui::EndPopup();
    }
    else {
        m_structure_select_popup_active_card = -1;
    }

    ImGui::PopStyleColor(11);
    ImGui::PopStyleVar(3);
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
            m_structure_select_show_default.insert(step_node_idx);
            m_structure_select_labels.erase(step_node_idx);
            return;
        }

        m_structure_select_show_default.erase(step_node_idx);
        if (selected_objects.empty()) {
            m_structure_select_labels[step_node_idx] = std::string();
            return;
        }

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

    m_structure_select_show_default.erase(step_node_idx);
    if (label.empty())
        m_structure_select_labels.erase(step_node_idx);
    else
        m_structure_select_labels[step_node_idx] = label;
}

void AssemblyStepsUtils::sync_structure_select_popup_to_canvas(const AssemblyTreeData& popup_tree)
{
    if (!m_selection || !m_model)
        return;
    set_selection_origin(SelectionOrigin::TreeNode);
    clear_selection();

    std::set<int> objects_with_checked_volumes;
    for (const auto& node : popup_tree.nodes) {
        if (node.volume_idx < 0)
            continue;
        auto it = m_structure_select_popup_checked.find(node.uid);
        if (it == m_structure_select_popup_checked.end() || !it->second)
            continue;
        if (node.object_idx >= 0 && node.object_idx < static_cast<int>(m_model->objects.size())) {
            m_selection->add_volume(node.object_idx, node.volume_idx, 0, false);
            objects_with_checked_volumes.insert(node.object_idx);
        }
    }

    for (const auto& node : popup_tree.nodes) {
        if (node.volume_idx >= 0)
            continue;
        auto it = m_structure_select_popup_checked.find(node.uid);
        if (it == m_structure_select_popup_checked.end() || !it->second)
            continue;
        if (objects_with_checked_volumes.find(node.object_idx) != objects_with_checked_volumes.end())
            continue;
        if (node.object_idx >= 0 && node.object_idx < static_cast<int>(m_model->objects.size()))
            m_selection->add_object(static_cast<unsigned int>(node.object_idx), false);
    }
    do_commond_callback("dirty");
}
void AssemblyStepsUtils::record_keyframe_logic(KeyFrameEntry &entry)
{
    if (!m_only_final_assembly_endframe_effect_real_assembly) {
        return;
    }
    KeyFrame &kf = entry.data;
    record_camera(kf);
    entry.need_save = true;
    record_selected_volumes_by_mo_mv(kf);

    save_assembly_steps_json_to_model();
}

void AssemblyStepsUtils::apply_keyframe_to_canvas(const KeyFrame &kf)
{
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
    m_keyframe_selected = insert_pos;
    refresh_guide_show_part_numbers_from_current();
}

void AssemblyStepsUtils::play_all_keyframes_for_current_node()
{
    m_keyframe_playing = true;
    build_local_play_queue();
    do_commond_callback("exit_gizmo");
    do_commond_callback("request_extra_frame");
}

bool AssemblyStepsUtils::should_show_panels()
{
    // Hide the assembly chrome (Structure / Guide panels, play bar, part-label
    const bool exporting = m_is_export_mode || m_steps_export_active ||
                           m_steps_video_export_active || m_video_intro_active || m_keyframe_playing;
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
    int folder = find_parent_folder(selected_node);
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
        record_selected_gl_volume_transforms_to_current_keyframe();
    } else {
        record_keyframe_logic(entries[idx]);
    }
    refresh_guide_show_part_numbers_from_current();
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
}

void AssemblyStepsUtils::play_cur_keyframe_logic()
{
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

bool AssemblyStepsUtils::is_empty_structure_step(int folder_idx) const
{
    if (!m_model)
        return true;
    const auto &step_nodes = m_model->get_assembly_steps_tree_data().nodes;
    if (folder_idx < 0 || folder_idx >= static_cast<int>(step_nodes.size()))
        return true;
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

void AssemblyStepsUtils::apply_keyframe_display_mode()
{
    if (!m_model)
        return;

    auto &step_nodes = m_model->get_assembly_steps_tree_data().nodes;
    auto &step_roots = m_model->get_assembly_steps_tree_data().roots;
    if (m_keyframe_display_mode == KeyframeDisplayMode::All || is_empty_structure_step(selected_node)) {
        show_all_volume_normal_render();
    } else if (m_keyframe_display_mode == KeyframeDisplayMode::OnlyCurrentStep) {
        if (has_selected_node()) {
            std::set<int> current_objs;
            int           target = selected_node;
            if (target >= 0 && target < (int) step_nodes.size())
                current_objs = collect_node_object_indices(target);

            for (auto &node : step_nodes) {
                if (node.type == AssemblyStepsTreeNode::Type::Object && node.object_idx >= 0)
                    node.visible = current_objs.count(node.object_idx) > 0;
            }

            for (const auto &node : step_nodes) {
                if (node.type != AssemblyStepsTreeNode::Type::Object || node.object_idx < 0) continue;
                bool is_current = current_objs.count(node.object_idx) > 0;
                apply_object_state(node.object_idx, {is_current, is_current ? 1.f : 0.f, !is_current});
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
            std::set<int> current_objs;
            int           target = selected_node;
            if (target >= 0 && target < (int) step_nodes.size())
                current_objs = collect_node_object_indices(target);

            for (const auto &node : step_nodes) {
                if (node.type != AssemblyStepsTreeNode::Type::Object || node.object_idx < 0) continue;
                bool is_current = current_objs.count(node.object_idx) > 0;
                apply_object_state(node.object_idx, {true, is_current ? 1.f : 0.15f,  !is_current});
            }
        }else{
            show_all_volume_normal_render();
        }
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
#ifdef _WIN32
        __debugbreak();
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
        Eigen::Quaterniond rq_a(mat_a.matrix().block<3, 3>(0, 0));
        Eigen::Quaterniond rq_b(mat_b.matrix().block<3, 3>(0, 0));
        rq_a.normalize();
        rq_b.normalize();
        Eigen::Quaterniond rq_interp = rq_a.slerp(t, rq_b);

        Vec3d pos_interp = (1.0 - t) * mat_a.translation() + t * mat_b.translation();

        Transform3d interp_mat                  = Transform3d::Identity();
        interp_mat.matrix().block<3, 3>(0, 0)   = rq_interp.toRotationMatrix();
        interp_mat.translation()                = pos_interp;
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
                    return candidate.object_idx == label.object_idx &&
                           candidate.volume_idx == label.volume_idx;
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

    // Only folder nodes represented by Assembly Structure cards are playable.
    for (int i = 0; i < (int)step_nodes.size(); ++i) {
        if (i == final_assembly_folder)
            continue;
        auto &entries = step_nodes[i].kf_data.entries;
        if (entries.empty())
            continue;
        if (step_nodes[i].type != AssemblyStepsTreeNode::Type::Folder) {
            log_unrelated_keyframe_node(i, "not_folder");
            continue;
        }
        if (step_card_nodes.find(i) == step_card_nodes.end()) {
            log_unrelated_keyframe_node(i, "no_step_card");
            continue;
        }
        append_node_entry(i);
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

void AssemblyStepsUtils::init_tree_icons()
{
    if (m_tree_icons_loaded)
        return;

    const unsigned icon_sz = 64;
    IMTexture::load_from_svg_file(m_images_dir + "tree_play.svg",      icon_sz, icon_sz, m_tree_icon_play);
    IMTexture::load_from_svg_file(m_images_dir + "tree_play_dark.svg", icon_sz, icon_sz, m_tree_icon_play_dark);
    IMTexture::load_from_svg_file(m_images_dir + "tree_pause.svg", icon_sz, icon_sz, m_tree_icon_pause);
    IMTexture::load_from_svg_file(m_images_dir + "tree_apply_camera.svg", icon_sz, icon_sz, m_tree_icon_apply_camera);
    IMTexture::load_from_svg_file(m_images_dir + "tree_explosion.svg", icon_sz, icon_sz, m_tree_icon_auto_explode);
    IMTexture::load_from_svg_file(m_images_dir + "tree_object.svg", icon_sz, icon_sz, m_tree_icon_object);
    IMTexture::load_from_svg_file(m_images_dir + "tree_part.svg", icon_sz, icon_sz, m_tree_icon_part);

    IMTexture::load_from_svg_file(m_images_dir + "tree_screw.svg",      icon_sz, icon_sz, m_tree_icon_screw);
    IMTexture::load_from_svg_file(m_images_dir + "tree_screw_dark.svg", icon_sz, icon_sz, m_tree_icon_screw_dark);
    IMTexture::load_from_svg_file(m_images_dir + "tree_glue.svg",       icon_sz, icon_sz, m_tree_icon_glue);
    IMTexture::load_from_svg_file(m_images_dir + "tree_glue_dark.svg",  icon_sz, icon_sz, m_tree_icon_glue_dark);
    IMTexture::load_from_svg_file(m_images_dir + "tree_clip.svg",       icon_sz, icon_sz, m_tree_icon_clip);
    IMTexture::load_from_svg_file(m_images_dir + "tree_clip_dark.svg",  icon_sz, icon_sz, m_tree_icon_clip_dark);
    IMTexture::load_from_svg_file(m_images_dir + "tree_rect.svg",        icon_sz, icon_sz, m_note_icon_rect);
    IMTexture::load_from_svg_file(m_images_dir + "tree_rect_dark.svg",   icon_sz, icon_sz, m_note_icon_rect_dark);
    IMTexture::load_from_svg_file(m_images_dir + "tree_circle.svg",      icon_sz, icon_sz, m_note_icon_circle);
    IMTexture::load_from_svg_file(m_images_dir + "tree_circle_dark.svg", icon_sz, icon_sz, m_note_icon_circle_dark);
    IMTexture::load_from_svg_file(m_images_dir + "tree_line.svg",   icon_sz, icon_sz, m_note_icon_line);
    IMTexture::load_from_svg_file(m_images_dir + "tree_vector.svg",      icon_sz, icon_sz, m_note_icon_vector);
    IMTexture::load_from_svg_file(m_images_dir + "tree_vector_dark.svg", icon_sz, icon_sz, m_note_icon_vector_dark);
    IMTexture::load_from_svg_file(m_images_dir + "tree_note.svg",        icon_sz, icon_sz, m_note_icon_tag);
    IMTexture::load_from_svg_file(m_images_dir + "tree_note_dark.svg",   icon_sz, icon_sz, m_note_icon_tag_dark);
    IMTexture::load_from_svg_file(m_images_dir + "tree_pencil.svg", icon_sz, icon_sz, m_note_icon_pencil);
    IMTexture::load_from_svg_file(m_images_dir + "tree_frame.svg",       icon_sz, icon_sz, m_tree_icon_frame);
    IMTexture::load_from_svg_file(m_images_dir + "cross.svg",       icon_sz, icon_sz, m_tree_icon_cross);
    IMTexture::load_from_svg_file(m_images_dir + "panel_collapse.svg", icon_sz, icon_sz, m_panel_collapse_icon);
    IMTexture::load_from_svg_file(m_images_dir + "panel_expand.svg", icon_sz, icon_sz, m_panel_expand_icon);
    IMTexture::load_from_svg_file(m_images_dir + "view_help.svg", icon_sz, icon_sz, m_structure_help_icon);
    IMTexture::load_from_svg_file(m_images_dir + "tree_option.svg", icon_sz, icon_sz, m_structure_option_icon);
    IMTexture::load_from_svg_file(m_images_dir + "tree_step_option.svg", icon_sz, icon_sz, m_structure_step_option_icon);
    IMTexture::load_from_svg_file(m_images_dir + "tree_unedit.svg",   icon_sz, icon_sz, m_structure_step_add_icon_unedit);
    IMTexture::load_from_svg_file(m_images_dir + "tree_cur_edit.svg", icon_sz, icon_sz, m_structure_step_add_icon_edit);
    IMTexture::load_from_svg_file(m_images_dir + "tree_from_assembly_end_frame.svg", icon_sz, icon_sz, m_tree_icon_from_assembly_end_frame);
    IMTexture::load_from_svg_file(m_images_dir + "tree_export.svg", icon_sz, icon_sz, m_btn_icon_export);
    IMTexture::load_from_svg_file(m_images_dir + "play_left.svg",   icon_sz, icon_sz, m_play_left_icon);
    IMTexture::load_from_svg_file(m_images_dir + "play_right.svg",  icon_sz, icon_sz, m_play_right_icon);
    load_assembly_tree_icons(m_imgui_scale > 0.0f ? m_imgui_scale : 1.0f);
    m_tree_icons_loaded = true;
}

ImTextureID AssemblyStepsUtils::get_arrow_svg_icon(const std::string &svg_name)
{
    if (svg_name == "screw") return m_is_dark ? m_tree_icon_screw_dark : m_tree_icon_screw;
    if (svg_name == "glue") return m_is_dark ? m_tree_icon_glue_dark : m_tree_icon_glue;
    if (svg_name == "clip") return m_is_dark ? m_tree_icon_clip_dark : m_tree_icon_clip;
    auto it = m_arrow_svg_icons.find(svg_name);
    if (it != m_arrow_svg_icons.end())
        return it->second;

    ImTextureID tex = nullptr;
    IMTexture::load_from_svg_file(m_images_dir + svg_name + ".svg", 64, 64, tex);
    m_arrow_svg_icons[svg_name] = tex;
    return tex;
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
    return s_assembly_tree_icons.loaded;
}

AssemblyTreeRenderResult AssemblyStepsUtils::render_assembly_tree_selector(
    const AssemblyTreeData& tree,
    std::unordered_map<std::string, bool>& checked,
    const AssemblyTreeRenderOptions& options,
    float sc)
{
    AssemblyTreeRenderResult result;
    if (tree.nodes.empty())
        return result;

    load_assembly_tree_icons(sc);

    const ImU32 text_col      = IM_COL32(38, 46, 48, 255);
    const ImU32 sub_text_col  = IM_COL32(144, 144, 144, 255);
    const ImU32 green_col     = IM_COL32(0, 174, 66, 255);
    const ImU32 line_col      = IM_COL32(209, 213, 216, 255);
    const ImU32 border_col    = IM_COL32(190, 190, 190, 255);
    const ImU32 separator_col = IM_COL32(229, 229, 229, 255);

    auto to_lower_ascii = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return value;
    };
    const std::string search_text_lc = to_lower_ascii(m_assembly_tree_search_text);

    std::function<bool(int)> node_matches_search;
    node_matches_search = [&tree, &search_text_lc, &to_lower_ascii, &node_matches_search](int node_id) {
        if (search_text_lc.empty())
            return true;
        if (node_id < 0 || node_id >= static_cast<int>(tree.nodes.size()))
            return false;
        const auto &node = tree.nodes[node_id];
        if (to_lower_ascii(node.label).find(search_text_lc) != std::string::npos)
            return true;
        for (int child_id : node.children) {
            if (node_matches_search(child_id))
                return true;
        }
        return false;
    };

    auto node_checkable = [&tree, &options](int node_id) {
        if (node_id < 0 || node_id >= static_cast<int>(tree.nodes.size()))
            return false;
        const auto& node = tree.nodes[node_id];
        if (!node.selectable)
            return false;
        if (node.volume_idx >= 0)
            return options.allow_volume_check;
        if (node.object_idx >= 0)
            return options.allow_object_check;
        return options.allow_object_check;
    };

    std::function<void(int, bool)> set_subtree_checked;
    set_subtree_checked = [&tree, &checked, &node_checkable, &set_subtree_checked](int node_id, bool checked_value) {
        if (node_id < 0 || node_id >= static_cast<int>(tree.nodes.size()))
            return;
        const auto& node = tree.nodes[node_id];
        if (node_checkable(node_id))
            checked[node.uid] = checked_value;
        for (int child_id : node.children)
            set_subtree_checked(child_id, checked_value);
    };

    std::function<bool(int)> has_selectable_descendant;
    has_selectable_descendant = [&tree, &node_checkable, &has_selectable_descendant](int node_id) {
        if (node_id < 0 || node_id >= static_cast<int>(tree.nodes.size()))
            return false;
        const auto &node = tree.nodes[node_id];
        for (int child_id : node.children) {
            if (node_checkable(child_id) || has_selectable_descendant(child_id))
                return true;
        }
        return false;
    };

    enum class AssemblyTreeCheckState { None, Partial, All };
    std::function<AssemblyTreeCheckState(int)> get_subtree_state;
    get_subtree_state = [&tree, &checked, &node_checkable, &has_selectable_descendant, &get_subtree_state](int node_id) {
        if (node_id < 0 || node_id >= static_cast<int>(tree.nodes.size()))
            return AssemblyTreeCheckState::None;
        const auto& node = tree.nodes[node_id];
        const bool checkable = node_checkable(node_id);
        bool checked_value = !checkable;
        if (checkable) {
            auto it = checked.find(node.uid);
            checked_value = it != checked.end() && it->second;
        }
        if (node.children.empty())
            return checked_value ? AssemblyTreeCheckState::All : AssemblyTreeCheckState::None;

        const bool has_descendant = has_selectable_descendant(node_id);
        bool has_state = checkable && !has_descendant;
        bool has_checked = has_state && checked_value;
        bool has_unchecked = has_state && !checked_value;
        for (int child_id : node.children) {
            if (!node_checkable(child_id) && !has_selectable_descendant(child_id))
                continue;
            has_state = true;
            switch (get_subtree_state(child_id)) {
            case AssemblyTreeCheckState::All:
                has_checked = true;
                break;
            case AssemblyTreeCheckState::None:
                has_unchecked = true;
                break;
            case AssemblyTreeCheckState::Partial:
                has_checked = true;
                has_unchecked = true;
                break;
            }
        }
        if (!has_state)
            return AssemblyTreeCheckState::All;
        if (has_checked && has_unchecked)
            return AssemblyTreeCheckState::Partial;
        return has_checked ? AssemblyTreeCheckState::All : AssemblyTreeCheckState::None;
    };

    std::function<void(int)> count_leaves;
    count_leaves = [&tree, &checked, &node_checkable, &has_selectable_descendant, &node_matches_search, &count_leaves, &result](int node_id) {
        if (node_id < 0 || node_id >= static_cast<int>(tree.nodes.size()))
            return;
        if (!node_matches_search(node_id))
            return;
        const auto& node = tree.nodes[node_id];
        if (node_checkable(node_id) && !has_selectable_descendant(node_id)) {
            ++result.leaf_count;
            auto it = checked.find(node.uid);
            if (it != checked.end() && it->second)
                ++result.checked_leaf_count;
            return;
        }
        for (int child_id : node.children)
            count_leaves(child_id);
    };
    for (int root_id : tree.roots)
        count_leaves(root_id);

    const float footer_h = options.show_footer ? 58.0f * sc : 0.0f;
    const float row_h = 36.0f * sc;
    const float indent_step = 26.0f * sc;
    const float checkbox_size = 20.0f * sc;
    const float arrow_size = 14.0f * sc;

    auto draw_checkbox = [sc, checkbox_size, green_col, border_col](ImDrawList* target_draw_list, const ImRect& rect, AssemblyTreeCheckState state) {
        const bool checked_state = state == AssemblyTreeCheckState::All;
        const bool partial = state == AssemblyTreeCheckState::Partial;
        if (checked_state) {
            target_draw_list->AddRectFilled(rect.Min, rect.Max, green_col, 3.0f * sc);
            if (s_assembly_tree_icons.select) {
                const float icon_w = 10.0f * sc;
                const float icon_h = 8.2f * sc;
                const ImVec2 icon_min(rect.Min.x + (checkbox_size - icon_w) * 0.5f, rect.Min.y + (checkbox_size - icon_h) * 0.5f);
                target_draw_list->AddImage(s_assembly_tree_icons.select, icon_min, ImVec2(icon_min.x + icon_w, icon_min.y + icon_h));
            }
        } else if (partial) {
            target_draw_list->AddRectFilled(rect.Min, rect.Max, IM_COL32(255, 255, 255, 255), 3.0f * sc);
            target_draw_list->AddRect(rect.Min, rect.Max, border_col, 3.0f * sc, 0, 2.0f * sc);
            target_draw_list->AddRectFilled(ImVec2(rect.Min.x + 4.0f * sc, rect.Min.y + 4.0f * sc),
                                            ImVec2(rect.Max.x - 4.0f * sc, rect.Max.y - 4.0f * sc), green_col, 1.0f * sc);
        } else {
            target_draw_list->AddRectFilled(rect.Min, rect.Max, IM_COL32(255, 255, 255, 255), 3.0f * sc);
            target_draw_list->AddRect(rect.Min, rect.Max, border_col, 3.0f * sc, 0, 2.0f * sc);
        }
    };

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImGui::BeginChild(options.child_id, ImVec2(0, options.show_footer ? -footer_h : 0), false, ImGuiWindowFlags_NoBackground);
    std::function<void(int, int, bool)> render_node;
    render_node = [this, &tree, &checked, &node_checkable, &set_subtree_checked, &get_subtree_state, &render_node,
                   &node_matches_search, search_text_lc, row_h, indent_step, checkbox_size, arrow_size, line_col, text_col, draw_checkbox, sc, options, &result]
                  (int node_id, int depth, bool is_last) {
        if (node_id < 0 || node_id >= static_cast<int>(tree.nodes.size()))
            return;
        if (!node_matches_search(node_id))
            return;
        const auto& node = tree.nodes[node_id];
        ImGui::PushID(node.uid.c_str());

        auto open_it = s_assembly_tree_open_nodes.find(node.uid);
        if (open_it == s_assembly_tree_open_nodes.end())
            open_it = s_assembly_tree_open_nodes.emplace(node.uid, true).first;
        bool& open = open_it->second;

        const bool checkable = node_checkable(node_id);
        const AssemblyTreeCheckState state = get_subtree_state(node_id);
        ImDrawList* child_draw_list = ImGui::GetWindowDrawList();
        const ImVec2 row_min = ImGui::GetCursorScreenPos();
        const float avail_w = ImGui::GetContentRegionAvail().x;
        ImGui::InvisibleButton("##assembly_tree_row", ImVec2(avail_w, row_h));
        const ImVec2 row_max = ImGui::GetItemRectMax();
        const bool hovered = ImGui::IsItemHovered();

        if (hovered)
            child_draw_list->AddRectFilled(row_min, row_max, IM_COL32(245, 247, 248, 255), 4.0f * sc);

        const float center_y = row_min.y + row_h * 0.5f;
        const float content_x = row_min.x + depth * indent_step;
        if (depth > 0) {
            const float line_x = content_x - indent_step * 0.50f;
            child_draw_list->AddLine(ImVec2(line_x, row_min.y), ImVec2(line_x, is_last ? center_y : row_max.y), line_col, 2.0f * sc);
            child_draw_list->AddLine(ImVec2(line_x, center_y), ImVec2(content_x - 5.0f * sc, center_y), line_col, 2.0f * sc);
        }

        const ImRect checkbox_rect(ImVec2(content_x, center_y - checkbox_size * 0.5f),
                                   ImVec2(content_x + checkbox_size, center_y + checkbox_size * 0.5f));
        if (checkable)
            draw_checkbox(child_draw_list, checkbox_rect, state);

        const bool has_children = !node.children.empty();
        const float arrow_x = checkable ? checkbox_rect.Max.x + 10.0f * sc : content_x;
        const ImVec2 arrow_min(arrow_x, center_y - arrow_size * 0.5f);
        const ImRect arrow_rect(arrow_min, ImVec2(arrow_min.x + arrow_size, arrow_min.y + arrow_size));
        if (has_children && s_assembly_tree_icons.loaded)
            child_draw_list->AddImage(open ? s_assembly_tree_icons.expand : s_assembly_tree_icons.collapse, arrow_rect.Min, arrow_rect.Max);

        const float text_x = has_children
            ? arrow_rect.Max.x + 10.0f * sc
            : (checkable ? checkbox_rect.Max.x + 10.0f * sc : content_x);
        const float text_max_x = row_max.x - 8.0f * sc;
        const float text_avail_w = std::max(0.0f, text_max_x - text_x);
        const std::string display_label = utf8_fit_with_ellipsis(node.label, text_avail_w);
        const char* display_begin = display_label.c_str();
        const char* display_end = display_begin + display_label.size();
        const ImVec2 text_size = ImGui::CalcTextSize(display_begin, display_end);
        const ImVec2 text_pos(text_x, center_y - text_size.y * 0.5f);
        child_draw_list->PushClipRect(text_pos, ImVec2(text_max_x, text_pos.y + text_size.y), true);
        child_draw_list->AddText(text_pos, text_col, display_begin, display_end);
        child_draw_list->PopClipRect();

        if (ImGui::IsItemClicked()) {
            const ImVec2 mouse_pos = ImGui::GetIO().MousePos;
            if (!options.readonly && checkable && checkbox_rect.Contains(mouse_pos)) {
                set_subtree_checked(node_id, state != AssemblyTreeCheckState::All);
                result.changed = true;
            } else if (has_children && arrow_rect.Contains(mouse_pos)) {
                open = !open;
            }
        }

        if (hovered && display_label != node.label)
            render_panel_tooltip(node.label);

        if ((open || !search_text_lc.empty()) && has_children) {
            for (size_t child_idx = 0; child_idx < node.children.size(); ++child_idx)
                render_node(node.children[child_idx], depth + 1, child_idx + 1 == node.children.size());
        }

        ImGui::PopID();
    };
    for (size_t root_idx = 0; root_idx < tree.roots.size(); ++root_idx)
        render_node(tree.roots[root_idx], 0, root_idx + 1 == tree.roots.size());
    ImGui::EndChild();

    if (!options.show_footer)
        return result;

    ImVec2 separator_start = ImGui::GetCursorScreenPos();
    draw_list->AddLine(separator_start, ImVec2(separator_start.x + ImGui::GetContentRegionAvail().x, separator_start.y), separator_col, 1.0f * sc);
    ImGui::Dummy(ImVec2(0.0f, 16.0f * sc));

    const ImVec2 footer_pos = ImGui::GetCursorScreenPos();
    const std::string checked_text = std::to_string(result.checked_leaf_count);
    const std::string total_text = " / " + std::to_string(result.leaf_count);
    draw_list->AddText(footer_pos, text_col, checked_text.c_str());
    const ImVec2 checked_text_size = ImGui::CalcTextSize(checked_text.c_str());
    draw_list->AddText(ImVec2(footer_pos.x + checked_text_size.x, footer_pos.y), sub_text_col, total_text.c_str());

    const ImVec2 cancel_size(86.0f * sc, 34.0f * sc);
    const ImVec2 ok_size(86.0f * sc, 34.0f * sc);
    const float button_gap = 12.0f * sc;
    const float buttons_x = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - ImGui::GetStyle().WindowPadding.x - cancel_size.x - button_gap - ok_size.x;
    const float buttons_y = footer_pos.y - 7.0f * sc;

    if (render_footer_button("##assembly_tree_cancel", _u8L("Cancel"), ImVec2(buttons_x, buttons_y), cancel_size, false, sc)) {
        checked.clear();
        result.cancel = true;
        result.changed = true;
    }
    if (render_footer_button("##assembly_tree_ok", _u8L("OK"), ImVec2(buttons_x + cancel_size.x + button_gap, buttons_y), ok_size, true, sc))
        result.confirm = true;

    ImGui::SetCursorScreenPos(ImVec2(footer_pos.x, footer_pos.y + ok_size.y));
    return result;
}

void AssemblyStepsUtils::clear_active_assembly_tree_checked()
{
    m_active_assembly_tree_checked = nullptr;
}

void AssemblyStepsUtils::create_assembly_steps_from_step_import_tree(
    const std::vector<Model::StepImportTreeNode>& step_nodes,
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

    auto child_node = [&step_nodes](size_t child_id) -> const Model::StepImportTreeNode* {
        if (child_id == 0 || child_id > step_nodes.size())
            return nullptr;
        return &step_nodes[child_id - 1];
    };

    auto is_compound = [](const Model::StepImportTreeNode& step_node) {
        return step_node.component_count > 0;
    };

    auto collect_direct_objects = [&](const Model::StepImportTreeNode &step_node, bool ignore_compound = false) {
        std::vector<int> object_idxs;
        for (size_t child_id : step_node.children) {
            const Model::StepImportTreeNode* child = child_node(child_id);
            if (child == nullptr || (!ignore_compound && is_compound(*child)))
                continue;
            if (!valid_object(child->model_object_idx))
                continue;
            if (std::find(object_idxs.begin(), object_idxs.end(), child->model_object_idx) == object_idxs.end())
                object_idxs.push_back(child->model_object_idx);
        }
        return object_idxs;
    };

    auto emit_step_for_compound = [&](const Model::StepImportTreeNode& compound, bool has_compound_child) {
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
        const Model::StepImportTreeNode* node = child_node(node_id);
        if (node == nullptr || !is_compound(*node))
            return;

        bool has_compound_child = false;
        bool has_solid_child = false;
        for (size_t child_id : node->children) {
            const Model::StepImportTreeNode* child = child_node(child_id);
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

    for (const Model::StepImportTreeNode& step_node : step_nodes) {
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

bool AssemblyStepsUtils::render_connection_type_btn(
    ImDrawList *dl, float x, float y, float w, float h,
    ImTextureID icon, const char *label,
    float icon_sz, float label_fs, float sc,
    bool selected, ImU32 label_col, ImU32 brand_col,
    const char *tooltip)
{
    const ImVec2 btn_min(x, y);
    const ImVec2 btn_max(x + w, y + h);
    const float rounding = 4.0f * sc;

    const ImU32 bg = m_is_dark ? IM_COL32(45, 45, 49, 255) : IM_COL32(255, 255, 255, 255);
    dl->AddRectFilled(btn_min, btn_max, bg, rounding);
    if (selected)
        dl->AddRect(btn_min, btn_max, brand_col, rounding, 0, 2.0f * sc);

    if (icon) {
        const float ico_x = x + (w - icon_sz) * 0.5f;
        const float ico_y = y + 8.0f * sc;
        dl->AddImage(icon, ImVec2(ico_x, ico_y), ImVec2(ico_x + icon_sz, ico_y + icon_sz));
    }

    const ImVec2 lbl_sz = ImGui::GetFont()->CalcTextSizeA(label_fs, FLT_MAX, 0.0f, label);
    const float lbl_x = x + (w - lbl_sz.x) * 0.5f;
    const float lbl_y = y + h - lbl_sz.y - 6.0f * sc;
    dl->AddText(ImGui::GetFont(), label_fs, ImVec2(lbl_x, lbl_y), label_col, label);

    ImGui::SetCursorScreenPos(btn_min);
    ImGui::PushID(label);
    ImGui::InvisibleButton("##ct", ImVec2(w, h));
    bool clicked = ImGui::IsItemClicked(0);
    bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    if (hovered && tooltip && tooltip[0] != '\0' && m_imgui) {
        // Restore non-zero WindowPadding for the tooltip window: callers that
        // already pushed WindowPadding=(0,0) on the outer window (e.g. the
        // assembly guide panel) would otherwise glue the text to the edges.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
            ImVec2(8.0f * sc, 6.0f * sc));
        m_imgui->tooltip(tooltip, 20.0f * m_imgui->scaled(1.0f));
        ImGui::PopStyleVar();
    }

    return clicked;
}

bool AssemblyStepsUtils::render_cyber_brick_section(
    ImDrawList *dl, ImVec2 card_min, float card_w, float card_h,
    float font_sz, float small_fs, float sc)
{
    const ImU32 grey300 = IM_COL32(238, 238, 238, 255);
    const ImU32 grey400 = IM_COL32(206, 206, 206, 255);
    const ImU32 grey500 = IM_COL32(172, 172, 172, 255);
    const ImU32 grey700 = IM_COL32(107, 107, 107, 255);
    const float rounding = 4.0f * sc;

    // "+" button (top-right)
    const float btn_sz = 16.0f * sc;
    const ImVec2 btn_min(card_min.x + card_w - 8.0f * sc - btn_sz, card_min.y + 6.0f * sc);
    const ImVec2 btn_max(btn_min.x + btn_sz, btn_min.y + btn_sz);
    dl->AddRectFilled(btn_min, btn_max, grey300, rounding);
    dl->AddRect(btn_min, btn_max, grey400, rounding, 0, 1.0f * sc);
    const float pp = 4.0f * sc;
    const float mx = (btn_min.x + btn_max.x) * 0.5f;
    const float my = (btn_min.y + btn_max.y) * 0.5f;
    dl->AddLine(ImVec2(mx, btn_min.y + pp), ImVec2(mx, btn_max.y - pp), grey700, 1.5f * sc);
    dl->AddLine(ImVec2(btn_min.x + pp, my), ImVec2(btn_max.x - pp, my), grey700, 1.5f * sc);

    // Dashed placeholder area
    const float area_x = card_min.x + 8.0f * sc;
    const float area_y = card_min.y + font_sz + 14.0f * sc;
    const float area_w = card_w - 16.0f * sc;
    const float area_h = 36.0f * sc;
    const ImVec2 area_min(area_x, area_y);
    const ImVec2 area_max(area_x + area_w, area_y + area_h);

    const float dash_len = 5.0f * sc;
    const float gap_len  = 3.0f * sc;
    auto draw_dashed_line = [&](ImVec2 p0, ImVec2 p1) {
        float dx = p1.x - p0.x, dy = p1.y - p0.y;
        float len = sqrtf(dx * dx + dy * dy);
        if (len < 0.01f) return;
        float nx = dx / len, ny = dy / len;
        float d = 0;
        while (d < len) {
            float seg = std::min(dash_len, len - d);
            dl->AddLine(
                ImVec2(p0.x + nx * d, p0.y + ny * d),
                ImVec2(p0.x + nx * (d + seg), p0.y + ny * (d + seg)),
                grey400, 1.0f * sc);
            d += dash_len + gap_len;
        }
    };
    dl->AddRectFilled(area_min, area_max, grey300, rounding);
    draw_dashed_line(area_min, ImVec2(area_max.x, area_min.y));
    draw_dashed_line(ImVec2(area_max.x, area_min.y), area_max);
    draw_dashed_line(area_max, ImVec2(area_min.x, area_max.y));
    draw_dashed_line(ImVec2(area_min.x, area_max.y), area_min);

    // Hint text
    const std::string hint_str = _u8L("Click to add Cyber Brick");
    const ImVec2 hint_size = ImGui::GetFont()->CalcTextSizeA(small_fs, FLT_MAX, 0.0f, hint_str.c_str());
    dl->AddText(ImGui::GetFont(), small_fs,
        ImVec2(area_min.x + (area_w - hint_size.x) * 0.5f,
               area_min.y + (area_h - hint_size.y) * 0.5f),
        grey500, hint_str.c_str());

    // Click detection on the dashed area
    ImGui::SetCursorScreenPos(area_min);
    ImGui::PushID("##cyber_brick_area");
    ImGui::InvisibleButton("##cb", ImVec2(area_w, area_h));
    bool clicked = ImGui::IsItemClicked(0);
    ImGui::PopID();

    // Also detect click on "+" button
    ImGui::SetCursorScreenPos(btn_min);
    ImGui::PushID("##cyber_brick_add");
    ImGui::InvisibleButton("##cb_add", ImVec2(btn_sz, btn_sz));
    if (ImGui::IsItemClicked(0))
        clicked = true;
    ImGui::PopID();

    return clicked;
}

int AssemblyStepsUtils::render_timeline_keyframe(
    ImDrawList *dl, float x, float y, float w, float h,
    bool has_keyframe, bool selected,
    const char *label, float label_fs, float sc,
    bool show_delete_badge)
{
    const ImU32 brand   = IM_COL32(0, 174, 66, 255);
    const ImU32 grey200 = m_is_dark ? IM_COL32(50, 50, 54, 255)  : IM_COL32(248, 248, 248, 255);
    const ImU32 grey300 = m_is_dark ? IM_COL32(60, 60, 64, 255)  : IM_COL32(238, 238, 238, 255);
    const ImU32 grey400 = m_is_dark ? IM_COL32(70, 70, 74, 255)  : IM_COL32(206, 206, 206, 255);
    const ImU32 grey600 = m_is_dark ? IM_COL32(0x90, 0x90, 0x90, 255) : IM_COL32(144, 144, 144, 255);
    const ImU32 grey700 = m_is_dark ? IM_COL32(0xA0, 0xA0, 0xA0, 255) : IM_COL32(107, 107, 107, 255);
    const ImU32 white_c = m_is_dark ? IM_COL32(55, 55, 59, 255)  : IM_COL32(255, 255, 255, 255);
    const float font_sz = ImGui::GetFontSize();

    int result = 0;

    if (!has_keyframe) {
        // Dashed placeholder with "+" icon
        const ImVec2 slot_min(x, y);
        const ImVec2 slot_max(x + w, y + h);
        dl->AddRectFilled(slot_min, slot_max, grey300);

        const float dash = 4.0f * sc, gap = 3.0f * sc;
        auto dashed = [&](ImVec2 p0, ImVec2 p1) {
            float dx = p1.x - p0.x, dy = p1.y - p0.y;
            float len = sqrtf(dx * dx + dy * dy);
            if (len < 0.01f) return;
            float nx = dx / len, ny = dy / len;
            for (float d = 0; d < len;) {
                float seg = std::min(dash, len - d);
                dl->AddLine(ImVec2(p0.x + nx * d, p0.y + ny * d),
                            ImVec2(p0.x + nx * (d + seg), p0.y + ny * (d + seg)),
                            grey400, 1.0f * sc);
                d += dash + gap;
            }
        };
        dashed(slot_min, ImVec2(slot_max.x, slot_min.y));
        dashed(ImVec2(slot_max.x, slot_min.y), slot_max);
        dashed(slot_max, ImVec2(slot_min.x, slot_max.y));
        dashed(ImVec2(slot_min.x, slot_max.y), slot_min);

        // "+" sign centered
        const float plus_len = 8.0f * sc;
        const float cx = x + w * 0.5f, cy = y + h * 0.5f;
        dl->AddLine(ImVec2(cx, cy - plus_len * 0.5f), ImVec2(cx, cy + plus_len * 0.5f), grey700, 1.5f * sc);
        dl->AddLine(ImVec2(cx - plus_len * 0.5f, cy), ImVec2(cx + plus_len * 0.5f, cy), grey700, 1.5f * sc);

        ImGui::SetCursorScreenPos(slot_min);
        ImGui::PushID(label);
        ImGui::InvisibleButton("##kf_add", ImVec2(w, h));
        if (ImGui::IsItemClicked(0)) result = 1;
        ImGui::PopID();
    } else {
        // Keyframe thumbnail
        const ImVec2 slot_min(x, y);
        const ImVec2 slot_max(x + w, y + h);

        if (selected) {
            dl->AddRectFilled(slot_min, slot_max, IM_COL32(44, 173, 0, 38));
            dl->AddRect(slot_min, slot_max, brand, 0, 0, 1.5f * sc);
        } else {
            dl->AddRectFilled(slot_min, slot_max, grey200);
            dl->AddRect(slot_min, slot_max, grey400, 0, 0, 1.0f * sc);
        }

        // Frame icon centered
        if (m_tree_icon_frame) {
            const float ico = 20.0f * sc;
            dl->AddImage(m_tree_icon_frame,
                ImVec2(x + (w - ico) * 0.5f, y + (h - ico) * 0.5f),
                ImVec2(x + (w + ico) * 0.5f, y + (h + ico) * 0.5f));
        }

        // Label below
        const ImVec2 lsz = ImGui::GetFont()->CalcTextSizeA(label_fs, FLT_MAX, 0.0f, label);
        const ImU32 lbl_col = selected ? brand : grey600;
        dl->AddText(ImGui::GetFont(), label_fs,
            ImVec2(x + (w - lsz.x) * 0.5f, y + h + 4.0f * sc), lbl_col, label);

        // Delete badge geometry (computed first for hit-test priority)
        const float badge_r = 7.0f * sc;
        const ImVec2 badge_c(slot_max.x - 3.0f * sc, slot_min.y + 3.0f * sc);
        const ImVec2 badge_min(badge_c.x - badge_r, badge_c.y - badge_r);
        const ImVec2 badge_max(badge_c.x + badge_r, badge_c.y + badge_r);

        bool slot_hovered = ImGui::IsMouseHoveringRect(slot_min, slot_max);
        bool badge_hovered = show_delete_badge && selected
            && ImGui::IsMouseHoveringRect(badge_min, badge_max);

        // Delete badge (top-right, rendered and clickable first to get priority).
        // Skipped entirely when `show_delete_badge` is false (e.g. the End frame
        // is mandatory and must not be deletable).
        if (show_delete_badge && slot_hovered && selected) {
            dl->AddCircleFilled(badge_c, badge_r, brand);
            if (m_tree_icon_cross) {
                const float cr = 5.0f * sc;
                dl->AddImage(m_tree_icon_cross,
                    ImVec2(badge_c.x - cr, badge_c.y - cr),
                    ImVec2(badge_c.x + cr, badge_c.y + cr),
                    ImVec2(0, 0), ImVec2(1, 1), white_c);
            }

            ImGui::SetCursorScreenPos(badge_min);
            ImGui::PushID(label);
            ImGui::PushID("##kf_del");
            ImGui::InvisibleButton("##del", ImVec2(badge_r * 2, badge_r * 2));
            if (ImGui::IsItemClicked(0)) result = -1;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", _u8L("Delete this keyframe").c_str());
            ImGui::PopID();
            ImGui::PopID();
        }

        // Slot click (only if not clicking the badge)
        if (result == 0) {
            ImGui::SetCursorScreenPos(slot_min);
            ImGui::PushID(label);
            ImGui::InvisibleButton("##kf_slot", ImVec2(w, h));
            if (ImGui::IsItemClicked(0) && !badge_hovered) result = 1;
            ImGui::PopID();
        }
    }

    return result;
}

bool AssemblyStepsUtils::render_note_tool_btn(
    ImDrawList *dl, float x, float y, float sz,
    ImTextureID icon, bool selected, const char *id, float sc,
    const char *tooltip)
{
    const ImU32 white_c = m_is_dark ? IM_COL32(45, 45, 49, 255) : IM_COL32(255, 255, 255, 255);
    const ImU32 grey400 = m_is_dark ? IM_COL32(70, 70, 74, 255) : IM_COL32(206, 206, 206, 255);
    const ImU32 brand   = IM_COL32(0, 174, 66, 255);
    const float rounding = 4.0f * sc;

    const ImVec2 bmin(x, y);
    const ImVec2 bmax(x + sz, y + sz);
    dl->AddRectFilled(bmin, bmax, white_c, rounding);
    dl->AddRect(bmin, bmax, selected ? brand : grey400, rounding, 0,
        selected ? 1.5f * sc : 1.0f * sc);

    if (icon) {
        const float ico = sz * 0.88f;
        const ImVec2 imin(x + (sz - ico) * 0.5f, y + (sz - ico) * 0.5f);
        dl->AddImage(icon, imin, ImVec2(imin.x + ico, imin.y + ico));
    }

    ImGui::SetCursorScreenPos(bmin);
    ImGui::PushID(id);
    ImGui::InvisibleButton("##nt", ImVec2(sz, sz));
    bool clicked = ImGui::IsItemClicked(0);
    bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    if (hovered && tooltip && tooltip[0] != '\0' && m_imgui) {
        // Restore non-zero WindowPadding so the tooltip popup window doesn't
        // inherit the outer panel's WindowPadding=(0,0) and glue text to edges.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
            ImVec2(8.0f * sc, 6.0f * sc));
        m_imgui->tooltip(tooltip, 20.0f * m_imgui->scaled(1.0f));
        ImGui::PopStyleVar();
    }

    return clicked;
}

struct NoteColorItem {
    std::array<int, 4> color;
    const char *id;
    const char *tip;
    bool has_border;
};

static const NoteColorItem kNoteColors[] = {
    { {213,  61,  64, 255}, "red",    "Red",    false },
    { {240, 159,  19, 255}, "orange", "Orange", false },
    { { 35, 169,  46, 255}, "green",  "Green",  false },
    { { 63, 130, 240, 255}, "blue",   "Blue",   false },
    { { 29,  32,  45, 255}, "black",  "Black",  false },
    { {125, 127, 134, 255}, "grey",   "Grey",   false },
    { {255, 255, 255, 255}, "white",  "White",  true  },
};

static ImU32 note_color_to_im_u32(const std::array<int, 4> &color)
{
    return IM_COL32(color[0], color[1], color[2], color[3]);
}

static std::array<float, 4> note_color_to_float_array(const std::array<int, 4> &color)
{
    return {color[0] / 255.0f, color[1] / 255.0f, color[2] / 255.0f, color[3] / 255.0f};
}

static int note_palette_index_from_color(const std::array<int, 4> &color)
{
    for (int i = 0; i < (int)IM_ARRAYSIZE(kNoteColors); ++i) {
        if (kNoteColors[i].color[0] == color[0] &&
            kNoteColors[i].color[1] == color[1] &&
            kNoteColors[i].color[2] == color[2])
            return i;
    }
    return 2;
}

static std::array<int, 4> note_color_from_palette_index(int idx)
{
    if (idx < 0 || idx >= (int)IM_ARRAYSIZE(kNoteColors))
        return kNoteColors[2].color; // fallback to green when out of range
    return kNoteColors[idx].color;
}

bool AssemblyStepsUtils::render_note_color_control(ImDrawList *dl, float x, float y, float sc)
{
    const float swatch_sz = 14.4f * sc;
    const float gap       = 4.8f * sc;
    const float pad_x     = 8.0f * sc;
    const float pad_y     = 4.0f * sc;
    const float rounding  = 4.0f * sc;
    const float swatch_rounding = 2.0f * sc;
    const float w = 2.0f * pad_x + IM_ARRAYSIZE(kNoteColors) * swatch_sz + (IM_ARRAYSIZE(kNoteColors) - 1) * gap;
    const float h = 2.0f * pad_y + swatch_sz;

    const ImVec2 min(x, y);
    const ImVec2 max(x + w, y + h);
    dl->AddRectFilled(min, max, m_is_dark ? IM_COL32(45, 45, 49, 255) : IM_COL32(255, 255, 255, 255), rounding);
    dl->AddRect(min, max, m_is_dark ? IM_COL32(70, 70, 74, 255) : IM_COL32(238, 238, 238, 255), rounding);

    bool changed = false;
    float sx = x + pad_x;
    const float sy = y + pad_y;
    for (int i = 0; i < (int)IM_ARRAYSIZE(kNoteColors); ++i) {
        const NoteColorItem &item = kNoteColors[i];
        const ImVec2 smin(sx, sy);
        const ImVec2 smax(sx + swatch_sz, sy + swatch_sz);
        dl->AddRectFilled(smin, smax, note_color_to_im_u32(item.color), swatch_rounding);
        if (item.has_border)
            dl->AddRect(smin, smax, m_is_dark ? IM_COL32(100, 100, 104, 255) : IM_COL32(172, 172, 172, 255), swatch_rounding);
        if (m_guide_note_color_selected == i)
            dl->AddRect(ImVec2(smin.x - 2.4f * sc, smin.y - 2.4f * sc),
                ImVec2(smax.x + 2.4f * sc, smax.y + 2.4f * sc),
                IM_COL32(0, 174, 66, 255), swatch_rounding + 2.4f * sc, 0, 1.8f * sc);

        ImGui::SetCursorScreenPos(smin);
        ImGui::PushID(item.id);
        ImGui::InvisibleButton("##note_color", ImVec2(swatch_sz, swatch_sz));
        if (ImGui::IsItemClicked(0)) {
            set_selection_origin(SelectionOrigin::NoteColorControl);
        }
        if (ImGui::IsItemClicked(0) && m_guide_note_color_selected != i) {
            m_guide_note_color_selected = i;
            auto *entries = get_current_kf_entries();
            if (entries && m_keyframe_selected >= 0 && m_keyframe_selected < (int)entries->size()) {
                KeyFrameEntry &cur_entry = (*entries)[m_keyframe_selected];
                AssemblyNote &note = cur_entry.data.assembly_note;
                const std::array<int, 4> color = item.color;
                if (m_note_selected_type == AssemblyNoteSelectionType::ArrowSvg &&
                    m_note_selected_idx >= 0 && m_note_selected_idx < (int)note.arrow_svgs.size())
                    note.arrow_svgs[m_note_selected_idx].color = color;
                else if (m_note_selected_type == AssemblyNoteSelectionType::TextLabel &&
                         m_note_selected_idx >= 0 && m_note_selected_idx < (int)note.text_labels.size())
                    note.text_labels[m_note_selected_idx].color = color;
                else if (m_note_selected_type == AssemblyNoteSelectionType::Circle &&
                         m_note_selected_idx >= 0 && m_note_selected_idx < (int)note.circle_notes.size())
                    note.circle_notes[m_note_selected_idx].color = color;
                else if (m_note_selected_type == AssemblyNoteSelectionType::Rectangle &&
                         m_note_selected_idx >= 0 && m_note_selected_idx < (int)note.rectangle_notes.size())
                    note.rectangle_notes[m_note_selected_idx].color = color;
                else if (m_note_selected_type == AssemblyNoteSelectionType::PlainArrow &&
                         m_note_selected_idx >= 0 && m_note_selected_idx < (int)note.plain_arrows.size())
                    note.plain_arrows[m_note_selected_idx].color = color;
                cur_entry.need_save = true;
                save_assembly_steps_json_to_model();
                do_commond_callback("dirty");
            }
            changed = true;
        }
        if (ImGui::IsItemHovered() && m_imgui) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                ImVec2(8.0f * sc, 6.0f * sc));
            m_imgui->tooltip(item.tip, 20.0f * m_imgui->scaled(1.0f));
            ImGui::PopStyleVar();
        }
        ImGui::PopID();

        sx += swatch_sz + gap;
    }

    return changed;
}

bool AssemblyStepsUtils::render_note_bg_color_control(ImDrawList *dl, float x, float y, float sc)
{
    // Visual layout matches render_note_color_control so the two rows align.
    const float swatch_sz = 14.4f * sc;
    const float gap       = 4.8f * sc;
    const float pad_x     = 8.0f * sc;
    const float pad_y     = 4.0f * sc;
    const float rounding  = 4.0f * sc;
    const float swatch_rounding = 2.0f * sc;
    const float w = 2.0f * pad_x + IM_ARRAYSIZE(kNoteColors) * swatch_sz + (IM_ARRAYSIZE(kNoteColors) - 1) * gap;
    const float h = 2.0f * pad_y + swatch_sz;

    const ImVec2 min(x, y);
    const ImVec2 max(x + w, y + h);
    dl->AddRectFilled(min, max, m_is_dark ? IM_COL32(45, 45, 49, 255) : IM_COL32(255, 255, 255, 255), rounding);
    dl->AddRect(min, max, m_is_dark ? IM_COL32(70, 70, 74, 255) : IM_COL32(238, 238, 238, 255), rounding);

    bool changed = false;
    float sx = x + pad_x;
    const float sy = y + pad_y;
    for (int i = 0; i < (int)IM_ARRAYSIZE(kNoteColors); ++i) {
        const NoteColorItem &item = kNoteColors[i];
        const ImVec2 smin(sx, sy);
        const ImVec2 smax(sx + swatch_sz, sy + swatch_sz);
        dl->AddRectFilled(smin, smax, note_color_to_im_u32(item.color), swatch_rounding);
        if (item.has_border)
            dl->AddRect(smin, smax, m_is_dark ? IM_COL32(100, 100, 104, 255) : IM_COL32(172, 172, 172, 255), swatch_rounding);
        if (m_guide_note_bg_color_selected == i)
            dl->AddRect(ImVec2(smin.x - 2.4f * sc, smin.y - 2.4f * sc),
                ImVec2(smax.x + 2.4f * sc, smax.y + 2.4f * sc),
                IM_COL32(0, 174, 66, 255), swatch_rounding + 2.4f * sc, 0, 1.8f * sc);

        ImGui::SetCursorScreenPos(smin);
        ImGui::PushID(item.id);
        ImGui::InvisibleButton("##note_bg_color", ImVec2(swatch_sz, swatch_sz));
        if (ImGui::IsItemClicked(0)) {
            set_selection_origin(SelectionOrigin::NoteColorControl);
        }
        if (ImGui::IsItemClicked(0) && m_guide_note_bg_color_selected != i) {
            m_guide_note_bg_color_selected = i;
            auto *entries = get_current_kf_entries();
            if (entries && m_keyframe_selected >= 0 && m_keyframe_selected < (int)entries->size()) {
                KeyFrameEntry &cur_entry = (*entries)[m_keyframe_selected];
                AssemblyNote &note = cur_entry.data.assembly_note;
                if (m_note_selected_type == AssemblyNoteSelectionType::TextLabel &&
                    m_note_selected_idx >= 0 && m_note_selected_idx < (int)note.text_labels.size()) {
                    // Preserve the historic ~0.85 alpha so the background still
                    // blends with the underlying canvas.
                    std::array<int, 4> bg = item.color;
                    bg[3] = 217;
                    note.text_labels[m_note_selected_idx].background_color = bg;
                    cur_entry.need_save = true;
                    save_assembly_steps_json_to_model();
                    do_commond_callback("dirty");
                }
            }
            changed = true;
        }
        if (ImGui::IsItemHovered() && m_imgui) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                ImVec2(8.0f * sc, 6.0f * sc));
            m_imgui->tooltip(item.tip, 20.0f * m_imgui->scaled(1.0f));
            ImGui::PopStyleVar();
        }
        ImGui::PopID();

        sx += swatch_sz + gap;
    }

    return changed;
}

bool AssemblyStepsUtils::render_footer_button(const char* id, const std::string& label,
                                              const ImVec2& pos, const ImVec2& size,
                                              bool primary, float sc)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
    const float min_pad_x = 14.0f * sc;
    const ImVec2 draw_size(std::max(size.x, text_size.x + 2.0f * min_pad_x), size.y);
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, draw_size);
    const bool clicked = ImGui::IsItemClicked();
    const bool hovered = ImGui::IsItemHovered();
    const bool disabled = ImGui::GetItemFlags() & ImGuiItemFlags_Disabled;

    const ImU32 bg = disabled ? IM_COL32(238, 238, 238, 255) :
        (primary ? (hovered ? IM_COL32(0, 190, 74, 255) : IM_COL32(0, 174, 66, 255)) : IM_COL32(255, 255, 255, 255));
    const ImU32 border = disabled ? IM_COL32(206, 206, 206, 255) :
        (primary ? bg : (hovered ? IM_COL32(0, 174, 66, 255) : IM_COL32(202, 202, 202, 255)));
    const ImU32 text = disabled ? IM_COL32(172, 172, 172, 255) :
        (primary ? IM_COL32(255, 255, 255, 255) : IM_COL32(38, 46, 48, 255));
    draw_list->AddRectFilled(pos, ImVec2(pos.x + draw_size.x, pos.y + draw_size.y), bg, draw_size.y * 0.5f);
    draw_list->AddRect(pos, ImVec2(pos.x + draw_size.x, pos.y + draw_size.y), border, draw_size.y * 0.5f, 0, 2.0f * sc);

    const ImVec2 text_pos(pos.x + (draw_size.x - text_size.x) * 0.5f, pos.y + (draw_size.y - text_size.y) * 0.5f);
    draw_list->AddText(text_pos, text, label.c_str());
    return !disabled && clicked;
}

void AssemblyStepsUtils::render_export_menu_popup(const char* popup_id, float sc)
{
    static const int kExportItemCount = 3;
    const std::string labels[] = { _u8L("Export PDF"), _u8L("Export Markdown"), _u8L("Export MP4") };
    const ExportType types[] = { ExportType::PDF, ExportType::MarkDown, ExportType::MP4 };
    const std::string markdown_tooltip = _u8L("After exporting the Markdown document, you can edit it in third-party software such as MarkText and then export it to PDF.");
    const float row_height = 28.0f * sc;
    const float row_spacing = 2.0f * sc;
    const float win_padding = 12.0f * sc;
    const float row_pad_x = 8.0f * sc;
    const float text_right_margin = 8.0f * sc;

    float max_text_width = 0.0f;
    for (int i = 0; i < kExportItemCount; ++i)
        max_text_width = std::max(max_text_width, ImGui::CalcTextSize(labels[i].c_str()).x);

    const float menu_width = std::max(128.0f * sc,
        2.0f * win_padding + row_pad_x + max_text_width + text_right_margin);
    const float menu_height = win_padding * 2.0f + row_height * kExportItemCount + row_spacing * (kExportItemCount - 1);
    ImGui::SetNextWindowSize(ImVec2(menu_width, menu_height), ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_PopupBg, m_is_dark ? ImVec4(45 / 255.0f, 45 / 255.0f, 49 / 255.0f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 77.0f / 255.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, m_is_dark ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(38.0f / 255.0f, 46.0f / 255.0f, 48.0f / 255.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f * sc);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(win_padding, win_padding));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

    if (ImGui::BeginPopup(popup_id, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove)) {
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows)) {
            ImGuiIO &io = ImGui::GetIO();
            io.WantCaptureMouse = true;
        }

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        for (int i = 0; i < kExportItemCount; ++i) {
            ImGui::PushID(i);
            ImVec2 row_pos = ImGui::GetCursorScreenPos();
            const float row_content_w = ImGui::GetContentRegionAvail().x;
            if (ImGui::InvisibleButton("##assembly_export_item", ImVec2(row_content_w, row_height))) {
                on_export(types[i]);
                ImGui::CloseCurrentPopup();
            }

            const bool hovered = ImGui::IsItemHovered();
            if (hovered) {
                const ImU32 bg = m_is_dark ? IM_COL32(55, 55, 59, 255) : IM_COL32(240, 240, 240, 255);
                draw_list->AddRectFilled(row_pos, ImVec2(row_pos.x + row_content_w, row_pos.y + row_height), bg, 4.0f * sc);
                if (types[i] == ExportType::MarkDown)
                    render_panel_tooltip(markdown_tooltip, false);
            }

            const ImVec2 text_size = ImGui::CalcTextSize(labels[i].c_str());
            draw_list->AddText(ImVec2(row_pos.x + row_pad_x, row_pos.y + (row_height - text_size.y) * 0.5f),
                               ImGui::GetColorU32(ImGuiCol_Text), labels[i].c_str());
            ImGui::PopID();
            if (i + 1 < kExportItemCount)
                ImGui::Dummy(ImVec2(0.0f, row_spacing));
        }
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(3);
}

void AssemblyStepsUtils::render_labels_show_type_menu_popup(const char* popup_id, float sc)
{
    static const int kTypeCount = 3;
    const std::string labels[] = {
        _u8L("Auto recommend"),
        _u8L("Model objects only"),
        _u8L("Model parts only")
    };
    const LabelsShowType types[] = {
        LabelsShowType::AutoRecommend,
        LabelsShowType::OnlyModelObject,
        LabelsShowType::OnlyModelVolume
    };
    // Trailing action row (not a type): relayout the current labels in place.
    const std::string action_label = _u8L("Auto-arrange labels in current view");

    const float row_height = 28.0f * sc;
    const float row_spacing = 2.0f * sc;
    const float win_padding = 12.0f * sc;
    const float row_pad_x = 8.0f * sc;
    const float check_w = 18.0f * sc;       // leading column for the current-selection mark
    const float text_right_margin = 8.0f * sc;
    const float sep_line_h = 1.0f * sc;
    const float sep_block_h = row_spacing + sep_line_h + row_spacing; // gap+line+gap

    float max_text_width = 0.0f;
    for (int i = 0; i < kTypeCount; ++i)
        max_text_width = std::max(max_text_width, ImGui::CalcTextSize(labels[i].c_str()).x);
    max_text_width = std::max(max_text_width, ImGui::CalcTextSize(action_label.c_str()).x);

    const float menu_width = std::max(128.0f * sc,
        2.0f * win_padding + row_pad_x + check_w + max_text_width + text_right_margin);
    const float menu_height = win_padding * 2.0f
        + row_height * (kTypeCount + 1)
        + row_spacing * (kTypeCount - 1)
        + sep_block_h;
    ImGui::SetNextWindowSize(ImVec2(menu_width, menu_height), ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_PopupBg, m_is_dark ? ImVec4(45 / 255.0f, 45 / 255.0f, 49 / 255.0f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 77.0f / 255.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, m_is_dark ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(38.0f / 255.0f, 46.0f / 255.0f, 48.0f / 255.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f * sc);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(win_padding, win_padding));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginPopup(popup_id, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove)) {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const ImU32 brand = IM_COL32(0, 174, 66, 255);
        for (int i = 0; i < kTypeCount; ++i) {
            ImGui::PushID(i);
            ImVec2 row_pos = ImGui::GetCursorScreenPos();
            const float row_content_w = ImGui::GetContentRegionAvail().x;
            if (ImGui::InvisibleButton("##assembly_labels_show_type_item", ImVec2(row_content_w, row_height))) {
                set_labels_show_type(types[i]);
                ImGui::CloseCurrentPopup();
            }

            const bool hovered = ImGui::IsItemHovered();
            if (hovered) {
                const ImU32 bg = m_is_dark ? IM_COL32(55, 55, 59, 255) : IM_COL32(240, 240, 240, 255);
                draw_list->AddRectFilled(row_pos, ImVec2(row_pos.x + row_content_w, row_pos.y + row_height), bg, 4.0f * sc);
            }

            // Leading check mark for the currently active type.
            if (types[i] == m_cur_labels_show_type) {
                const float mk = 10.0f * sc;
                const float mx = row_pos.x + row_pad_x;
                const float my = row_pos.y + (row_height - mk) * 0.5f;
                draw_list->AddLine(ImVec2(mx + mk * 0.10f, my + mk * 0.55f),
                                   ImVec2(mx + mk * 0.40f, my + mk * 0.85f), brand, 1.6f * sc);
                draw_list->AddLine(ImVec2(mx + mk * 0.40f, my + mk * 0.85f),
                                   ImVec2(mx + mk * 0.90f, my + mk * 0.15f), brand, 1.6f * sc);
            }

            const ImVec2 text_size = ImGui::CalcTextSize(labels[i].c_str());
            draw_list->AddText(ImVec2(row_pos.x + row_pad_x + check_w, row_pos.y + (row_height - text_size.y) * 0.5f),
                               ImGui::GetColorU32(ImGuiCol_Text), labels[i].c_str());
            ImGui::PopID();
            if (i + 1 < kTypeCount)
                ImGui::Dummy(ImVec2(0.0f, row_spacing));
        }

        // Separator between the type list and the action row.
        ImGui::Dummy(ImVec2(0.0f, row_spacing));
        {
            const ImVec2 sp = ImGui::GetCursorScreenPos();
            const float  full_w = ImGui::GetContentRegionAvail().x;
            draw_list->AddLine(ImVec2(sp.x, sp.y), ImVec2(sp.x + full_w, sp.y),
                               m_is_dark ? IM_COL32(70, 70, 74, 255) : IM_COL32(228, 228, 228, 255),
                               sep_line_h);
        }
        ImGui::Dummy(ImVec2(0.0f, row_spacing + sep_line_h));

        // Action row: re-layout labels in the current view (no camera reframe).
        {
            ImGui::PushID("##assembly_labels_auto_layout_cur_view");
            ImVec2      row_pos       = ImGui::GetCursorScreenPos();
            const float row_content_w = ImGui::GetContentRegionAvail().x;
            if (ImGui::InvisibleButton("##assembly_labels_auto_layout_item", ImVec2(row_content_w, row_height))) {
                auto_layout_labels_in_current_view();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) {
                const ImU32 bg = m_is_dark ? IM_COL32(55, 55, 59, 255) : IM_COL32(240, 240, 240, 255);
                draw_list->AddRectFilled(row_pos, ImVec2(row_pos.x + row_content_w, row_pos.y + row_height), bg, 4.0f * sc);
            }
            const ImVec2 text_size = ImGui::CalcTextSize(action_label.c_str());
            draw_list->AddText(ImVec2(row_pos.x + row_pad_x + check_w, row_pos.y + (row_height - text_size.y) * 0.5f),
                               ImGui::GetColorU32(ImGuiCol_Text), action_label.c_str());
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(3);
}

bool AssemblyStepsUtils::render_checkbox(
    ImDrawList *dl, float x, float y, float sz,
    bool *checked, const char *id, float sc)
{
    const ImU32 white_c = IM_COL32(255, 255, 255, 255);
    const ImU32 grey400 = IM_COL32(206, 206, 206, 255);
    const ImU32 brand   = IM_COL32(0, 174, 66, 255);
    const float rounding = 2.0f * sc;

    const ImVec2 cb_min(x, y);
    const ImVec2 cb_max(x + sz, y + sz);

    bool is_checked = checked && *checked;
    if (is_checked) {
        dl->AddRectFilled(cb_min, cb_max, brand, rounding);
        // Draw checkmark (two line segments)
        const float a_x = x + sz * 0.22f, a_y = y + sz * 0.52f;
        const float b_x = x + sz * 0.43f, b_y = y + sz * 0.72f;
        const float c_x = x + sz * 0.78f, c_y = y + sz * 0.32f;
        dl->AddLine(ImVec2(a_x, a_y), ImVec2(b_x, b_y), white_c, 1.5f * sc);
        dl->AddLine(ImVec2(b_x, b_y), ImVec2(c_x, c_y), white_c, 1.5f * sc);
    } else {
        dl->AddRectFilled(cb_min, cb_max, white_c, rounding);
        dl->AddRect(cb_min, cb_max, grey400, rounding, 0, 1.0f * sc);
    }

    ImGui::SetCursorScreenPos(cb_min);
    ImGui::PushID(id);
    ImGui::InvisibleButton("##cb", ImVec2(sz, sz));
    bool clicked = ImGui::IsItemClicked(0);
    ImGui::PopID();
    if (clicked && checked) {
        *checked = !*checked;
        return true;
    }
    return false;
}

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

bool AssemblyStepsUtils::rects_overlap(const ImVec2 &lhs_min, const ImVec2 &lhs_max,
                                       const ImVec2 &rhs_min, const ImVec2 &rhs_max)
{
    if (lhs_min.x >= lhs_max.x || lhs_min.y >= lhs_max.y ||
        rhs_min.x >= rhs_max.x || rhs_min.y >= rhs_max.y)
        return false;

    return lhs_min.x < rhs_max.x && lhs_max.x > rhs_min.x &&
           lhs_min.y < rhs_max.y && lhs_max.y > rhs_min.y;
}

bool AssemblyStepsUtils::is_part_number_label_layout_overlapped(const ImVec2 &rect_min, const ImVec2 &rect_max) const
{
    return rects_overlap(rect_min, rect_max,
                         m_part_number_label_forbidden_left_area.min,
                         m_part_number_label_forbidden_left_area.max) ||
           rects_overlap(rect_min, rect_max,
                         m_part_number_label_forbidden_bottom_area.min,
                         m_part_number_label_forbidden_bottom_area.max);
}

void AssemblyStepsUtils::render_assembly_guide_export_button(float panel_x, float panel_y, float sc)
{
    if (!m_imgui)
        return;
    if (m_gizmo_active)
        return;
    if (!is_selected_final_assembly_node())
        return;

    const float pad_x         = 8.0f * sc;
    const float pad_y         = 4.0f * sc;
    const float icon_sz       = 24.0f * sc;
    const float icon_inset    = icon_sz * 0.125f;            // Figma inset-[12.5%]
    const float icon_body_sz  = icon_sz - icon_inset * 2.0f; // 18*sc effective
    const float label_fs      = ImGui::GetFontSize();
    const float label_line    = std::max(20.0f * sc, label_fs + 4.0f * sc);
    const float rounding      = 4.0f * sc;
    const float gap           = 12.0f * sc; // horizontal gap to the guide panel

    // Measure the label up-front so the card is wide enough for the bigger font.
    // (Localised "Export" can be longer than the icon e.g. zh "Export" is fine but
    // some locales need more horizontal room.)
    const std::string label_str = _u8L("Export");
    const ImVec2 text_sz = ImGui::GetFont()->CalcTextSizeA(label_fs, FLT_MAX, 0.0f, label_str.c_str());

    const float btn_w = std::max(icon_sz, text_sz.x) + pad_x * 2.0f;
    const float btn_h = pad_y + icon_sz + label_line + pad_y;

    const float btn_x = panel_x - btn_w - gap;
    const float btn_y = panel_y;

    ImGui::SetNextWindowPos(ImVec2(btn_x, btn_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(btn_w, btn_h), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    // Window background is transparent the rgba(0,0,0,0.3) card is painted
    // explicitly below so we can clip it with the 4px rounded rect.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

    m_imgui->begin(std::string("##assembly_guide_export_btn"),
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImDrawList *dl      = ImGui::GetWindowDrawList();
    const ImVec2 win_min = ImGui::GetWindowPos();
    const ImVec2 win_max(win_min.x + btn_w, win_min.y + btn_h);

    // Hit area first so we can paint a hover overlay over the base card.
    ImGui::SetCursorScreenPos(win_min);
    ImGui::InvisibleButton("##t", ImVec2(btn_w, btn_h));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked(0);

    // Base card: rgba(0,0,0,0.3) - alpha 0.3 = 77/255.
    dl->AddRectFilled(win_min, win_max, IM_COL32(0, 0, 0, 77), rounding);
    if (hovered) {
        // Subtle hover lift: slightly darker card.
        dl->AddRectFilled(win_min, win_max, IM_COL32(0, 0, 0, 38), rounding);
    }

    // Layout reserves a full icon_sz (24*sc) box so the label band positioning
    // stays Figma-faithful, but the SVG is blitted into the 12.5%-inset inner
    // 18*sc square -- effectively (pad_y + icon_inset) = 7*sc gap between the
    // card's top edge and the icon body, which matches the Figma reference.
    const float icon_box_x = win_min.x + (btn_w - icon_sz) * 0.5f;
    const float icon_box_y = win_min.y + pad_y;
    const float icon_x = icon_box_x + icon_inset;
    const float icon_y = icon_box_y + icon_inset;
    if (m_btn_icon_export) {
        dl->AddImage(m_btn_icon_export,
                     ImVec2(icon_x, icon_y),
                     ImVec2(icon_x + icon_body_sz, icon_y + icon_body_sz));
    }

    // Label "Export": white, centered horizontally within the label band.
    // label_str / text_sz were measured up-front so btn_w already fits the text.
    const float text_x = win_min.x + (btn_w - text_sz.x) * 0.5f;
    const float text_y = icon_box_y + icon_sz + (label_line - text_sz.y) * 0.5f;
    dl->AddText(ImGui::GetFont(), label_fs, ImVec2(text_x, text_y),
                IM_COL32(255, 255, 255, 255), label_str.c_str());
    const char *export_popup_id = "##assembly_export_menu";
    if (clicked) {
        update_final_assembly_step_number_to_max();
        ImGui::OpenPopup(export_popup_id);
    }
    render_export_menu_popup(export_popup_id, sc);

    if (hovered) {
        // Tooltip restores non-zero WindowPadding so the popup has margin.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * sc, 6.0f * sc));
        m_imgui->tooltip(_u8L("Export assembly guide PDF, Markdown or MP4"),
                         20.0f * m_imgui->scaled(1.0f));
        ImGui::PopStyleVar();
    }

    m_imgui->end();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}

void AssemblyStepsUtils::render_assembly_guide_panel(float panel_x, float panel_y, float panel_w, float panel_h, float sc, bool is_dark)
{
    m_panel_rect_guide_min = ImVec2(panel_x, panel_y);
    m_panel_rect_guide_max = ImVec2(panel_x + panel_w, panel_y + panel_h);
    // Single-shot diagnostics: log the FIRST frame where each early-return
    auto log_skip_once = [&](const char *reason) {
        static int s_last_logged_node = -2;
        static std::string s_last_reason;
        if (s_last_logged_node == selected_node && s_last_reason == reason)
            return;
        s_last_logged_node = selected_node;
        s_last_reason      = reason;
        BOOST_LOG_TRIVIAL(warning)
            << "render_assembly_guide_panel: skip (" << reason
            << ") selected_node=" << selected_node
            << ", parent_folder=" << find_parent_folder(selected_node)
            << ", only_step=" << (m_only_step_node_create_key_frame ? 1 : 0)
            << ", panel=(" << panel_x << "," << panel_y << "," << panel_w << "x" << panel_h << ")"
            << ", sc=" << sc;
    };

    if (!has_selected_node()) {
        log_skip_once("no selected node");
        return;
    }
    if (!m_imgui || m_model == nullptr) {
        log_skip_once("no imgui or model");
        return;
    }

    // The original gate was "if user only allows step-node keyframing AND the
    if (m_only_step_node_create_key_frame && find_parent_folder(selected_node) < 0) { // has_selected_step_node
        log_skip_once("orphan node (no parent folder)");
        return;
    }

    const int current_folder = find_parent_folder(selected_node);
    if (is_empty_structure_step(current_folder)) {
        return;
    }

    // Floating "Export" button sits to the LEFT of the panel and shares the
    render_assembly_guide_export_button(panel_x, panel_y, sc);

    ImGuiWrapper &imgui = *m_imgui;

    const ImU32 grey900    = is_dark ? IM_COL32(0xE0, 0xE0, 0xE0, 255) : IM_COL32(38, 46, 48, 255);
    const ImU32 grey700    = is_dark ? IM_COL32(0xA0, 0xA0, 0xA0, 255) : IM_COL32(107, 107, 107, 255);
    const ImU32 grey600    = is_dark ? IM_COL32(0x90, 0x90, 0x90, 255) : IM_COL32(144, 144, 144, 255);
    const ImU32 grey500    = is_dark ? IM_COL32(0x80, 0x80, 0x80, 255) : IM_COL32(172, 172, 172, 255);
    const ImU32 grey400    = is_dark ? IM_COL32(70, 70, 74, 255)       : IM_COL32(206, 206, 206, 255);
    const ImU32 grey300    = is_dark ? IM_COL32(60, 60, 64, 255)       : IM_COL32(238, 238, 238, 255);
    const ImU32 grey200    = is_dark ? IM_COL32(50, 50, 54, 255)       : IM_COL32(248, 248, 248, 255);
    const ImU32 white_col  = is_dark ? IM_COL32(55, 55, 59, 255)       : IM_COL32(255, 255, 255, 255);
    const ImU32 brand_col  = IM_COL32(0, 174, 66, 255);

    const float font_sz      = ImGui::GetFontSize();
    const float small_fs     = std::max(font_sz * 0.77f, 10.0f * sc);
    const float section_gap  = 12.0f * sc;
    const float card_pad     = 9.0f * sc;
    const float card_rounding = 4.0f * sc;
    const float card_w       = panel_w - 2.0f * card_pad;

    // Pre-compute total content height for adaptive window sizing
    const float header_h = 36.0f * sc;

    const float ct_icon_sz  = 24.0f * sc;
    // Use native font size for crisp text rendering (ImGui rasterizes at this size).
    const float ct_label_fs = font_sz;
    const std::string ct_screw_str = _u8L("Screw");
    const ImVec2 ct_label_max = ImGui::GetFont()->CalcTextSizeA(ct_label_fs, FLT_MAX, 0.0f,
        ct_screw_str.c_str());
    const float ct_item_w   = std::max(ct_icon_sz + 28.0f * sc, ct_label_max.x + 16.0f * sc);
    const float ct_item_h   = 8.0f * sc + ct_icon_sz + 6.0f * sc + ct_label_max.y + 6.0f * sc;
    const float ct_gap      = 10.0f * sc;
    const float ct_pad      = 10.0f * sc;
    const float ct_cont_h   = ct_item_h + 2.0f * ct_pad;
    const float title_h_ct  = font_sz + 12.0f * sc;

    const float color_row_h_an  = 22.4f * sc;
    const float color_row_gap_an = 6.0f * sc;

    const float btn_sz_an   = 30.0f * sc;
    const float title_h_an  = font_sz + 12.0f * sc;
    // ArrowSvg (glue/clip/screw) notes are created from the "Connection Type"
    const bool show_connection_color_control = is_note_edit_controls_visible() &&
        m_note_selected_type == AssemblyNoteSelectionType::ArrowSvg;
    // Color control for the Add-Notes tools (rect / circle / arrow / text).
    const bool show_note_color_control = is_note_edit_controls_visible() &&
        note_tool_index_from_selection(m_note_selected_type) >= 0;
    // The background-color row only applies to TextLabel notes.
    const bool  show_note_bg_color_control = show_note_color_control &&
        m_note_selected_type == AssemblyNoteSelectionType::TextLabel;
    const float card_h_ct   = title_h_ct + ct_cont_h + 8.0f * sc +
        (show_connection_color_control ? color_row_gap_an + color_row_h_an : 0.0f);
    const float card_h_an   = title_h_an + btn_sz_an +
        (show_note_color_control ? color_row_gap_an + color_row_h_an : 0.0f) +
        (show_note_bg_color_control ? color_row_gap_an + color_row_h_an : 0.0f) +
        12.0f * sc;

    auto       *sp_entries     = get_current_kf_entries();
    // Title / description follow the currently selected label-show type so the
    // section name matches what the menu (auto / objects only / parts only) shows.
    std::string sp_title_str;
    std::string desc_sp_str;
    switch (m_cur_labels_show_type) {
    case LabelsShowType::OnlyModelObject:
        sp_title_str = _u8L("Show Object Numbers");
        desc_sp_str  = _u8L("Show object names on the model");
        break;
    case LabelsShowType::OnlyModelVolume:
        sp_title_str = _u8L("Show Part Numbers");
        desc_sp_str  = _u8L("Show part numbers and names on models");
        break;
    case LabelsShowType::AutoRecommend:
    default:
        sp_title_str = _u8L("Show Object/Part Numbers");
        desc_sp_str  = _u8L("Show object and part numbers and names on models");
        break;
    }
    const float desc_sp_wrap = card_w - 16.0f * sc;
    const ImVec2 desc_sp_size = ImGui::GetFont()->CalcTextSizeA(small_fs, FLT_MAX, desc_sp_wrap,
        desc_sp_str.c_str(), nullptr, nullptr);
    const float card_h_sp   = font_sz + 14.0f * sc + desc_sp_size.y + 8.0f * sc;

    const float thumb_h_tl  = 48.0f * sc;

    // Endframe tip: surfaced under the timeline thumbs whenever the user has
    std::string endframe_tip_str;
    {
        const int   tip_folder      = find_parent_folder(selected_node);
        auto       *tip_entries     = sp_entries;
        const auto &tip_step_nodes  = m_model->get_assembly_steps_tree_data().nodes;
        const bool  endframe_selected =
            tip_folder >= 0 && tip_folder < (int)tip_step_nodes.size() &&
            tip_entries != nullptr &&
            m_keyframe_selected >= 0 && m_keyframe_selected < (int)tip_entries->size() &&
            (*tip_entries)[m_keyframe_selected].is_last();
        if (endframe_selected) {
            endframe_tip_str = tip_step_nodes[tip_folder].is_final_assembly ?
                                   (_u8L("Note") + ":" + _u8L("Pose changes (e.g. translation) made on the final-assembly step's keyframe will affect the actual assembly display.")) :
                                   (_u8L("Note") + ":" + _u8L("Pose changes (e.g. translation) made on this step's keyframe do not affect the actual assembly display.") +
                                    _u8L("If necessary, a relatively good position can be restored by pressing the \"apply actual assembly pose\" button at present.") +
                                                       _u8L("undoing these edits is not yet supported and will be added in a later version."));
        }
    }
    const float endframe_tip_wrap = card_w - 16.0f * sc;
    ImVec2      endframe_tip_size = endframe_tip_str.empty()
                                    ? ImVec2(0.0f, 0.0f)
                                    : ImGui::GetFont()->CalcTextSizeA(small_fs, FLT_MAX, endframe_tip_wrap,
                                                                       endframe_tip_str.c_str(), nullptr, nullptr);

    // Truncate to at most 3 wrapped lines; the full string is shown on hover
    const float max_3line_h    = small_fs * 3.0f + 0.5f;
    bool        tip_truncated  = false;
    std::string tip_display_str = endframe_tip_str;
    if (!endframe_tip_str.empty() && endframe_tip_size.y > max_3line_h) {
        const std::string ellipsis = "...";
        std::string       best;
        size_t            i = 0;
        while (i < endframe_tip_str.size()) {
            const unsigned char b   = static_cast<unsigned char>(endframe_tip_str[i]);
            size_t              adv = 1;
            if      ((b & 0x80) == 0x00) adv = 1;
            else if ((b & 0xE0) == 0xC0) adv = 2;
            else if ((b & 0xF0) == 0xE0) adv = 3;
            else if ((b & 0xF8) == 0xF0) adv = 4;
            if (i + adv > endframe_tip_str.size())
                break;

            std::string  candidate = endframe_tip_str.substr(0, i + adv) + ellipsis;
            const ImVec2 cand_sz   = ImGui::GetFont()->CalcTextSizeA(
                small_fs, FLT_MAX, endframe_tip_wrap, candidate.c_str(), nullptr, nullptr);
            if (cand_sz.y > max_3line_h)
                break;
            best = std::move(candidate);
            i += adv;
        }
        if (!best.empty()) {
            tip_display_str  = std::move(best);
            endframe_tip_size = ImGui::GetFont()->CalcTextSizeA(
                small_fs, FLT_MAX, endframe_tip_wrap, tip_display_str.c_str(), nullptr, nullptr);
            tip_truncated = true;
        }
    }

    const float endframe_tip_extra_h = endframe_tip_str.empty()
                                           ? 0.0f
                                           : (endframe_tip_size.y + 8.0f * sc);

    const float card_h_tl   = font_sz + 12.0f * sc + thumb_h_tl + small_fs + 16.0f * sc + endframe_tip_extra_h;

    const float total_content = 8.0f * sc
        + card_h_ct + section_gap
        + card_h_an + section_gap
        + card_h_sp + section_gap
        + card_h_tl + 8.0f * sc;
    // When collapsed, only the header bar is visible. Add a small safety buffer
    // (expanded mode) so the bottom card never gets clipped due to sub-pixel
    // rounding or any residual ImGui spacing.
    const float desired_h = m_guide_panel_collapsed
        ? header_h
        : header_h + total_content + 4.0f * sc;

    ImGui::SetNextWindowPos(ImVec2(panel_x, panel_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panel_w, desired_h), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f * sc);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    // Zero ItemSpacing so Dummy heights match our desired_h budget exactly.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, is_dark ? ImVec4(55/255.f, 55/255.f, 59/255.f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    imgui.begin(std::string("##assembly_guide_panel"),
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings);

    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    const ImVec2 win_pos = ImGui::GetWindowPos();

    {
        const ImVec2 header_min = win_pos;
        const ImVec2 header_max(win_pos.x + panel_w, win_pos.y + header_h);
        const ImU32 grad_top = is_dark ? IM_COL32(48, 48, 52, 255) : IM_COL32(248, 248, 248, 255);
        const ImU32 grad_bot = is_dark ? IM_COL32(42, 42, 46, 255) : IM_COL32(241, 241, 241, 255);
        draw_list->AddRectFilled(header_min, header_max, grad_top, 4.0f * sc,
            m_guide_panel_collapsed ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersTop);
        draw_list->AddRectFilledMultiColor(header_min, header_max, grad_top, grad_top, grad_bot, grad_bot);

        // Collapse / expand toggle button on the LEFT of the title (Figma node 691:17048).
        const float toggle_sz   = 16.0f * sc;
        const float toggle_pad  = 10.0f * sc;
        const ImVec2 toggle_min(win_pos.x + toggle_pad,
                                win_pos.y + (header_h - toggle_sz) * 0.5f);
        const ImVec2 toggle_max(toggle_min.x + toggle_sz, toggle_min.y + toggle_sz);

        ImTextureID toggle_icon = m_guide_panel_collapsed ? m_panel_expand_icon
                                                          : m_panel_collapse_icon;
        if (toggle_icon) {
            draw_list->AddImage(toggle_icon, toggle_min, toggle_max);
        } else {
            // Fallback: simple chevron drawn inline so the toggle still works
            // before the SVGs finish loading.
            const float cx = (toggle_min.x + toggle_max.x) * 0.5f;
            const float cy = (toggle_min.y + toggle_max.y) * 0.5f;
            const float r  = toggle_sz * 0.25f;
            const ImU32 col = grey700;
            if (m_guide_panel_collapsed) {
                draw_list->AddTriangleFilled(ImVec2(cx - r, cy - r),
                    ImVec2(cx - r, cy + r), ImVec2(cx + r, cy), col);
            } else {
                draw_list->AddTriangleFilled(ImVec2(cx + r, cy - r),
                    ImVec2(cx + r, cy + r), ImVec2(cx - r, cy), col);
            }
        }

        // Invisible hit area for the toggle.
        ImGui::SetCursorScreenPos(toggle_min);
        ImGui::PushID("##guide_panel_toggle");
        ImGui::InvisibleButton("##t", ImVec2(toggle_sz, toggle_sz));
        if (ImGui::IsItemClicked(0)) {
            m_guide_panel_collapsed = !m_guide_panel_collapsed;
            do_commond_callback(m_guide_panel_collapsed
                ? "guide_panel:collapse" : "guide_panel:expand");
        }
        if (ImGui::IsItemHovered()) {
            // Subtle hover indicator: light overlay rectangle.
            draw_list->AddRectFilled(toggle_min, toggle_max,
                IM_COL32(38, 46, 48, 18), 3.0f * sc);
            render_panel_tooltip(m_guide_panel_collapsed ? _u8L("Expand") : _u8L("Collapse"));
        }
        ImGui::PopID();

        // Title shifts right to make room for the toggle icon.
        const float title_x = toggle_max.x + 6.0f * sc;
        const ImVec2 title_pos(title_x, win_pos.y + (header_h - font_sz) * 0.5f);
        draw_list->AddText(ImGui::GetFont(), font_sz, title_pos, grey900,
            _u8L("Assembly Guide").c_str());

        ImGui::SetCursorScreenPos(ImVec2(win_pos.x, win_pos.y + header_h));
    }

    // When collapsed, hide every section; the header bar is the only visible chrome.
    if (m_guide_panel_collapsed) {
        imgui.end();
        ImGui::PopStyleColor(1);
        ImGui::PopStyleVar(4);
        return;
    }

    ImGui::Dummy(ImVec2(0, 8.0f * sc));

    auto section_begin = [&](const char *title, float height) {
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const ImVec2 card_min(cursor.x + card_pad, cursor.y);
        const ImVec2 card_max(card_min.x + card_w, card_min.y + height);

        draw_list->AddRectFilled(card_min, card_max, grey200, card_rounding);
        draw_list->AddRect(card_min, card_max, grey300, card_rounding);

        const ImVec2 title_pos(card_min.x + 8.0f * sc, card_min.y + 6.0f * sc);
        draw_list->AddText(ImGui::GetFont(), font_sz, title_pos, grey900, title);

        return card_min;
    };

    // === Section 1: Connection Type ===
    {
        ImVec2 card_min = section_begin(_u8L("Connection Type").c_str(), card_h_ct);

        const float cont_y = card_min.y + title_h_ct;
        const float total_w = 3.0f * ct_item_w + 2.0f * ct_gap;
        const ImVec2 cont_min(card_min.x + 8.0f * sc, cont_y);
        const ImVec2 cont_max(cont_min.x + total_w + 2.0f * ct_pad, cont_min.y + ct_cont_h);
        draw_list->AddRectFilled(cont_min, cont_max, grey300, card_rounding);

        const std::string ct_labels[] = {
            _u8L("Clip"),
            _u8L("Glue"),
            _u8L("Screw")
        };
        const std::string ct_tooltips[] = {
            _u8L("Add Clip"),
            _u8L("Add Glue"),
            _u8L("Add Screw")
        };
        ImTextureID ct_icons[] = {
            m_is_dark ? m_tree_icon_clip_dark : m_tree_icon_clip,
            m_is_dark ? m_tree_icon_glue_dark : m_tree_icon_glue,
            m_is_dark ? m_tree_icon_screw_dark : m_tree_icon_screw
        };

        for (int i = 0; i < 3; i++) {
            const float bx = cont_min.x + ct_pad + i * (ct_item_w + ct_gap);
            const float by = cont_min.y + ct_pad;
            bool sel = (m_guide_connection_selected == i);
            if (render_connection_type_btn(draw_list, bx, by, ct_item_w, ct_item_h,
                    ct_icons[i], ct_labels[i].c_str(), ct_icon_sz, ct_label_fs, sc,
                    sel, grey700, brand_col, ct_tooltips[i].c_str())) {
                m_guide_connection_selected = sel ? -1 : i;
                // Each connection type has its own action: Glue / Screw add an
                // arrow-svg note (same as the keyframe note-edit toolbar buttons),
                // Clip emits a generic command for higher layers to handle.
                switch (i) {
                case 0: add_arrow_svg_note("clip"); break;
                case 1: add_arrow_svg_note("glue");  break;
                case 2: add_arrow_svg_note("screw"); break;
                default: break;
                }
            }
        }

        // When a glue/clip/screw note is selected, show its color control right
        if (show_connection_color_control) {
            auto *entries = get_current_kf_entries();
            if (entries && m_keyframe_selected >= 0 && m_keyframe_selected < (int)entries->size()) {
                const AssemblyNote &note = (*entries)[m_keyframe_selected].data.assembly_note;
                if (m_note_selected_idx >= 0 && m_note_selected_idx < (int)note.arrow_svgs.size())
                    m_guide_note_color_selected = note_palette_index_from_color(note.arrow_svgs[m_note_selected_idx].color);
            }
            const float color_row_y = cont_max.y + color_row_gap_an;
            render_note_color_control(draw_list, cont_min.x, color_row_y, sc);
        }

        ImGui::SetCursorScreenPos(ImVec2(card_min.x, card_min.y + card_h_ct));
        ImGui::Dummy(ImVec2(card_w, 0));
        ImGui::Dummy(ImVec2(0, section_gap));
    }
    // === Section 2: Add Notes ===
    {
        ImVec2 card_min = section_begin(_u8L("Add Notes").c_str(), card_h_an);

        const float tools_y = card_min.y + title_h_an;
        float btn_x = card_min.x + 8.0f * sc;
        const float btn_gap = 6.0f * sc;

        // Lazy-init the note-tool table on first use. Cannot be a static const
        // because the action lambdas must capture `this` and the icon textures
        // are loaded after construction. Three tools currently map to existing
        // note-creation helpers; commented-out rows are placeholders for
        // future shape-tool support (rect / line / pencil).
        if (m_note_tools.empty()) {
            // NOTE: button order MUST mirror `note_tool_index_from_selection`.
            m_note_tools = {
                {"##nt_rect",   m_note_icon_rect,   m_note_icon_rect_dark,   _u8L("Add Rectangle"),
                    [this]() { add_rectangle_note(); }},
                {"##nt_circle", m_note_icon_circle, m_note_icon_circle_dark, _u8L("Add Circle"),
                    [this]() { add_circle_note(); }},
                //{"##nt_line",   m_note_icon_line,   _u8L("Line"),      nullptr},
                {"##nt_vector", m_note_icon_vector, m_note_icon_vector_dark, _u8L("Add Arrow"),
                    [this]() { add_plain_arrow_note(); }},
                {"##nt_tag",    m_note_icon_tag,    m_note_icon_tag_dark,    _u8L("Add Text"),
                    [this]() { add_text_label_note(); }},
                //{"##nt_pencil", m_note_icon_pencil, _u8L("Pencil"),    nullptr},
            };
        }

        m_guide_note_tool_selected = is_note_edit_controls_visible()
            ? note_tool_index_from_selection(m_note_selected_type)
            : -1;
        if (show_note_color_control) {
            auto *entries = get_current_kf_entries();
            if (entries && m_keyframe_selected >= 0 && m_keyframe_selected < (int)entries->size()) {
                const AssemblyNote &note = (*entries)[m_keyframe_selected].data.assembly_note;
                if (m_note_selected_type == AssemblyNoteSelectionType::TextLabel &&
                    m_note_selected_idx >= 0 && m_note_selected_idx < (int)note.text_labels.size()) {
                    m_guide_note_color_selected    = note_palette_index_from_color(note.text_labels[m_note_selected_idx].color);
                    m_guide_note_bg_color_selected = note_palette_index_from_color(note.text_labels[m_note_selected_idx].background_color);
                } else if (m_note_selected_type == AssemblyNoteSelectionType::Circle &&
                         m_note_selected_idx >= 0 && m_note_selected_idx < (int)note.circle_notes.size())
                    m_guide_note_color_selected = note_palette_index_from_color(note.circle_notes[m_note_selected_idx].color);
                else if (m_note_selected_type == AssemblyNoteSelectionType::Rectangle &&
                         m_note_selected_idx >= 0 && m_note_selected_idx < (int)note.rectangle_notes.size())
                    m_guide_note_color_selected = note_palette_index_from_color(note.rectangle_notes[m_note_selected_idx].color);
                else if (m_note_selected_type == AssemblyNoteSelectionType::PlainArrow &&
                         m_note_selected_idx >= 0 && m_note_selected_idx < (int)note.plain_arrows.size())
                    m_guide_note_color_selected = note_palette_index_from_color(note.plain_arrows[m_note_selected_idx].color);
            }
        }
        for (int i = 0; i < (int) m_note_tools.size(); ++i) {
            const NoteTool &tool = m_note_tools[i];
            bool sel = (m_guide_note_tool_selected == i);
            ImTextureID icon = (m_is_dark && tool.icon_dark) ? tool.icon_dark : tool.icon;
            bool clicked = render_note_tool_btn(draw_list, btn_x, tools_y, btn_sz_an,
                icon, sel, tool.id, sc, tool.tip.c_str());
            if (clicked) {
                set_selection_origin(SelectionOrigin::ImGuiNote);
                if (sel) {
                    exit_note_edit();
                } else if (tool.action) {
                    // Picking a note tool cancels any active Connection Type highlight.
                    m_guide_connection_selected = -1;
                    tool.action();
                    m_guide_note_tool_selected = is_note_edit_controls_visible()
                        ? note_tool_index_from_selection(m_note_selected_type)
                        : -1;
                }
            }
            btn_x += btn_sz_an + btn_gap;
        }
        if (show_note_color_control) {
            const float color_row_y = tools_y + btn_sz_an + color_row_gap_an;
            const float palette_left_x = card_min.x + 8.0f * sc;

            if (show_note_bg_color_control) {
                // Text label tool gets two color rows; prefix each row with a
                // small "Text" / "Background" caption so the user can tell them
                // apart. Reserve the larger of the two label widths so the two
                // palettes start at the same x.
                const std::string fg_caption = _u8L("Text");
                const std::string bg_caption = _u8L("Background");
                ImFont *font_ptr = ImGui::GetFont();
                const ImVec2 fg_sz = font_ptr->CalcTextSizeA(small_fs, FLT_MAX, 0.0f, fg_caption.c_str());
                const ImVec2 bg_sz = font_ptr->CalcTextSizeA(small_fs, FLT_MAX, 0.0f, bg_caption.c_str());
                const float caption_gap = 4.0f * sc;
                const float caption_w   = std::max(fg_sz.x, bg_sz.x);
                const float palette_x   = palette_left_x + caption_w + caption_gap;

                const float bg_row_y    = color_row_y + color_row_h_an + color_row_gap_an;
                const float fg_caption_y = color_row_y + (color_row_h_an - fg_sz.y) * 0.5f;
                const float bg_caption_y = bg_row_y    + (color_row_h_an - bg_sz.y) * 0.5f;
                const ImU32 caption_col = grey700;
                draw_list->AddText(font_ptr, small_fs,
                    ImVec2(palette_left_x, fg_caption_y), caption_col, fg_caption.c_str());
                draw_list->AddText(font_ptr, small_fs,
                    ImVec2(palette_left_x, bg_caption_y), caption_col, bg_caption.c_str());

                render_note_color_control(draw_list, palette_x, color_row_y, sc);
                render_note_bg_color_control(draw_list, palette_x, bg_row_y, sc);
            } else {
                render_note_color_control(draw_list, palette_left_x, color_row_y, sc);
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(card_min.x, card_min.y + card_h_an));
        ImGui::Dummy(ImVec2(card_w, 0));
        ImGui::Dummy(ImVec2(0, section_gap));
    }

    // === Section 3: Show Part Numbers ===
    {
        ImVec2 card_min = section_begin(sp_title_str.c_str(), card_h_sp);

        // Whole-card click opens the label-type menu. Submitted before the
        // checkbox (+ SetItemAllowOverlap) so the checkbox still wins its own
        // top-right corner; clicks over the checkbox are filtered out below.
        const char *sp_menu_popup_id = "##assembly_labels_show_type_menu";
        ImGui::SetCursorScreenPos(card_min);
        ImGui::PushID("##sp_card_menu");
        const bool sp_card_clicked = ImGui::InvisibleButton("##sp_card", ImVec2(card_w, card_h_sp));
        ImGui::SetItemAllowOverlap();
        ImGui::PopID();

        const float cb_sz = 16.0f * sc;
        const float cb_x  = card_min.x + card_w - 8.0f * sc - cb_sz;
        const float cb_y  = card_min.y + 6.0f * sc;
        if (render_checkbox(draw_list, cb_x, cb_y, cb_sz,
                &m_guide_show_part_numbers, "##show_pn", sc)) {
            toggle_part_number_labels();
        }
        if (ImGui::IsMouseHoveringRect(ImVec2(cb_x, cb_y), ImVec2(cb_x + cb_sz, cb_y + cb_sz))) {
            std::string pn_tip = _u8L("Automatically select the optimal camera perspective and auto-arrange auto recommend labels each time the checkbox is re-ticked.");
            bool cur_is_end_frame = false;
            if (auto *pn_entries = get_current_kf_entries();
                pn_entries && m_keyframe_selected >= 0 && m_keyframe_selected < (int) pn_entries->size())
                cur_is_end_frame = (*pn_entries)[m_keyframe_selected].is_last();
            //if (!cur_is_end_frame)
               // pn_tip += " " + std::string(_u8L("The final frame will also automatically optimize the camera angle and label layout."));
            render_panel_tooltip(pn_tip);
        }

        // Open the label-type menu on a card click that did not land on the checkbox.
        const bool over_cb = ImGui::IsMouseHoveringRect(ImVec2(cb_x, cb_y), ImVec2(cb_x + cb_sz, cb_y + cb_sz));
        if (sp_card_clicked && !over_cb) {
            ImGui::OpenPopup(sp_menu_popup_id);
        }
        render_labels_show_type_menu_popup(sp_menu_popup_id, sc);

        draw_list->AddText(ImGui::GetFont(), small_fs,
            ImVec2(card_min.x + 8.0f * sc, card_min.y + font_sz + 14.0f * sc),
            grey500, desc_sp_str.c_str(), nullptr, desc_sp_wrap);

        ImGui::SetCursorScreenPos(ImVec2(card_min.x, card_min.y + card_h_sp));
        ImGui::Dummy(ImVec2(card_w, 0));
        ImGui::Dummy(ImVec2(0, section_gap));
    }

    // === Section 4: Timeline ===
    {
        const float thumb_w   = 65.0f * sc;
        const float thumb_h   = thumb_h_tl;
        const float thumb_gap = 14.0f * sc;
        const float title_h   = font_sz + 12.0f * sc;

        const std::string tl_title = _u8L("Timeline");
        ImVec2 card_min = section_begin(tl_title.c_str(), card_h_tl);
        const float thumb_y = card_min.y + title_h;

        // Inline "Play" button to the right of the title text (10px spacing).
        float next_btn_x = 0.f;
        ImTextureID play_icon = m_keyframe_playing ? m_tree_icon_pause : (m_is_dark ? m_tree_icon_play_dark : m_tree_icon_play);
        if (play_icon) {
            auto *tl_entries = get_current_kf_entries();
            const bool can_play_current_node = tl_entries && tl_entries->size() >= 2;
            const ImVec2 title_sz = ImGui::GetFont()->CalcTextSizeA(font_sz,
                FLT_MAX, 0.0f, tl_title.c_str());
            const float btn_sz = font_sz;                        // square; matches title height
            const float btn_x  = card_min.x + 8.0f * sc + title_sz.x + 10.0f * sc;
            const float btn_y  = card_min.y + 6.0f * sc + (title_sz.y - btn_sz) * 0.5f;

            const ImU32 icon_tint = can_play_current_node ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 128);
            draw_list->AddImage(play_icon,
                ImVec2(btn_x, btn_y), ImVec2(btn_x + btn_sz, btn_y + btn_sz),
                ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), icon_tint);

            ImGui::SetCursorScreenPos(ImVec2(btn_x, btn_y));
            ImGui::PushID("##tl_play_inline");
            imgui.disabled_begin(!can_play_current_node);
            ImGui::InvisibleButton("##p", ImVec2(btn_sz, btn_sz));
            if (ImGui::IsItemClicked(0)) {
                if (m_keyframe_playing)
                    pause_global_frame();
                else
                    play_all_keyframes_for_current_node();
            }
            if (ImGui::IsItemHovered())
                render_panel_tooltip(can_play_current_node ? (m_keyframe_playing ? _u8L("Pause") : _u8L("Play all frames for current node")) :
                    _u8L("At least two keyframes are required to play."));
            imgui.disabled_end();
            ImGui::PopID();
            next_btn_x = btn_x + btn_sz + 6.0f * sc;
        }

        // "Auto explode" button: pushes the current frame's objects/parts
        // outward by their dominant direction from the current overall bbox.
        {
            const int auto_explode_folder = find_parent_folder(selected_node);
            auto *auto_explode_entries = get_current_kf_entries();
            const auto &auto_explode_nodes = m_model->get_assembly_steps_tree_data().nodes;
            const bool auto_explode_is_final =
                auto_explode_folder >= 0 &&
                auto_explode_folder < static_cast<int>(auto_explode_nodes.size()) &&
                auto_explode_nodes[auto_explode_folder].is_final_assembly;
            const bool auto_explode_is_end =
                auto_explode_entries != nullptr &&
                m_keyframe_selected >= 0 &&
                m_keyframe_selected < static_cast<int>(auto_explode_entries->size()) &&
                (*auto_explode_entries)[m_keyframe_selected].is_last();
            const bool show_auto_explode =
                m_tree_icon_auto_explode != nullptr &&
                next_btn_x > 0.f &&
                auto_explode_folder >= 0 &&
                auto_explode_entries != nullptr &&
                m_keyframe_selected >= 0 &&
                m_keyframe_selected < static_cast<int>(auto_explode_entries->size()) &&
                (!auto_explode_is_final || !auto_explode_is_end);
            if (show_auto_explode) {
                const ImVec2 title_sz = ImGui::GetFont()->CalcTextSizeA(font_sz,
                    FLT_MAX, 0.0f, tl_title.c_str());
                const float btn_sz = font_sz;
                const float btn_y  = card_min.y + 6.0f * sc + (title_sz.y - btn_sz) * 0.5f;

                draw_list->AddImage(m_tree_icon_auto_explode,
                    ImVec2(next_btn_x, btn_y), ImVec2(next_btn_x + btn_sz, btn_y + btn_sz));

                ImGui::SetCursorScreenPos(ImVec2(next_btn_x, btn_y));
                ImGui::PushID("##tl_auto_explode");
                ImGui::InvisibleButton("##ae", ImVec2(btn_sz, btn_sz));
                if (ImGui::IsItemClicked(0))
                    auto_explode_current_keyframe();
                if (ImGui::IsItemHovered())
                    render_panel_tooltip(auto_explode_is_final ?
                        _u8L("Automatically explode all objects.") :
                        _u8L("Automatically explode all parts."));//Excluding some previously appeared objects
                ImGui::PopID();
                next_btn_x += btn_sz + 6.0f * sc;
            }
        }

        // "Apply from final-assembly end frame": pulls the live assembled
        {
            const int   from_fae_folder = find_parent_folder(selected_node);
            auto       *from_fae_entries = get_current_kf_entries();
            const auto &fae_nodes        = m_model->get_assembly_steps_tree_data().nodes;
            const bool  show_from_fae    =
                m_tree_icon_from_assembly_end_frame != nullptr &&
                next_btn_x > 0.f &&
                from_fae_folder >= 0 && from_fae_folder < (int)fae_nodes.size() &&
                !fae_nodes[from_fae_folder].is_final_assembly &&
                from_fae_entries != nullptr &&
                m_keyframe_selected >= 0 &&
                m_keyframe_selected < (int)from_fae_entries->size();
            if (show_from_fae) {
                const ImVec2 title_sz = ImGui::GetFont()->CalcTextSizeA(font_sz,
                    FLT_MAX, 0.0f, tl_title.c_str());
                const float btn_sz = font_sz;
                const float btn_y  = card_min.y + 6.0f * sc + (title_sz.y - btn_sz) * 0.5f;

                // Greyed out when the current keyframe's pose already
                const bool  fae_disabled = current_keyframe_matches_final_assembly_end_frame_transforms();
                const ImU32 fae_tint     = fae_disabled
                    ? IM_COL32(255, 255, 255, 128)
                    : IM_COL32_WHITE;
                draw_list->AddImage(m_tree_icon_from_assembly_end_frame,
                    ImVec2(next_btn_x, btn_y), ImVec2(next_btn_x + btn_sz, btn_y + btn_sz),
                    ImVec2(0, 0), ImVec2(1, 1), fae_tint);

                ImGui::SetCursorScreenPos(ImVec2(next_btn_x, btn_y));
                ImGui::PushID("##tl_apply_from_assembly_end");
                ImGui::InvisibleButton("##fae", ImVec2(btn_sz, btn_sz));
                if (!fae_disabled && ImGui::IsItemClicked(0))
                    apply_final_assembly_end_frame_transforms_to_current_keyframe();
                if (ImGui::IsItemHovered()) {
                    // Localized counterpart in zh_CN should read along the
                    render_panel_tooltip(_u8L("Apply final-assembly pose onto the current step"));
                }
                ImGui::PopID();
                next_btn_x += btn_sz + 6.0f * sc;
            }
        }

        // "Apply camera" button: applies current camera to all frames in the step.
        if (m_tree_icon_apply_camera && next_btn_x > 0.f) {
            const ImVec2 title_sz = ImGui::GetFont()->CalcTextSizeA(font_sz,
                FLT_MAX, 0.0f, tl_title.c_str());
            const float btn_sz = font_sz;
            const float btn_y  = card_min.y + 6.0f * sc + (title_sz.y - btn_sz) * 0.5f;

            draw_list->AddImage(m_tree_icon_apply_camera,
                ImVec2(next_btn_x, btn_y), ImVec2(next_btn_x + btn_sz, btn_y + btn_sz));

            ImGui::SetCursorScreenPos(ImVec2(next_btn_x, btn_y));
            ImGui::PushID("##tl_apply_camera");
            ImGui::InvisibleButton("##ac", ImVec2(btn_sz, btn_sz));
            if (ImGui::IsItemClicked(0)) {
                int folder = find_parent_folder(selected_node);
                auto &tree_nodes = m_model->get_assembly_steps_tree_data().nodes;
                if (folder >= 0 && folder < (int) tree_nodes.size()) {
                    auto &entries = tree_nodes[folder].kf_data.entries;
                    for (auto &entry : entries) {
                        record_camera(entry.data);
                        entry.need_save = true;
                        save_assembly_steps_json_to_model();
                    }
                }
            }
            if (ImGui::IsItemHovered())
                render_panel_tooltip(_u8L("Apply current camera angle to all frames of the current step"));
            ImGui::PopID();
            next_btn_x += btn_sz + 6.0f * sc;
        }

        //{//no use
        //    const int current_folder = find_parent_folder(selected_node);
        //    auto *current_entries = get_current_kf_entries();
        //    const auto &step_nodes = m_model->get_assembly_steps_tree_data().nodes;
        //    const bool show_apply_start_transforms =
        //        current_folder >= 0 && current_folder < (int)step_nodes.size() &&
        //        step_nodes[current_folder].is_final_assembly &&
        //        current_entries != nullptr &&
        //        m_keyframe_selected >= 0 &&
        //        m_keyframe_selected < (int)current_entries->size() &&
        //        !(*current_entries)[m_keyframe_selected].is_last();

        //    auto render_start_transform_button = [&](ImTextureID icon, const char *id, const std::string &tip,
        //                                             bool include_volume_transforms) {
        //        if (!show_apply_start_transforms || !icon || next_btn_x <= 0.f)
        //            return;

        //        const ImVec2 title_sz = ImGui::GetFont()->CalcTextSizeA(font_sz,
        //            FLT_MAX, 0.0f, tl_title.c_str());
        //        const float btn_sz = font_sz;
        //        const float btn_y  = card_min.y + 6.0f * sc + (title_sz.y - btn_sz) * 0.5f;

        //        draw_list->AddImage(icon,
        //            ImVec2(next_btn_x, btn_y), ImVec2(next_btn_x + btn_sz, btn_y + btn_sz));

        //        ImGui::SetCursorScreenPos(ImVec2(next_btn_x, btn_y));
        //        ImGui::PushID(id);
        //        ImGui::InvisibleButton("##btn", ImVec2(btn_sz, btn_sz));
        //        if (ImGui::IsItemClicked(0))
        //            apply_regular_steps_start_frame_transforms_to_current(include_volume_transforms);
        //        if (ImGui::IsItemHovered())
        //            render_panel_tooltip(tip, sc);
        //        ImGui::PopID();
        //        next_btn_x += btn_sz + 6.0f * sc;
        //    };

        //    render_start_transform_button(
        //        m_tree_icon_object,
        //        "##tl_apply_regular_start_objects",
        //        _u8L("Apply model object matrices from start frames of all non-final steps"),
        //        false);
        //    render_start_transform_button(
        //        m_tree_icon_part,
        //        "##tl_apply_regular_start_parts",
        //        _u8L("Apply model object and model volume matrices from start frames of all non-final steps"),
        //        true);
        //}

        // Render a real timeline driven by the current node's keyframe entries
        const float slot_x0    = card_min.x + 12.0f * sc;
        const float card_right = card_min.x + card_w - 4.0f * sc;

        auto *kf_entries = get_current_kf_entries();
        const int kf_count = kf_entries ? (int) kf_entries->size() : 0;

        // Cap is configurable via `m_keyframe_max_count` (3 by default). The
        // "+ add" slot disappears once we hit the cap.
        const int  kMaxTimelineKeyframes = std::max(2, m_keyframe_max_count);
        const bool show_add_slot         = (kf_count < kMaxTimelineKeyframes);

        // The "+ add" slot must always sit immediately before the end frame
        // (id == 0) when one exists, regardless of how many transition frames
        // already precede it. If there is no end frame yet (rare/transient
        // state) we fall back to appending it at the end of the row, which
        // also covers the empty-list case.
        int end_frame_idx = -1;
        if (kf_entries) {
            for (int i = 0; i < kf_count; ++i) {
                if ((*kf_entries)[i].is_last()) {
                    end_frame_idx = i;
                    break;
                }
            }
        }
        const int add_slot_pos =
            (end_frame_idx >= 0) ? end_frame_idx : kf_count;

        bool  entries_mutated = false;
        bool  add_slot_done   = false;
        float slot_x          = slot_x0;

        // Helper: render the "+" placeholder at the current slot_x and advance.
        auto render_add_slot = [&]() {
            if (slot_x + thumb_w > card_right) return;
            ImGui::PushID("##tl_add_kf");
            int r = render_timeline_keyframe(draw_list, slot_x, thumb_y,
                thumb_w, thumb_h,
                /*has_keyframe=*/ false, false,
                "", small_fs, sc);
            ImGui::PopID();
            // Hover tooltip mirrors the keyframe-slot tooltips below; uses a
            const ImVec2 slot_min(slot_x, thumb_y);
            const ImVec2 slot_max(slot_x + thumb_w, thumb_y + thumb_h);
            if (ImGui::IsMouseHoveringRect(slot_min, slot_max))
                render_panel_tooltip(_u8L("Add keyframe"));
            if (r == 1)
                insert_keyframe_after_selected();
            slot_x += thumb_w + thumb_gap;
            add_slot_done = true;
        };

        if (kf_entries) {
            for (int i = 0; i < (int) kf_entries->size(); ++i) {
                // Drop the "+ add" slot in front of the entry that lives at
                // `add_slot_pos` (== end frame index when one exists).
                if (show_add_slot && !add_slot_done && i == add_slot_pos)
                    render_add_slot();

                if (slot_x + thumb_w > card_right) break; // soft overflow guard
                auto &entry = (*kf_entries)[i];
                const bool is_last = entry.is_last();
                const bool is_sel  = (m_keyframe_selected == i);

                // Use index-scoped PushID so duplicate frame names (e.g. several
                // "transition frame" entries) don't collide on the inner buttons.
                ImGui::PushID(i + 50000);
                int r = render_timeline_keyframe(draw_list, slot_x, thumb_y,
                    thumb_w, thumb_h,
                    /*has_keyframe=*/ true, is_sel,
                    entry.data.name.c_str(), small_fs, sc,
                    /*show_delete_badge=*/ !is_last);
                ImGui::PopID();

                // Hover tooltip whose copy mirrors the next click action.
                // Geometry-based hit-test (instead of IsItemHovered) so it stays
                // accurate even when the delete badge is the last registered item.
                const ImVec2 slot_min(slot_x, thumb_y);
                const ImVec2 slot_max(slot_x + thumb_w, thumb_y + thumb_h);
                if (ImGui::IsMouseHoveringRect(slot_min, slot_max)) {
                    render_panel_tooltip(is_sel
                        ? _u8L("Click to re-record this frame")
                        : _u8L("Click to select this frame"));
                }

                if (r == 1) {
                    // Two-step click semantics:
                    m_selected_screen_center_dirty_ = true;//on_selected_keyframe_change
                    if (is_sel)
                        record_keyframe_at(i);
                    else
                        on_keyframe_list_item_clicked(i, entry);
                } else if (r == -1) {
                    m_keyframe_selected = i;
                    delete_selected_keyframe();

                    entries_mutated = true;
                    break;
                }
                slot_x += thumb_w + thumb_gap;
            }
        }

        // Trailing "+ add" slot: empty list, or no end frame in the entries
        // yet (so add_slot_pos == kf_count and the in-loop branch never fires).
        if (show_add_slot && !add_slot_done && !entries_mutated)
            render_add_slot();

        // Endframe tip (grey, wrapped) under the thumb row. Only rendered when
        if (!tip_display_str.empty()) {
            const float tip_y = thumb_y + thumb_h_tl + small_fs + 8.0f * sc;
            const float tip_x = card_min.x + 8.0f * sc;
            draw_list->AddText(ImGui::GetFont(), small_fs,
                ImVec2(tip_x, tip_y),
                grey500, tip_display_str.c_str(), nullptr, endframe_tip_wrap);
            if (tip_truncated) {
                // Hovering the truncated text reveals the original (un-clipped)
                // wording in the panel-style tooltip used by the rest of this
                // section.
                const ImVec2 tip_min(tip_x, tip_y);
                const ImVec2 tip_max(tip_x + endframe_tip_wrap, tip_y + endframe_tip_size.y);
                if (ImGui::IsMouseHoveringRect(tip_min, tip_max))
                    render_panel_tooltip(endframe_tip_str);
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(card_min.x, card_min.y + card_h_tl));
        ImGui::Dummy(ImVec2(card_w, 0));
        ImGui::Dummy(ImVec2(0, section_gap));
    }

    ImGui::Dummy(ImVec2(0, 8.0f * sc));

    imgui.end();
    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar(4);
}

void AssemblyStepsUtils::apply_assembly_tree_checked_to_step(
    int active_step_node,
    const AssemblyTreeData& tree,
    const std::unordered_map<std::string, bool>& checked)
{
    if (!m_model || active_step_node < 0)
        return;
    auto& steps_tree = m_model->get_assembly_steps_tree_data();
    if (active_step_node >= static_cast<int>(steps_tree.nodes.size()))
        return;

    // `checked` usually aliases steps_tree.nodes[active_step_node].assembly_tree_checked.
    // Adding object nodes below may reallocate steps_tree.nodes, so keep a stable copy.
    const std::unordered_map<std::string, bool> checked_snapshot = checked;

    // Object indices the user wants in THIS step (checked in the tree UI).
    std::set<int> checked_objects;
    for (const auto& node : tree.nodes) {
        if (!node.selectable || node.object_idx < 0)
            continue;
        auto checked_it = checked_snapshot.find(node.uid);
        if (checked_it != checked_snapshot.end() && checked_it->second)
            checked_objects.insert(node.object_idx);
    }

    bool step_changed = false;
    std::set<int> existing_objects;

    // 1) Drop object children that got unchecked. Membership is per-step, so we
    //    only ever touch this folder's own children, never other steps' nodes.
    {
        auto& folder_children = steps_tree.nodes[active_step_node].children;
        std::vector<int> kept_children;
        kept_children.reserve(folder_children.size());
        for (int child_idx : folder_children) {
            if (child_idx < 0 || child_idx >= static_cast<int>(steps_tree.nodes.size()))
                continue;
            const auto& child = steps_tree.nodes[child_idx];
            if (child.type == AssemblyStepsTreeNode::Type::Object && child.object_idx >= 0) {
                if (checked_objects.find(child.object_idx) == checked_objects.end()) {
                    step_changed = true;
                    continue;
                }
                existing_objects.insert(child.object_idx);
            }
            kept_children.push_back(child_idx);
        }
        if (kept_children.size() != folder_children.size())
            folder_children = std::move(kept_children);
    }

    // 2) Add a fresh, step-owned object node for every newly checked object.
    std::vector<int> new_object_nodes;
    for (int object_idx : checked_objects) {
        if (existing_objects.find(object_idx) != existing_objects.end())
            continue;
        int object_node_idx = create_object_node(object_idx, get_object_name(object_idx), get_object_id_id(object_idx));
        if (object_node_idx < 0)
            continue;
        ensure_default_keyframe(object_node_idx);
        new_object_nodes.push_back(object_node_idx);
        step_changed = true;
    }
    if (!new_object_nodes.empty()) {
        // create_object_node may have reallocated steps_tree.nodes, so re-access
        // the children container instead of holding a stale reference.
        auto& children = steps_tree.nodes[active_step_node].children;
        children.insert(children.end(), new_object_nodes.begin(), new_object_nodes.end());
    }

    if (step_changed) {
        m_structure_select_labels.erase(active_step_node);
        m_structure_select_show_default.erase(active_step_node);
        m_structure_select_popup_tree_card = -1;
        m_structure_select_popup_tree_step_node = -1;
        m_structure_select_popup_tree.clear();
        m_structure_select_popup_checked_card = -1;
        m_structure_select_popup_checked.clear();
        sync_keyframe_tree();
        // The step's children just changed (e.g. an empty step that got objects
        // added). Aggregate the children's current transforms into this folder's
        // keyframe entries, mirroring add_objects_to_assembly_step().
        fill_folder_keyframes_from_children(active_step_node);
        save_assembly_steps_json_to_model();
        do_commond_callback("dirty");
        do_commond_callback("request_extra_frame");
    }

    if (m_selection) {
        set_selection_origin(SelectionOrigin::TreeNode);
        clear_selection();
        for (int object_idx : checked_objects) {
            m_selection->add_object(static_cast<unsigned int>(object_idx), false);
        }
    }
    m_assembly_tree_ui_original_checked = checked_snapshot;
    invalidate_play_frame_refs();
    apply_keyframe_display_mode();
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
        if (!node.selectable || node.object_idx < 0 || node.volume_idx >= 0)
            continue;
        if (step_objects.find(node.object_idx) != step_objects.end())
            (*checked)[node.uid] = true;
    }

    m_active_assembly_tree_checked = &*checked;
    m_assembly_tree_ui_original_checked = *checked;
    m_assembly_tree_ui_current_folder_node = step_node_idx;
}

void AssemblyStepsUtils::render_assembly_tree_ui(float panel_x, float panel_y, float panel_w, float panel_h, float sc)
{
    auto& steps_tree = m_model->get_assembly_steps_tree_data();
    const AssemblyTreeData *tree = nullptr;
    tree                         = &m_model->get_assembly_tree_data();

    if (!tree || tree->nodes.empty())
        return;

    auto step_node_from_card = [&steps_tree](int card_idx) {
        if (card_idx <= 0)
            return -1;
        int step_card_idx = 0;
        for (int root_idx : steps_tree.roots) {
            if (root_idx < 0 || root_idx >= static_cast<int>(steps_tree.nodes.size()))
                continue;
            if (steps_tree.nodes[root_idx].type != AssemblyStepsTreeNode::Type::Folder)
                continue;
            if (step_card_idx == card_idx - 1)
                return root_idx;
            ++step_card_idx;
        }
        return -1;
    };

    clear_active_assembly_tree_checked();
    int active_step_node = m_structure_add_tree_step_node;
    if (active_step_node < 0)
        active_step_node = step_node_from_card(m_structure_add_tree_card);
    if (active_step_node < 0) {//boost debug_break
        active_step_node = find_parent_folder(selected_node);//temp no use
    }

    if (active_step_node >= 0) {
        auto& checked = steps_tree.nodes[active_step_node].assembly_tree_checked;
        if (!checked)
            checked.emplace();
        m_active_assembly_tree_checked = &*checked;
    }

    if (m_active_assembly_tree_checked != nullptr &&
        m_assembly_tree_ui_current_folder_node != active_step_node) {
        reseed_assembly_tree_checked_from_step(active_step_node, *tree);
    }

    ImGuiWrapper& imgui = *m_imgui;

    ImGui::SetNextWindowPos(ImVec2(panel_x, panel_y), ImGuiCond_Always, ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(panel_w, panel_h), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * sc);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * sc, 14.0f * sc));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f * sc);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(38 / 255.0f, 46 / 255.0f, 48 / 255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(144 / 255.0f, 144 / 255.0f, 144 / 255.0f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(144 / 255.0f, 144 / 255.0f, 144 / 255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(120 / 255.0f, 120 / 255.0f, 120 / 255.0f, 1.0f));
    imgui.begin(_L("Assembly tree"), ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);

    load_assembly_tree_icons(sc);

    const ImU32 separator_col  = IM_COL32(229, 229, 229, 255);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 tree_window_min = ImGui::GetWindowPos();
    const ImVec2 tree_window_max(tree_window_min.x + ImGui::GetWindowSize().x,
                                 tree_window_min.y + ImGui::GetWindowSize().y);
    bool close_from_outside_click = false;
    {
        const ImVec2 header_min = ImGui::GetCursorScreenPos();
        const float header_w = ImGui::GetContentRegionAvail().x;
        const float header_h = 36.0f * sc;
        const float search_h = 28.0f * sc;
        const float icon_sz = 24.0f * sc;

        if (m_assembly_tree_search_active) {
            const ImVec2 search_min(header_min.x, header_min.y + (header_h - search_h) * 0.5f);
            const ImVec2 search_max(search_min.x + header_w, search_min.y + search_h);
            draw_list->AddRectFilled(search_min, search_max, IM_COL32(248, 248, 248, 255), 14.0f * sc);
            draw_list->AddRect(search_min, search_max, IM_COL32(238, 238, 238, 255), 14.0f * sc);
            const ImVec2 icon_min(search_min.x + 10.0f * sc, search_min.y + (search_h - 16.0f * sc) * 0.5f);
            if (s_assembly_tree_icons.search)
                draw_list->AddImage(s_assembly_tree_icons.search, icon_min,
                    ImVec2(icon_min.x + 16.0f * sc, icon_min.y + 16.0f * sc));
            ImGui::SetCursorScreenPos(ImVec2(search_min.x, search_min.y));
            ImGui::InvisibleButton("##assembly_tree_search_close", ImVec2(34.0f * sc, search_h));
            if (ImGui::IsItemClicked(0)) {
                m_assembly_tree_search_active = false;
                m_assembly_tree_search_focus_pending = false;
                m_assembly_tree_search_text.clear();
            }

            ImGui::SetCursorScreenPos(ImVec2(search_min.x + 34.0f * sc, search_min.y + 2.0f * sc));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 4.0f * sc));
            ImGui::SetNextItemWidth(std::max(0.0f, search_max.x - search_min.x - 44.0f * sc));
            if (m_assembly_tree_search_focus_pending) {
                ImGui::SetKeyboardFocusHere();
                m_assembly_tree_search_focus_pending = false;
            }
            ImGui::InputTextWithHint("##assembly_tree_search", _u8L("Search").c_str(), &m_assembly_tree_search_text);
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
        } else {
            const std::string title = _u8L("List");
            const ImVec2 title_size = ImGui::CalcTextSize(title.c_str());
            draw_list->AddText(ImVec2(header_min.x, header_min.y + (header_h - title_size.y) * 0.5f),
                IM_COL32(38, 46, 48, 255), title.c_str());
            const ImVec2 icon_min(header_min.x + header_w - icon_sz, header_min.y + (header_h - icon_sz) * 0.5f);
            if (s_assembly_tree_icons.search)
                draw_list->AddImage(s_assembly_tree_icons.search, icon_min,
                    ImVec2(icon_min.x + icon_sz, icon_min.y + icon_sz));
            ImGui::SetCursorScreenPos(icon_min);
            ImGui::InvisibleButton("##assembly_tree_search_open", ImVec2(icon_sz, icon_sz));
            if (ImGui::IsItemHovered()) {
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * sc, 6.0f * sc));
                m_imgui->tooltip(_u8L("Search"), 20.0f * m_imgui->scaled(1.0f));
                ImGui::PopStyleVar();
            }
            if (ImGui::IsItemClicked(0)) {
                m_assembly_tree_search_active = true;
                m_assembly_tree_search_focus_pending = true;
            }
        }
        ImGui::SetCursorScreenPos(ImVec2(header_min.x, header_min.y + header_h));
    }
    ImVec2 separator_start = ImGui::GetCursorScreenPos();
    draw_list->AddLine(separator_start, ImVec2(separator_start.x + ImGui::GetContentRegionAvail().x, separator_start.y), separator_col, 1.0f * sc);
    ImGui::Dummy(ImVec2(0.0f, 14.0f * sc));

    std::unordered_map<std::string, bool> dummy_checked;
    std::unordered_map<std::string, bool>& checked = m_active_assembly_tree_checked != nullptr
        ? *m_active_assembly_tree_checked
        : dummy_checked;
    bool quick_select_changed = false;
    if (m_show_assembly_tree_step_quick_select) {
        ImGui::TextColored(ImVec4(172 / 255.0f, 172 / 255.0f, 172 / 255.0f, 1.0f),
            "%s", _u8L("Select all parts in a step").c_str());
        const float chip_h = 20.0f * sc;
        const float chip_gap = 6.0f * sc;
        const float chip_pad_x = 6.0f * sc;
        float chip_x = ImGui::GetCursorScreenPos().x;
        float chip_y = ImGui::GetCursorScreenPos().y + 8.0f * sc;
        const float chip_right = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - ImGui::GetStyle().WindowPadding.x;

        auto collect_step_objects = [&steps_tree](int node_idx) {
            std::set<int> object_idxs;
            std::function<void(int)> collect = [&](int idx) {
                if (idx < 0 || idx >= static_cast<int>(steps_tree.nodes.size()))
                    return;
                const auto &node = steps_tree.nodes[idx];
                if (node.type == AssemblyStepsTreeNode::Type::Object && node.object_idx >= 0)
                    object_idxs.insert(node.object_idx);
                for (int child_idx : node.children)
                    collect(child_idx);
            };
            collect(node_idx);
            return object_idxs;
        };

        int chip_idx = 0;
        for (int root_idx : steps_tree.roots) {
            if (root_idx < 0 || root_idx >= static_cast<int>(steps_tree.nodes.size()))
                continue;
            const auto &root = steps_tree.nodes[root_idx];
            if (root.type != AssemblyStepsTreeNode::Type::Folder || root.is_final_assembly)
                continue;
            const std::string label = assembly_step_display_name(root);
            const ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
            const float chip_w = text_size.x + 2.0f * chip_pad_x;
            if (chip_x + chip_w > chip_right) {
                chip_x = ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x;
                chip_y += chip_h + chip_gap;
            }

            const ImVec2 chip_min(chip_x, chip_y);
            const ImVec2 chip_max(chip_x + chip_w, chip_y + chip_h);
            draw_list->AddRectFilled(chip_min, chip_max, IM_COL32(248, 248, 248, 255), 6.0f * sc);
            draw_list->AddText(ImVec2(chip_min.x + chip_pad_x, chip_min.y + (chip_h - text_size.y) * 0.5f),
                IM_COL32(107, 107, 107, 255), label.c_str());

            ImGui::SetCursorScreenPos(chip_min);
            ImGui::PushID(chip_idx++);
            ImGui::InvisibleButton("##step_quick_select", ImVec2(chip_w, chip_h));
            if (ImGui::IsItemClicked(0)) {
                const std::set<int> object_idxs = collect_step_objects(root_idx);
                checked.clear();
                for (const auto &node : tree->nodes) {
                    if (!node.selectable || node.object_idx < 0)
                        continue;
                    if (object_idxs.find(node.object_idx) != object_idxs.end())
                        checked[node.uid] = true;
                }
                quick_select_changed = true;
            }
            ImGui::PopID();
            chip_x += chip_w + chip_gap;
        }
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x,
            chip_y + chip_h + 12.0f * sc));
    }
    AssemblyTreeRenderOptions render_options;
    render_options.allow_object_check = true;
    render_options.allow_volume_check = false;
    render_options.show_footer = true;
    render_options.readonly = false;
    render_options.child_id = "##assembly_tree_nodes";
    AssemblyTreeRenderResult render_result = render_assembly_tree_selector(*tree, checked, render_options, sc);
    if (quick_select_changed)
        render_result.changed = true;
    if (!m_structure_add_tree_opened_this_frame &&
        ImGui::IsMouseClicked(0) &&
        !ImGui::IsMouseHoveringRect(tree_window_min, tree_window_max, true)) {
        close_from_outside_click = true;
        render_result.cancel = true;
    }
    if (render_result.cancel && m_active_assembly_tree_checked != nullptr) {
        checked = m_assembly_tree_ui_original_checked;
    }
    if (render_result.changed && !render_result.cancel) {
        save_assembly_steps_json_to_model();
        do_commond_callback("dirty");
    }
    if (render_result.confirm && active_step_node >= 0 && m_active_assembly_tree_checked != nullptr &&
        checked != m_assembly_tree_ui_original_checked) {
        apply_assembly_tree_checked_to_step(active_step_node, *tree, checked);
    } else if (render_result.confirm) {
        do_commond_callback("dirty");
    }
    if (render_result.cancel || render_result.confirm) {
        exit_render_assembly_tree_ui();
        if (close_from_outside_click)
            do_commond_callback("request_extra_frame");
    }

    imgui.end();
    ImGui::PopStyleColor(7);
    ImGui::PopStyleVar(4);
    m_structure_add_tree_opened_this_frame = false;
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
        do_commond_callback("request_extra_frame");
        return;
    }

    do_commond_callback("request_extra_frame");
}

void AssemblyStepsUtils::process_assembly_pdf_capture()
{
    if (m_steps_export_active)
        process_assembly_steps_export();
}

} // namespace GUI
} // namespace Slic3r
